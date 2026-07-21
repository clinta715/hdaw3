# GUI Inspection MCP Tools — Design Spec

**Status:** Draft v1 — for review
**Date:** 2026-07-21
**Target version:** HDAW 0.11.0 (post-v0.10.0)
**Supersedes:** `docs/superpowers/specs/2026-06-30-qt-mcp-ui-inspector-roadmap.md` (Phase 1 + Phase 4)

## Overview

Add a `gui.*` tool domain to the existing MCP server that lets an LLM client
observe the spatial layout and interaction state of HDAW's **Qt GUI** (when
built with `-DHDAW_GUI=ON`). The client can query clip positions on the
timeline, track header geometry, selection state, scroll offsets, active
panel, and piano-roll contents — then assert that mutations (create/move/
delete) produce the expected visual result.

> **Important:** The primary GUI is the **React/Electron frontend** (see
> "React/Electron Frontend Architecture" in AGENTS.md). The Qt 6 desktop
> GUI is deprecated. These tools inspect Qt widget state, which is only
> available when `HDAW_GUI=ON`. For the React frontend, model-state
> inspection is already covered by existing MCP tools (`read.snapshot`,
> `list_tracks`, `list_clips`, etc.).

**Headline decisions:**

- **Integration:** Tools register into the existing `McpServer` as
  `registerGuiInspectTools(McpServer&, AudioEngine*)`. No second TCP
  listener, no separate process. The existing stdio/HTTP/loopback transports
  all work unchanged.
- **GUI-build only:** Tools are compiled unconditionally but return a
  structured "headless" response (`{available: false, reason: "no_gui"}`)
  when no `MainWindow` exists (i.e. in the default browser/Electron modes).
  This keeps the headless build linkable without `#ifdef` guards.
- **Scene-space coordinates:** All positions are reported in scene units
  (pixels at the current `pixelsPerSecond` zoom), not screen pixels.
  Deterministic and resolution-independent.
- **Read-only in v1:** No event injection (click, type, key-press). That is
  Phase 2 (the roadmap's "Interaction" phase). v1 is observation + assertion.
- **Test seam:** The existing `TransportLoopback` drives all integration
  tests. A new `GuiInspector` helper class is the single point of contact
  with the widget tree, making it mockable in unit tests.
- **React frontend note:** For the primary React/Electron frontend, the
  model state IS the GUI state. Existing MCP tools (`read.snapshot`,
  `list_tracks`, `list_clips`, `get_clip`, `get_transport`, etc.) provide
  full model-state inspection. The `gui.*` tools add widget-level spatial
  inspection for the deprecated Qt GUI only.

## Goals

1. An MCP client can query the full spatial layout of the timeline (every
   clip's scene position, size, track, selection, visibility).
2. An MCP client can query track header state (count, heights, selection,
   mute/solo/arm per track).
3. An MCP client can query the active bottom-panel tab and piano-roll
   contents (visible notes, zoom, scroll).
4. An MCP client can query scroll/zoom state (timeline scroll X/Y,
   pixelsPerSecond, piano-roll scrollY).
5. An MCP client can perform a hit-test at scene coordinates and learn what
   item (if any) is under that point.
6. A gtest integration suite exercises mutate → inspect → assert patterns
   via the loopback transport, covering clip CRUD, track CRUD, note CRUD,
   undo, and selection.

## Non-goals (v1)

- **Event injection.** No `qt_click`, `qt_type`, `qt_key_press`. Those are
  Phase 2 and require careful main-thread dispatch via
  `QMetaObject::invokeMethod`.
- **Screenshot capture.** `QWidget::grab()` → PNG is useful but orthogonal.
  Deferred to Phase 2.
- **Generic QObject tree walk.** The roadmap's `qt_snapshot` (full widget
  tree) is too noisy for LLM consumption. We expose *domain-specific*
  structured snapshots instead.
- **Pixel-level rendering assertions.** We verify layout (positions, sizes,
  visibility), not painted pixels or colors.
- **Headless-mode spatial simulation.** When no GUI exists, tools return
  `{available: false}`. We do not synthesize fake geometry from the model.

## 1. Architecture

```
MCP Client (LLM)
    │  JSON-RPC over stdio / HTTP / loopback
    ▼
McpServer::dispatchRequest
    │  handleToolsCall → tool handler lambda
    ▼
registerGuiInspectTools handlers
    │  capture AudioEngine* + MainWindow* (nullable)
    ▼
GuiInspector (src/ui/GuiInspector.h)
    │  walks TimelineScene::clipItemMap, TrackHeaderWidget,
    │  QStackedWidget, PianoRollWidget, QGraphicsView scrollbars
    ▼
Structured JSON response
```

### 1.1 GuiInspector helper

A non-QObject utility class that takes `MainWindow*` (nullable) and exposes
typed query methods. All methods are `const` and run on the main thread
(guaranteed by MCP tool dispatch).

```cpp
// src/ui/GuiInspector.h
#pragma once
#include <QJsonObject>
#include <QJsonArray>

class MainWindow;

namespace HDAW {

class GuiInspector {
public:
    explicit GuiInspector(MainWindow* mw);

    bool isAvailable() const;

    QJsonObject timelineSnapshot() const;
    QJsonArray  clipGeometry() const;
    QJsonArray  trackLayout() const;
    QJsonObject selectionState() const;
    QJsonObject scrollState() const;
    QJsonObject panelState() const;
    QJsonObject pianoRollState() const;
    QJsonObject hitTest(double sceneX, double sceneY) const;

private:
    MainWindow* mw;
};

} // namespace HDAW
```

### 1.2 MainWindow access

`MainWindow` gains a single public accessor:

```cpp
MainWindow* AudioEngine::getMainWindow() const;  // nullptr in headless
```

`AudioEngine` stores the pointer (set by `MainWindow`'s constructor via
`engine.setMainWindow(this)`). This avoids `qApp->findChild<MainWindow*>()`
fragility and keeps the dependency direction clean (engine does not include
MainWindow.h — it stores a `void*` or forward-declared pointer).

### 1.3 Tool registration

```cpp
// In McpTools.cpp (or a new McpGuiInspectTools.cpp):
static void registerGuiInspectTools(McpServer& s, AudioEngine* e);
```

Called from `registerAllTools` after the existing 10 registrars.

## 2. Tool Definitions

### 2.1 `gui.snapshot`

Full structured dump of the GUI state. The "give me everything" tool.

**Input:** `{}` (no parameters)

**Output:**
```json
{
  "available": true,
  "pixelsPerSecond": 42.5,
  "timeline": {
    "sceneWidth": 4000,
    "sceneHeight": 290,
    "scrollX": 120.0,
    "scrollY": 0.0,
    "trackCount": 3,
    "clipCount": 5
  },
  "selection": {
    "selectedTrack": 1,
    "selectedClips": [2, 5],
    "selectedNotes": []
  },
  "panel": {
    "activeTab": "PianoRoll",
    "activeTabIndex": 1
  },
  "tracks": [ ... ],
  "clips": [ ... ]
}
```

### 2.2 `gui.get_clip_geometry`

Per-clip spatial data from the scene.

**Input:** `{ "clipId": int (optional) }` — omit for all clips.

**Output:**
```json
[
  {
    "clipId": 1,
    "trackIndex": 0,
    "x": 170.0,
    "y": 30.0,
    "width": 340.0,
    "height": 80.0,
    "selected": false,
    "visible": true,
    "type": "audio",
    "name": "bass.wav"
  }
]
```

Coordinates: `x = startTime * pixelsPerSecond`, `y = trackY(trackIndex)`,
`width = duration * pixelsPerSecond`, `height = trackHeight`.

### 2.3 `gui.get_track_layout`

Per-track header geometry and state.

**Input:** `{}` (no parameters)

**Output:**
```json
[
  {
    "index": 0,
    "name": "Track 1",
    "y": 30.0,
    "height": 80.0,
    "selected": true,
    "muted": false,
    "soloed": false,
    "armed": false,
    "clipCount": 2
  }
]
```

### 2.4 `gui.get_selection`

Current selection state across all editors.

**Input:** `{}` (no parameters)

**Output:**
```json
{
  "selectedTrack": 1,
  "selectedClips": [2, 5],
  "selectedNotes": [101, 102, 103],
  "pianoRollClipId": 2
}
```

### 2.5 `gui.get_scroll`

Scroll and zoom state.

**Input:** `{}` (no parameters)

**Output:**
```json
{
  "timelineScrollX": 120.0,
  "timelineScrollY": 0.0,
  "pixelsPerSecond": 42.5,
  "pianoRollScrollY": 350,
  "pianoRollZoomX": 1.0
}
```

### 2.6 `gui.get_panel_state`

Bottom-panel tab state.

**Input:** `{}` (no parameters)

**Output:**
```json
{
  "activeTab": "Mixer",
  "activeTabIndex": 0,
  "tabCount": 7,
  "tabs": ["Mixer", "PianoRoll", "FxChain", "Automation",
           "AudioEditor", "StepSequencer", "Modulation"]
}
```

### 2.7 `gui.get_piano_roll`

Piano-roll contents (only meaningful when a MIDI clip is loaded).

**Input:** `{}` (no parameters)

**Output:**
```json
{
  "loaded": true,
  "clipId": 2,
  "clipName": "melody",
  "notes": [
    {"noteId": 101, "pitch": 60, "velocity": 100,
     "startBeat": 0.0, "durationBeats": 1.0,
     "y": 350, "x": 0.0, "width": 42.5, "height": 10}
  ],
  "scrollY": 350,
  "keyHeight": 10,
  "snapEnabled": true,
  "snapDivision": 4
}
```

### 2.8 `gui.hit_test`

What's at a given scene coordinate?

**Input:** `{ "x": number, "y": number }` (scene coordinates)

**Output:**
```json
{
  "hit": true,
  "type": "clip",
  "clipId": 3,
  "trackIndex": 1,
  "name": "drums"
}
```

Or `{"hit": false}` if nothing is at that point. Uses
`QGraphicsScene::itemAt(x, y, QTransform())`.

## 3. Headless Fallback

When `AudioEngine::getMainWindow()` returns `nullptr`:

```json
{
  "available": false,
  "reason": "no_gui",
  "hint": "Launch with --gui to enable GUI inspection tools"
}
```

All 8 tools return this shape. The `available` field lets the LLM client
branch without parsing error strings.

## 4. Thread Safety

All tool handlers run on the main (GUI) thread — this is the existing MCP
contract (`AGENTS.md`: "Every tool runs on the main thread"). `GuiInspector`
methods read widget state directly (no locks needed). The `TransportStdio`
reader thread posts to the main thread via `Qt::QueuedConnection` before
dispatch, so by the time the handler lambda executes, we are on the GUI
thread.

## 5. Testing Strategy

### 5.1 Test infrastructure

Tests use the existing `TransportLoopback` seam. A new test fixture
`GuiInspectTest` creates a `QApplication`, `AudioEngine`, `MainWindow`
(real widgets, off-screen), and `McpServer` with loopback transport.

```cpp
class GuiInspectTest : public ::testing::Test {
protected:
    void SetUp() override;   // creates app, engine, MainWindow, server
    void TearDown() override;
    QJsonValue call(const QString& tool, const QJsonObject& args);
};
```

### 5.2 Test cases (mutate → inspect → assert)

| # | Test | Mutation | Inspection | Assertion |
|---|------|----------|------------|-----------|
| 1 | Clip creation visible | `create_clip` | `gui.get_clip_geometry` | clip exists at expected x/y |
| 2 | Clip move updates geometry | `move_clip` | `gui.get_clip_geometry` | x changed to new beat × pps |
| 3 | Clip deletion removes item | `remove_clip` | `gui.get_clip_geometry` | clip absent from list |
| 4 | Track addition adds row | `add_track` | `gui.get_track_layout` | trackCount incremented |
| 5 | Track removal clears row | `remove_track` | `gui.get_track_layout` | trackCount decremented |
| 6 | Note visible in piano roll | `add_note` (after `select_clip`) | `gui.get_piano_roll` | note in list with correct pitch |
| 7 | Selection tracks click | `select_clip` | `gui.get_selection` | selectedClips contains clipId |
| 8 | Undo restores geometry | `move_clip` → `undo` | `gui.get_clip_geometry` | x back to original |
| 9 | Panel switches on clip type | `select_clip` (audio) | `gui.get_panel_state` | activeTab == "AudioEditor" |
| 10 | Zoom changes pps | `gui.zoom_to_fit` (or `set_zoom`) | `gui.get_scroll` | pixelsPerSecond changed |
| 11 | Hit-test finds clip | `create_clip` | `gui.hit_test` at clip center | hit==true, correct clipId |
| 12 | Headless returns unavailable | (no MainWindow) | any `gui.*` tool | available==false |

### 5.3 Determinism

- No `sleep()` or timed waits.
- Widget geometry is deterministic after `rebuildFromValueTree()` — no
  animation or async layout in HDAW's custom widgets.
- `QApplication::processEvents()` is called after mutations to flush
  deferred scene updates before inspection.

### 5.4 Off-screen rendering

Tests create `MainWindow` with `setAttribute(Qt::WA_DontShowOnScreen, true)`
so they run in CI without a display. Qt widget geometry is computed
correctly even when not shown, as long as `resize()` is called explicitly.

## 6. Build Integration

- `src/ui/GuiInspector.{h,cpp}` — new files, added to `HDAW_lib` sources.
- `src/mcp/McpGuiInspectTools.{h,cpp}` — new files (or inline in
  `McpTools.cpp` if < 200 lines).
- `tests/integration/mcp/gui_inspect_test.cpp` — new test file.
- No new dependencies. No new CMake options. The tools compile in all
  builds; they return `{available: false}` when headless.

## 7. Future Phases (not in scope)

- **Phase 2 — Interaction:** `gui.click(x, y)`, `gui.keyPress("Ctrl+Z")`,
  `gui.typeText("...")`. Requires `QMetaObject::invokeMethod` dispatch and
  careful focus management.
- **Phase 2 — Screenshot:** `gui.screenshot(widget?)` → base64 PNG.
- **Phase 3 — Generic QObject walk:** `qt_snapshot` for arbitrary widget
  tree inspection (the roadmap's original vision). Useful for debugging
  third-party plugin editor windows.
- **Phase 3 — CI smoke runner:** A script that launches HDAW with `--gui`,
  connects via MCP HTTP, and runs a sequence of mutate→inspect assertions
  as a regression gate.
