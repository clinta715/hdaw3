# AGENTS.md

Project-specific lessons learned. Read this before working on the timeline,
the project model, or the frontend â€" these are the pitfalls that cost
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
drag Y-tracking â€” clips now follow the mouse smoothly across tracks
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
teardown in AudioEngine, MARKER_LIST teardown),
and an optimized track removal (incremental `removeTrackRow()` replacing
full `rebuildFromValueTree()` on track deletion). **v0.12.0+** adds
batch clip RPCs (`duplicateClips`, `moveClips`, `removeClips`, `addClips`)
to fix the recurring ctrl-drag stale-closure regression, optimistic
placement for NoteGrid/FXChain operations, frontend test infrastructure
(Vitest + Playwright, 86 tests), frontend pitfalls documentation, and
`build-fast.bat` for incremental builds. For the full list of working
features and the priority-ordered roadmap, see `README.md`.

## Documentation Directory

Detailed documentation has been split into domain-specific files:

| File | Contents |
|------|----------|
| [`docs/architecture.md`](docs/architecture.md) | Build, Version Management, Key Classes, GUI-Engine Decoupling, Frontend Architecture, Timestretch, JUCE 9 Migration |
| [`docs/realtime-safety.md`](docs/realtime-safety.md) | Audio-Thread Safety Rules, Hardening Lessons, Diagnostic Pattern, Codebase Hardening, Plugin Process Isolation |

| [`docs/pitfalls-juce.md`](docs/pitfalls-juce.md) | VST3 scan blacklisting, default project samples, DBG macro collision, build pipeline (MOC/PDB), AudioProcessorGraph bus layout |
| [`docs/pitfalls-frontend.md`](docs/pitfalls-frontend.md) | Stale closures after async, optimistic placement + syncSnapshot conflict, drag double-movement, store vs prop reads |
| [`docs/testing-mcp.md`](docs/testing-mcp.md) | GTest Suite, TransportLoopback Test Seam, MCP Server Architecture, MCP Tool Safety, File Browser Audio Preview |
| [`docs/valuetree-listener-contract.md`](docs/valuetree-listener-contract.md) | ValueTree listener registration contract, orphan prevention, ReadModel alternative, audit checklist |

**Quick reference:** For a specific pitfall, search the relevant domain file. For architecture questions, start with `docs/architecture.md`. For realtime safety constraints, see `docs/realtime-safety.md`.

### Build (summary)

- Configuration: `cmake --build build --config Debug`
- Outputs: `build/Debug/HDAW.exe`, `build/Debug/HDAW_headless.exe`, `build/Debug/hdaw_tests.exe`
- Do NOT run `build/Release/HDAW.exe` â€” stale binary, contains none of the fixes.
- **Two launch modes:** Default (browser), Headless (Electron)
- **Frontend build:** `cd frontend && npm run build`, then rebuild C++ project.
- See [`docs/architecture.md`](docs/architecture.md) for full build details.

## Testing

- **C++ engine tests (gtest):** `build/Debug/hdaw_tests.exe`
  - Filter: `--gtest_filter=SuiteName.*`
  - ~62 tests covering MCP tools, transport, tracks, clips, notes, FX,
    automation, undo, save/load, phrase generation, error conditions, batch ops
- **Frontend unit tests (Vitest):** `cd frontend && npm test`
  - Component tests: `src/components/*.test.tsx`
  - Store tests: `src/store/*.test.ts`
  - ~86 tests covering Zustand stores (transport, ui, project, notify, meter, browser)
    and React components (StatusBar, Toaster, BottomTabs) and hooks (useTimelineDrag)
  - Watch mode: `npm run test:watch`
  - Coverage: `npm run test:coverage`
- **Frontend E2E tests (Playwright):** `cd frontend && npm run test:e2e`
  - Tests: `e2e/*.spec.ts`
  - Requires running `HDAW.exe` (engine serves the frontend on port 8765)
  - Interactive UI: `npm run test:e2e:ui`

## Version Management

Version numbers are stored in **two places** and must be kept in sync manually:
- `CMakeLists.txt` → `project(HDAW VERSION 0.12.0 ...)` — **canonical source** for C++ code
- `frontend/package.json` → `"version": "0.12.0"` — **canonical source** for the React frontend

See [docs/architecture.md](docs/architecture.md) for full version management details.

## Code Style

See [docs/architecture.md](docs/architecture.md) for code style conventions.

## Frontend Pitfalls

These are recurring, non-obvious bugs that have cost real debugging time.
Full details in [`docs/pitfalls-frontend.md`](docs/pitfalls-frontend.md).

1. **Stale `clips` closure after async mutations.** Hooks receive `clips`
   as a prop (from the component's render). After an async operation
   (duplicate, syncSnapshot, etc.), the store has newer data than the
   prop. Any code that looks up clips by ID **after** an async op must
   read from `useProjectStore.getState().snapshot?.clips`, NOT from the
   `clips` prop. The original ctrl-drag bug: `handleMouseUp` used
   `clips.find(id)` on the stale prop, silently skipped the RPC move for
   duplicated clips, then `syncSnapshot` overwrote the optimistic update
   — clips jumped back to their original position.

2. **Optimistic placement + `syncSnapshot` conflict.** If you
   optimistically update the store AND then call `syncSnapshot`, the
   sync replaces the entire snapshot. If the RPC didn't actually make
   the change on the backend (e.g., it was skipped due to pitfall #1),
   the optimistic update is lost. Always verify the RPC path runs
   correctly before calling `syncSnapshot`.

3. **Optimistic placement during continuous drag is wrong.** Creating
   optimistic clips at the initial mouse position during a drag causes
   double-movement: the clip is placed at position X, then the drag
   applies an offset relative to the original, moving it again. For
   drag operations where position changes continuously, either:
   - Don't use optimistic placement (let RPC complete first), OR
   - Update the optimistic clip's position on every mousemove, OR
   - Only place optimistically on mouseup (like normal drag does)

4. **`dragSelectedIdsRef` changes identity during async ops.** When
   ctrl-drag duplicates clips, `dragSelectedIdsRef.current` is replaced
   with new clip IDs. But the `clips` array in the closure still
   references the old clips. Any code that iterates over
   `dragSelectedIdsRef.current` after the async duplicate must look up
   clips from the store, not from the closure prop.

5. **Window-level listeners and stale closures.** The drag hook
   registers `mousemove`/`mouseup` on `window` via refs
   (`handleMouseMoveRef`/`handleMouseUpRef`) to avoid stale closures.
   But the refs point to `useCallback` functions that may still close
   over stale `clips`. The fix from pitfall #1 applies here too: read
   from the store inside the callback, not from the closure.

6. **Clip placement must go through `moveClipWithOverlap`.** Any
   function that places a clip at a position (duplicate, paste, import)
   must call `moveClipWithOverlap` after `clipList.addChild` to
   handle overlapping clips (trim, split, remove). Without this,
   clips can overlap instead of overwriting. Functions that skip
   this: `duplicateClipTo`, `duplicateClips`, `addClips`. Always
   call `moveClipWithOverlap(newId, trackIndex, start)` after adding.

## Further Reading

All detailed documentation lives in the domain-specific files above. This file
serves as the entry point and quick reference. For the full content that was
previously in this file, see:

- **Architecture, Build, Frontend:** [docs/architecture.md](docs/architecture.md)
- **Realtime Safety, Hardening:** [docs/realtime-safety.md](docs/realtime-safety.md)
- **Frontend Pitfalls:** [docs/pitfalls-frontend.md](docs/pitfalls-frontend.md)
- **JUCE Engine Pitfalls:** [docs/pitfalls-juce.md](docs/pitfalls-juce.md)
- **Testing, MCP Server:** [docs/testing-mcp.md](docs/testing-mcp.md)
- **ValueTree Listener Contract:** [docs/valuetree-listener-contract.md](docs/valuetree-listener-contract.md)
