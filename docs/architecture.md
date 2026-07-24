# HDAW Architecture Reference

Domain-specific documentation split from AGENTS.md. 
For the original combined file, see `../AGENTS.md`.

Sections: Build, Version Management, Key Classes, GUI-Engine Decoupling, 
Frontend Architecture, Timestretch, JUCE 9 Migration.

---

# AGENTS.md

Project-specific lessons learned. Read this before working on the timeline,
the project model, or the main window — these are the pitfalls that cost
real debugging time.

**Current scope**: HDAW is a JUCE 8 desktop DAW at version **0.12.0**
with a **React 19 + TypeScript frontend** (Zustand state management,
Vite build). The frontend runs in two contexts:
system browser (default) or Electron shell. The C++ engine
exposes its state via JSON-RPC 2.0 over WebSocket (port 8766) and serves
the bundled React SPA via HTTP (port 8765). The
core engine (project model, transport, routing, JUCE plugin hosting,
internal FX) and the frontend (track headers, timeline, mixer, piano
roll, FX chain, automation) work end-to-end.
v0.3.x added the MCP server and a gtest test suite. v0.4.x added
multi-clip selection, clipboard, markers, MIDI CC recording, MIDI
channel routing, FX drag-reorder, status bar, zoom-to-fit, expanded
preferences, and a bugfix pass. v0.5.0 added full automation
parameters (Volume, Pan, Mute as default lanes; plugin FX parameter
automation via `TrackFXSlot` atomic cache and compound paramID scheme),
Mute automation recording, and compile-time build optimizations
(LTO, `/MP` parallel builds, JUCE define cleanup). v0.7.0 added
GUI-engine decoupling via abstract command interfaces, ReadModel,
PluginParamService, and PluginService/MidiService interfaces;
automation copy-paste and selection model, snap persistence across
editors, file browser drive navigation, and several bug fixes. **v0.8.0**
added pitch-preserving audio clip timestretch (SoundTouch, off-thread
render), BPM metadata extraction on import, auto tempo-match on import,
project-tempo tracking for existing tempo-matched clips, and fixed
the waveform visibility dark-background issue. **v0.9.0** adds the
per-clip audio editor (waveform display, zoom, gain, fades, timestretch
controls, loop, offset/duration), gain envelope editor with draggable
control points, audio clip slicing (at playhead, at transients, at
region boundaries), and the region clipboard (drag-select to copy/cut
audio regions and paste at the playhead). **v0.9.1** fixes the clip
drag Y-tracking — clips now follow the mouse smoothly across tracks
instead of snapping at track boundaries with a discrete jump.
**v0.10.0** adds tempo point interactions on the ruler (add/edit/
remove/drag), duplicate track with ID-safe deep copy, audio device
preference persistence, transport-synced loop preview, auto-trim
silence on import (-60 dB), missing-file error indicators, and two
new MCP tools (`duplicate_track`, `add_track_with_fx`). **v0.12.0**
adds 49 MCP functionality tests, the Plugin Manager dialog, file
browser audio preview, collapsible right panel, and bugfixes for
clip rubber-band selection (live highlighting, stale-click guard),
trim/fade handle click propagation, track header resize handle
click propagation, audio clip deletion not stopping playback
(missing `valueTreeChildRemoved` → `rebuildRoutingGraph()`), and
three additional asymmetric ValueTree listener fixes (TRANSPORT
teardown in AudioEngine, MARKER_LIST teardown). For the
full list of working features and the priority-ordered roadmap, see
`README.md`.

## Build

- Configuration: `cmake --build build --config Debug`
- Outputs: `build/Debug/HDAW.exe` (engine + React frontend, default build), `build/Debug/HDAW_headless.exe` (engine-only for Electron), `build/Debug/hdaw_tests.exe` (gtest)
- Do NOT run `build/Release/HDAW.exe` — it is a stale binary from before
  the bug-fix series began and contains none of the fixes.
- On Windows, the `HDAW_lib` static library, `HDAW` exe, and `hdaw_tests`
  exe each run a `windeployqt` POST_BUILD step (see `hdaw_deploy_qt()`
  in `CMakeLists.txt`) that copies the required Qt DLLs
  (`Qt6Cored.dll`, `Qt6HttpServerd.dll`, `Qt6WebSocketsd.dll`, …) into
  `build/Debug/`. On a clean machine this is the difference between
  the test binary loading and `STATUS_DLL_NOT_FOUND`. Qt 6.11+
  `windeployqt` auto-detects required modules; older Qt versions may
  need `--http-server`/`--websockets` flags.
- Logging: `HDAW_LOG` (or the older `DBG` macro is **not** available —
  JUCE defines its own `DBG` and shadows it). All paths to `HDAW_LOG`
  must `#include "DebugLog.h"`. Output is appended to `%TEMP%/hdaw_debug.log`.
- **Two launch modes** (all in one `HDAW.exe`):
  - **Default (browser mode):** `HDAW.exe` — starts engine + WebSocket server (port 8766) + HTTP server (port 8765), opens system browser to `http://127.0.0.1:8765`. The React SPA is bundled in Qt resources and served by the C++ HTTP server.
  - **Headless (Electron mode):** `HDAW_headless.exe --headless` — starts engine + WebSocket server only. Electron spawns this as a child process and connects via WebSocket.
- CLI flags: `--mcp-stdio` forces headless stdio MCP server (run when
  launched as a subprocess by an MCP client), `--no-mcp` disables MCP
  entirely, `--mcp-http-port=<N>` overrides the MCP HTTP server's bind
  port. `--port=N` overrides the WebSocket port (default 8766). `--http-port=N` overrides the HTTP serving port (default 8765).
- **Frontend build:** The React frontend is built separately with `cd frontend && npm run build`. The output (`dist/index.html`, `dist/assets/index.js`, `dist/assets/index.css`) is compiled into `HDAW.exe` via Qt resources (`src/resources/frontend.qrc`). Changes to the frontend require rebuilding the frontend, then rebuilding the C++ project.

## Version Management

Version numbers are stored in **two places** and must be kept in sync manually:
- `CMakeLists.txt` → `project(HDAW VERSION 0.12.0 ...)` — **canonical source** for C++ code
- `frontend/package.json` → `"version": "0.12.0"` — **canonical source** for the React frontend

When bumping the version, update **both** files, then run:
```
cmake --build build --target check_version
```
to verify they match. The `check_version` target runs automatically during full builds.

**C++ code** reads the version via `#include "common/Version.h"` which defines `HDAW_VERSION` (generated from CMake's `PROJECT_VERSION` at configure time). Do **not** hardcode the version string in `.cpp` or `.h` files — always use the `HDAW_VERSION` macro.

**Frontend code** reads the version from `frontend/src/version.ts` which is auto-generated by the `prebuild` npm script from `package.json`. The `prebuild` script runs automatically before `npm run build`.

## Key Classes Quick Reference

| Role | File | Class |
|------|------|-------|
| Data model | `src/model/ProjectModel.h` | `ProjectModel` |
| Audio engine facade | `src/engine/AudioEngine.h` | `AudioEngine` |
| Root audio processor | `src/engine/MainAudioProcessor.h` | `MainAudioProcessor` |
| Transport state | `src/engine/TransportManager.h` | `TransportManager` |
| Graph routing | `src/engine/RoutingManager.h` | `RoutingManager` |
| Track processing | `src/engine/Track.h` | `Track` |
| Audio clip playback | `src/engine/ClipSourceProcessor.h` | `ClipSourceProcessor` |
| MIDI clip playback | `src/engine/MidiClipProcessor.h` | `MidiClipProcessor` |
| FX slot / plugin host | `src/engine/TrackFXSlot.h` | `TrackFXSlot` |
| Automation | `src/engine/AutomationManager.h` | `AutomationManager` |
| Sends | `src/engine/SendProcessor.h` | `SendProcessor` |
| Buses | `src/engine/MasterBusProcessor.h` | `MasterBusProcessor`, `GroupBusProcessor`, `FxBusProcessor` |
| Plugin scanning | `src/engine/PluginManager.h` | `PluginManager` |
| CLAP hosting | `src/engine/CLAPPluginInstance.h` | `CLAPPluginInstance` |
| Recording | `src/engine/AudioRecorder.h` | `AudioRecorder` |
| Export | `src/engine/ExportManager.h` | `ExportManager` |
| Timestretch renderer (SoundTouch wrapper + worker thread) | `src/engine/StretchRenderer.h` | `HDAW::StretchRenderer` |
| Timestretch cache (keyed entry store + `entryReady` signal) | `src/engine/StretchCache.h` | `HDAW::StretchCache` |
| Composition (phrase / chord / progression generator) | `src/engine/PhraseGenerator.h` | `PhraseGenerator` |
| UI→Audio bridge | `src/engine/SPSCBridge.h` | `SPSCBridge` |
| Level metering | `src/engine/LevelMeter.h` | `LevelMeter` |
| File save/load | `src/engine/ProjectSerializer.h` | `ProjectSerializer` |
| Waveform thumbnails | `src/engine/ProjectPool.h` | `ProjectPool` |
| Audio preview player | `src/engine/AudioPreviewPlayer.h` | `HDAW::AudioPreviewPlayer` |
| Audio import | `src/engine/AudioImport.h` | `HDAW::importAudioFile()` |
| MIDI import | `src/engine/MidiImport.h` | `HDAW::importMidiFile()` |
| Frontend WebSocket server | `src/frontend/FrontendServer.h` | `frontend::FrontendServer` |
| Frontend RPC router | `src/frontend/FrontendRouter.h` | `frontend::dispatch()` |
| Frontend RPC constants | `src/frontend/FrontendRpc.h` | `frontend::method::*`, `frontend::notify::*` |
| Frontend tree watcher (ValueTree→WebSocket push) | `src/frontend/FrontendTreeWatcher.h` | `frontend::FrontendTreeWatcher` |
| Frontend HTTP server (serves React SPA) | `src/frontend/UiHttpServer.h` | `frontend::UiHttpServer` |
| MCP server core (tool registry, dispatch, lifecycle) | `src/mcp/McpServer.h` | `mcp::McpServer` |
| MCP JSON-RPC 2.0 framing (requests/responses/notifications) | `src/mcp/McpJsonRpc.h` | `mcp` namespace (`McpRequest`/`McpResponse`/`McpNotification`, `err::`, `parseLine`, `validateRequest`) |
| MCP JSON-Schema subset validator (type/required/properties/items/enum/min/max) | `src/mcp/McpSchema.h` | `mcp::validateSchema` |
| MCP transport interface (4 virtuals: `start`/`stop`/`send`/`notify`) | `src/mcp/McpTransport.h` | `mcp::Transport` |
| MCP stdio transport (newline-delimited JSON, reader thread) | `src/mcp/McpTransportStdio.h` | `mcp::TransportStdio` |
| MCP Streamable HTTP transport (loopback only, real round-trip) | `src/mcp/McpTransportHttp.h` | `mcp::TransportHttp` |
| MCP loopback test transport (in-memory `QByteArray` queues) | `src/mcp/McpTransportLoopback.h` | `mcp::TransportLoopback` |
| MCP tool record (name, description, inputSchema, handler) | `src/mcp/McpToolDef.h` | `mcp::McpToolDef` / `mcp::McpToolResult` / `mcp::McpHandler` |
| MCP tool registrations (per-domain: read/transport/track/clip/note/composition/automation/fx) | `src/mcp/McpTools.{h,cpp}` | `mcp::registerAllTools(McpServer&)` |
| MCP export tool (extracted from McpTools) | `src/mcp/McpExportTool.{h,cpp}` | `mcp::registerExportTool(McpServer&)` |
| Plugin isolation: shared types | `src/proxy/ProxyCommon.h` | `proxy::MessageType`, `ProxyMessage`, `ProxyResponse`, `ShmHeader`, `MidiEvent` |
| Plugin isolation: SPSC ring buffer | `src/proxy/ProxyRingBuffer.h` | `proxy::RingBuffer<T>` |
| Plugin isolation: named pipe | `src/proxy/ProxyPipe.h` | `proxy::PipeServer`, `proxy::PipeClient` |
| Plugin isolation: shared memory | `src/proxy/ProxySharedMemory.h` | `proxy::ShmRegion` |
| Plugin isolation: process manager | `src/proxy/ProxyProcessManager.h` | `proxy::ProxyProcessManager` |
| Plugin isolation: proxy slot | `src/proxy/PluginProxySlot.h` | `proxy::PluginProxySlot` |
| Plugin isolation: proxy editor | `src/proxy/ProxyEditor.h` | `proxy::ProxyEditor` |
| Plugin isolation: crash dialog | `src/proxy/CrashDialog.h` | `proxy::CrashDialog` |
| Plugin isolation: child process | `src/proxy/host/PluginHost.h` | `PluginHost` |
| Debug logging | `src/common/DebugLog.h` | `HDAW_LOG` macro |

## Common Practices

- **Always read the current state of a file before editing.** The
  codebase evolves quickly; stale assumptions cause cascading errors.
- **Follow existing patterns.** When adding a new widget, look at how
  the nearest neighbor is structured (e.g. `FXChainWidget` for a new
  bottom-panel tab, `MixerStripWidget` for a new strip-style widget).
- **Run the build after every non-trivial change.** A clean compile
  catches 80% of issues before they reach runtime.
- **Check `CMakeLists.txt` source list.** New `.cpp` files must be
  added to `add_executable` or they silently won't compile (see
  "Build pipeline" section).
- **Use `HDAW_LOG` liberally during debugging.** The log file at
  `%TEMP%/hdaw_debug.log` is the primary diagnostic tool (see
  "Diagnostic pattern" section).

## Commit Message Style

- Use `<area>: <imperative-summary>` format:
  - `TimelineScene: fix clip selection after track removal`
  - `AudioEngine: add CLAP plugin scanning`
  - `build: update JUCE to 8.0.4`
- `<area>` is the most relevant class name, module name, or
  subsystem. For broad changes use `build`, `docs`, `ui`, `engine`.
- Keep the summary line under 72 characters.
- Bullet-point significant details in the body.
- Reference code with backticks: `use \`TrackFXSlot\` instead of
  bare \`AudioProcessorEditor\``.

## Engine Decoupling (v0.7.0)

The audio engine layer (`src/engine/`) is decoupled via four abstract
command interfaces and a ReadModel. The frontend RPC router
(`src/frontend/FrontendRouter.cpp`) uses these interfaces rather than
accessing `engine.getProjectModel()` / `engine.getMainProcessor()` directly.

### Abstract Command Interfaces (`src/common/`)

| Interface | Methods | Purpose |
|-----------|---------|---------|
| `ProjectCommands` | 66 | All mutations: tracks, clips, notes, FX, automation, markers, tempo, loop, undo, save/load |
| `TransportCommands` | 9 | Play/stop/pause/rewind, seek, record, toggle loop |
| `AudioGraphCommands` | 6 | Routing rebuild, FX rebuild, automation cache, modulation, toggle editor, clip take |
| `ReadModel` | 19 | Read-only snapshots: tracks, clips, notes, transport, FX slots, automation lanes/points, markers, tempo points, automatable params, meters, dirty state |

**Concrete implementation:** `AudioEngineCommands` (inherits all three
command interfaces) + `ReadModelImpl` (backed by `ProjectModel`), both
in `src/engine/`. `AudioEngine` exposes `getProjectCommands()`,
`getTransportCommands()`, `getAudioGraphCommands()`, `getReadModel()`.

**Consumer pattern:** The frontend RPC router and MCP tools use
`readModel->getTrackCount()`, `projectCmds->setTrackVolume(...)`,
etc. instead of `engine.getProjectModel().getTrackListTree()` directly.

### Service Interfaces (`src/common/`)

### Service Interfaces (`src/common/`)

Two additional abstract interfaces expose engine services without
coupling the UI to concrete engine types:

| Interface | Methods | Used By |
|-----------|---------|---------|
| `PluginService` | 11 | Plugin discovery, scanning, blacklist management |
| `MidiService` | 3 | MIDI device enumeration, open/close |

**Concrete implementation:** `PluginServiceImpl` (+ `MidiServiceImpl`)
in `src/engine/`, delegating to `PluginManager` (+ `MidiInputManager`).
`AudioEngine` exposes `getPluginService()`, `getMidiService()`.

**Consumer pattern:** Same as command interfaces — callers use
`pluginService->getPlugins()` instead of `engine.getPluginManager().getPlugins()`.

### Automation Clipboard (`src/engine/AutomationClipboard.h`)

| Type | Detail |
|------|--------|
| Storage | Static global `std::vector<AutomationPointEntry>` (value-type, no heap) |
| Copy | `copyPoints(points, paramID)` — computes `minTime` for paste offset |
| Paste | Points are offset by `playhead - minTime`, pasted at play position |

**Widget integration:** `AutomationLaneWidget::keyPressEvent` handles
Ctrl+C (copy), Ctrl+X (cut), Ctrl+V (paste), Ctrl+D (duplicate),
Delete (remove selected), Ctrl+A (select all), Escape (clear selection).
The selection model uses `std::set<int> selectedPoints` with
click-to-select, Ctrl+click-to-toggle, and selected-point highlighting
in `paintEvent`.

### Snap Persistence

| Editor | QSettings Key | Default |
|--------|--------------|---------|
| Timeline | `snapEnabled` / `snapDivision` | true / Beat(1) |
| Piano roll | `pianoRoll/snapEnabled` / `pianoRoll/snapDivision` | true / 1/16(4) |

Timeline snap is saved in both `PreferencesDialog::onApply()` and
`MainWindow::closeEvent`. Piano roll snap persists on every change
via the signal handlers. On load, both editors restore their last
snap values from QSettings.

### Automation Cache Rebuild Post-Mutation

`AudioEngineCommands` calls `rebuildAutomationCache(trackIndex)` after
every automation mutation (add/remove lane, add/remove point, toggle
enabled). The MCP tools and the recording path in
`AudioEngine::valueTreePropertyChanged` also trigger cache rebuilds.
Without this, automation points are stored in the ValueTree but silently
ignored during playback until a UI interaction forces a rebuild.

### Project Load — No Auto-Play

`ProjectSerializer::load()` explicitly clears `isPlaying=false` and
`position=0.0` on the transport tree after loading, preventing a
project saved while playing from auto-starting on the next load.

## React/Electron Frontend Architecture (v0.10.0+)

The primary GUI is a **React 19 + TypeScript** SPA using **Zustand 5** for
state management and **Vite 6** for builds. It runs in three contexts:
system browser (default), Electron shell, or Vite dev server.

### Communication: JSON-RPC 2.0 over WebSocket

The frontend communicates with the C++ engine via a single WebSocket
connection carrying JSON-RPC 2.0 messages. The protocol is identical to
the MCP protocol — same request/response/notification framing.

**Client** (`frontend/src/rpc/client.ts`):
- `RpcClient` wraps a `WebSocket`, auto-reconnects with exponential backoff
- `call(method, params)` returns a Promise resolved by the matching response
- `onNotification(method, handler)` subscribes to server-pushed notifications

**Server** (`src/frontend/FrontendServer.h`):
- `QWebSocketServer` on loopback, port 8766
- Routes via `frontend::dispatch()` in `FrontendRouter.cpp`
- Pushes notifications: `notify.meters` (30 Hz), `notify.transport` (30 Hz,
  deduplicated), `notify.treeChanged` (16ms debounce on ValueTree mutations),
  `notify.scanProgress`, `notify.exportProgress`

### Frontend State: Zustand Stores

| Store | Key State | Source |
|-------|-----------|--------|
| `projectStore` | `snapshot` (full project), `notesByClip`, `isDirty`, `filePath` | `read.snapshot` RPC |
| `transportStore` | `transport` (bpm, isPlaying, loopStart/End, currentTime) | `notify.transport` push |
| `meterStore` | `master`, `tracks` (VU levels left/right) | `notify.meters` push |
| `uiStore` | `selectedClipIds`, `selectedTrackIndex`, `activeBottomTab`, `snapEnabled` | Local UI state |
| `automationStore` | `lanes`, `pointsByLane`, `selectedPointTimes` | `read.getAutomationLanes` |
| `markerStore` | `markers` | `read.getMarkers` |
| `browserStore` | `folders`, `expandedPaths`, `selectedFile`, `visible` | Local + localStorage |
| `notifyStore` | `toasts` | Local |
| `transportExtrasStore` | `metronomeEnabled`, `countInEnabled`, `followPlayhead` | Local |

### Frontend File Structure

```
frontend/
  src/
    App.tsx              # Root component
    main.tsx             # Entry point
    rpc.ts               # RPC client singleton
    rpc/
      client.ts          # RpcClient class (WebSocket + JSON-RPC)
      types.ts           # TypeScript types matching C++ snapshot structs
    store/               # Zustand stores (see table above)
    components/          # React components (Timeline, Mixer, PianoRoll, etc.)
    hooks/               # Custom React hooks
  electron/
    main.ts              # Electron main process (spawns HDAW_headless.exe)
    preload.ts           # Context bridge for native dialogs
```

### Build Pipeline

1. `cd frontend && npm run build` → Vite produces `dist/index.html`, `dist/assets/index.js`, `dist/assets/index.css`
2. `cmake --build build --config Debug` → C++ compiles `frontend.qrc` (Qt resource bundle) containing the dist files
3. `HDAW.exe` serves the bundled SPA via `UiHttpServer` on port 8765

Changes to the frontend require rebuilding both the frontend and the C++ project.

### Electron Packaging

`electron-builder.yml` packages:
- The Vite-built React SPA (`dist/`)
- The compiled Electron main/preload (`dist-electron/`)
- The C++ engine binaries (`HDAW_headless.exe`, `hdaw_plugin_scanner.exe`, DLLs) as `extraResources`

Electron's `main.ts` spawns `HDAW_headless.exe --port=8766` as a child process,
polls until the WebSocket port is ready, then creates a `BrowserWindow` that
loads the React SPA.

## Audio Clip Timestretch (v0.8.0)

Pitch-preserving time-stretch for audio clips, built on **SoundTouch**
(LGPL-2.1, dynamically linked). Each audio clip carries a stretch
*intent* in the ValueTree; a stretched copy of the source is rendered
**off the audio thread** and adopted during the normal
`rebuildRoutingGraph()` path, so the realtime `processBlock` stays a
cheap 1:1 copy.

### ValueTree properties (audio clips only)

All on the CLIP node (`src/model/ProjectModel.h`):

| ID | Type | Default | Meaning |
|----|------|---------|---------|
| `sourceBpm` | double | `0.0` | Musical tempo of the source file. `0` = unknown. |
| `stretchMode` | int | `0` | `0=Off`, `1=TempoMatch`, `2=ManualRatio` |
| `stretchRatio` | double | `1.0` | Time ratio vs. original source (`targetDuration/sourceDuration`). TempoMatch derives it; ManualRatio is user-set. |
| `sourceDuration` | double | (set at import) | Original source length in seconds; cached so re-renders don't re-open the file. |

`createAudioClip` (`ProjectModel.cpp`) seeds these defaults. **Once
stretched, the clip's existing `duration` = `sourceDuration * stretchRatio`**
(timeline-visible length); `offset`/`gain`/`fade` window into the
stretched buffer exactly as before. MIDI clips are untouched.

### Realtime path — the minimal change

`ClipSourceProcessor` (`src/engine/ClipSourceProcessor.h`) gained:

- `juce::HeapBlock<int> stretchedData[2]`, `int64_t stretchedLength`,
  `std::atomic<int> activeBuffer{0}` (0=original, 1=stretched), `int clipID`.
- `processBlock` does **one** `activeBuffer.load(acquire)` per block and
  picks the read pointer/length from it. The existing read loop, gain/
  fade envelope, and bounds/loop checks are unchanged — they just
  operate on the selected buffer.

That's the entire RT impact: **one atomic load + one pointer select**.
No allocation, no locks, no resampling on the audio thread. The
swap-in happens on the message thread inside `rebuildRoutingGraph()`
(under `graphLock`, where the audio thread already emits silence).

### Why stretch has no SPSC paramID

Unlike volume/pan/gain/fade, stretch is **decided at graph-build time**,
not per-sample. Changing stretchMode/ratio writes to the ValueTree; the
`AudioEngine` CLIP-property listener
(`src/engine/AudioEngine.cpp`, the `stretchMode || stretchRatio`
branch) triggers `rebuildRoutingGraph()`, and `RoutingManager::
rebuildClipsForTrack` reads the resolved ratio and either adopts a
cached buffer or requests a background render. There is no realtime
paramID for stretch, and none should be added — stretch is too expensive
to apply mid-block.

### Off-thread render — `StretchRenderer` + `StretchCache`

`StretchRenderer` (`src/engine/StretchRenderer.{h,cpp}`) mirrors
`ExportManager` exactly: `std::thread renderThread`,
`std::atomic<bool> active/cancelFlag`, `std::function<void(float)>
onProgress`, `std::function<void(const Result&)> onComplete` (both
invoked on the worker thread). The worker decodes the source via
`formatManager.createReaderFor` → `juce::AudioBuffer<float>`, feeds it
through `soundtouch::SoundTouch` (`setTempo(1.0/ratio)`, pitch
untouched), and emits two `HeapBlock<int>` matching
`ClipSourceProcessor::preloadedData`'s int PCM format (divide-by-32768
on read). Cancellation is cooperative (polls `cancelFlag` per block).

`StretchCache` (`src/engine/StretchCache.{h,cpp}`, a `QObject` owned by
`AudioEngine`) keys entries by `(clipID, ratio, sampleRate)`. On
`requestRender`, if a matching ready entry exists it's a no-op;
otherwise it spawns a render. `onComplete` (worker thread) stores the
result, then hops to the message thread via
`QMetaObject::invokeMethod(this, ..., Qt::QueuedConnection)` to emit
`entryReady(clipID)`. `AudioEngine::initialize` connects that signal to
`mainProcessor->rebuildRoutingGraph()`. The cache coalesces overlapping
requests (one render at a time; the most recent request wins).

`RoutingManager::rebuildClipsForTrack` calls `stretchCache->lookup()`
after setting `clipID`/`stretchRatio` on the new processor; on a hit it
calls `clipProc->adoptStretchedBuffer(...)`, on a miss it calls
`stretchCache->requestRender(...)`. The processor falls back to its
original `preloadedData` (`activeBuffer=0`) until the next rebuild.

### Commands

`ProjectCommands` gains: `setClipSourceBpm`, `setClipStretchMode`,
`setClipStretchRatio`, `tempoMatchClip`, `fitClipToLoop`. Each writes
the ValueTree props via the `UndoManager` (so `Ctrl+Z` rolls back) and
keeps `duration` consistent with `sourceDuration * ratio`. `ReadModel::
ClipSnapshot` carries `sourceBpm`/`stretchMode`/`stretchRatio`/
`sourceDuration` for the editor to populate widgets.

`fitClipToLoop(clipId)` stretches the **entire source** to span the
current loop region (`ratio = (loopEnd-loopStart)/sourceDuration`,
mode=ManualRatio, duration=loopLength, offset=0). No-op if the loop
region is empty.

### UI

`AudioClipEditorWidget` (`src/ui/AudioClipEditorWidget.cpp`) — the
audio-clip properties panel (bottom stack tab index 4, shown on audio-
clip select) — gained four controls in its control bar: **Src BPM**
spin (0–400), **Stretch** combo (Off / Tempo Match / Manual), a
**ratio** spin (0.25–4.0, enabled only in Manual mode), and a **Fit to
Loop** button. The existing `settingUi` guard suppresses feedback when
populating widgets programmatically.

### Build

`cmake/SoundTouchHelper.cmake` fetches the SoundTouch source and
builds it as a **shared library (DLL on Windows)** to satisfy the
LGPL-2.1 dynamic-linking obligation cleanly. Two gotchas documented
in the helper:

1. **`CMAKE_POLICY_VERSION_MINIMUM=3.5`** must be set before
   `FetchContent_MakeAvailable` — SoundTouch's `cmake_minimum_required`
   predates the CMake 3.5 compatibility removal.
2. **`SOUNDTOUCH_FLOAT_SAMPLES`** must be propagated as a PUBLIC define
   on the `SoundTouch` target. SoundTouch's own CMakeLists sets it
   PRIVATE, but `SAMPLETYPE` (float vs short) is part of the public ABI
   exposed via `STTypes.h` — consumers MUST see the same sample type or
   `putSamples`/`receiveSamples` overload resolution breaks at the
   call site. (The fetched fork's `STTypes.h` lines 77–78 also
   unconditionally force `SOUNDTOUCH_INTEGER_SAMPLES`; the renderer
   declares its buffers as `soundtouch::SAMPLETYPE` so it adapts to
   whichever path the header resolves to.)

`StretchRenderer` isolates the SoundTouch dependency behind HDAW's own
interface, so swapping to Rubber Band (paid commercial license, higher
quality) later is a one-file change in `StretchRenderer.cpp`. The LGPL
note: SoundTouch is dynamically linked; do not statically link it
without revisiting the LGPL obligation.

### Testing

`tests/unit/engine/stretch_test.cpp` covers the renderer (ratio 2.0
≈ doubles length; cancellation is deadlock-free), the cache
(request → `entryReady` → `lookup` hit, via `QSignalSpy` +
`QCoreApplication::processEvents`, never `sleep`), and the command math
(fit-to-loop and tempo-match ratios). The test target links `Qt6::Test`
(added to the top-level `find_package(Qt6 ... COMPONENTS ... Test)`).

### Phasing

- **Phase 1 (done, v0.8.0):** properties, renderer, cache, RT path,
  commands, editor UI (Manual Ratio + Fit to Loop + Source BPM +
  Tempo Match via the combo), tests.
- **Phase 2 (deferred):** **Tempo-match on import** — read BPM from
  WAV `bext`/`INFO`/ID3v2 `TBPM` metadata (first `getMetadataValues()`
  use in the codebase) at the import/drop sites (`AudioImport.cpp`,
  `TimelineView::handleFileDrop`), plus an `autoTempoMatchOnImport`
  preference (default off). Infrastructure is ready; this is import-
  path glue.
- **Phase 3 (deferred):** **Follow project tempo** — extend the
  `AudioEngine` project-tempo listener to iterate TempoMatch clips,
  re-derive `stretchRatio = sourceBpm/newBpm`, and re-render.

### Out of scope (noted, not built)

- Realtime stretch-during-playback without rebuild (would need SPSC +
  lock-free buffer swap; changing stretch mid-playback does a brief
  silence blip, same as clip add today).
- Persisting rendered buffers in the `.hdaw` file (re-rendered from
  props on load).
- Per-clip "preview original vs stretched" A/B UI.
- MCP tools (`set_clip_stretch`, `fit_clip_to_loop`).

## JUCE 9 Migration Preparation

JUCE 9.0 (released July 21, 2026) focuses on SVG/variable fonts, macOS
CoreAudio aggregates, and software renderer performance. The big-ticket
item — **AudioProcessor v2** (sample-accurate automation, CLAP-specific
features, MIDI UMP) — is still WIP and will land in follow-up releases.
Do not upgrade until APv2 ships; there are no killer features in 9.0
that justify the migration risk.

### Already applied: `Font` deprecated constructor

JUCE 8.0.0 deprecated all `Font` constructors except
`Font(FontOptions)`. JUCE 9 will remove them. The one HDAW call site
was at `src/proxy/ProxyEditor.cpp:13`:

```cpp
// BAD — deprecated in JUCE 8, removed in JUCE 9:
nameLabel.setFont(juce::Font(16.0f, juce::Font::bold));

// GOOD:
nameLabel.setFont(juce::Font(juce::FontOptions(16.0f).withBold()));
```

### Not affected (verified against JUCE 8.0.0 source)

- **`AudioProcessorGraph::Node::nodeID`** — `nodeID` is a plain `const
  NodeID` struct member. Direct access (`node->nodeID`) is the correct
  API. No `.get()` accessor exists; the audit-agent report that it was
  deprecated was incorrect.
- **`AudioFormatReader::read(AudioBuffer<float>*, int, int, int64,
  bool, bool)`** — The 6-arg `read` overload is the modern JUCE 8 form
  and exists in 8.0.0. JUCE 9 is not expected to change it.

### High-risk area for when APv2 ships

The 8 `AudioProcessor` subclasses (`MainAudioProcessor`, `Track`,
`ClipSourceProcessor`, `MidiClipProcessor`, `BusProcessorBase`,
`SendProcessor`, `PluginProxySlot`, `CLAPPluginInstance`) override
`processBlock`, `prepareToPlay`, `isBusesLayoutSupported`, and
parameter management. APv2 changes how parameters are owned (by value
on the derived type instead of on `AudioProcessor`). A compatibility
layer is planned, but expect porting work in these files. Pin
`GIT_TAG 8.0.0` in `cmake/JUCEHelper.cmake` until APv2 stabilizes.
