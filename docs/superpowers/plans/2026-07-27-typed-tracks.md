# Typed Tracks Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add `trackType` property (audio/instrument/folder) to tracks with folder collapse/expand and mute/solo cascade behavior.

**Architecture:** Extend the existing flat TRACK_LIST ValueTree with a `trackType` property. Folder tracks use `childIds` (comma-separated indices) to reference children, and children have a `parentId`. ReadModel computes `effectiveMuted`/`effectiveSoloed` by walking the parent chain. Audio graph skips folder tracks entirely.

**Tech Stack:** JUCE 8 (ValueTree, AudioProcessorGraph), React 19, TypeScript, Zustand, Vite

---

## File Structure

### C++ Layer

| File | Responsibility |
|------|---------------|
| `src/model/ProjectModel.h` | ValueTree ID declarations (`trackType`, `childIds`, `parentId`, `isCollapsed`) |
| `src/engine/AudioEngineCommands.cpp` | Set `trackType` in `createTrackValueTree()` |
| `src/common/ReadModel.h` | `TrackSnapshot` struct with new fields |
| `src/engine/ReadModelImpl.cpp` | Read properties + compute effective state |
| `src/common/ProjectCommands.h` | Command interface (virtual methods) |
| `src/engine/AudioEngineCommands.h` | Command declarations |
| `src/engine/AudioEngineCommands_Tracks.cpp` | Command implementations |
| `src/engine/RoutingManager.cpp` | Skip folder tracks in `rebuildFromValueTree()` |
| `src/frontend/FrontendRouter.cpp` | RPC handlers |
| `src/mcp/McpTools_Project.cpp` | MCP tool parameters |

### Frontend Layer

| File | Responsibility |
|------|---------------|
| `frontend/src/rpc/types.ts` | TypeScript interfaces |
| `frontend/src/rpc/rpc.ts` | RPC wrapper functions |
| `frontend/src/store/projectStore.ts` | Folder-aware track filtering |
| `frontend/src/components/TrackHeaders.tsx` | Type indicator, folder chevron, indent |
| `frontend/src/components/TrackHeaders.css` | Folder styles |
| `frontend/src/components/TimelineMinimal.tsx` | Hide collapsed children, folder row styling |
| `frontend/src/components/MixerStrip.tsx` | Effective mute display |

---

## Task 1: Add ValueTree ID declarations

**Files:**
- Modify: `src/model/ProjectModel.h:124`

- [ ] **Step 1: Add new IDs after trackHeight**

Open `src/model/ProjectModel.h`. After line 123 (`DECLARE_ID(trackHeight)`), add:

```cpp
    DECLARE_ID(trackType)
    DECLARE_ID(childIds)
    DECLARE_ID(parentId)
    DECLARE_ID(isCollapsed)
```

- [ ] **Step 2: Verify build compiles**

Run: `cmake --build build --config Debug --target HDAW`
Expected: BUILD SUCCEEDED (new IDs are just string constants, no logic changes)

- [ ] **Step 3: Commit**

```bash
git add src/model/ProjectModel.h
git commit -m "feat: add trackType, childIds, parentId, isCollapsed ValueTree IDs"
```

---

## Task 2: Set trackType in createTrackValueTree()

**Files:**
- Modify: `src/engine/AudioEngineCommands.cpp:132-161`

- [ ] **Step 1: Add trackType parameter and property**

Change the function signature at line 132 from:

```cpp
juce::ValueTree AudioEngineCommands::createTrackValueTree(const std::string& name, int color, int parentBus)
```

to:

```cpp
juce::ValueTree AudioEngineCommands::createTrackValueTree(const std::string& name, int color, int parentBus, int trackType)
```

After line 143 (`track.setProperty(IDs::trackHeight, 80.0, nullptr);`), add:

```cpp
    track.setProperty(IDs::trackType, trackType, nullptr);
```

- [ ] **Step 2: Update declaration in header**

Open `src/engine/AudioEngineCommands.h`. Find the `createTrackValueTree` declaration and add the `trackType` parameter:

```cpp
juce::ValueTree createTrackValueTree(const std::string& name, int color = -1, int parentBus = -1, int trackType = 0);
```

- [ ] **Step 3: Find all callers and update them**

Run: `rg "createTrackValueTree" --type cpp` to find callers. Each caller needs the new parameter. Most will use the default `0` (audio). Update any caller that should pass a specific type.

- [ ] **Step 4: Verify build compiles**

Run: `cmake --build build --config Debug --target HDAW`
Expected: BUILD SUCCEEDED

- [ ] **Step 5: Commit**

```bash
git add src/engine/AudioEngineCommands.cpp src/engine/AudioEngineCommands.h
git commit -m "feat: add trackType parameter to createTrackValueTree"
```

---

## Task 3: Add trackType to ReadModel

**Files:**
- Modify: `src/common/ReadModel.h:5-18`
- Modify: `src/engine/ReadModelImpl.cpp:40-57`

- [ ] **Step 1: Add trackType to TrackSnapshot struct**

Open `src/common/ReadModel.h`. After line 16 (`int midiChannel = 1;`), add:

```cpp
    int trackType = 0;            // 0=audio, 1=instrument, 2=folder
    bool isCollapsed = false;     // folder only
    bool effectiveMuted = false;  // computed: true if self or ancestor muted
    bool effectiveSoloed = false; // computed: true if self or ancestor soloed
    int parentId = -1;            // track index of parent folder, -1 if none
```

- [ ] **Step 2: Read trackType in buildTrackSnapshotFromTree**

Open `src/engine/ReadModelImpl.cpp`. After line 53 (`ts.midiChannel = ...`), add:

```cpp
    ts.trackType   = static_cast<int>(trackTree.getProperty(IDs::trackType, 0));
    ts.isCollapsed = trackTree.getProperty(IDs::isCollapsed, false);
    ts.parentId    = trackTree.getProperty(IDs::parentId, -1);
```

- [ ] **Step 3: Verify build compiles**

Run: `cmake --build build --config Debug --target HDAW`
Expected: BUILD SUCCEEDED

- [ ] **Step 4: Commit**

```bash
git add src/common/ReadModel.h src/engine/ReadModelImpl.cpp
git commit -m "feat: add trackType, isCollapsed, parentId to TrackSnapshot"
```

---

## Task 4: Add effective state computation

**Files:**
- Modify: `src/engine/ReadModelImpl.cpp`

- [ ] **Step 1: Add helper to build parent lookup**

Open `src/engine/ReadModelImpl.cpp`. Before the `buildTrackSnapshotFromTree` function (around line 39), add:

```cpp
static std::map<int, int> buildChildToParentMap(const juce::ValueTree& trackList)
{
    std::map<int, int> childToParent;
    for (int t = 0; t < trackList.getNumChildren(); ++t)
    {
        auto track = trackList.getChild(t);
        int parentId = track.getProperty(IDs::parentId, -1);
        if (parentId >= 0)
            childToParent[t] = parentId;
    }
    return childToParent;
}
```

- [ ] **Step 2: Find the function that builds the full snapshot**

Run: `rg "buildTrackSnapshotFromTree\|TrackSnapshot" src/engine/ReadModelImpl.cpp` to find where all track snapshots are collected. There should be a function that iterates all tracks and builds a vector of TrackSnapshot. We need to add effective state computation there.

- [ ] **Step 3: Add effective state computation after building all snapshots**

Find the function that collects all TrackSnapshot objects (likely `buildProjectSnapshot` or similar). After the loop that builds all snapshots, add:

```cpp
    // Compute effective mute/solo by walking parent chain
    auto childToParent = buildChildToParentMap(trackList);
    for (auto& ts : snapshots)  // snapshots is the vector of TrackSnapshot
    {
        bool effMuted = ts.muted;
        bool effSoloed = ts.soloed;
        int current = ts.index;
        while (true)
        {
            auto it = childToParent.find(current);
            if (it == childToParent.end()) break;
            int parentIdx = it->second;
            if (parentIdx < 0 || parentIdx >= (int)snapshots.size()) break;
            const auto& parent = snapshots[parentIdx];
            effMuted = effMuted || parent.muted;
            effSoloed = effSoloed || parent.soloed;
            current = parentIdx;
        }
        ts.effectiveMuted = effMuted;
        ts.effectiveSoloed = effSoloed;
    }
```

- [ ] **Step 4: Verify build compiles**

Run: `cmake --build build --config Debug --target HDAW`
Expected: BUILD SUCCEEDED

- [ ] **Step 5: Commit**

```bash
git add src/engine/ReadModelImpl.cpp
git commit -m "feat: compute effectiveMuted/effectiveSoloed in ReadModel"
```

---

## Task 5: Add trackType commands to ProjectCommands

**Files:**
- Modify: `src/common/ProjectCommands.h:24`
- Modify: `src/engine/AudioEngineCommands.h`
- Modify: `src/engine/AudioEngineCommands_Tracks.cpp`

- [ ] **Step 1: Add virtual methods to ProjectCommands.h**

Open `src/common/ProjectCommands.h`. After line 24 (`setTrackMidiChannel`), add:

```cpp
    virtual void setTrackType(int trackIndex, int type) = 0;
    virtual void setTrackCollapsed(int trackIndex, bool collapsed) = 0;
    virtual void moveTrackIntoFolder(int trackIndex, int folderIndex) = 0;
    virtual void moveTrackOutOfFolder(int trackIndex) = 0;
```

- [ ] **Step 2: Add declarations to AudioEngineCommands.h**

Open `src/engine/AudioEngineCommands.h`. Find where other `setTrack*` methods are declared and add:

```cpp
    void setTrackType(int trackIndex, int type) override;
    void setTrackCollapsed(int trackIndex, bool collapsed) override;
    void moveTrackIntoFolder(int trackIndex, int folderIndex) override;
    void moveTrackOutOfFolder(int trackIndex) override;
```

- [ ] **Step 3: Implement in AudioEngineCommands_Tracks.cpp**

Open `src/engine/AudioEngineCommands_Tracks.cpp`. Add implementations:

```cpp
void AudioEngineCommands::setTrackType(int trackIndex, int type)
{
    auto& project = engine_.getProjectModel();
    auto trackList = project.getTrackListTree();
    if (trackIndex < 0 || trackIndex >= trackList.getNumChildren()) return;
    auto track = trackList.getChild(trackIndex);
    track.setProperty(IDs::trackType, type, &engine_.getUndoManager());
}

void AudioEngineCommands::setTrackCollapsed(int trackIndex, bool collapsed)
{
    auto& project = engine_.getProjectModel();
    auto trackList = project.getTrackListTree();
    if (trackIndex < 0 || trackIndex >= trackList.getNumChildren()) return;
    auto track = trackList.getChild(trackIndex);
    track.setProperty(IDs::isCollapsed, collapsed, &engine_.getUndoManager());
}

void AudioEngineCommands::moveTrackIntoFolder(int trackIndex, int folderIndex)
{
    auto& project = engine_.getProjectModel();
    auto trackList = project.getTrackListTree();
    if (trackIndex < 0 || trackIndex >= trackList.getNumChildren()) return;
    if (folderIndex < 0 || folderIndex >= trackList.getNumChildren()) return;
    if (trackIndex == folderIndex) return;

    auto track = trackList.getChild(trackIndex);
    auto folder = trackList.getChild(folderIndex);

    // Check folder is actually a folder
    if (static_cast<int>(folder.getProperty(IDs::trackType, 0)) != 2) return;

    // Remove from old parent if any
    int oldParent = track.getProperty(IDs::parentId, -1);
    if (oldParent >= 0 && oldParent < trackList.getNumChildren())
    {
        auto oldFolder = trackList.getChild(oldParent);
        auto childIds = oldFolder.getProperty(IDs::childIds, "").toString().toStdString();
        // Remove trackIndex from childIds
        std::string newChildIds;
        std::istringstream iss(childIds);
        std::string token;
        bool first = true;
        while (std::getline(iss, token, ','))
        {
            if (!token.empty() && std::stoi(token) != trackIndex)
            {
                if (!first) newChildIds += ",";
                newChildIds += token;
                first = false;
            }
        }
        oldFolder.setProperty(IDs::childIds, juce::String(newChildIds), &engine_.getUndoManager());
    }

    // Add to new folder
    auto childIds = folder.getProperty(IDs::childIds, "").toString().toStdString();
    if (!childIds.empty()) childIds += ",";
    childIds += std::to_string(trackIndex);
    folder.setProperty(IDs::childIds, juce::String(childIds), &engine_.getUndoManager());
    track.setProperty(IDs::parentId, folderIndex, &engine_.getUndoManager());
}

void AudioEngineCommands::moveTrackOutOfFolder(int trackIndex)
{
    auto& project = engine_.getProjectModel();
    auto trackList = project.getTrackListTree();
    if (trackIndex < 0 || trackIndex >= trackList.getNumChildren()) return;

    auto track = trackList.getChild(trackIndex);
    int parentId = track.getProperty(IDs::parentId, -1);
    if (parentId < 0 || parentId >= trackList.getNumChildren()) return;

    auto parent = trackList.getChild(parentId);

    // Remove from parent's childIds
    auto childIds = parent.getProperty(IDs::childIds, "").toString().toStdString();
    std::string newChildIds;
    std::istringstream iss(childIds);
    std::string token;
    bool first = true;
    while (std::getline(iss, token, ','))
    {
        if (!token.empty() && std::stoi(token) != trackIndex)
        {
            if (!first) newChildIds += ",";
            newChildIds += token;
            first = false;
        }
    }
    parent.setProperty(IDs::childIds, juce::String(newChildIds), &engine_.getUndoManager());
    track.setProperty(IDs::parentId, -1, &engine_.getUndoManager());
}
```

Note: You may need to add `#include <sstream>` at the top of the file.

- [ ] **Step 4: Verify build compiles**

Run: `cmake --build build --config Debug --target HDAW`
Expected: BUILD SUCCEEDED

- [ ] **Step 5: Commit**

```bash
git add src/common/ProjectCommands.h src/engine/AudioEngineCommands.h src/engine/AudioEngineCommands_Tracks.cpp
git commit -m "feat: add setTrackType, setTrackCollapsed, moveTrackIntoFolder, moveTrackOutOfFolder commands"
```

---

## Task 6: Add RPC handlers

**Files:**
- Modify: `src/frontend/FrontendRouter.cpp:148-170`

- [ ] **Step 1: Update addTrack handler to accept trackType**

Open `src/frontend/FrontendRouter.cpp`. Find the `addTrack` handler (around line 151). Change:

```cpp
    if (m == "addTrack") {
        std::string name; if (!requireString(o, "name", name, nullptr)) name = "Track";
        int color = optInt(o, "color", -1, nullptr);
        int parentBus = optInt(o, "parentBus", -1, nullptr);
        return { false, c.addTrack(name, color, parentBus) };
    }
```

to:

```cpp
    if (m == "addTrack") {
        std::string name; if (!requireString(o, "name", name, nullptr)) name = "Track";
        int color = optInt(o, "color", -1, nullptr);
        int parentBus = optInt(o, "parentBus", -1, nullptr);
        int trackType = optInt(o, "trackType", 0, nullptr);
        return { false, c.addTrack(name, color, parentBus, trackType) };
    }
```

- [ ] **Step 2: Add new RPC handlers after setTrackMidiChannel**

After the `setTrackMidiChannel` handler (around line 170), add:

```cpp
    if (m == "setTrackType") { int i, t; if (!requireInt(o, "trackIndex", i, nullptr) || !requireInt(o, "trackType", t, nullptr)) return makeError(-32602, "trackIndex and trackType required"); c.setTrackType(i, t); return { false, QJsonValue::Null }; }
    if (m == "setTrackCollapsed") { int i; bool b; if (!requireInt(o, "trackIndex", i, nullptr) || !requireBool(o, "collapsed", b, nullptr)) return makeError(-32602, "trackIndex and collapsed required"); c.setTrackCollapsed(i, b); return { false, QJsonValue::Null }; }
    if (m == "moveTrackIntoFolder") { int i, f; if (!requireInt(o, "trackIndex", i, nullptr) || !requireInt(o, "folderIndex", f, nullptr)) return makeError(-32602, "trackIndex and folderIndex required"); c.moveTrackIntoFolder(i, f); return { false, QJsonValue::Null }; }
    if (m == "moveTrackOutOfFolder") { int i; if (!requireInt(o, "trackIndex", i, nullptr)) return makeError(-32602, "trackIndex required"); c.moveTrackOutOfFolder(i); return { false, QJsonValue::Null }; }
```

- [ ] **Step 3: Update addTrack to pass trackType**

Find where `addTrack` is called in the command interface. The `addTrack` method signature needs the `trackType` parameter. Open `src/common/ProjectCommands.h` line 12 and change:

```cpp
    virtual int addTrack(const std::string& name, int color = -1, int parentBus = -1) = 0;
```

to:

```cpp
    virtual int addTrack(const std::string& name, int color = -1, int parentBus = -1, int trackType = 0) = 0;
```

Then update the implementation in `AudioEngineCommands_Tracks.cpp`. Find the `addTrack` method and change its signature to match:

```cpp
int AudioEngineCommands::addTrack(const std::string& name, int color, int parentBus, int trackType)
```

And update the call to `createTrackValueTree` inside it:

```cpp
    auto trackTree = createTrackValueTree(name, color, parentBus, trackType);
```

- [ ] **Step 4: Verify build compiles**

Run: `cmake --build build --config Debug --target HDAW`
Expected: BUILD SUCCEEDED

- [ ] **Step 5: Commit**

```bash
git add src/frontend/FrontendRouter.cpp src/common/ProjectCommands.h
git commit -m "feat: add RPC handlers for typed tracks and folder operations"
```

---

## Task 7: Skip folder tracks in audio graph

**Files:**
- Modify: `src/engine/RoutingManager.cpp:79-84`

- [ ] **Step 1: Add isFolderTrack helper**

Open `src/engine/RoutingManager.h`. In the private helpers section (around line 64), add:

```cpp
    bool isFolderTrack(int trackIndex) const;
```

Open `src/engine/RoutingManager.cpp`. Add implementation:

```cpp
bool RoutingManager::isFolderTrack(int trackIndex) const
{
    auto trackList = projectModel.getTrackListTree();
    if (trackIndex < 0 || trackIndex >= trackList.getNumChildren()) return false;
    return static_cast<int>(trackList.getChild(trackIndex).getProperty(IDs::trackType, 0)) == 2;
}
```

- [ ] **Step 2: Skip folder tracks in rebuildFromValueTree**

Open `src/engine/RoutingManager.cpp`. In `rebuildFromValueTree()`, find the track iteration loop (lines 79-84):

```cpp
    auto trackList = projectModel.getTrackListTree();
    for (int t = 0; t < trackList.getNumChildren(); ++t)
    {
        auto trackTree = trackList.getChild(t);
        addTrack(t, trackTree);
    }
```

Change to:

```cpp
    auto trackList = projectModel.getTrackListTree();
    for (int t = 0; t < trackList.getNumChildren(); ++t)
    {
        auto trackTree = trackList.getChild(t);
        if (isFolderTrack(t)) continue;  // folder tracks are visual-only
        addTrack(t, trackTree);
    }
```

- [ ] **Step 3: Verify build compiles**

Run: `cmake --build build --config Debug --target HDAW`
Expected: BUILD SUCCEEDED

- [ ] **Step 4: Commit**

```bash
git add src/engine/RoutingManager.h src/engine/RoutingManager.cpp
git commit -m "feat: skip folder tracks in audio graph building"
```

---

## Task 8: Add migration logic for existing projects

**Files:**
- Modify: `src/model/ProjectModel.cpp`

- [ ] **Step 1: Find project load function**

Run: `rg "loadProject\|openProject\|loadFromFile" src/model/ProjectModel.cpp` to find where projects are loaded.

- [ ] **Step 2: Add migration after loading**

Find the function that loads/opens a project. After the project tree is loaded, add migration logic:

```cpp
    // Migration: ensure all tracks have trackType property
    auto trackList = project.getChildWithName(IDs::TRACK_LIST);
    if (trackList.isValid())
    {
        for (int t = 0; t < trackList.getNumChildren(); ++t)
        {
            auto track = trackList.getChild(t);
            if (!track.hasProperty(IDs::trackType))
                track.setProperty(IDs::trackType, 0, nullptr);  // default to audio
        }
    }
```

- [ ] **Step 3: Verify build compiles**

Run: `cmake --build build --config Debug --target HDAW`
Expected: BUILD SUCCEEDED

- [ ] **Step 4: Commit**

```bash
git add src/model/ProjectModel.cpp
git commit -m "feat: add migration for trackType on legacy projects"
```

---

## Task 9: Update frontend TypeScript types

**Files:**
- Modify: `frontend/src/rpc/types.ts:12-28`

- [ ] **Step 1: Add trackType to TrackSnapshot**

Open `frontend/src/rpc/types.ts`. After line 26 (`midiChannel: number;`), add:

```typescript
  trackType: number;        // 0=audio, 1=instrument, 2=folder
  isCollapsed: boolean;     // folder only
  effectiveMuted: boolean;  // computed: true if self or ancestor muted
  effectiveSoloed: boolean; // computed: true if self or ancestor soloed
  parentId: number;         // track index of parent folder, -1 if none
```

- [ ] **Step 2: Verify TypeScript compiles**

Run: `cd frontend && npx tsc --noEmit`
Expected: No errors

- [ ] **Step 3: Commit**

```bash
git add frontend/src/rpc/types.ts
git commit -m "feat: add trackType, folder properties to frontend TrackSnapshot"
```

---

## Task 10: Add frontend RPC wrappers

**Files:**
- Modify: `frontend/src/rpc/rpc.ts`

- [ ] **Step 1: Find existing track RPC wrappers**

Run: `rg "setTrackMuted\|setTrackSoloed\|addTrack" frontend/src/rpc/rpc.ts` to find the pattern.

- [ ] **Step 2: Add new RPC wrappers**

Add the following functions after the existing track setters:

```typescript
export function setTrackType(trackIndex: number, trackType: number) {
  return rpc.call("project.setTrackType", { trackIndex, trackType });
}

export function setTrackCollapsed(trackIndex: number, collapsed: boolean) {
  return rpc.call("project.setTrackCollapsed", { trackIndex, collapsed });
}

export function moveTrackIntoFolder(trackIndex: number, folderIndex: number) {
  return rpc.call("project.moveTrackIntoFolder", { trackIndex, folderIndex });
}

export function moveTrackOutOfFolder(trackIndex: number) {
  return rpc.call("project.moveTrackOutOfFolder", { trackIndex });
}
```

- [ ] **Step 3: Verify TypeScript compiles**

Run: `cd frontend && npx tsc --noEmit`
Expected: No errors

- [ ] **Step 4: Commit**

```bash
git add frontend/src/rpc/rpc.ts
git commit -m "feat: add frontend RPC wrappers for typed tracks"
```

---

## Task 11: Update TrackHeaders component

**Files:**
- Modify: `frontend/src/components/TrackHeaders.tsx`
- Modify: `frontend/src/components/TrackHeaders.css`

- [ ] **Step 1: Add imports**

Open `frontend/src/components/TrackHeaders.tsx`. Add imports at the top:

```typescript
import { setTrackType, setTrackCollapsed, moveTrackIntoFolder, moveTrackOutOfFolder } from "../rpc";
```

- [ ] **Step 2: Add type icons helper**

Before the component function, add:

```typescript
const TRACK_TYPE_ICONS: Record<number, string> = {
  0: "\u25B2",       // audio: triangle (waveform placeholder)
  1: "\u266B",       // instrument: music note
  2: "\u25BC",       // folder: down triangle
};

const TRACK_TYPE_COLORS: Record<number, string> = {
  0: "#4a9eff",      // audio: blue
  1: "#9b59b6",      // instrument: purple
  2: "#f39c12",      // folder: orange
};
```

- [ ] **Step 3: Add folder-aware filtering**

After `const tracks = snapshot?.tracks ?? [];`, add:

```typescript
  // Build set of hidden track indices (children of collapsed folders)
  const hiddenIndices = new Set<number>();
  for (const track of tracks) {
    if (track.trackType === 2 && track.isCollapsed) {
      for (const child of tracks) {
        if (child.parentId === track.index) {
          hiddenIndices.add(child.index);
        }
      }
    }
  }
  const visibleTracks = tracks.filter(t => !hiddenIndices.has(t.index));
```

- [ ] **Step 4: Update track iteration to use visibleTracks**

Change `tracks.map((track, i) => {` to `visibleTracks.map((track, i) => {`.

- [ ] **Step 5: Add type indicator and folder chevron**

In the render, before the color swatch, add type indicator:

```tsx
<div className="th-type-badge" style={{ color: TRACK_TYPE_COLORS[track.trackType] ?? TRACK_TYPE_COLORS[0] }}>
  {track.trackType === 2 && (
    <span className="th-chevron" onClick={(e) => { e.stopPropagation(); setTrackCollapsed(track.index, !track.isCollapsed); }}>
      {track.isCollapsed ? "\u25B6" : "\u25BC"}
    </span>
  )}
  {TRACK_TYPE_ICONS[track.trackType] ?? TRACK_TYPE_ICONS[0]}
</div>
```

- [ ] **Step 6: Add indentation for child tracks**

On the `th-row` div, add left padding for children:

```tsx
style={{ paddingLeft: track.parentId >= 0 ? 20 : 0 }}
```

- [ ] **Step 7: Add CSS for folder styles**

Open `frontend/src/components/TrackHeaders.css`. Add:

```css
.th-type-badge {
  display: flex;
  align-items: center;
  gap: 2px;
  font-size: 10px;
  width: 20px;
  flex-shrink: 0;
}

.th-chevron {
  cursor: pointer;
  font-size: 8px;
  user-select: none;
}

.th-row[data-track-type="2"] {
  background: rgba(243, 156, 18, 0.08);
}
```

- [ ] **Step 8: Verify TypeScript compiles**

Run: `cd frontend && npx tsc --noEmit`
Expected: No errors

- [ ] **Step 9: Commit**

```bash
git add frontend/src/components/TrackHeaders.tsx frontend/src/components/TrackHeaders.css
git commit -m "feat: add type indicator, folder chevron, and indentation to TrackHeaders"
```

---

## Task 12: Update TimelineMinimal component

**Files:**
- Modify: `frontend/src/components/TimelineMinimal.tsx`

- [ ] **Step 1: Add folder-aware filtering**

After the tracks are read from the snapshot, add the same hidden indices logic:

```typescript
  const hiddenIndices = new Set<number>();
  for (const track of tracks) {
    if (track.trackType === 2 && track.isCollapsed) {
      for (const child of tracks) {
        if (child.parentId === track.index) {
          hiddenIndices.add(child.index);
        }
      }
    }
  }
  const visibleTracks = tracks.filter(t => !hiddenIndices.has(t.index));
```

- [ ] **Step 2: Update totalH calculation**

Change `const totalH = tracks.length * TRACK_HEIGHT;` to:

```typescript
  const totalH = visibleTracks.length * TRACK_HEIGHT;
```

- [ ] **Step 3: Update track row iteration**

Change the `tracks.map((track, idx) => {` to `visibleTracks.map((track, idx) => {`.

- [ ] **Step 4: Add folder row styling**

On the `tl-track-row` div, add:

```tsx
data-track-type={track.trackType}
```

And in the CSS, add:

```css
.tl-track-row[data-track-type="2"] {
  background: rgba(243, 156, 18, 0.05);
}
```

- [ ] **Step 5: Update track index calculations**

All places that use `idx` to calculate track position (e.g., `idx * TRACK_HEIGHT`) should use the visible index. The `track.index` from the snapshot is the original index in the full list. Ensure that `visibleTracks.map((track, idx) => ...)` uses `idx` for positioning (not `track.index`).

- [ ] **Step 6: Verify TypeScript compiles**

Run: `cd frontend && npx tsc --noEmit`
Expected: No errors

- [ ] **Step 7: Commit**

```bash
git add frontend/src/components/TimelineMinimal.tsx
git commit -m "feat: hide collapsed folder children in timeline, add folder row styling"
```

---

## Task 13: Update MixerStrip component

**Files:**
- Modify: `frontend/src/components/MixerStrip.tsx`

- [ ] **Step 1: Use effectiveMuted for meter display**

Find where the meter is rendered. Change `track.muted` to `track.effectiveMuted` for deciding whether to silence the meter.

- [ ] **Step 2: Add visual indicator for folder tracks**

If the track is a folder (`track.trackType === 2`), show a folder icon or different styling.

- [ ] **Step 3: Verify TypeScript compiles**

Run: `cd frontend && npx tsc --noEmit`
Expected: No errors

- [ ] **Step 4: Commit**

```bash
git add frontend/src/components/MixerStrip.tsx
git commit -m "feat: use effectiveMuted in MixerStrip meters"
```

---

## Task 14: Run full build and tests

- [ ] **Step 1: Build C++**

Run: `cmake --build build --config Debug`
Expected: BUILD SUCCEEDED

- [ ] **Step 2: Build frontend**

Run: `cd frontend && npm run build`
Expected: BUILD SUCCEEDED

- [ ] **Step 3: Run C++ tests**

Run: `build\Debug\hdaw_tests.exe`
Expected: All tests pass

- [ ] **Step 4: Run frontend tests**

Run: `cd frontend && npm test`
Expected: All tests pass

- [ ] **Step 5: Run E2E tests**

Run: `cd frontend && npm run test:e2e`
Expected: All tests pass

- [ ] **Step 6: Final commit**

```bash
git add -A
git commit -m "feat: typed tracks (audio/instrument/folder) with collapse and mute cascade"
```
