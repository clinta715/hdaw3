# Project Load Progress Indicator & Acceleration

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Show a loading overlay with live progress messages while projects load, and defer timestretch cache renders during initial load to reduce time-to-interactive.

**Architecture:** Add a `notify.loadProgress` notification from C++ that fires at checkpoints during `loadProject`. Use `QCoreApplication::processEvents()` (same pattern as export) to flush notifications mid-load. Frontend shows a semi-transparent overlay with status text, subscribing to the notification for live updates. Defer stretch cache renders during graph rebuild to avoid cascading rebuilds.

**Tech Stack:** C++ (JUCE, Qt event loop), React 19, TypeScript, Zustand

---

## File Map

| File | Action | Purpose |
|------|--------|---------|
| `src/frontend/FrontendRpc.h` | Modify | Add `notify::LoadProgress` constant |
| `src/engine/AudioEngineCommands_Undo.cpp` | Modify | Send progress notifications + processEvents during load |
| `frontend/src/store/projectStore.ts` | Modify | Add `loadingProject` state |
| `frontend/src/components/LoadingOverlay.tsx` | Create | Loading overlay component |
| `frontend/src/components/LoadingOverlay.css` | Create | Overlay styles |
| `frontend/src/components/FileMenu.tsx` | Modify | Set loading state before/after load |
| `frontend/src/components/StartupDialog.tsx` | Modify | Set loading state before/after load |
| `frontend/src/main.tsx` | Modify | Subscribe to `notify.loadProgress`, handle loading state |
| `src/engine/RoutingManager.cpp` | Modify | Defer stretch cache renders during load |

---

## Task 1: Add `notify::LoadProgress` Notification Type

**Files:**
- Modify: `src/frontend/FrontendRpc.h:42-48`

- [ ] **Step 1: Add the notification constant**

In `src/frontend/FrontendRpc.h`, add `LoadProgress` to the `notify` namespace:

```cpp
namespace notify {
    inline constexpr const char* TreeChanged    = "notify.treeChanged";
    inline constexpr const char* Meters         = "notify.meters";
    inline constexpr const char* Transport      = "notify.transport";
    inline constexpr const char* ScanProgress   = "notify.scanProgress";
    inline constexpr const char* ExportProgress = "notify.exportProgress";
    inline constexpr const char* LoadProgress   = "notify.loadProgress";
} // namespace notify
```

- [ ] **Step 2: Commit**

```bash
git add src/frontend/FrontendRpc.h
git commit -m "feat: add notify.loadProgress notification type"
```

---

## Task 2: Send Progress Notifications During Project Load

**Files:**
- Modify: `src/engine/AudioEngineCommands_Undo.cpp:40-54`

The `loadProject` function currently does two things synchronously. We'll add progress notifications at each checkpoint and call `QCoreApplication::processEvents()` to flush them mid-load (same pattern as the export worker in `FrontendRouter.cpp:848`).

- [ ] **Step 1: Add includes and progress-sending code to loadProject**

Replace the `loadProject` function in `src/engine/AudioEngineCommands_Undo.cpp`:

```cpp
bool AudioEngineCommands::loadProject(const std::string& filePath)
{
    // Helper to send progress and flush the Qt event loop so the
    // notification reaches the frontend before we block again.
    auto sendProgress = [](const QString& msg, float pct) {
        if (auto* server = FrontendServer::instance()) {
            QJsonObject payload{
                { "progress", static_cast<double>(pct) },
                { "message", msg },
            };
            server->broadcastNotification(notify::LoadProgress, payload);
        }
        QCoreApplication::processEvents();
    };

    sendProgress("Reading project file...", 0.0f);

    bool ok = HDAW::ProjectSerializer::load(engine_.getProjectModel(), juce::File(filePath));
    if (!ok)
    {
        HDAW_LOG("DIAG", "loadProject: load FAILED");
        sendProgress("Load failed", 1.0f);
        return false;
    }

    sendProgress("Building audio graph...", 0.3f);

    auto* proc = engine_.getMainProcessor();
    HDAW_LOG("DIAG", "loadProject: calling rebuildRoutingGraph after load, trackCount=" + std::to_string(engine_.getProjectModel().getTrackListTree().getNumChildren()));
    if (proc) proc->rebuildRoutingGraph();

    sendProgress("Done", 1.0f);
    return true;
}
```

**Note:** This requires `FrontendServer::instance()` to exist — a static accessor. If it doesn't exist yet, we need to add it. Let me check...

- [ ] **Step 2: Check if FrontendServer has a static instance accessor**

Read `src/frontend/FrontendServer.h` to check. If no static instance exists, add one:

```cpp
// In FrontendServer.h, in the class public section:
static FrontendServer* instance() { return instance_; }
```

And in the constructor/destructor:
```cpp
// Constructor:
instance_ = this;
// Destructor:
if (instance_ == this) instance_ = nullptr;
```

With a private static member:
```cpp
static inline FrontendServer* instance_ = nullptr;
```

- [ ] **Step 3: Add required includes to AudioEngineCommands_Undo.cpp**

Add at the top of the file:
```cpp
#include <QCoreApplication>
#include "../frontend/FrontendServer.h"
#include "../frontend/FrontendRpc.h"
```

- [ ] **Step 4: Commit**

```bash
git add src/engine/AudioEngineCommands_Undo.cpp src/frontend/FrontendServer.h
git commit -m "feat: send progress notifications during project load"
```

---

## Task 3: Add Loading State to Project Store

**Files:**
- Modify: `frontend/src/store/projectStore.ts`

- [ ] **Step 1: Add loadingProject state and actions**

Add to the `ProjectState` interface:
```typescript
interface ProjectState {
  snapshot: ProjectSnapshot | null;
  notesByClip: Map<number, NoteSnapshot[]>;
  lastSync: number;
  isDirty: boolean;
  filePath: string | null;
  recentProjects: string[];
  loadingProject: boolean;
  loadProgress: { message: string; percent: number } | null;
  // ... existing methods ...
}
```

Add to the store defaults:
```typescript
loadingProject: false,
loadProgress: null,
```

Add actions:
```typescript
setLoadingProject: (loading: boolean) => set({ loadingProject: loading, loadProgress: loading ? { message: "Loading...", percent: 0 } : null }),
updateLoadProgress: (message: string, percent: number) => set({ loadProgress: { message, percent } }),
```

And add to the interface:
```typescript
setLoadingProject: (loading: boolean) => void;
updateLoadProgress: (message: string, percent: number) => void;
```

- [ ] **Step 2: Commit**

```bash
git add frontend/src/store/projectStore.ts
git commit -m "feat: add loadingProject and loadProgress state to projectStore"
```

---

## Task 4: Create Loading Overlay Component

**Files:**
- Create: `frontend/src/components/LoadingOverlay.tsx`
- Create: `frontend/src/components/LoadingOverlay.css`

- [ ] **Step 1: Create the component**

```tsx
// frontend/src/components/LoadingOverlay.tsx
import { useProjectStore } from "../store/projectStore";
import "./LoadingOverlay.css";

export function LoadingOverlay() {
  const loadingProject = useProjectStore((s) => s.loadingProject);
  const loadProgress = useProjectStore((s) => s.loadProgress);

  if (!loadingProject) return null;

  return (
    <div className="loading-overlay">
      <div className="loading-overlay__card">
        <div className="loading-overlay__spinner" />
        <div className="loading-overlay__text">
          {loadProgress?.message ?? "Loading project..."}
        </div>
        {loadProgress && loadProgress.percent > 0 && loadProgress.percent < 1 && (
          <div className="loading-overlay__bar-track">
            <div
              className="loading-overlay__bar-fill"
              style={{ width: `${Math.round(loadProgress.percent * 100)}%` }}
            />
          </div>
        )}
      </div>
    </div>
  );
}
```

- [ ] **Step 2: Create the CSS**

```css
/* frontend/src/components/LoadingOverlay.css */
.loading-overlay {
  position: fixed;
  inset: 0;
  z-index: 9999;
  display: flex;
  align-items: center;
  justify-content: center;
  background: rgba(0, 0, 0, 0.6);
  backdrop-filter: blur(4px);
}

.loading-overlay__card {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 16px;
  padding: 32px 48px;
  border-radius: 8px;
  background: #1a1a2e;
  border: 1px solid rgba(255, 255, 255, 0.1);
  box-shadow: 0 8px 32px rgba(0, 0, 0, 0.4);
}

.loading-overlay__spinner {
  width: 32px;
  height: 32px;
  border: 3px solid rgba(255, 255, 255, 0.15);
  border-top-color: var(--accent, #d97706);
  border-radius: 50%;
  animation: loading-spin 0.8s linear infinite;
}

@keyframes loading-spin {
  to { transform: rotate(360deg); }
}

.loading-overlay__text {
  color: #e0e0e0;
  font-size: 14px;
  font-family: system-ui, -apple-system, sans-serif;
}

.loading-overlay__bar-track {
  width: 200px;
  height: 4px;
  background: rgba(255, 255, 255, 0.1);
  border-radius: 2px;
  overflow: hidden;
}

.loading-overlay__bar-fill {
  height: 100%;
  background: var(--accent, #d97706);
  border-radius: 2px;
  transition: width 0.2s ease;
}
```

- [ ] **Step 3: Commit**

```bash
git add frontend/src/components/LoadingOverlay.tsx frontend/src/components/LoadingOverlay.css
git commit -m "feat: create LoadingOverlay component"
```

---

## Task 5: Wire Up Loading Overlay in App

**Files:**
- Modify: `frontend/src/main.tsx`
- Modify: `frontend/src/components/FileMenu.tsx`
- Modify: `frontend/src/components/StartupDialog.tsx`

- [ ] **Step 1: Subscribe to `notify.loadProgress` in main.tsx**

In `main.tsx`, add a notification subscription in `setupSubscriptions()`:

```typescript
cleanups.push(rpc.onNotification("notify.loadProgress", (_method, params) => {
  const p = params as { message?: string; progress?: number } | undefined;
  if (p) {
    useProjectStore.getState().updateLoadProgress(
      p.message ?? "Loading...",
      typeof p.progress === "number" ? p.progress : 0
    );
  }
}));
```

- [ ] **Step 2: Render LoadingOverlay in Root**

Import and render `LoadingOverlay` in the `Root` component:

```tsx
import { LoadingOverlay } from "./components/LoadingOverlay";

function Root() {
  const [showStartup, setShowStartup] = useState(true);

  return (
    <React.StrictMode>
      <LoadingOverlay />
      {showStartup && <StartupDialog onClose={() => setShowStartup(false)} />}
      {!showStartup && <App />}
    </React.StrictMode>
  );
}
```

- [ ] **Step 3: Set loading state in FileMenu handleOpen**

In `FileMenu.tsx`, wrap the load calls with loading state:

```typescript
const handleOpen = async () => {
  setOpen(false);
  if (!checkUnsaved()) return;
  const { setLoadingProject } = useProjectStore.getState();
  setLoadingProject(true);
  try {
    if (window.hdaw) {
      const result = await window.hdaw.showOpenDialog({
        title: "Open Project",
        filters: [
          { name: "HDAW Projects", extensions: ["hdaw"] },
          { name: "All Files", extensions: ["*"] },
        ],
        properties: ["openFile"],
      });
      if (!result.canceled && result.filePaths.length > 0) {
        const path = result.filePaths[0];
        await rpc.call("project.loadProject", { filePath: path }).catch((err) => reportRpcError("project.loadProject", err));
        useProjectStore.getState().addRecentProject(path);
        await useProjectStore.getState().syncDirtyFlag(rpc);
        await useProjectStore.getState().syncSnapshot(rpc);
      }
    } else {
      const path = prompt("Open project:", filePath ?? "project.hdaw");
      if (!path) { setLoadingProject(false); return; }
      await rpc.call("project.loadProject", { filePath: path }).catch((err) => reportRpcError("project.loadProject", err));
      useProjectStore.getState().addRecentProject(path);
      await useProjectStore.getState().syncDirtyFlag(rpc);
      await useProjectStore.getState().syncSnapshot(rpc);
    }
  } finally {
    setLoadingProject(false);
  }
};
```

Also update `handleOpenRecent`:
```typescript
const handleOpenRecent = (path: string) => doAction(async () => {
  if (!checkUnsaved()) return;
  const { setLoadingProject } = useProjectStore.getState();
  setLoadingProject(true);
  try {
    await rpc.call("project.loadProject", { filePath: path }).catch((err) => reportRpcError("project.loadProject", err));
    useProjectStore.getState().addRecentProject(path);
  } finally {
    setLoadingProject(false);
  }
});
```

- [ ] **Step 4: Set loading state in StartupDialog handleOpen**

In `StartupDialog.tsx`, wrap the load call:

```typescript
const handleOpen = async () => {
  // ... dialog logic ...
  const { setLoadingProject } = useProjectStore.getState();
  setLoadingProject(true);
  try {
    await rpc.call("project.loadProject", { filePath: path });
    useProjectStore.getState().addRecentProject(path);
    onClose();
  } finally {
    setLoadingProject(false);
  }
};
```

- [ ] **Step 5: Commit**

```bash
git add frontend/src/main.tsx frontend/src/components/FileMenu.tsx frontend/src/components/StartupDialog.tsx
git commit -m "feat: wire up loading overlay across project open paths"
```

---

## Task 6: Defer Timestretch Cache Renders During Load

**Files:**
- Modify: `src/engine/RoutingManager.cpp:409-433`

During `rebuildClipsForTrack`, `stretchCache->requestRender()` triggers off-thread renders that call `rebuildRoutingGraph` again when complete (via `StretchCache::entryReady` signal). This causes cascading rebuilds during load. Defer these by adding a flag.

- [ ] **Step 1: Add a `loadingPhase` flag to RoutingManager**

In `src/engine/RoutingManager.h`, add:
```cpp
bool loadingPhase = false;
```

- [ ] **Step 2: Skip stretch cache requests during load**

In `src/engine/RoutingManager.cpp`, in the clip rebuild loop where `stretchCache->requestRender()` is called, guard it:

```cpp
if (!loadingPhase) {
    stretchCache->requestRender(...);
}
```

- [ ] **Step 3: Set/clear the flag in AudioEngineCommands::loadProject**

In `src/engine/AudioEngineCommands_Undo.cpp`:

```cpp
bool AudioEngineCommands::loadProject(const std::string& filePath)
{
    // ... progress helpers ...

    sendProgress("Reading project file...", 0.0f);

    bool ok = HDAW::ProjectSerializer::load(engine_.getProjectModel(), juce::File(filePath));
    if (!ok) { /* ... */ return false; }

    sendProgress("Building audio graph...", 0.3f);

    // Signal routing manager to defer stretch cache renders
    auto* proc = engine_.getMainProcessor();
    if (proc) {
        auto* routingMgr = proc->getRoutingManager();
        if (routingMgr) routingMgr->loadingPhase = true;
    }

    if (proc) proc->rebuildRoutingGraph();

    if (proc) {
        auto* routingMgr = proc->getRoutingManager();
        if (routingMgr) routingMgr->loadingPhase = false;
    }

    sendProgress("Done", 1.0f);
    return true;
}
```

**Note:** This requires `getRoutingManager()` to be accessible on `MainAudioProcessor`. Check if it exists; if not, add a simple accessor.

- [ ] **Step 4: Commit**

```bash
git add src/engine/RoutingManager.h src/engine/RoutingManager.cpp src/engine/AudioEngineCommands_Undo.cpp
git commit -m "perf: defer timestretch cache renders during project load"
```

---

## Task 7: Build and Verify

- [ ] **Step 1: Build the C++ project**

```powershell
cmake --build build --config Debug
```

Expected: Build succeeds with no errors.

- [ ] **Step 2: Build the frontend**

```powershell
cd frontend && npm run build
```

Expected: Build succeeds.

- [ ] **Step 3: Rebuild C++ (to pick up frontend resource changes)**

```powershell
cmake --build build --config Debug
```

- [ ] **Step 4: Run tests**

```powershell
build\Debug\hdaw_tests.exe
```

Expected: All tests pass.

- [ ] **Step 5: Manual smoke test**

1. Run `build\Debug\HDAW.exe`
2. Open a project via File > Open
3. Verify loading overlay appears with progress message
4. Verify overlay disappears after load completes
5. Verify project loads correctly (tracks, clips, plugins visible)

- [ ] **Step 6: Commit any fixes**

```bash
git add -A && git commit -m "fix: address review feedback for load progress"
```
