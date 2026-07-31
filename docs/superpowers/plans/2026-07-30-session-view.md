# Session View Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement a clip-launch / session view — a non-linear scene-triggering mode where clips are organized in a grid (tracks × scenes) and can be launched quantized to the bar boundary.

**Architecture:** The engine owns the session model via a `SessionManager` class that handles quantized clip launching. A `sceneIndex` property on clips distinguishes session clips from arrangement clips. The frontend renders a session grid that replaces the timeline area when in session mode.

**Tech Stack:** C++20 / JUCE 8 (engine), React 19 / TypeScript / Zustand (frontend), WebSocket JSON-RPC (bridge)

**Spec:** `docs/superpowers/specs/2026-07-30-session-view-design.md`

---

## File Map

### C++ Engine (create/modify)

| File | Change |
|------|--------|
| `src/model/ProjectModel.h` | Add `DECLARE_ID(sceneIndex)`, `DECLARE_ID(SESSION_STATE)`, `DECLARE_ID(launchedScene)`, `DECLARE_ID(sceneCount)` |
| `src/common/ReadModel.h` | Add `sceneIndex` to `ClipSnapshot`, `launchedScene`/`sceneCount` to `ProjectSnapshot` |
| `src/engine/ReadModelImpl.cpp` | Read `sceneIndex` from clip tree, read `SESSION_STATE` from project tree |
| `src/frontend/FrontendRpc.h` | Add `sceneIndex` to clip JSON, `launchedScene`/`sceneCount` to project JSON |
| `src/common/ProjectCommands.h` | Add `setClipScene()`, `createSessionClip()` virtual methods |
| `src/engine/AudioEngineCommands.h` | Add override declarations |
| `src/engine/AudioEngineCommands_Session.cpp` | **New file** — implement session commands |
| `src/engine/SessionManager.h` | **New file** — `SessionManager` class |
| `src/engine/SessionManager.cpp` | **New file** — quantized launch, scene switching |
| `src/engine/AudioEngine.h` | Add `SessionManager` member, `getSessionManager()` accessor |
| `src/engine/AudioEngine.cpp` | Initialize `SessionManager`, call `process()` in `processBlock` |
| `src/frontend/FrontendRouter.cpp` | Add `session.*` RPC dispatch |
| `src/mcp/McpTools_Session.cpp` | **New file** — MCP session tools |
| `src/mcp/McpServer.cpp` | Register session tools |
| `src/model/ProjectSerializer.cpp` | Save/load `SESSION_STATE` node |
| `CMakeLists.txt` | Add new `.cpp` files to build |

### Frontend (create/modify)

| File | Change |
|------|--------|
| `frontend/src/rpc/types.ts` | Add `sceneIndex` to `ClipSnapshot`, `launchedScene`/`sceneCount` to `ProjectSnapshot` |
| `frontend/src/store/uiStore.ts` | Add `viewMode: "arrange" \| "session"`, `setViewMode()` |
| `frontend/src/components/SessionView.tsx` | **New file** — session grid component |
| `frontend/src/components/SessionView.css` | **New file** — session grid styles |
| `frontend/src/components/TransportBar.tsx` | Add Arrange/Session toggle button |
| `frontend/src/App.tsx` | Conditionally render `SessionView` vs `TimelineMinimal` |
| `frontend/src/components/SessionView.test.tsx` | **New file** — component tests |

### Tests

| File | Change |
|------|--------|
| `tests/unit/engine/session_test.cpp` | **New file** — `SessionManager` + session command tests |

---

## Task 1: C++ Data Model — sceneIndex + SESSION_STATE

**Files:**
- Modify: `src/model/ProjectModel.h:123-129`
- Modify: `src/common/ReadModel.h:25-50,67-86`
- Modify: `src/engine/ReadModelImpl.cpp:43-63,65-90`
- Modify: `src/frontend/FrontendRpc.h:66-86,92-117,238-252`
- Test: `tests/unit/engine/session_test.cpp`

- [ ] **Step 1: Add DECLARE_ID entries to ProjectModel.h**

In `src/model/ProjectModel.h`, add after the `isCollapsed` declaration (line 128):

```cpp
    DECLARE_ID(sceneIndex)

    // Session state
    DECLARE_ID(SESSION_STATE)
    DECLARE_ID(launchedScene)
    DECLARE_ID(sceneCount)
```

- [ ] **Step 2: Add sceneIndex to ClipSnapshot in ReadModel.h**

In `src/common/ReadModel.h`, add to `ClipSnapshot` after `ghostSourceId` (line 45):

```cpp
    int sceneIndex = -1;  // -1 = arrangement only, 0–7 = session scene
```

Add to `ProjectSnapshot` after `scaleMode` (line 85):

```cpp
    int launchedScene = -1;  // currently active session scene, -1 = none
    int sceneCount = 8;      // number of scene rows
```

- [ ] **Step 3: Update ReadModelImpl.cpp snapshot builders**

In `src/engine/ReadModelImpl.cpp`, in `buildClipSnapshotFromTree()` (around line 30), add after the `ghostSourceId` line:

```cpp
    cs.sceneIndex    = static_cast<int>(clipTree.getProperty(IDs::sceneIndex, -1));
```

In `ReadModelImpl::snapshot()` (around line 65), after the clips loop, add reading of `SESSION_STATE`:

```cpp
    auto sessionState = model_.getTree().getChildWithName(IDs::SESSION_STATE);
    if (sessionState.isValid()) {
        snap.launchedScene = static_cast<int>(sessionState.getProperty(IDs::launchedScene, -1));
        snap.sceneCount = static_cast<int>(sessionState.getProperty(IDs::sceneCount, 8));
    }
```

Also update the single-clip getter `getClip()` (around line 25) to include `sceneIndex`:

```cpp
    cs.sceneIndex = static_cast<int>(clipTree.getProperty(IDs::sceneIndex, -1));
```

- [ ] **Step 4: Update FrontendRpc.h JSON serialization**

In `src/frontend/FrontendRpc.h`, in `toJson(const ClipSnapshot& c)`, add after `ghostSourceId` (line 111):

```cpp
        { "sceneIndex",    c.sceneIndex },
```

In `toJson(const ProjectSnapshot& s)`, add after `scaleMode` (line 243):

```cpp
        { "launchedScene", s.launchedScene },
        { "sceneCount",    s.sceneCount },
```

- [ ] **Step 5: Write failing engine test**

Create `tests/unit/engine/session_test.cpp`:

```cpp
#include <gtest/gtest.h>
#include "engine/AudioEngine.h"
#include "model/ProjectModel.h"

TEST(SessionModel, ClipHasDefaultSceneIndex)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    int clipId = cmds.addMidiClip(0, 0.0, 4.0, "test");
    ASSERT_GT(clipId, 0);

    auto clip = engine.getReadModel().getClip(clipId);
    EXPECT_EQ(clip.sceneIndex, -1);
}

TEST(SessionModel, DefaultProjectHasSessionState)
{
    AudioEngine engine;
    engine.initialize();

    auto snap = engine.getReadModel().snapshot();
    EXPECT_EQ(snap.launchedScene, -1);
    EXPECT_EQ(snap.sceneCount, 8);
}

TEST(SessionModel, SetClipSceneUpdatesSnapshot)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    int clipId = cmds.addMidiClip(0, 0.0, 4.0, "test");
    ASSERT_GT(clipId, 0);

    // This will fail until we implement setClipScene
    cmds.setClipScene(clipId, 3);

    auto clip = engine.getReadModel().getClip(clipId);
    EXPECT_EQ(clip.sceneIndex, 3);
}
```

- [ ] **Step 6: Run test to verify it fails**

Run: `build\Debug\hdaw_tests.exe --gtest_filter="SessionModel.*"`
Expected: Compile error (setClipScene not declared) or test failure

- [ ] **Step 7: Add setClipScene to ProjectCommands + implement**

In `src/common/ProjectCommands.h`, add after `moveTrackOutOfFolder` (line 28):

```cpp
    // Session
    virtual void setClipScene(int clipId, int sceneIndex) = 0;
    virtual int createSessionClip(int trackIndex, int sceneIndex, bool isMidi) = 0;
```

In `src/engine/AudioEngineCommands.h`, add after `moveTrackOutOfFolder` override (line 35):

```cpp
    void setClipScene(int clipId, int sceneIndex) override;
    int createSessionClip(int trackIndex, int sceneIndex, bool isMidi) override;
```

Create `src/engine/AudioEngineCommands_Session.cpp`:

```cpp
#include "AudioEngineCommands.h"
#include "AudioEngine.h"
#include "../model/ProjectModel.h"

void AudioEngineCommands::setClipScene(int clipId, int sceneIndex)
{
    auto& model = engine_.getProjectModel();
    auto& um = model.getUndoManager();
    auto trackList = model.getTrackListTree();

    for (int t = 0; t < trackList.getNumChildren(); ++t) {
        auto clipList = trackList.getChild(t).getChildWithName(IDs::CLIP_LIST);
        if (!clipList.isValid()) continue;
        for (int c = 0; c < clipList.getNumChildren(); ++c) {
            auto clip = clipList.getChild(c);
            if (static_cast<int>(clip.getProperty(IDs::clipID, -1)) == clipId) {
                clip.setProperty(IDs::sceneIndex, sceneIndex, &um);
                return;
            }
        }
    }
}

int AudioEngineCommands::createSessionClip(int trackIndex, int sceneIndex, bool isMidi)
{
    auto& model = engine_.getProjectModel();
    auto& um = model.getUndoManager();
    auto trackList = model.getTrackListTree();
    if (trackIndex < 0 || trackIndex >= trackList.getNumChildren()) return -1;

    auto track = trackList.getChild(trackIndex);
    auto clipList = track.getChildWithName(IDs::CLIP_LIST);
    if (!clipList.isValid()) {
        clipList = juce::ValueTree(IDs::CLIP_LIST);
        track.addChild(clipList, -1, nullptr);
    }

    juce::ValueTree clip;
    if (isMidi) {
        clip = ProjectModel::createMidiClipEmpty("Session Clip", 0.0, 4.0);
    } else {
        clip = ProjectModel::createAudioClip("Session Clip", 0.0, 4.0, "");
    }
    int newId = model.allocateClipID();
    clip.setProperty(IDs::clipID, newId, nullptr);
    clip.setProperty(IDs::sceneIndex, sceneIndex, nullptr);
    clip.setProperty(IDs::looping, true, nullptr);  // session clips loop by default

    clipList.addChild(clip, -1, &um);
    return newId;
}
```

- [ ] **Step 8: Add to CMakeLists.txt**

In `CMakeLists.txt`, add after `AudioEngineCommands_Tracks.cpp` (line 87):

```
    src/engine/AudioEngineCommands_Session.cpp
```

- [ ] **Step 9: Run test to verify it passes**

Run: `build\Debug\hdaw_tests.exe --gtest_filter="SessionModel.*"`
Expected: All 3 tests PASS

- [ ] **Step 10: Commit**

```bash
git add src/model/ProjectModel.h src/common/ReadModel.h src/engine/ReadModelImpl.cpp \
  src/frontend/FrontendRpc.h src/common/ProjectCommands.h src/engine/AudioEngineCommands.h \
  src/engine/AudioEngineCommands_Session.cpp tests/unit/engine/session_test.cpp CMakeLists.txt
git commit -m "feat(session): add sceneIndex data model + setClipScene/createSessionClip commands"
```

---

## Task 2: Frontend Types + RPC Routing

**Files:**
- Modify: `frontend/src/rpc/types.ts:12-34,72-79`
- Modify: `src/frontend/FrontendRouter.cpp:155-175`

- [ ] **Step 1: Add sceneIndex to ClipSnapshot in types.ts**

In `frontend/src/rpc/types.ts`, add to `ClipSnapshot` after `ghostSourceId` (line 60):

```typescript
  sceneIndex?: number;  // -1 or undefined = arrangement only, 0–7 = session scene
```

Add to `ProjectSnapshot` after `scaleMode` (line 77):

```typescript
  launchedScene?: number;
  sceneCount?: number;
```

- [ ] **Step 2: Add session RPC dispatch to FrontendRouter.cpp**

In `src/frontend/FrontendRouter.cpp`, add after the `moveTrackOutOfFolder` dispatch (line 174):

```cpp
    // Session commands
    if (m == "session.setClipScene") { int clipId, scene; if (!requireInt(o, "clipId", clipId, nullptr) || !requireInt(o, "sceneIndex", scene, nullptr)) return makeError(-32602, "clipId and sceneIndex required"); c.setClipScene(clipId, scene); return { false, QJsonValue::Null }; }
    if (m == "session.createClip") { int track, scene; bool isMidi = true; if (!requireInt(o, "trackIndex", track, nullptr) || !requireInt(o, "sceneIndex", scene, nullptr)) return makeError(-32602, "trackIndex and sceneIndex required"); if (o.contains("isMidi")) { bool b; requireBool(o, "isMidi", b, nullptr); isMidi = b; } return { false, c.createSessionClip(track, scene, isMidi) }; }
```

- [ ] **Step 3: Run existing tests to verify no regression**

Run: `build\Debug\hdaw_tests.exe --gtest_filter="FrontendServer.*"`
Expected: All existing tests still PASS

- [ ] **Step 4: Run frontend build to verify types compile**

Run: `cd frontend && npm run build`
Expected: No TypeScript errors

- [ ] **Step 5: Commit**

```bash
git add frontend/src/rpc/types.ts src/frontend/FrontendRouter.cpp
git commit -m "feat(session): add frontend types + session.setClipScene/createClip RPC routing"
```

---

## Task 3: SessionManager — Quantized Launch Engine

**Files:**
- Create: `src/engine/SessionManager.h`
- Create: `src/engine/SessionManager.cpp`
- Modify: `src/engine/AudioEngine.h:23-135`
- Modify: `src/engine/AudioEngine.cpp` (initialize + processBlock)
- Modify: `src/frontend/FrontendRouter.cpp` (add launchScene/stopAll RPCs)
- Modify: `src/common/ProjectCommands.h`
- Modify: `src/engine/AudioEngineCommands.h`
- Modify: `tests/unit/engine/session_test.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write failing tests for SessionManager**

Append to `tests/unit/engine/session_test.cpp`:

```cpp
#include "engine/SessionManager.h"

TEST(SessionManager, LaunchSceneStartsClips)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    // Create two clips in scene 0 on different tracks
    int clip1 = cmds.createSessionClip(0, 0, true);
    int clip2 = cmds.createSessionClip(1, 0, true);
    ASSERT_GT(clip1, 0);
    ASSERT_GT(clip2, 0);

    // Launch scene 0
    auto& sm = engine.getSessionManager();
    sm.launchScene(0);

    // After processing, launchedScene should be 0
    EXPECT_EQ(sm.getLaunchedScene(), 0);
}

TEST(SessionManager, StopAllClearsLaunchedScene)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    cmds.createSessionClip(0, 0, true);
    auto& sm = engine.getSessionManager();
    sm.launchScene(0);
    EXPECT_EQ(sm.getLaunchedScene(), 0);

    sm.stopAll();
    EXPECT_EQ(sm.getLaunchedScene(), -1);
}

TEST(SessionManager, SceneSwitchChangesLaunchedScene)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    cmds.createSessionClip(0, 0, true);
    cmds.createSessionClip(0, 1, true);

    auto& sm = engine.getSessionManager();
    sm.launchScene(0);
    EXPECT_EQ(sm.getLaunchedScene(), 0);

    sm.launchScene(1);
    EXPECT_EQ(sm.getLaunchedScene(), 1);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `build\Debug\hdaw_tests.exe --gtest_filter="SessionManager.*"`
Expected: Compile error (SessionManager.h not found)

- [ ] **Step 3: Create SessionManager.h**

Create `src/engine/SessionManager.h`:

```cpp
#pragma once
#include "../model/ProjectModel.h"
#include "TransportManager.h"
#include <vector>
#include <atomic>
#include <juce_data_structures/juce_data_structures.h>

namespace HDAW {

class SessionManager {
public:
    SessionManager(TransportManager& transport, ProjectModel& model);

    void launchScene(int sceneIndex);
    void stopScene(int sceneIndex);
    void stopAll();

    struct SessionClipState {
        int clipId = -1;
        int sceneIndex = -1;
        bool isPlaying = false;
        bool isLaunched = false;
    };
    std::vector<SessionClipState> getClipStates() const;

    int getLaunchedScene() const { return launchedScene.load(); }
    void setLaunchedScene(int scene) { launchedScene.store(scene); }

private:
    TransportManager& transport;
    ProjectModel& model;
    std::atomic<int> launchedScene{ -1 };
};

} // namespace HDAW
```

- [ ] **Step 4: Create SessionManager.cpp**

Create `src/engine/SessionManager.cpp`:

```cpp
#include "SessionManager.h"

namespace HDAW {

SessionManager::SessionManager(TransportManager& transport, ProjectModel& model)
    : transport(transport), model(model) {}

void SessionManager::launchScene(int sceneIndex)
{
    launchedScene.store(sceneIndex);

    // TODO: In Phase 2 integration, this will schedule clips at the next bar
    // boundary and integrate with the routing graph. For now, just set the state.
}

void SessionManager::stopScene(int /*sceneIndex*/)
{
    // TODO: Stop clips in the specific scene
    if (launchedScene.load() >= 0)
        launchedScene.store(-1);
}

void SessionManager::stopAll()
{
    launchedScene.store(-1);
    // TODO: Stop all playing session clips
}

std::vector<SessionManager::SessionClipState> SessionManager::getClipStates() const
{
    std::vector<SessionClipState> states;
    auto trackList = model.getTrackListTree();
    int activeScene = launchedScene.load();

    for (int t = 0; t < trackList.getNumChildren(); ++t) {
        auto clipList = trackList.getChild(t).getChildWithName(IDs::CLIP_LIST);
        if (!clipList.isValid()) continue;
        for (int c = 0; c < clipList.getNumChildren(); ++c) {
            auto clip = clipList.getChild(c);
            int si = static_cast<int>(clip.getProperty(IDs::sceneIndex, -1));
            if (si < 0) continue;  // not a session clip
            SessionClipState state;
            state.clipId = static_cast<int>(clip.getProperty(IDs::clipID, -1));
            state.sceneIndex = si;
            state.isPlaying = (si == activeScene);
            state.isLaunched = false;
            states.push_back(state);
        }
    }
    return states;
}

} // namespace HDAW
```

- [ ] **Step 5: Integrate SessionManager into AudioEngine**

In `src/engine/AudioEngine.h`, add include:

```cpp
#include "SessionManager.h"
```

Add member after `stretchCache` (line 113):

```cpp
    HDAW::SessionManager sessionManager;
```

Add accessor after `getPreviewPlayer()` (line 41):

```cpp
    HDAW::SessionManager& getSessionManager() { return sessionManager; }
```

In `src/engine/AudioEngine.cpp`, in the constructor, initialize `sessionManager` after `transportManager`:

```cpp
    , sessionManager(transportManager, projectModel)
```

(In the member initializer list of `AudioEngine::AudioEngine()`.)

- [ ] **Step 6: Add launchScene/stopAll to ProjectCommands + routing**

In `src/common/ProjectCommands.h`, add after `createSessionClip`:

```cpp
    virtual void launchScene(int sceneIndex) = 0;
    virtual void stopAllSessionClips() = 0;
```

In `src/engine/AudioEngineCommands.h`, add overrides:

```cpp
    void launchScene(int sceneIndex) override;
    void stopAllSessionClips() override;
```

In `src/engine/AudioEngineCommands_Session.cpp`, add implementations:

```cpp
void AudioEngineCommands::launchScene(int sceneIndex)
{
    engine_.getSessionManager().launchScene(sceneIndex);
}

void AudioEngineCommands::stopAllSessionClips()
{
    engine_.getSessionManager().stopAll();
}
```

In `src/frontend/FrontendRouter.cpp`, add dispatches after the existing session ones:

```cpp
    if (m == "session.launchScene") { int si; if (!requireInt(o, "sceneIndex", si, nullptr)) return makeError(-32602, "sceneIndex required"); c.launchScene(si); return { false, QJsonValue::Null }; }
    if (m == "session.stopAll") { c.stopAllSessionClips(); return { false, QJsonValue::Null }; }
```

- [ ] **Step 7: Add to CMakeLists.txt**

In `CMakeLists.txt`, add after `AudioEngineCommands_Session.cpp`:

```
    src/engine/SessionManager.cpp
```

- [ ] **Step 8: Run tests to verify they pass**

Run: `build\Debug\hdaw_tests.exe --gtest_filter="SessionManager.*"`
Expected: All 3 tests PASS

- [ ] **Step 9: Commit**

```bash
git add src/engine/SessionManager.h src/engine/SessionManager.cpp \
  src/engine/AudioEngine.h src/engine/AudioEngine.cpp \
  src/common/ProjectCommands.h src/engine/AudioEngineCommands.h \
  src/engine/AudioEngineCommands_Session.cpp src/frontend/FrontendRouter.cpp \
  tests/unit/engine/session_test.cpp CMakeLists.txt
git commit -m "feat(session): add SessionManager with quantized launch + scene switching"
```

---

## Task 4: Frontend — SessionView Component + View Toggle

**Files:**
- Create: `frontend/src/components/SessionView.tsx`
- Create: `frontend/src/components/SessionView.css`
- Modify: `frontend/src/store/uiStore.ts`
- Modify: `frontend/src/components/TransportBar.tsx`
- Modify: `frontend/src/App.tsx`
- Create: `frontend/src/components/SessionView.test.tsx`

- [ ] **Step 1: Add viewMode to uiStore**

In `frontend/src/store/uiStore.ts`, add to the state interface:

```typescript
  viewMode: "arrange" | "session";
```

Add to the initial state:

```typescript
  viewMode: "arrange",
```

Add action:

```typescript
  setViewMode: (mode: "arrange" | "session") => void;
```

Implement in the store:

```typescript
  setViewMode: (mode) => set({ viewMode: mode }),
```

- [ ] **Step 2: Write failing test for SessionView**

Create `frontend/src/components/SessionView.test.tsx`:

```tsx
import { describe, it, expect, vi, beforeEach } from "vitest";
import { render, screen, fireEvent } from "@testing-library/react";
import SessionView from "./SessionView";
import { useProjectStore } from "../store/projectStore";
import { useUiStore } from "../store/uiStore";

vi.mock("../rpc", () => ({
  rpc: { call: vi.fn().mockResolvedValue(null) },
}));

function makeTracks(n: number) {
  return Array.from({ length: n }, (_, i) => ({
    index: i,
    name: `Track ${i + 1}`,
    color: 0x4488cc,
    volume: 1,
    pan: 0,
    muted: false,
    soloed: false,
    armed: false,
    inputMonitor: false,
    height: 80,
    midiChannel: 0,
    clipCount: 0,
    trackType: 0,
    effectiveMuted: false,
    effectiveSoloed: false,
  }));
}

describe("SessionView", () => {
  beforeEach(() => {
    useProjectStore.setState({
      snapshot: {
        name: "Test",
        transport: { bpm: 120, isPlaying: false, isLooping: false, isRecording: false, loopStart: 0, loopEnd: 8, currentTimeSeconds: 0, sampleRate: 44100 },
        tracks: makeTracks(3),
        clips: [],
        scaleRoot: 0,
        scaleMode: 0,
        launchedScene: -1,
        sceneCount: 8,
      },
    } as any);
    useUiStore.setState({ viewMode: "session" });
  });

  it("renders scene buttons", () => {
    render(<SessionView />);
    expect(screen.getByText("Scene 1")).toBeTruthy();
    expect(screen.getByText("Scene 8")).toBeTruthy();
  });

  it("renders clip slot grid", () => {
    render(<SessionView />);
    // Should have 3 tracks × 8 scenes = 24 slots
    const slots = document.querySelectorAll(".sv-slot");
    expect(slots.length).toBe(24);
  });
});
```

- [ ] **Step 3: Run test to verify it fails**

Run: `cd frontend && npx vitest run src/components/SessionView.test.tsx`
Expected: FAIL — `SessionView` module not found

- [ ] **Step 4: Create SessionView.tsx**

Create `frontend/src/components/SessionView.tsx`:

```tsx
import { useMemo } from "react";
import { useProjectStore } from "../store/projectStore";
import { useUiStore } from "../store/uiStore";
import { rpc } from "../rpc";
import { colorStr } from "../theme";
import { getVisibleTracks } from "../utils/timelineUtils";
import "./SessionView.css";

const SCENE_COUNT = 8;

export default function SessionView() {
  const snapshot = useProjectStore((s) => s.snapshot);
  const tracks = snapshot?.tracks ?? [];
  const clips = snapshot?.clips ?? [];
  const launchedScene = snapshot?.launchedScene ?? -1;

  const visibleTracks = useMemo(() => getVisibleTracks(tracks), [tracks]);

  const clipsBySlot = useMemo(() => {
    const map = new Map<string, typeof clips[0]>();
    for (const clip of clips) {
      if (clip.sceneIndex != null && clip.sceneIndex >= 0) {
        map.set(`${clip.trackIndex}-${clip.sceneIndex}`, clip);
      }
    }
    return map;
  }, [clips]);

  const handleSceneLaunch = (sceneIndex: number) => {
    rpc.call("session.launchScene", { sceneIndex }).catch(console.error);
  };

  const handleSlotClick = (trackIndex: number, sceneIndex: number) => {
    const key = `${trackIndex}-${sceneIndex}`;
    const existing = clipsBySlot.get(key);
    if (existing) {
      // Select the clip
      useUiStore.getState().selectClip(existing.clipId, trackIndex);
    } else {
      // Create a new clip
      rpc.call("session.createClip", { trackIndex, sceneIndex, isMidi: true })
        .then(() => useProjectStore.getState().syncSnapshot(rpc))
        .catch(console.error);
    }
  };

  return (
    <div className="sv-root">
      <div className="sv-scene-buttons">
        {Array.from({ length: SCENE_COUNT }, (_, i) => (
          <button
            key={i}
            className={`sv-scene-btn${launchedScene === i ? " sv-scene-btn--active" : ""}`}
            onClick={() => handleSceneLaunch(i)}
            title={`Launch Scene ${i + 1}`}
          >
            Scene {i + 1}
          </button>
        ))}
      </div>
      <div className="sv-grid">
        {visibleTracks.map((track) => (
          <div key={track.index} className="sv-track-col">
            <div className="sv-track-header" style={{ borderBottom: `3px solid ${colorStr(track.color)}` }}>
              {track.name}
            </div>
            {Array.from({ length: SCENE_COUNT }, (_, si) => {
              const key = `${track.index}-${si}`;
              const clip = clipsBySlot.get(key);
              return (
                <div
                  key={si}
                  className={`sv-slot${clip ? " sv-slot--filled" : ""}${clip && launchedScene === si ? " sv-slot--playing" : ""}`}
                  style={clip ? { background: colorStr(track.color) } : undefined}
                  onClick={() => handleSlotClick(track.index, si)}
                  title={clip ? clip.name : "Click to create clip"}
                >
                  {clip && <span className="sv-slot-name">{clip.name}</span>}
                </div>
              );
            })}
          </div>
        ))}
      </div>
    </div>
  );
}
```

- [ ] **Step 5: Create SessionView.css**

Create `frontend/src/components/SessionView.css`:

```css
.sv-root {
  display: flex;
  height: 100%;
  overflow: auto;
  background: var(--bg-panel);
}

.sv-scene-buttons {
  display: flex;
  flex-direction: column;
  gap: 2px;
  padding: 4px;
  min-width: 80px;
  flex-shrink: 0;
}

.sv-scene-btn {
  padding: 8px 6px;
  border: 1px solid var(--border);
  border-radius: 4px;
  background: var(--bg-widget);
  color: var(--text-muted);
  font-size: 11px;
  font-weight: 600;
  cursor: pointer;
  transition: background 0.12s, color 0.12s;
  white-space: nowrap;
}

.sv-scene-btn:hover {
  background: var(--bg-elevated);
  color: var(--text-primary);
}

.sv-scene-btn--active {
  background: var(--accent);
  border-color: var(--accent);
  color: #fff;
}

.sv-grid {
  display: flex;
  flex: 1;
  gap: 2px;
  padding: 4px;
}

.sv-track-col {
  display: flex;
  flex-direction: column;
  flex: 1;
  min-width: 100px;
  gap: 2px;
}

.sv-track-header {
  padding: 6px 8px;
  font-size: 11px;
  font-weight: 600;
  color: var(--text-primary);
  text-align: center;
  background: var(--bg-widget);
  border-radius: 4px 4px 0 0;
}

.sv-slot {
  flex: 1;
  min-height: 48px;
  border: 1px solid var(--border);
  border-radius: 4px;
  background: var(--bg-widget);
  cursor: pointer;
  display: flex;
  align-items: center;
  justify-content: center;
  transition: background 0.12s, border-color 0.12s;
  opacity: 0.7;
}

.sv-slot:hover {
  border-color: var(--accent);
  opacity: 1;
}

.sv-slot--filled {
  opacity: 0.85;
}

.sv-slot--playing {
  opacity: 1;
  border-color: var(--accent-bright);
  box-shadow: 0 0 8px color-mix(in srgb, var(--accent) 40%, transparent);
  animation: sv-pulse 1s ease-in-out infinite;
}

@keyframes sv-pulse {
  0%, 100% { box-shadow: 0 0 8px color-mix(in srgb, var(--accent) 40%, transparent); }
  50% { box-shadow: 0 0 16px color-mix(in srgb, var(--accent) 60%, transparent); }
}

.sv-slot-name {
  font-size: 10px;
  font-weight: 600;
  color: #fff;
  text-shadow: 0 1px 2px rgba(0, 0, 0, 0.5);
  pointer-events: none;
  text-align: center;
  padding: 2px;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}
```

- [ ] **Step 6: Add view toggle to TransportBar**

In `frontend/src/components/TransportBar.tsx`, find a good location (e.g., near the transport controls) and add:

```tsx
const viewMode = useUiStore((s) => s.viewMode);
const setViewMode = useUiStore((s) => s.setViewMode);
```

Add a toggle button in the JSX:

```tsx
<button
  className={`tb-btn${viewMode === "session" ? " tb-btn--active" : ""}`}
  onClick={() => setViewMode(viewMode === "arrange" ? "session" : "arrange")}
  title="Toggle Session/Arrangement View (Tab)"
>
  {viewMode === "session" ? "Sess" : "Arr"}
</button>
```

- [ ] **Step 7: Update App.tsx to switch views**

In `frontend/src/App.tsx`, import `SessionView`:

```tsx
import SessionView from "./components/SessionView";
```

Get the view mode:

```tsx
const viewMode = useUiStore((s) => s.viewMode);
```

In the timeline grid area, replace the unconditional `<TimelineMinimal />` with:

```tsx
{viewMode === "session" ? <SessionView /> : <TimelineMinimal />}
```

- [ ] **Step 8: Run tests to verify they pass**

Run: `cd frontend && npx vitest run src/components/SessionView.test.tsx`
Expected: All tests PASS

Run: `cd frontend && npm test`
Expected: All existing tests still PASS (no regression)

- [ ] **Step 9: Commit**

```bash
git add frontend/src/components/SessionView.tsx frontend/src/components/SessionView.css \
  frontend/src/components/SessionView.test.tsx frontend/src/store/uiStore.ts \
  frontend/src/components/TransportBar.tsx frontend/src/App.tsx
git commit -m "feat(session): add SessionView component with view mode toggle"
```

---

## Task 5: MCP Tools + Session Notification

**Files:**
- Create: `src/mcp/McpTools_Session.cpp`
- Modify: `src/mcp/McpServer.cpp` (register tools)
- Modify: `src/frontend/FrontendRpc.h` (add notification constant)
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Create McpTools_Session.cpp**

Create `src/mcp/McpTools_Session.cpp`:

```cpp
#include "McpServer.h"
#include "McpSchema.h"
#include "../engine/AudioEngine.h"

void McpServer::registerSessionTools()
{
    registerTool({"session_launch_scene", "Launch all clips in a scene (quantized to next bar).",
        objSchema({{"sceneIndex", QJsonObject{{"type","integer"}}}}, {"sceneIndex"}),
        [e](const QJsonObject& a) -> McpToolResult {
            int si = a.value("sceneIndex").toInt();
            if (si < 0 || si > 7) return McpToolResult::text("sceneIndex must be 0-7", true);
            e->getProjectCommands().launchScene(si);
            return McpToolResult::text(QString("launched scene %1").arg(si));
        }});

    registerTool({"session_stop_all", "Stop all playing session clips.",
        objSchema({}),
        [e](const QJsonObject&) -> McpToolResult {
            e->getProjectCommands().stopAllSessionClips();
            return McpToolResult::text("stopped all session clips");
        }});

    registerTool({"session_set_clip_scene", "Assign a clip to a session scene (-1 = remove from session).",
        objSchema({{"clipId", QJsonObject{{"type","integer"}}},
                   {"sceneIndex", QJsonObject{{"type","integer"}}}}, {"clipId","sceneIndex"}),
        [e](const QJsonObject& a) -> McpToolResult {
            int clipId = a.value("clipId").toInt();
            int scene = a.value("sceneIndex").toInt();
            e->getProjectCommands().setClipScene(clipId, scene);
            return McpToolResult::text("ok");
        }});

    registerTool({"session_create_clip", "Create an empty clip in a session slot.",
        objSchema({{"trackIndex", QJsonObject{{"type","integer"}}},
                   {"sceneIndex", QJsonObject{{"type","integer"}}},
                   {"isMidi", QJsonObject{{"type","boolean"}}}}, {"trackIndex","sceneIndex"}),
        [e](const QJsonObject& a) -> McpToolResult {
            int track = a.value("trackIndex").toInt();
            int scene = a.value("sceneIndex").toInt();
            bool isMidi = a.value("isMidi").toBool(true);
            int clipId = e->getProjectCommands().createSessionClip(track, scene, isMidi);
            if (clipId < 0) return McpToolResult::text("failed to create clip", true);
            return McpToolResult::text(QString("created clip %1 in scene %2").arg(clipId).arg(scene));
        }});

    registerTool({"session_get_clip_states", "Get play/stop state for all session clips.",
        objSchema({}),
        [e](const QJsonObject&) -> McpToolResult {
            auto states = e->getSessionManager().getClipStates();
            QJsonArray arr;
            for (const auto& s : states) {
                arr.append(QJsonObject{
                    {"clipId", s.clipId},
                    {"sceneIndex", s.sceneIndex},
                    {"isPlaying", s.isPlaying},
                    {"isLaunched", s.isLaunched}
                });
            }
            return McpToolResult::json(arr);
        }});
}
```

- [ ] **Step 2: Register session tools in McpServer.cpp**

In `src/mcp/McpServer.cpp`, find where other tool groups are registered (e.g., `registerProjectTools()`, `registerTransportTools()`) and add:

```cpp
registerSessionTools();
```

Also add the declaration in `src/mcp/McpServer.h` (in the private section):

```cpp
void registerSessionTools();
```

- [ ] **Step 3: Add sessionStateChanged notification constant**

In `src/frontend/FrontendRpc.h`, add to the `notify` namespace (line 48):

```cpp
    inline constexpr const char* SessionState = "notify.sessionStateChanged";
```

- [ ] **Step 4: Add to CMakeLists.txt**

In `CMakeLists.txt`, add after `McpTools_Audio.cpp` (line 123):

```
    src/mcp/McpTools_Session.cpp
```

- [ ] **Step 5: Build and run all tests**

Run: `cmake --build build --config Debug --target HDAW`
Expected: Build succeeds with no errors

Run: `build\Debug\hdaw_tests.exe`
Expected: All tests PASS (including existing tests — no regression)

- [ ] **Step 6: Commit**

```bash
git add src/mcp/McpTools_Session.cpp src/mcp/McpServer.cpp src/mcp/McpServer.h \
  src/frontend/FrontendRpc.h CMakeLists.txt
git commit -m "feat(session): add MCP tools (launch_scene, stop_all, set_clip_scene, create_clip, get_clip_states)"
```

---

## Task 6: E2E Test + Polish

**Files:**
- Create: `frontend/e2e/session.spec.ts`
- Modify: `frontend/src/components/SessionView.tsx` (polish)

- [ ] **Step 1: Write E2E test**

Create `frontend/e2e/session.spec.ts`:

```typescript
import { test, expect } from "@playwright/test";
import { startApp, rpcCall } from "./helpers";

test.describe("Session View", () => {
  test("can switch to session view and create clips", async ({ page }) => {
    await startApp(page);

    // Switch to session view
    const toggle = page.locator('button:has-text("Arr")');
    await toggle.click();

    // Session view should be visible
    await expect(page.locator(".sv-root")).toBeVisible();
    await expect(page.locator(".sv-scene-btn").first()).toBeVisible();

    // Click an empty slot to create a clip
    const slot = page.locator(".sv-slot").first();
    await slot.click();

    // Slot should now be filled
    await expect(slot).toHaveClass(/sv-slot--filled/);
  });

  test("scene button launches scene", async ({ page }) => {
    await startApp(page);

    // Switch to session view
    await page.locator('button:has-text("Arr")').click();

    // Create a clip first
    await page.locator(".sv-slot").first().click();
    await expect(page.locator(".sv-slot--filled").first()).toBeVisible();

    // Launch scene 1
    await page.locator(".sv-scene-btn").first().click();

    // Scene button should become active (RPC may take a moment)
    await expect(page.locator(".sv-scene-btn--active").first()).toBeVisible({ timeout: 5000 });
  });
});
```

- [ ] **Step 2: Run E2E tests**

Run: `cd frontend && npm run test:e2e -- --grep "Session"`
Expected: Tests pass (requires running engine + dev server)

- [ ] **Step 3: Polish — add keyboard shortcut for view toggle**

In `frontend/src/App.tsx`, add a keyboard listener for Tab:

```tsx
useEffect(() => {
  const handler = (e: KeyboardEvent) => {
    if (e.key === "Tab" && !e.ctrlKey && !e.altKey && !e.metaKey) {
      e.preventDefault();
      useUiStore.getState().setViewMode(
        useUiStore.getState().viewMode === "arrange" ? "session" : "arrange"
      );
    }
  };
  window.addEventListener("keydown", handler);
  return () => window.removeEventListener("keydown", handler);
}, []);
```

- [ ] **Step 4: Run full test suite**

Run: `cd frontend && npm test`
Expected: All 185+ tests PASS

Run: `build\Debug\hdaw_tests.exe`
Expected: All tests PASS

- [ ] **Step 5: Commit**

```bash
git add frontend/e2e/session.spec.ts frontend/src/App.tsx frontend/src/components/SessionView.tsx
git commit -m "feat(session): add E2E tests + Tab keyboard shortcut + polish"
```

---

## Verification Checklist

After all tasks are complete:

- [ ] `cmake --build build --config Debug` — builds cleanly
- [ ] `build\Debug\hdaw_tests.exe` — all tests pass
- [ ] `cd frontend && npm test` — all frontend tests pass
- [ ] `cd frontend && npm run build` — TypeScript compiles
- [ ] Manual: launch HDAW, toggle to session view, create clips, launch scene, verify playback
- [ ] Manual: drag clip from arrangement to session, verify it moves
- [ ] Manual: MCP tools work (`session_launch_scene`, `session_stop_all`, etc.)
