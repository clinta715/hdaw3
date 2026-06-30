# AGENTS.md

Project-specific lessons learned. Read this before working on the timeline,
the project model, or the main window — these are the pitfalls that cost
real debugging time.

**Current scope**: HDAW is a Qt 6 + JUCE 8 desktop DAW at version
**0.3.x**. The core engine (project model, transport, routing,
JUCE plugin hosting, internal FX) and the basic UI shell
(track headers, timeline, mixer, piano roll, FX chain,
automation) work end-to-end. The project is pre-1.0 and
pre-per-clip-audio-editor. **v0.3.x** adds the MCP server (see
"MCP server" below) and a gtest test suite. For the full list of
working features and the priority-ordered roadmap, see `README.md`.

## Build

- Configuration: `cmake --build build --config Debug`
- Outputs: `build/Debug/HDAW.exe` (GUI + stdio MCP), `build/Debug/hdaw_tests.exe` (gtest)
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
- CLI flags (MCP headless mode): `--mcp-stdio` forces headless stdio
  MCP server (run when launched as a subprocess by an MCP client),
  `--no-mcp` disables MCP entirely, `--mcp-http-port=<N>` overrides
  the HTTP server's bind port for this launch. Without any flag the
  GUI starts; if `stdin`/`stdout` are not TTYs the stdio MCP server
  starts automatically and the GUI is skipped.

## Code Style

- Use `auto` where the type is obvious from the initializer
  (e.g. `const auto& tree = clip.getChildWithName(IDs::CLIP_LIST);`,
  `auto* port = ...`).
- Avoid implicit `int`→`float` / `double`→`float` conversions — use
  `static_cast<float>` explicitly.
- Prefer `std::clamp` over manual `std::min`/`std::max` clamping.
- Use `std::span` instead of raw pointer + size pairs where applicable.
- Avoid variable shadowing — use descriptive prefixes when a local
  would shadow a class member (e.g. `projectBpm` instead of `bpm`
  when `bpm` is a member).
- Prefer pimpl (pointer to implementation) for non-trivial members
  that don't need to be exposed in the header, to reduce include
  dependencies and improve compile times.
- Use `std::unique_ptr` / `std::make_unique` for heap ownership;
  never bare `new` without an owning wrapper.
- Use `nullptr` (not `NULL` or `0`) for null pointers.
- Use `override` and `const` on virtual method overrides consistently.
- Use `static_cast` / `dynamic_cast` instead of C-style casts.

## Realtime / Audio-Thread Safety

The audio render path (`MainAudioProcessor::processBlock`,
`Track::processBlock`, `ClipSourceProcessor::processBlock`,
`MidiClipProcessor::processBlock`, `AutomationManager::getValueAt`)
runs on the audio thread. Violating realtime safety causes
dropouts, xruns, and hard-to-reproduce crashes.

**Rules for audio-thread code:**

- **Never allocate.** No `new`, `malloc`, `std::vector::push_back`,
  `std::string` construction, or `juce::String` construction inside
  `processBlock` or any function called from it.
- **Never lock.** No `std::mutex`, `juce::SpinLock` (write path),
  `juce::CriticalSection`, or any blocking wait. The
  `AutomationManager` uses `SpinLock` only for the *write* path
  (ValueTree listener on UI thread); the read path in
  `getValueAt` is lock-free.
- **Never call UI code.** No `emit`, no `QMetaObject::invokeMethod`,
  no `juce::MessageManager::callAsync` from the audio thread.
- **Use the SPSC bridge for UI→audio parameter updates.** The
  `SPSCBridge` (`src/engine/SPSCBridge.h`) is a single-producer
  single-consumer queue. UI code pushes `ParamUpdate` structs;
  the audio thread pops them at the top of `processBlock`.
  This is the only sanctioned cross-thread path for parameter
  changes.
- **Level meters use atomics.** `LevelMeter` (`src/engine/LevelMeter.h`)
  stores peak/RMS as `std::atomic<float>`. The audio thread writes;
  the UI thread (VUMeter timer) reads. No locks.
- **Beware of indirect violations.** A realtime-safe function
  becomes unsafe if you call a non-realtime-safe function from it.
  Check the full call chain. Common traps: `juce::Logger::outputDebugString`
  allocates; `juce::ValueTree::getProperty` is safe for reads but
  listeners that trigger writes must not fire on the audio thread.

## Qt Signal/Slot Best Practices

- **Always use the 4-argument `connect` form** when connecting to
  a lambda: `connect(sender, &Sender::signal, receiver, [receiver]() { ... })`.
  The `receiver` context argument ensures Qt auto-disconnects the
  signal if `receiver` is destroyed, preventing use-after-free crashes.
- **Never `connect` to a raw `this` in a constructor** if the
  sender might outlive `this` and the lambda captures `this`.
  Use the 4-arg form with the appropriate context object.
- **Signal emission order is not guaranteed** when multiple slots
  connect to the same signal. If ordering matters, chain explicitly
  or use a single handler that calls sub-handlers in sequence.
- **Avoid signal loops.** A slot that modifies a property whose
  `notify` signal triggers the same slot creates an infinite
  recursion. Use a guard flag or `blockSignals(true)` / `false`
  around programmatic changes.
- **`QStackedWidget::currentChanged` does not fire when the index
  is unchanged.** If you call `setCurrentIndex(0)` on a freshly-
  created stack whose index is already 0, the signal does not
  emit. Do any initial state setup after connecting.

## Key Classes Quick Reference

| Role | File | Class |
|------|------|-------|
| Main window | `src/ui/MainWindow.h` | `MainWindow` |
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
| Composition (phrase / chord / progression generator) | `src/engine/PhraseGenerator.h` | `PhraseGenerator` |
| UI→Audio bridge | `src/engine/SPSCBridge.h` | `SPSCBridge` |
| Level metering | `src/engine/LevelMeter.h` | `LevelMeter` |
| File save/load | `src/engine/ProjectSerializer.h` | `ProjectSerializer` |
| Waveform thumbnails | `src/engine/ProjectPool.h` | `ProjectPool` |
| Timeline composite | `src/ui/TimelineView.h` | `TimelineView` |
| Timeline scene | `src/ui/TimelineScene.h` | `TimelineScene` |
| Timeline interaction | `src/ui/TimelineInteraction.h` | `TimelineInteraction` |
| Clip graphics base | `src/ui/ClipItem.h` | `ClipItem` |
| Audio waveform clip | `src/ui/AudioClipItem.h` | `AudioClipItem` |
| MIDI mini-preview | `src/ui/MidiClipItem.h` | `MidiClipItem` |
| Track headers | `src/ui/TrackHeaderWidget.h` | `TrackHeaderWidget` |
| Time ruler | `src/ui/TimeRuler.h` | `TimeRuler` |
| Playhead | `src/ui/PlayheadCursor.h` | `PlayheadCursor` |
| Loop markers | `src/ui/LoopMarker.h` | `LoopMarker` |
| Mixer | `src/ui/MixerWidget.h` | `MixerWidget` |
| Mixer strip | `src/ui/MixerStripWidget.h` | `MixerStripWidget` |
| VU meter | `src/ui/VUMeter.h` | `VUMeter` |
| Piano roll | `src/ui/PianoRollWidget.h` | `PianoRollWidget` |
| Piano roll model | `src/ui/PianoRollModel.h` | `PianoRollModel` |
| Note grid | `src/ui/NoteGridWidget.h` | `NoteGridWidget` |
| FX chain UI | `src/ui/FXChainWidget.h` | `FXChainWidget` |
| Automation UI | `src/ui/AutomationLaneWidget.h` | `AutomationLaneWidget` |
| Audio clip editor | `src/ui/AudioClipEditorWidget.h` | `AudioClipEditorWidget` |
| Theme colors | `src/ui/Theme.h` | `ThemeColors` |
| Debug logging | `src/ui/DebugLog.h` | `HDAW_LOG` macro |
| MCP server core (tool registry, dispatch, lifecycle) | `src/mcp/McpServer.h` | `mcp::McpServer` |
| MCP JSON-RPC 2.0 framing (requests/responses/notifications) | `src/mcp/McpJsonRpc.h` | `mcp` namespace (`McpRequest`/`McpResponse`/`McpNotification`, `err::`, `parseLine`, `validateRequest`) |
| MCP JSON-Schema subset validator (type/required/properties/items/enum/min/max) | `src/mcp/McpSchema.h` | `mcp::validateSchema` |
| MCP transport interface (4 virtuals: `start`/`stop`/`send`/`notify`) | `src/mcp/McpTransport.h` | `mcp::Transport` |
| MCP stdio transport (newline-delimited JSON, reader thread) | `src/mcp/McpTransportStdio.h` | `mcp::TransportStdio` |
| MCP Streamable HTTP transport (loopback only, real round-trip) | `src/mcp/McpTransportHttp.h` | `mcp::TransportHttp` |
| MCP loopback test transport (in-memory `QByteArray` queues) | `src/mcp/McpTransportLoopback.h` | `mcp::TransportLoopback` |
| MCP tool record (name, description, inputSchema, handler) | `src/mcp/McpToolDef.h` | `mcp::McpToolDef` / `mcp::McpToolResult` / `mcp::McpHandler` |
| MCP tool registrations (all 36 tools live in one file) | `src/mcp/McpTools.{h,cpp}` | `mcp::registerAllTools(McpServer&)` |

## Testing

The project has a gtest suite (added in v0.3.x via the `hdaw_lib`
static library split; see `tests/CMakeLists.txt` and the `HDAW_BUILD_TESTS`
option in the top-level `CMakeLists.txt`). The MCP module is the pilot —
its tests live under `tests/unit/mcp/` and `tests/integration/mcp/`.

- **Run all tests**: `ctest --test-dir build -C Debug --output-on-failure`
  (registers the `hdaw_tests` aggregate) — or run the binary directly:
  `build/Debug/hdaw_tests.exe`. Filter a single gtest sub-suite with
  the binary's `--gtest_filter=SuiteName.*` (e.g. `--gtest_filter=JsonRpc.*`).
  The project's CTest setup registers only the aggregate `hdaw_tests`
  target, not individual gtest sub-suites, so `ctest -R JsonRpc` does
  **not** work — use the gtest binary's filter instead.
- **Layout** mirrors the source path: `tests/unit/mcp/json_rpc_test.cpp`
  tests `src/mcp/McpJsonRpc.h`, `tests/integration/mcp/mcp_server_test.cpp`
  exercises the full server end-to-end. Filenames end in `_test.cpp`.
- **The `TransportLoopback` is the test seam** for the MCP server. It
  uses in-memory `QByteArray` queues and a `pumpIncoming` /
  `waitForOutgoing` API, so the full JSON-RPC protocol can be exercised
  without real stdin/stdout, sockets, or an audio device. Any future
  transport implements the same `Transport` interface and is
  interchangeable in tests.
- **Determinism**: never `sleep()` or use timed waits for
  synchronization — use `QSignalSpy::wait()`, the loopback's
  `waitForOutgoing` (bounded timeout), or condition variables.
- **Temp directories**: keep unique per test (e.g. via `QStandardPaths::TempLocation` + a UUID), clean up in teardown.
- **Known test-infra note**: `McpServer.HttpRoundTrip` is order-sensitive
  in the test binary — it must run **first** in the `McpServer` suite
  because the JUCE WASAPI audio-device teardown from earlier tests
  leaves process-wide COM/WinHTTP state that a subsequent HTTP test
  cannot recover from. Single-run is stable; `--gtest_repeat` is
  flaky at the start of iteration 2. A comment in the test file
  documents this. A future fix is to make the test order-independent
  (e.g. by isolating the audio device).

## MCP server (v0.3.x)

A new `src/mcp/` module exposes HDAW as an **MCP** (Model Context
Protocol) server so an LLM client (Claude Desktop, opencode, etc.)
can drive the DAW. 36 tools cover transport, tracks, clips, MIDI notes,
composition (`PhraseGenerator`), FX, automation, undo, and audio export.

- **Two transports**, both behind the `Transport` interface
  (`src/mcp/McpTransport.h`): `McpTransportStdio` (newline-delimited
  JSON over `stdin`/`stdout`, with a dedicated reader thread that
  posts requests to the server via `Qt::QueuedConnection`) and
  `McpTransportHttp` (Streamable HTTP on `127.0.0.1`, loopback only,
  no auth). `McpTransportLoopback` is the in-memory test transport.
- **Tool safety**: every destructive tool (`remove_*`, `clear_notes`,
  `duplicate_clip`, `export_audio`) accepts `dryRun: true` and reports
  what it would do without mutating. Every mutation goes through the
  `UndoManager` so `undo` / `redo` tools (or the GUI's `Ctrl+Z`) can
  roll it back. `notifications/cancelled` sets a `std::atomic<bool>`
  cancel flag (`McpServer::isCancelRequested()`); the spec's
  worker-thread follow-up will poll this for cancellable exports.
- **Tool-execution errors are not JSON-RPC errors.** Per the MCP
  contract, a tool that runs but fails returns
  `{isError: true, content: [{type:"text", text:"…"}]}` in a SUCCESSFUL
  JSON-RPC response. JSON-RPC errors (`{code, message}`) are reserved
  for parse/validation/method-not-found failures. `McpServer::dispatchRequest`
  in `src/mcp/McpServer.cpp` is the single dispatch path used by both
  the stdio transport (via the `handleRequest` slot) and the HTTP
  transport (directly, synchronously).
- **Every tool runs on the main thread.** This is the same
  single-thread rule as the rest of the project: tools access the
  engine/model directly without locks, and the audio thread is
  never touched. Audio-thread concerns (e.g. plugin parameter
  changes) are the tool handler's responsibility — use `SPSCBridge` for
  audio-thread notifications, as documented in the next section.
- **Spec / plan** documents: `docs/superpowers/specs/2026-06-29-hdaw-mcp-server-design.md`
  and `docs/superpowers/plans/2026-06-29-hdaw-mcp-server-phase{1,2}.md`.

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

## QGraphicsView initial vertical scroll position — the silent show-stealer

**Symptom**: At startup, the main window appears with the three default
tracks laid out, but **none of the track rows are visible** in the
track-header area on the left. The headers are present (the right size,
the right width) but the actual painted track rectangles are scrolled
above the visible clip-rect, so the user sees a blank band.

After clicking "Add Track", the layout reflows and the tracks suddenly
become visible. The user perceives this as "tracks are missing at
startup" and assumes a data path bug. **It is not a data path bug.
It is a scroll-position bug.**

**Root cause**: `QGraphicsView` computes its vertical scroll-bar value
during the first layout pass, *after* `setupUI` returns. Setting
`verticalScrollBar()->setValue(0)` in `setupUI` is a no-op: the
viewport has not been sized yet, the scroll-bar range is `0..0`, and
whatever value `setupUI` sets is overwritten when the layout pass
finishes. With a 4000×2000 sceneRect and a viewport that lands at
~535 px tall after layout, the value lands at roughly
`(2000 - 535) / 2 ≈ 732`, not at 0.

That scroll value propagates through `TimelineView::syncRulerWithScene`
into `TrackHeaderWidget::setScrollOffset`. The track header widget's
`paintEvent` then computes `trackY = rulerHeight - scrollOffset` and
paints every track at negative y. The widget's clip-rect starts at
y = 30, so all painted track rows are above the clip and nothing is
visible.

**The fix** lives in `src/ui/TimelineView.cpp` — override
`QWidget::showEvent` and reset both scroll bars there, after the
layout has fully resolved. Also keep `syncRulerWithScene()` clamped
to a valid range as a defensive measure:

```cpp
void TimelineView::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    if (graphicsView != nullptr)
    {
        graphicsView->verticalScrollBar()->setValue(0);
        graphicsView->horizontalScrollBar()->setValue(0);
        syncRulerWithScene();
    }
}

void TimelineView::syncRulerWithScene()
{
    double sceneHeight = timelineScene->sceneRect().height();
    double viewportHeight = graphicsView->viewport()->height();
    double maxScrollY = std::max(0.0, sceneHeight - viewportHeight);
    double scrollY = std::clamp(
        static_cast<double>(graphicsView->verticalScrollBar()->value()),
        0.0, maxScrollY);
    // ... rest of sync, including trackHeaders->setScrollOffset(scrollY)
}
```

**Diagnostic signature** (if the symptom recurs): the
`TrackHeaderWidget::paintEvent` will log `scrollOffset` values like
`~737` (not 0), and the per-track y ranges will be negative
(`y=[-707,-627) h=80`). That confirms this pitfall, not a different
one.

**Why this is easy to re-introduce**: the symptom is identical to a
"data is missing" bug. Future contributors will see "no tracks
visible at startup" and start hunting through `createDefaultProject`,
clip creation, and the scene rebuild. None of that is the cause. The
cause is the *timing* of when the QGraphicsView commits its scroll
position, which is the last thing any reasonable code review would
check.

## Track headers need a `sizeHint` override — the layout will not infer it

`QWidget::sizeHint` returns a useless default (~100 px tall) for
custom widgets. If a custom widget lives in a `QHBoxLayout` next to
a `QGraphicsView` and relies on the row height to fit its content, it
**must** override `sizeHint()`. Otherwise the row collapses to 100 px
and most of the widget's content is clipped.

For `TrackHeaderWidget`, the override sums the ruler height, all
track heights, and a small margin:

```cpp
QSize TrackHeaderWidget::sizeHint() const
{
    auto trackList = engine.getProjectModel().getTrackListTree();
    int count = trackList.getNumChildren();
    double totalH = rulerHeight;
    for (int i = 0; i < count; ++i)
        totalH += getTrackHeight(i);
    int hintH = static_cast<int>(totalH) + 20;
    int hintW = static_cast<int>(headerWidth);
    return QSize(hintW, std::max(hintH, minimumHeight()));
}
```

`minimumSizeHint()` should be overridden too with the fixed header
width and the existing `minimumHeight()` floor.

## `setAlignment(Qt::AlignTop | Qt::AlignLeft)` is required on the timeline QGraphicsView

`QGraphicsView`'s default alignment is `Qt::AlignCenter`. When the
scene is smaller than the viewport (which is the common case for a
DAW timeline with a 290-tall content area in a 535-tall row), the
scene is centered. The headers share the same row at y=0; the
centered scene is offset by half the empty space. The user sees
"track info on the left" at the top of the row and "track data on
the right" shifted down — the misalignment is the alignment default,
not a layout bug.

Set it in `setupUI`, right after constructing the QGraphicsView:

```cpp
graphicsView->setAlignment(Qt::AlignTop | Qt::AlignLeft);
```

## Default project should not reference non-existent sample files

`ProjectModel::createDefaultProject` historically created audio clips
on Track 1 and Vocals with `sourceFile` set to `samples/bass.wav`,
`samples/drums.wav`, and `samples/vocals.wav`. None of these files
ship with the project. The clips would silently render a 10% white
tint (`AudioClipItem::paintContent` fallback) and the user would see
"empty audio clips" with no indication that the data was missing.

Audio tracks should be created with an **empty `CLIP_LIST`**. Users
populate them by drag-dropping real audio files. Do not add
fake/sample audio clips back to the default project without
also shipping the actual sample files.

## Scene-mouse event routing — the installEventFilter trap

`QGraphicsScene::installEventFilter` does **not** receive scene mouse
events. Mouse events from the `QGraphicsView` are dispatched
directly to the hit-tested `QGraphicsItem`, never to the scene
QObject's event filter. If you want to intercept scene mouse events,
override `QGraphicsScene::mousePressEvent` / `mouseMoveEvent` /
`mouseReleaseEvent` / `mouseDoubleClickEvent` directly.

This was the root cause of "can't create or edit clips in the
timeline" in earlier sessions. The interaction code in
`TimelineInteraction` was correctly written but was being installed
on the wrong object.

## `ClipItem` must not have `ItemIsSelectable`

`QGraphicsItem::mousePressEvent`'s default implementation calls
`event->accept()` for any item with `ItemIsSelectable`. That
terminates the scene-mouse-event dispatch before the interaction can
process it, so trim/move/fade drags never start.

Set only `ItemSendsGeometryChanges`. Drive selection explicitly from
the interaction via `clip->setSelected(true)`. The `ClipItem::paint`
method already paints a custom selection outline, so visuals are
preserved.

## `DBG` macro collides with JUCE — use `HDAW_LOG`, do not redefine

JUCE defines `DBG(textToWrite)` as a single-argument macro in
`juce_PlatformDefs.h` (used in 100+ places across the project).
Trying to `#define DBG(tag, msg)` to add a two-argument debug
log is wrong on two counts:

1. **Redefinition warning** — the compiler emits `C4005: 'DBG':
   macro redefinition` because JUCE's version is already in scope
   from any TU that includes a JUCE header.
2. **Signature mismatch** — the 8 existing `DBG("TSCtor", ...)`
   call sites in `TimelineScene.cpp` and `MainWindow.cpp` pass
   two arguments (tag + message). JUCE's `DBG` takes one
   argument. Either the build fails outright or the calls bind
   to the wrong macro and silently produce garbage.

The project's own logging facility is `HDAW_LOG(tag, msg)`, defined
in `src/ui/DebugLog.h`. It writes NDJSON to
`%TEMP%/hdaw_debug.log`. All TUs that call it must
`#include "DebugLog.h"`.

**Rule**: never use the bare `DBG` identifier in this project. If
you see `DBG(...)` in source, rename it to `HDAW_LOG(...)`. If you
add a new logging macro, pick a name that does not collide with
JUCE — `HDAW_LOG`, `LOG_INFO`, `AppLog`, anything but `DBG`.

## `paintEvent` clip-rect must include the ruler offset — `TrackHeaderWidget`

`TrackHeaderWidget::paintEvent` clips its drawing to a region that
*excludes* the ruler area:

```cpp
painter.setClipRect(0, static_cast<int>(rulerHeight),
                    w, height() - static_cast<int>(rulerHeight));
```

If the widget's actual height is smaller than
`rulerHeight + oneTrackHeight` (i.e. less than 110 px), the clip
rect's height becomes negative or zero, and the visible track rows
collapse to nothing. The early-history symptom was that the widget
was sized to 100 px (its `minimumHeight()` floor) and the user saw
"track header is empty."

The fix is twofold:

1. **Override `sizeHint()`** so the layout allocates enough vertical
   space for the ruler plus all track rows. The "Track headers
   need a `sizeHint` override" section above documents the
   override.
2. **Keep the clip-rect at the top of `paintEvent`** — the ruler
   area is painted elsewhere (or not at all, in current code) but
   the clip is what hides the per-track backgrounds below the
   ruler.

If you change the `rulerHeight` constant in `TrackHeaderWidget.h`,
make sure both `sizeHint` and the clip-rect use the same value.
The two are intentionally coupled and live in the same header
file as `static constexpr double rulerHeight = 30.0;`.

## `TimelineView` setupUI / connectSignals ordering — null-deref trap

`MainWindow::setupLayout` calls
`connect(bottomStack, &QStackedWidget::currentChanged, ...)`
to keep the tab-button checked-state in sync with the stack.
If you write that `connect` *before* `bottomStack = new
QStackedWidget(...)`, you dereference a null pointer and the
`MainWindow` ctor crashes during startup with no useful error
message. The crash happens *after* the scene ctor logs
`[TSCtor] sceneRect=...` and *before* any UI event, which looks
like a Qt init failure if you don't read the log carefully.

**Rule for `MainWindow::setupLayout`**: create the `QStackedWidget`
first, populate it with all four child widgets (Mixer, Piano Roll,
FX Chain, Automation), *then* connect `currentChanged` to the
button-group sync lambda, *then* call `setCurrentIndex(0)`. The
order is:

```cpp
bottomStack = new QStackedWidget(bottomContainer);
bottomStack->addWidget(mixerWidget);
bottomStack->addWidget(pianoRollWidget);
bottomStack->addWidget(fxChainWidget);
bottomStack->addWidget(automationWidget);

// connect currentChanged HERE, after bottomStack exists
connect(bottomStack, &QStackedWidget::currentChanged, ...);
// initial setChecked, then setCurrentIndex(0)
bottomStack->setCurrentIndex(0);
```

If you see a startup crash that produces no error dialog and no
console output, check that any new `connect(...)` call does not
reference a member variable that hasn't been `new`'d yet.

## Piano-roll pitfalls — `MIDI_NOTE_LIST`, vertical scroll, note culling

`PianoRollWidget` and `NoteGridWidget` have three traps that all
look like "I can't edit MIDI" from the user's side:

1. **MIDI clips must carry a `MIDI_NOTE_LIST` child.** If a clip
   is missing the note container, `PianoRollModel::addNote`
   returns an empty `ValueTree` and clicks silently no-op. The
   defensive fix in `PianoRollModel::setClipTree` is to create
   the container if it's missing:

   ```cpp
   noteList = clip.getChildWithName(IDs::MIDI_NOTE_LIST);
   if (!noteList.isValid() && clip.isValid()) {
       noteList = juce::ValueTree(IDs::MIDI_NOTE_LIST);
       clip.addChild(noteList, -1, nullptr);  // no undo manager
   }
   ```

2. **Default `scrollY` lands above middle C.** With
   `keyHeight = 10` and the default 6 notes in the project's
   `createMidiClip` (notes 60, 64, 67, 72, 71, 69), a scrollY of
   0 places note 95 at the top of the grid — middle C (note 60)
   is at y=350 and completely off-screen. Use
   `NoteGridWidget::defaultScrollYForMiddleC()` (= 350) as the
   initial `scrollY` in `PianoRollWidget::loadClip`. Also clamp
   `setScrollOffset` to `[0, 128 * keyHeight - height()]` so the
   user cannot scroll past the end.

3. **The vertical scrollbar overlays the note grid.** Adding a
   `QScrollBar` to the same `QHBoxLayout` as the note grid
   consumes the rightmost 17 px of the grid. Use a `QGridLayout`
   with three columns (keys | noteGrid | vScrollBar) so the
   scrollbar has its own column and never overlaps the grid.

4. **The `paintEvent` cull is horizontal only.** The current
   cull in `NoteGridWidget::paintEvent` skips notes whose rect
   lies entirely outside the viewport horizontally but does
   NOT cull vertically. If a future feature moves notes far
   from y=0, add a vertical cull (`r.bottom() < 0 || r.top() >
   h`) to avoid the painter being asked to draw off-screen
   rects.

## Tab buttons in `QStackedWidget` need a `QButtonGroup` + `currentChanged` bridge

`QStackedWidget::setCurrentIndex(N)` does not emit a `clicked`
signal on any button. If your tab buttons are `checkable: true`
and you only connect their `clicked` to `setCurrentIndex(N)`,
then programmatic `setCurrentIndex(M)` (e.g. from a
`clipSelected` signal) leaves the button checked-state
unchanged. The user sees the bottom panel change but the tab
button stays on the old tab — it looks like nothing happened.

The fix:

- Put all tab buttons in a `QButtonGroup` with
  `setExclusive(true)`.
- Connect `bottomStack->currentChanged` to a lambda that
  iterates the buttons and sets `setChecked(i == index)`.
- Also do an explicit `setChecked` on the initial
  `currentIndex` after connecting — `setCurrentIndex(0)` won't
  fire `currentChanged` if the stack's current index is already
  0.

The `MainWindow::tabGroup`, `MainWindow::tabButtons` members and
the `makeTab` lambda are the working pattern. Don't restructure
them; copy them when adding new tabs.

## Forward-declare to break circular includes between `TimelineScene` and `TimelineInteraction`

`TimelineScene.h` and `TimelineInteraction.h` had a circular
include. `TimelineScene` needs to call into
`TimelineInteraction`'s handlers (or hold a pointer to it for
delegation); `TimelineInteraction` needs to know about
`TimelineScene` (its `scene` member, plus a forward `TimelineScene*`
in member functions). Including both headers from each other
produces incomplete-type errors when the compiler reaches the
`TimelineScene::mousePressEvent(QGraphicsSceneMouseEvent*)`
override in the `.cpp` and the `ClipItem` member
`dragItem` is referenced from `TimelineInteraction.h`.

**The fix** in `TimelineInteraction.h`:

```cpp
#pragma once
#include <QObject>
#include <QGraphicsSceneMouseEvent>
#include <juce_data_structures/juce_data_structures.h>

class TimelineScene;
class ClipItem;
class AudioEngine;
```

Three forward declarations, no header includes for those types.
The `TimelineInteraction.cpp` then `#include "TimelineScene.h"`
and `#include "ClipItem.h"` to get the full definitions before
calling methods on them. This pattern is needed any time two
headers mutually reference each other — the right move is to
forward-declare in the header and include in the cpp.

Also: never make the four scene-mouse-handler methods
(`handleMousePress`, `handleMouseMove`, `handleMouseRelease`,
`handleMouseDoubleClick`) `private` on `TimelineInteraction` if
`TimelineScene` needs to call them. They must be `public` (or
`TimelineScene` must be a `friend class`). The access check
fails at compile time with `C2248: cannot access private
member` and the error message doesn't make the cause obvious
because the method *names* are public-looking.

## Build pipeline: MOC, autogen, stale PDB, parallel-link

The project uses Qt 6 with `qt_standard_project_setup()` which
enables `CMAKE_AUTOMOC` automatically. MOC processes any header
that contains `Q_OBJECT`. A few things to know:

- **Stale PDB on parallel builds**. The first time
  `cmake --build build --config Debug` is invoked after a large
  edit, MSBuild's parallel-link may fail with
  `C1041: cannot open program database 'vc145.pdb'; if multiple
  CL.EXE write to the same .PDB file, please use /FS`. The fix
  is to kill any orphaned `cl.exe` and `Tracker.exe` processes
  left over from a previous aborted build, then re-run. The
  command:

  ```powershell
  Get-Process cl, Tracker, MSBuild -ErrorAction SilentlyContinue |
      Stop-Process -Force
  cmake --build build --config Debug
  ```

- **Header-only edits are not always detected.** The build
  system uses header mtime to decide what to recompile. If you
  change a `.h` and the build does not pick it up (you see
  unchanged behaviour despite a clear source diff), force a
  recompile by touching the corresponding `.cpp` or by deleting
  the relevant `.obj` files in `build/HDAW.dir/Debug/`.

- **The Release binary is stale.** If the user reports
  "nothing changed visually," check whether they are running
  `build\Debug\HDAW.exe` (29 MB) or `build\Release\HDAW.exe`
  (5 MB). The Release one was built before the bug-fix series
  and is intentionally not maintained. Always run the Debug
  binary.

- **Sources must be added to `add_executable` in `CMakeLists.txt`.**
  Adding a new `.cpp` file without listing it in the CMake
  source list will not produce a build error — the file just
  will not be compiled. Always check the source list when
  adding a new translation unit.

## Diagnostic pattern when something is mysteriously wrong

When a fix doesn't take effect despite clear evidence the binary
contains it, add `HDAW_LOG` calls at the *exact* points where the
problem-state is decided. The pattern is:

1. One log at construction / data-setup time, showing the input state.
2. One log in the rendering or event-handling path, showing what the
   widget actually sees at runtime.
3. Cross-reference the two. If the data is correct but the
   rendering is wrong, the bug is in the rendering path. If the
   data is wrong, trace upstream.

This is what surfaced the QGraphicsView scroll-position bug. A
similar `paintEvent` log on `TrackHeaderWidget` revealed that
`scrollOffset=737` at first paint, immediately pointing at the
QGraphicsView's internal layout-time scroll commit.

**The log file is the first place to look, not the source code.**
When a user reports "nothing changed visually" or "X is broken,"
read `%TEMP%/hdaw_debug.log` directly. The `TSCtor`,
`TSRebuild START/DONE`, `MWAddTrk`, `TIEvt`, `TIPress`,
`TIDblClk` lines (pre-existing) give a complete picture of what
the app did during startup and on each user interaction. If a
fix is supposed to take effect and the log doesn't show the
expected state, the fix isn't running — probably a stale
binary (see "Build pipeline" above) or a missed compile of the
edited file.

## Plugin editors need a DocumentWindow wrapper — `setVisible(true)` alone is invisible

`TrackFXSlot::showEditor()` used to call `createEditor()` followed by
`setVisible(true)` with no parent or desktop backing. A JUCE `Component`
that isn't added to a parent hierarchy and hasn't called `addToDesktop()`
is invisible — it has no native window.

The fix: wrap the editor in a `juce::DocumentWindow` subclass
(`PluginEditorWindow` in `TrackFXSlot.h`) that:
- Takes ownership via `setContentOwned(editor, true)`
- Provides a native title bar with close button
- Calls `closeEditor()` via a `std::function<void()>` callback on close
- Centres on screen using the editor's preferred size

`TrackFXSlot` stores `std::unique_ptr<juce::DocumentWindow> editorWindow`
instead of `std::unique_ptr<juce::AudioProcessorEditor> editor`.

If a future refactor moves plugin hosting to a separate class, the
`PluginEditorWindow` helper should move with it. The close-button
callback pattern (`[this]() { closeEditor(); }`) assumes the
`TrackFXSlot` outlives the window, which is guaranteed by member
destruction order (`editorWindow` is destroyed before `this`).

## TrackHeaderWidget selection highlight — paint style matters

`TrackHeaderWidget::paintEvent` draws a subtle blue overlay on the selected
track row at alpha 40. If the selection is hard to see (user reports "I can't
tell which track is selected"), make the highlight more visible:

- Use a brighter color `(80, 160, 255)` and higher alpha `60`.
- Use a thicker border (`rect.adjusted(2, 1, -1, -1)`).
- Widen the left color strip from 3 px to 6 px when selected, and use
  `trackColor.lighter(140)` for extra contrast.

The `selectedTrack` member (`TrackHeaderWidget.h:79`, default `-1`) is
write-on-read. It is set by clicking on the track body in
`TrackHeaderWidget::mousePressEvent` and via the public
`setSelectedTrack(int)` slot added in v0.2.2. The slot calls `update()`
to trigger a repaint.

## clipSelected must also update the track header selection

`TimelineScene::clipSelected` is emitted when a clip is clicked in the
timeline. `MainWindow` connects this signal to open the piano roll or
audio editor. **Critically, it must also update `TrackHeaderWidget::selectedTrack`**
or the user sees no highlight in the track header.

The fix (added in v0.2.2):

1. `TrackHeaderWidget`: public slot `setSelectedTrack(int)`.
2. `TimelineView`: public slot `selectTrack(int)` → forwards to
   `trackHeaders->setSelectedTrack(index)`.
3. `MainWindow::clipSelected` lambda: extract the parent track via
   `clipTree.getParent().getParent()`, find its index in the track list
   with `trackList.indexOf(trackTree)`, then call
   `timelineView->selectTrack(idx)` and update `selectedTrack`.

The parent-traversal chain is: `CLIP` → `CLIP_LIST` → `TRACK`. Both
intermediate parents must validate correctly or the index lookup returns -1.

## Loop playback ignores region if bounds aren't synced on init

**Root cause**: `AudioEngine::initialize()` never syncs loop start/end
from the `ValueTree` (TRANSPORT node) to the `TransportManager` atomics
(`loopStartSample`, `loopEndSample`). The atomics default to `0`, so
`TransportManager::advance()` never wraps — playback ignores the loop
region entirely.

**Fix** (`AudioEngine::initialize`, v0.2.2): after setting the playhead,
explicitly sync loop bounds:

```cpp
auto transportTree = projectModel.getTransportTree();
double loopStart = transportTree.getProperty(IDs::loopStart, 0.0);
double loopEnd = transportTree.getProperty(IDs::loopEnd, 4.0);
transportManager.setLoopStart(loopStart);
transportManager.setLoopEnd(loopEnd);
```

Also add a `valueTreePropertyChanged` listener for `loopStart`, `loopEnd`,
and `isLooping` so runtime changes (e.g. from `LoopMarker` drag or ruler
context menu) reach the audio thread.

## TimeRuler context menu for loop region

`TimeRuler` did not override `contextMenuEvent`, so right-clicking on the
ruler was silently ignored. The user had no way to set the loop region
without dragging markers (which also had issues — see the next section).

**Fix** (`TimeRuler`, v0.2.2): override `contextMenuEvent` with three
actions:

- **Set Loop Start Here** → sets `loopStart` to the beat at the click position.
- **Set Loop End Here** → sets `loopEnd` to the beat at the click position.
- **Toggle Loop** → toggles `isLooping`.

Each action writes to the TRANSPORT ValueTree via the UndoManager.
After setting bounds, emit `loopBoundsChanged()` so `LoopMarker` updates
its position.

## LoopMarker drag must commit bounds to ValueTree

`LoopMarker::mouseMoveEvent` updates the marker's position on the scene but
**never writes the new position back to the TRANSPORT ValueTree**. On
project reload, the loop bounds revert to what was last saved — all drag
edits are lost.

**Fix** (`LoopMarker`, v0.2.2): override `mouseReleaseEvent` to call a new
`commitLoopBounds()` method that writes the marker's current beat position
to the appropriate property (`loopStart` or `loopEnd`) on the TRANSPORT
ValueTree:

```cpp
void LoopMarker::commitLoopBounds()
{
    if (!engine || !engine->isValid())
        return;
    auto transportTree = engine->getProjectModel().getTransportTree();
    double beatPos = pos().x() / pixelsPerBeat;
    transportTree.setProperty(isStart ? IDs::loopStart : IDs::loopEnd,
                              beatPos, nullptr);
    emit loopBoundsChanged();
}
```

## LoopMarker right-click must pass through to ruler

`LoopMarker` default accepts all mouse buttons, which steals right-click
events from the `TimeRuler` below. The user right-clicks on the loop marker
expecting a context menu, and nothing happens.

**Fix** (`LoopMarker`, v0.2.2): call `setAcceptedMouseButtons(Qt::LeftButton)`
in the constructor. Right-click events fall through the marker to the ruler,
which now handles them via its own `contextMenuEvent`.

## Out-of-scope: known gaps deferred to future work

- **No per-clip audio editor**. Double-clicking an audio clip
  currently routes the user to the global Mixer panel, not to a
  per-clip waveform/properties editor. Adding one is a
  ~200-400-line feature, not a bug fix. (This is the main v0.4
  candidate.)
- **MCP server documented v1 follow-ups** (tracked in
  `docs/superpowers/specs/2026-06-29-hdaw-mcp-server-design.md` §10):
  - HTTP authentication for non-loopback exposure.
  - `resources/*` and `prompts/*` (the v1 surface is tools-only).
  - `export_audio` worker-thread + per-block cancellation (the v1
    implementation is synchronous on the main thread with a
    cancel-watcher thread; full async is a v1 follow-up).
- **MCP `McpServer.HttpRoundTrip` test ordering fragility** — the
  test must run first in the `McpServer` suite because the JUCE
  WASAPI audio-device teardown from earlier tests leaves
  process-wide COM/WinHTTP state that a subsequent HTTP test cannot
  recover from. Documented in the test file; a future fix isolates
  the audio device.
