# AGENTS.md

Project-specific lessons learned. Read this before working on the timeline,
the project model, or the frontend — these are the pitfalls that cost real
debugging time.

**Current scope**: HDAW is a JUCE 8 desktop DAW at version **0.13.1** with a
**React 19 + TypeScript frontend** (Zustand, Vite). The frontend runs in two
contexts: system browser (default) or Electron shell. The C++ engine exposes
state via JSON-RPC 2.0 over WebSocket (port 8766) and serves the bundled React
SPA over HTTP (port 8765). The core engine (project model, transport, routing,
JUCE plugin hosting, internal FX) and the frontend (track headers, timeline,
mixer, piano roll, FX chain, automation) work end-to-end. For the feature
history and roadmap see `README.md`; per-version changes live in the git log.

## Documentation Directory

Detailed documentation is split into domain-specific files. For a specific
pitfall, search the relevant file; for architecture start with
`docs/architecture.md`; for realtime constraints see `docs/realtime-safety.md`.

| File | Contents |
|------|----------|
| [`docs/architecture.md`](docs/architecture.md) | Build, version management, key classes, GUI-engine decoupling, frontend architecture, timestretch, JUCE 9 migration, **beats-vs-seconds unit convention** |
| [`docs/realtime-safety.md`](docs/realtime-safety.md) | Audio-thread safety rules, hardening lessons, diagnostic pattern, plugin process isolation, **transport-stopped early-out (audio buzz), auto-stop / projectEndSample staleness** |
| [`docs/pitfalls-juce.md`](docs/pitfalls-juce.md) | VST3 scan blacklisting, default project samples, DBG macro collision, build pipeline (MOC/PDB), AudioProcessorGraph bus layout, **setProperty no-op on unchanged value, notify.transport dedup** |
| [`docs/pitfalls-frontend.md`](docs/pitfalls-frontend.md) | Stale closures after async, optimistic placement + syncSnapshot conflict, drag double-movement, store vs prop reads, **vertical fader `direction: reverse` invalid** |
| [`docs/testing-mcp.md`](docs/testing-mcp.md) | GTest suite, TransportLoopback test seam, MCP server architecture, MCP tool safety, file browser audio preview |
| [`docs/valuetree-listener-contract.md`](docs/valuetree-listener-contract.md) | ValueTree listener registration contract, orphan prevention, ReadModel alternative, audit checklist, **delta-sync cannot compute derived state** |

## Lessons learned

These cost real debugging time — read before touching the relevant area:

1. **Beats vs seconds is the #1 data-convention bug source.** Frontend speaks
   beats; the clip ValueTree (`startTime`/`duration`) and the processors speak
   seconds. Every boundary-crossing command must convert. See
   `docs/architecture.md` → "Time-unit convention".
2. **`ValueTree::setProperty` is a silent no-op when the value is unchanged.**
   Any command relying on the listener side-effect (transport
   rewind/stop/play/seek) must drive the manager directly or nudge the value.
   See `docs/pitfalls-juce.md`.
3. **`processBlock` must early-out when the transport is stopped**, or clips at
   the current position replay the same block every callback (audible buzz).
   See `docs/realtime-safety.md`.
4. **The delta-sync path can't compute derived state** (`effectiveMuted`/
   `effectiveSoloed`); mute/solo changes escalate to fullSync. See
   `docs/valuetree-listener-contract.md` §6.
5. **`projectEndSample` (auto-stop) goes stale on SPSC timing edits** because
   those don't rebuild the graph; it's recomputed in `processBlock` when a
   timing param changes. See `docs/realtime-safety.md`.
6. **`rebuildRoutingGraph()` is O(project) per call.** Clip add/remove is
   coalesced into one rebuild per message-loop tick (`AsyncUpdater`), but a
   single rebuild still tears down and re-instantiates every clip/plugin — at
   extreme bursts (128+ clips) it can stall ~30s. Incremental routing is the
   remaining follow-up.

## Build (summary)

- Configure/build: `cmake --build build --config Debug`
- Outputs: `build/Debug/HDAW.exe`, `build/Debug/HDAW_headless.exe`, `build/Debug/hdaw_tests.exe`
- Do NOT run `build/Release/HDAW.exe` — stale binary, contains none of the fixes.
- **Two launch modes:** Default (browser), Headless (Electron).
- **Frontend build:** `cd frontend && npm run build`, then rebuild the C++ project.
- See [`docs/architecture.md`](docs/architecture.md) for full build details.

### How frontend changes reach the running app (the stale-frontend trap)

The React frontend is delivered three ways, and **a plain `cmake --build`
updates NONE of them**. If a frontend fix "doesn't take effect after
rebuilding," this is almost certainly why:

| Run mode | Binary | Frontend source | To pick up frontend changes |
|----------|--------|-----------------|------------------------------|
| **Packaged Electron** | `frontend/release/win-unpacked/HDAW.exe` | Frozen in `resources/app.asar` | **Repackage:** `frontend\build.bat` (or `npm run build && npm run package:dir`). Ctrl+Shift+R does nothing here. |
| **Browser (standalone exe)** | `build/Debug/HDAW.exe` | Embedded via `frontend.qrc` | `frontend\build.bat` forces a clean C++ rebuild when `dist/` is newer (AUTORCC under the VS generator does NOT treat changed `dist/` as a rebuild trigger). |
| **Vite dev server** | `npm run dev` (+ engine for WS on 8766) | Live from `frontend/src` | Hard-refresh the browser (Ctrl+Shift+R). No build needed. |

**The packaged Electron app is the one users run.** Its frontend is baked into
`app.asar` at packaging time — editing source, rebuilding `dist/`, or
refreshing the window has zero effect until you repackage. `frontend\build.bat`
rebuilds the SPA, the C++ engine, runs the tests, and repackages Electron in one
command. Both build scripts detect an obsolete `app.asar` and fail/warn loudly,
so you can't silently iterate against a stale `.asar`.

## Testing

- **C++ engine tests (gtest):** `build/Debug/hdaw_tests.exe`
  - Filter: `--gtest_filter=SuiteName.*`
  - 250+ cases across ~35 suites: MCP tools/server, transport, tracks, clips,
    notes, FX, automation, undo, save/load, phrase generation, slicing, merge,
    ghost clips, stretch, markers, error conditions, batch ops.
  - **Engine change test discipline:** when modifying the C++ core, RPC surface,
    or JUCE interfaces, assess test impact before finishing: (1) identify gtest
    suites that exercise the changed code (RPC handlers → MCP tool tests,
    ValueTree mutations → track/clip/note tests, audio-thread logic → transport
    tests) and update them if signatures/return shapes/behavior changed; (2) if
    the change adds a new RPC method, command, or JUCE-facing interface with no
    coverage, add a gtest — the suite is the contract the frontend and MCP
    server rely on; (3) run `build/Debug/hdaw_tests.exe` to confirm no
    regression. An engine change with no test consideration is incomplete.
- **Frontend unit tests (Vitest):** `cd frontend && npm test`
  - ~177 tests: Zustand stores (transport, ui, project, notify, meter, browser),
    hooks (useTimelineDrag), utils (rowLayout, theme, grooveUtils), and
    components (StatusBar, Toaster, BottomTabs, MidiFxChain, WaveformCanvas,
    MidiThumbnailCanvas, StepSequencer, TimelineContextMenu, MixerStrip,
    TrackHeaders, Icons).
  - Watch: `npm run test:watch` · Coverage: `npm run test:coverage`
- **Frontend E2E tests (Playwright):** `cd frontend && npm run test:e2e`
  - ~197 tests in `e2e/*.spec.ts`. `app.spec.ts` = render smoke; the rest are
    user-journey regressions that drive the real app (click/drag/keyboard) and
    assert on DOM/canvas/snapshot state — the layer that catches the recurring
    interaction bugs unit tests miss (drag stale-closures, rubber-band
    hit-testing, waveform display, selection→editor opening, context menus).
  - The `webServer` in `playwright.config.ts` auto-starts the engine
    (`build\Debug\HDAW.exe`, with `HDAW_NO_BROWSER=1`) plus the Vite dev server
    (port 5173); tests run against the **live** frontend, so frontend changes
    are picked up with no rebuild/repackage. Requires a current
    `build/Debug/HDAW.exe` and Playwright browsers (`npx playwright install chromium`).
  - `workers: 1` — the engine is a singleton serving one project, so tests run
    serially; each calls `startApp()` (clicks "New Project") for a clean state.
  - Test seams: `window.rpc` for RPC setup, `data-clip-id` on `.tl-clip` for
    targeting clips, `HDAW_NO_BROWSER`. Shared helpers in `e2e/helpers.ts`
    (`startApp`, `rpcCall`, `addMidiClip`/`addAudioClip`, `dragClip`, `writeSineWav`).
  - Select by **`title`/role, not styling class**, for transport buttons — the
    icon restyle broke class-based selectors.
  - **Regression wall:** every UI bug fix should ship with an E2E test that
    reproduces it. The pitfalls in `docs/pitfalls-frontend.md` are a backlog of
    tests to write.
  - **UI change test discipline:** when creating/modifying UI, assess test
    impact before finishing: (1) identify Vitest component/store tests and
    Playwright E2E tests that exercise the changed element and update them if
    selectors/behavior/expectations changed; (2) if the change adds new
    user-facing behavior (interaction, panel, shortcut), add coverage — a
    Vitest unit/component test for isolated logic and/or a Playwright E2E test
    for the journey; (3) run the affected suites (`npm test`, `npm run test:e2e`).
    A UI change with no test consideration is incomplete.
  - Interactive UI: `npm run test:e2e:ui`

## Version Management

Version numbers are stored in **two places** and must be kept in sync manually:
- `CMakeLists.txt` → `project(HDAW VERSION 0.13.1 ...)` — **canonical** for C++.
- `frontend/package.json` → `"version": "0.13.1"` — **canonical** for the frontend.

See [docs/architecture.md](docs/architecture.md) for full details.

## Code Style

See [docs/architecture.md](docs/architecture.md) for code style conventions.

## Frontend Pitfalls

Recurring, non-obvious bugs that have cost real debugging time. Full detail in
[`docs/pitfalls-frontend.md`](docs/pitfalls-frontend.md).

1. **Stale `clips` closure after async mutations.** After an async op the store
   is newer than the `clips` prop. Look up clips from
   `useProjectStore.getState().snapshot?.clips`, never the closure prop.
2. **Optimistic placement + `syncSnapshot` conflict.** `syncSnapshot` replaces
   the whole snapshot; if the RPC didn't actually apply, the optimistic update
   is lost. Verify the RPC path runs before syncing.
3. **Optimistic placement during continuous drag double-moves.** Either skip
   optimistic placement, update it on every mousemove, or place only on mouseup.
4. **`dragSelectedIdsRef` changes identity during async ops.** After a
   ctrl-drag duplicate it holds new ids while the closure's `clips` are stale —
   read from the store.
5. **Window-level listeners still close over stale data.** The ref indirection
   avoids stale *handlers* but not stale *closures*; read from the store inside
   the callback.
6. **Clip placement must go through `moveClipWithOverlap`.** Any function that
   places a clip (duplicate, paste, import) must call it after
   `clipList.addChild` to handle overlaps.

## Frontend ↔ engine state synchronization

The timeline reads a `ProjectSnapshot` (tracks + clips) from the engine. Edits
propagate via **incremental deltas**, not whole-snapshot re-fetches:

- `FrontendTreeWatcher` (root `ValueTree` listener) feeds every JUCE change into
  a `TreeDeltaAccumulator` (`src/frontend/TreeDeltaAccumulator.{h,cpp}`), which
  coalesces a burst (16 ms debounce) into a minimal delta broadcast on
  `notify.treeChanged` as
  `{ fullSync: false, clipsUpserted, clipsRemoved, tracksUpserted }`.
- The frontend `applyDelta` (`frontend/src/store/projectStore.ts`) patches the
  snapshot in place (upsert/remove clips by id, tracks by index), keeping object
  references stable for unchanged entities.
- **Fallback:** any change that can't be expressed as a clip/track delta — track
  add/remove, markers, tempo, FX, automation, sub-clip detail (notes/CC/gain-env/
  automation points), reorder, project load, undo/redo — sets `fullSync: true`
  and the frontend re-fetches `read.snapshot`. Automation lane refresh lives on
  this branch, so recording still updates.
- **Kill-switch:** `HDAW_FORCE_FULL_SYNC=1` (read once at watcher startup) routes
  every snapshot-relevant change to a fullSync re-fetch. Default off.
- **Key invariant (tested in `frontend_server_test.cpp`):** a clip add/remove
  reaches the root listener as an incremental delta, *not* a fullSync. This
  relies on JUCE propagating `valueTreeChildAdded/Removed/PropertyChanged` up to
  the root listener while `valueTreeParentChanged` only propagates down.
- **Pending placeholders:** Duplicate / Add-Clip (which mint ids on the backend)
  show a translucent placeholder (`addPendingClip`, negative temp id from
  `nextTempId()`), then reconcile when the RPC returns the real id
  (`resolvePending`) and the delta delivers the real clip. A 1500 ms sweep
  removes unresolved placeholders.

When adding a new edit operation: if it only changes clip/track properties or
adds/removes clips, it deltas automatically. If it restructures the tree or
touches other entity types, it fullSyncs (correct, just slower) — no extra
wiring needed.
