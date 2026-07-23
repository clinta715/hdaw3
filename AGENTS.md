# AGENTS.md

Project-specific lessons learned. Read this before working on the timeline,
the project model, or the main window â€” these are the pitfalls that cost
real debugging time.

**Current scope**: HDAW is a JUCE 8 desktop DAW at version **0.12.0**
with a **React 19 + TypeScript frontend** (Zustand state management,
Vite build) as the primary GUI. The frontend runs in three contexts:
system browser (default), Electron shell, or dev server. The C++ engine
exposes its state via JSON-RPC 2.0 over WebSocket (port 8766) and serves
the bundled React SPA via HTTP (port 8765). The Qt 6 desktop GUI is
**deprecated** (optional, requires `-DHDAW_GUI=ON`, `--gui` flag). The
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
drag Y-tracking â€” clips now follow the mouse smoothly across tracks
instead of snapping at track boundaries with a discrete jump.
**v0.10.0** adds tempo point interactions on the ruler (add/edit/
remove/drag), duplicate track with ID-safe deep copy, audio device
preference persistence, transport-synced loop preview, auto-trim
silence on import (-60 dB), missing-file error indicators, and two
new MCP tools (`duplicate_track`, `add_track_with_fx`). **v0.12.0**
adds the GUI inspection MCP tool suite (`inspect_*` tools for
traversing the full QWidget/QGraphicsView scene graph, verifying
widget visibility, geometry, paint, and layout state), 49 new
gtest GUI functionality tests, the Plugin Manager dialog, file
browser audio preview, collapsible right panel, and bugfixes for
clip rubber-band selection (live highlighting, stale-click guard),
trim/fade handle click propagation, track header resize handle
click propagation, audio clip deletion not stopping playback
(missing `valueTreeChildRemoved` â†’ `rebuildRoutingGraph()`), and
three additional asymmetric ValueTree listener fixes (TRANSPORT
teardown in AudioEngine, TRACK teardown in TimelineScene, TRANSPORT
UI teardown in MainWindow, MARKER_LIST teardown in TimelineView),
and an optimized track removal (incremental `removeTrackRow()` replacing
full `rebuildFromValueTree()` on track deletion). For the
full list of working features and the priority-ordered roadmap, see
`README.md`.

## Documentation Directory

Detailed documentation has been split into domain-specific files:

| File | Contents |
|------|----------|
| [`docs/architecture.md`](docs/architecture.md) | Build, Version Management, Key Classes, GUI-Engine Decoupling, Frontend Architecture, Timestretch, JUCE 9 Migration |
| [`docs/realtime-safety.md`](docs/realtime-safety.md) | Audio-Thread Safety Rules, Hardening Lessons, Diagnostic Pattern, Codebase Hardening, Plugin Process Isolation |
| [`docs/pitfalls-qt.md`](docs/pitfalls-qt.md) | QGraphicsView scroll, TrackHeaderWidget, installEventFilter, ClipItem selection, paintEvent, setupUI ordering, piano-roll, QStackedWidget tabs, circular includes, plugin editors, null-collaborator fallback, ValueTree listener orphans |
| [`docs/pitfalls-juce.md`](docs/pitfalls-juce.md) | VST3 scan blacklisting, default project samples, DBG macro collision, build pipeline (MOC/PDB), AudioProcessorGraph bus layout |
| [`docs/testing-mcp.md`](docs/testing-mcp.md) | GTest Suite, TransportLoopback Test Seam, MCP Server Architecture, MCP Tool Safety, File Browser Audio Preview |
| [`docs/valuetree-listener-contract.md`](docs/valuetree-listener-contract.md) | ValueTree listener registration contract, orphan prevention, ReadModel alternative, audit checklist |

**Quick reference:** For a specific pitfall, search the relevant domain file. For architecture questions, start with `docs/architecture.md`. For realtime safety constraints, see `docs/realtime-safety.md`.

### Build (summary)

- Configuration: `cmake --build build --config Debug`
- Outputs: `build/Debug/HDAW.exe`, `build/Debug/HDAW_headless.exe`, `build/Debug/hdaw_tests.exe`
- Do NOT run `build/Release/HDAW.exe` â€” stale binary, contains none of the fixes.
- **Three launch modes:** Default (browser), Headless (Electron), Qt GUI (deprecated)
- **Frontend build:** `cd frontend && npm run build`, then rebuild C++ project.
- See [`docs/architecture.md`](docs/architecture.md) for full build details.

## Version Management

Version numbers are stored in **two places** and must be kept in sync manually:
- `CMakeLists.txt` → `project(HDAW VERSION 0.12.0 ...)` — **canonical source** for C++ code
- `frontend/package.json` → `"version": "0.12.0"` — **canonical source** for the React frontend

See [docs/architecture.md](docs/architecture.md) for full version management details.

## Code Style

See [docs/architecture.md](docs/architecture.md) for code style conventions.

## Further Reading

All detailed documentation lives in the domain-specific files above. This file
serves as the entry point and quick reference. For the full content that was
previously in this file, see:

- **Architecture, Build, Frontend:** [docs/architecture.md](docs/architecture.md)
- **Realtime Safety, Hardening:** [docs/realtime-safety.md](docs/realtime-safety.md)
- **Qt Widget Pitfalls:** [docs/pitfalls-qt.md](docs/pitfalls-qt.md)
- **JUCE Engine Pitfalls:** [docs/pitfalls-juce.md](docs/pitfalls-juce.md)
- **Testing, MCP Server:** [docs/testing-mcp.md](docs/testing-mcp.md)
- **ValueTree Listener Contract:** [docs/valuetree-listener-contract.md](docs/valuetree-listener-contract.md)
