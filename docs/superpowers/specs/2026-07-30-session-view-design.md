# Session View Design

**Date:** 2026-07-30
**Status:** Approved
**Scope:** Clip-launch / session view — non-linear scene triggering paradigm

---

## Overview

HDAW's session view is a non-linear clip-launching mode inspired by Ableton
Live's Session View. It provides a grid of clip slots organized by **scenes**
(rows) and **tracks** (columns). Clips can be triggered independently,
quantized to the global beat grid, enabling live performance and idea
sketching alongside the existing linear arrangement view.

### Key decisions

- **Placement:** Replaces the timeline area (toggle between Arrange and Session)
- **Clip data:** Shared clip pool — session clips use the same `ClipSnapshot` model
- **Playback:** Quantized scene triggering (clips start at next bar boundary)
- **Scenes:** Fixed 8 scene rows

---

## 1. Data Model

### Clip scene assignment

A `sceneIndex` property is added to the `CLIP` ValueTree node:

```
CLIP
  sceneIndex: int   // -1 = arrangement only, 0–7 = session scene
```

- **Arrangement-only clip:** `sceneIndex = -1` (or absent), has `startTime` set
- **Session-only clip:** `sceneIndex >= 0`, may or may not have `startTime`
- **Dual-view clip:** `sceneIndex >= 0` AND `startTime` set — appears in both views

Clips are stored in the same `CLIP_LIST` under each `TRACK`. The `sceneIndex`
property is the only distinction between arrangement and session clips.

### Session state

A new `SESSION_STATE` ValueTree node under `PROJECT`:

```
SESSION_STATE
  launchedScene: int   // -1 = none, 0–7 = currently active scene
  sceneCount: int      // default 8
```

`launchedScene` tracks which scene is currently playing. When a new scene is
launched, the previous scene's clips stop and the new scene's clips start.

### ReadModel changes

`ClipSnapshot` (in `ReadModel.h`) gains:

```cpp
int sceneIndex = -1;  // -1 = arrangement only, 0–7 = session scene
```

`ProjectSnapshot` gains:

```cpp
int launchedScene = -1;  // currently active session scene
int sceneCount = 8;      // number of scene rows
```

The `FrontendRpc.h` JSON serialization adds `sceneIndex` to clip JSON and
`launchedScene`/`sceneCount` to project JSON.

### Frontend types

`ClipSnapshot` (in `types.ts`) gains:

```typescript
sceneIndex?: number;  // -1 or undefined = arrangement only, 0–7 = session scene
```

`ProjectSnapshot` gains:

```typescript
launchedScene?: number;
sceneCount?: number;
```

---

## 2. Engine: SessionManager

### Class design

```cpp
class SessionManager {
public:
    SessionManager(TransportManager& transport, ProjectModel& model);

    // Launch all clips in a scene at the next bar boundary
    void launchScene(int sceneIndex);

    // Stop all clips in a specific scene
    void stopScene(int sceneIndex);

    // Stop all playing session clips
    void stopAll();

    // Called from processBlock each audio block
    void process(juce::int64 currentSample, int numSamples);

    // Query current state
    struct SessionClipState {
        int clipId;
        int sceneIndex;
        bool isPlaying;
        bool isLaunched;  // triggered, waiting for quantize boundary
    };
    std::vector<SessionClipState> getClipStates() const;

    int getLaunchedScene() const;
    void setLaunchedScene(int scene);  // for project load
};
```

### Quantization logic

1. `launchScene(sceneIndex)` is called from the RPC handler
2. If transport is stopped, start it first
3. Compute next bar boundary:
   - Get current beat position from `TransportManager`
   - Get beats-per-bar from time signature
   - `nextBarBeat = ceil(currentBeat / beatsPerBar) * beatsPerBar`
   - Convert to sample position via tempo map
4. Store a pending launch: `{ sceneIndex, targetSample, clipIds[] }`
5. In `process()`, when `currentSample >= targetSample`:
   - Stop all currently-playing session clips (from previous scene)
   - Start all clips in the new scene
   - Update `launchedScene`
   - Notify frontend via `notify.sessionStateChanged`

### Integration with AudioEngine

- `AudioEngine` owns `SessionManager` as a member
- `processBlock` calls `sessionManager_.process()` after transport advance
- `RoutingManager` handles session clip processors the same as arrangement clips
  — when a scene launches, the routing graph includes those clip processors
- Project load/save reads/writes `SESSION_STATE` and `sceneIndex` on clips

---

## 3. RPC Surface

### New RPCs (namespace: `session`)

| Method | Params | Returns | Description |
|--------|--------|---------|-------------|
| `session.launchScene` | `{ sceneIndex: int }` | `null` | Launch scene (quantized to next bar) |
| `session.stopScene` | `{ sceneIndex: int }` | `null` | Stop all clips in scene |
| `session.stopAll` | `{}` | `null` | Stop all session clips |
| `session.setClipScene` | `{ clipId: int, sceneIndex: int }` | `null` | Assign clip to scene (-1 = remove) |
| `session.createClip` | `{ trackIndex: int, sceneIndex: int, type: "midi"\|"audio" }` | `clipId` | Create empty clip in slot (default: 1 bar duration, looping=true, MIDI type creates empty note list) |
| `session.getClipStates` | `{}` | `SessionClipState[]` | Get play/stop states |

### FrontendRouter dispatch

Added to `FrontendRouter.cpp` alongside existing `project.*` dispatches.
Pattern matches existing RPCs (e.g., `project.setTrackCollapsed`).

### MCP tools

| Tool | Description |
|------|-------------|
| `session_launch_scene` | Launch a scene by index |
| `session_stop_all` | Stop all session clips |
| `session_set_clip_scene` | Assign clip to scene |
| `session_create_clip` | Create clip in session slot |

### Notifications

`notify.sessionStateChanged` — pushed when:
- A scene is launched or stopped
- A clip starts or stops playing
- `launchedScene` changes

Payload: `{ launchedScene: int, clipStates: SessionClipState[] }`

---

## 4. Frontend UI

### View mode toggle

- Toggle button in `TransportBar`: "Arrange" / "Session"
- State: `uiStore.viewMode: "arrange" | "session"` (default: `"arrange"`)
- When `"session"`, the `timeline` grid area renders `<SessionView />`
  instead of `<TimelineMinimal />`
- Keyboard shortcut: Tab (standard DAW convention)

### SessionView component

```
┌─────────────────────────────────────────────────────────┐
│              │ Track 1 │ Track 2 │ Track 3 │ Track 4   │
├──────────────┼─────────┼─────────┼─────────┼───────────┤
│ [▶ Scene 1]  │  Clip   │  Clip   │  (empty) │  Clip    │
│ [▶ Scene 2]  │  Clip   │  (empty) │  Clip   │  (empty) │
│ [▶ Scene 3]  │  (empty) │  Clip   │  Clip   │  Clip    │
│ [▶ Scene 4]  │  Clip   │  Clip   │  Clip   │  (empty) │
│ [▶ Scene 5]  │  (empty) │  (empty) │  (empty) │  (empty) │
│ [▶ Scene 6]  │  (empty) │  (empty) │  (empty) │  (empty) │
│ [▶ Scene 7]  │  (empty) │  (empty) │  (empty) │  (empty) │
│ [▶ Scene 8]  │  (empty) │  (empty) │  (empty) │  (empty) │
└─────────────────────────────────────────────────────────┘
```

**Scene buttons** (left column, 8 rows):
- Click: launch scene (calls `session.launchScene`)
- Right-click: context menu (stop scene, rename, clear)
- Active scene: highlighted with accent color, pulsing indicator
- Launched (waiting for quantize): dimmed highlight

**Clip slots** (grid cells):
- Empty slot: click to create new clip (`session.createClip`)
- Existing clip: shows clip name, track color tint
- Playing clip: pulsing glow animation
- Click: select clip (opens in bottom panel editor)
- Right-click: context menu (delete, duplicate, rename, assign to different scene)
- Drag: move clip to different slot (track or scene)

**Track columns** match the current track list (same tracks as arrangement).
Track headers on the left are the same `TrackHeaders` component.

### Interaction patterns

- **Double-click clip:** Open in piano roll (MIDI) or audio editor (audio)
  — same as arrangement clips
- **Drag from arrangement:** Drop onto session slot → sets `sceneIndex`
- **Drag from session:** Drop onto arrangement → sets `startTime`
- **Ctrl+C/V:** Copy/paste clips between views

---

## 5. Playback Behavior

### Scene launching

1. User clicks scene button (or MCP tool call)
2. `session.launchScene` RPC → engine's `SessionManager::launchScene()`
3. If transport is stopped: start transport, then schedule clips
4. Compute next bar boundary from current position
5. Store pending launch with target sample position
6. At target sample: stop ALL currently-playing session clips (only one scene
   active at a time), start the new scene's clips
7. Update `launchedScene` to the new scene index
8. Push `notify.sessionStateChanged` to frontend

### Stop behavior

- **Click playing clip:** Stop that clip at next bar boundary
- **Click scene button again:** Stop that scene's clips
- **Stop All button:** Stop all session clips
- **Transport stop:** All session clips stop immediately

### Loop behavior

- Session clips loop by default (the `looping` property is `true` by default)
- Clip duration defines the loop length
- Clips loop until explicitly stopped or scene changes

### Interaction with arrangement playback

- Session clips and arrangement clips can play simultaneously
- When the user triggers a session scene while the arrangement is playing,
  both play together (like Ableton's session/arrangement overdub)
- Session clips are mixed into the same audio output via the existing
  routing graph
- Stopping the transport stops both arrangement and session playback

### Quantize options (future)

Default: quantize to 1 bar. Future enhancement:
- Beat quantize (1/4 note)
- Half-bar quantize
- No quantize (immediate)

---

## 6. Phased Implementation

### Phase 1: Data model (C++ + frontend types)

- Add `sceneIndex` to `CLIP` ValueTree IDs
- Add `SESSION_STATE` ValueTree node
- Update `ClipSnapshot` with `sceneIndex`
- Update `ProjectSnapshot` with `launchedScene`, `sceneCount`
- Update `FrontendRpc.h` JSON serialization
- Update `ReadModelImpl.cpp` snapshot builders
- Update frontend `types.ts`
- Add `session.setClipScene` RPC
- Add `session.createClip` RPC
- Engine tests for scene assignment

### Phase 2: Session manager (C++)

- Implement `SessionManager` class
- Quantization logic (bar boundary computation)
- Integration with `AudioEngine::processBlock`
- Integration with `RoutingManager` for session clip playback
- `session.launchScene`, `session.stopScene`, `session.stopAll` RPCs
- `notify.sessionStateChanged` notification
- Engine tests for launch/stop/quantize

### Phase 3: Frontend grid (React)

- `SessionView` component (grid layout, scene buttons, clip slots)
- View mode toggle in `TransportBar`
- `uiStore.viewMode` state
- Clip slot interactions (click to create, click to select, right-click menu)
- Playing/launched state indicators
- Frontend unit tests for `SessionView`

### Phase 4: Polish

- Drag clips between arrangement and session
- Session clip editing (double-click → piano roll/audio editor)
- MCP tools (`session_launch_scene`, `session_stop_all`, etc.)
- E2E tests (create clips, launch scene, verify state)
- Visual polish (animations, colors, layout refinement)
- Keyboard shortcuts (Tab to toggle view, Enter to launch scene)

---

## 7. Testing Strategy

### Engine tests (gtest)

- `SessionManagerTest.LaunchSceneSchedulesAtBarBoundary`
- `SessionManagerTest.StopSceneStopsClips`
- `SessionManagerTest.StopAllStopsEverything`
- `SessionManagerTest.SceneSwitchStopsPreviousLaunchesNew`
- `SessionManagerTest.TransportStoppedAutoStarts`
- `SessionManagerTest.LoadSceneFromProject`
- `ClipSceneTest.SetClipSceneUpdatesValueTree`
- `ClipSceneTest.CreateClipInSessionSlot`

### Frontend tests (Vitest)

- `SessionView.test.tsx` — renders grid, scene buttons, clip slots
- `SessionView.test.tsx` — click scene calls RPC
- `SessionView.test.tsx` — clip state display (playing/stopped/empty)

### E2E tests (Playwright)

- Create MIDI clip in session slot
- Launch scene and verify clip states
- Switch between arrangement and session views
- Drag clip from arrangement to session
