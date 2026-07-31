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
| [`docs/realtime-safety.md`](docs/realtime-safety.md) | Audio-thread safety rules, hardening lessons, diagnostic pattern, plugin process isolation (default ON), **transport-stopped early-out (audio buzz), auto-stop / projectEndSample staleness, latency evaluation, quality/fidelity evaluation** |
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
   remaining follow-up. For batched multi-clip ops that need slicing (ripple
   delete, future insert-silence/duplicate-region), call the model-level
   `ProjectModel::sliceClipAtTimes` directly — it does NOT rebuild — and do one
   `rebuildRoutingGraph()` at the end; the command-layer `sliceClipAtTimes`
   wrapper rebuilds per call, which defeats the coalescing.
7. **Every audio engine change affects latency — evaluate it explicitly.**
   Any modification to `processBlock`, plugin graph topology, bus layout,
   buffer sizes, or signal-path length changes the overall input-to-output
   latency. Before merging an engine change, measure the reported latency
   (`getTotalLatency()`) before and after, and verify that plugin delay
   compensation still aligns all tracks. A regression here causes audible
   phase issues and MIDI timing drift. See `docs/realtime-safety.md`.
8. **Every audio engine change affects quality and fidelity — evaluate it explicitly.**
   Modifications to signal processing (sample rate conversion, timestretch,
   mixing, FX chain, plugin hosting, buffer handling) can introduce artifacts:
   clicks, pops, DC offset, aliasing, clipping, or degraded dynamic range.
   Before merging, A/B test with critical listening on reference material and
   verify no unintended signal degradation. Check for denormalized floats,
   integer overflow in accumulators, and incorrect gain staging. See
   `docs/realtime-safety.md`.
9. **The default project isn't empty — snapshot tests must scope to a track
   they control.** `createDefaultProject()` ships track 1 ("Synth") with
   `Melody`/`Chords` clips; only track 0 ("Track 1") starts empty. Any
   project-wide op (ripple delete, region ops) processes those too, so
   `getReadModel().snapshot().clips` contains them — a count assertion that
   assumes "only the clips I added" will be off. Scope assertions to a track
   you control (filter by `trackIndex`); `merge_clips_test` dodges this by
   asserting on specific ids, `clip_slicing_test` by reading the raw
   `ValueTree`. Corollary: `ProjectModel::sliceClipAtTimes` **reassigns ids**
   to the pieces (the original clip is removed), so across a slice, track clips
   by position/count, not by the original id.

## Performance rules: batch RPCs, walk the tree incrementally

Standing rules for any code that mutates or reads the project. These are what
keep the arrange view smooth and avoid the "black screen" cliff (lesson 6).

1. **Consolidate RPC calls — one batch, not N loops.** A set of related
   mutations must be a single batch RPC (`addClips`, `removeClips`, `moveClips`,
   `duplicateClips`, `paintClips`, `mergeClips`) or one
   `beginTransaction`/`endTransaction` block — never N separate `await rpc.call`
   in a loop. This is the most efficient path, and the win is *not* just fewer
   round-trips: every engine mutation fires the root `ValueTree` listeners
   synchronously, so one batched call lands in a single message-loop tick and the
   16 ms `TreeDeltaAccumulator` + `AsyncUpdater` coalesce it into **one** delta
   broadcast and **one** graph rebuild (and one atomic undo unit). N separate
   calls are N round-trips that can span ticks → N rebuilds, N deltas.
2. **Prefer incremental deltas over full re-serialization.** Express a change as
   a clip/track delta whenever possible so `notify.treeChanged` carries a minimal
   payload and the frontend `applyDelta` patches the snapshot in place (stable
   object references → minimal React re-render). Reserve `fullSync` (whole
   `read.snapshot` re-fetch) for changes that restructure the tree or touch
   non-clip/track entities. Derived state (`effectiveMuted`/`effectiveSoloed`)
   can't be deltaed (lesson 4) — that's the exception, not the default.
3. **Don't re-walk the whole `ValueTree` to touch one node.** Use indexed access
   / `getChildWithProperty` and held references instead of full-tree scans per
   mutation. `rebuildRoutingGraph()` and `ReadModel` snapshot building are
   O(project) and are the hot path to keep incremental (lesson 6 is the remaining
   follow-up).

## MCP feature parity

The MCP server is a first-class client of the engine, not a secondary surface.
**Any feature available to the user through the UI must also be available
through the MCP** — if a human can do it from the frontend, an MCP tool must be
able to do it too. When you add a user-facing capability (a command, edit op,
transport action, or project mutation), expose it as an MCP tool in the same
change; when you audit a feature gap, the MCP side counts as unfinished until
it's reachable. The UI and MCP share the same RPC/command layer, so this is
usually wiring a tool onto an existing command rather than new engine work.

## Generative composition, randomization & modulation

HDAW is a *generative* DAW, not just a recorder. Assisted creation is a core
product pillar and should be reached for wherever it fits:

- **Generative composition** lives in `PhraseGenerator` (`src/engine/PhraseGenerator.h`):
  scale-aware phrase styles (Standard, Arpeggio, BassLine, ChordStab, Pad, Lead,
  RandomWalk, Buildup), single-chord and chord-progression generation, scale
  modes, chord types/voicings/inversions. Exposed over RPC as
  `composition.generatePhrase/generateChord/generateProgression` (and matching
  MCP tools), surfaced in the UI by `PhraseGeneratorDialog` (TransportBar 🎵 /
  Ctrl+Shift+G).
- **Randomization / humanization** — note timing, velocity, and pitch
  humanize in the piano roll (`NoteGrid`) and clip editor (`ClipEditor`).
- **Modulation** — a per-track LFO system (`ModulationManager` /
  `LFOModulationSource`, track `MODULATION_LIST` ValueTree, `rebuildModulation`)
  that modulates parameters in the audio engine.

**Guideline: when adding a feature, ask whether the generative/random/modulation
toolkit applies.** New note or parameter editing should offer humanize/randomize;
new content types should consider a generative path; new modulatable parameters
should be wired as modulation targets. Prefer extending these shared utilities
over one-off randomness, so behavior (and its MCP/RPC surface) stays consistent.

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
    ripple delete, ghost clips, stretch, markers, error conditions, batch ops.
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
  - **Clip-position assertions must poll.** After an RPC that shifts clips
    (insert silence, duplicate region, move), the tree-change notification is
    debounced (~16 ms) so the DOM doesn't update immediately. Assert positions
    with Playwright's `expect.toPass()` polling, not a one-shot read:
    `await expect(async () => { expect(await clipLeft(...)).toBeGreaterThan(...); }).toPass({ timeout: 10000 });`
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

## UI design & aesthetic guidelines

HDAW's interface idiom is **Bitwig's Arranger + Ableton's fixed-tile flow**.
The north star is **spatial stability**: the screen is a fixed mosaic of
regions that never move, so the user's spatial memory stays valid and nothing
breaks their flow. Depth comes from **drilling down in place**, never from
popping windows that reflow the layout. This is Ableton's "flow" — simple up
front, a small amount of drilling down for much more control, and a screen
that never interrupts the user's train of thought. When in doubt, ask: *"would
this feel at home in Bitwig or Ableton?"* If it feels like a Cubase floating
window, it doesn't belong here.

### Core principles

1. **Spatial stability is sacred.** A fixed set of screen regions, always in
   the same place. Regions are shown/hidden or resized — never rearranged,
   never overlapped by floating windows. The user's hands and eyes build a map
   of the screen; don't invalidate it.
2. **Progressive disclosure — simple up front, depth a click away.** The
   surface shows the essentials; detail lives one drill-down away, *in place*.
   Never stack windows to reach more control.
3. **One primary focus area at a time.** Switching views swaps content inside a
   stable frame; it doesn't change the frame.
4. **Conform to muscle memory.** Timeline/track/clip/mixer conventions follow
   the established DAW idiom (Bitwig/Cubase lineage). Deviation is a friction
   cost — spend it only where HDAW is deliberately better.

### The fixed layout (HDAW's regions)

The app shell (`App.tsx` / `App.css`) is a CSS grid of fixed regions. New UI
must live inside this mosaic — do not add floating/absolute-positioned panels
to the core workflow.

| Region | Grid area | Role | Model |
|--------|-----------|------|-------|
| Transport | `transport` (top, 48px) | Play/stop/record, position, tempo, global toggles | Always present |
| Track headers | `headers` (left, 220px) | Track list, mute/solo, selection | Always present |
| Timeline | `timeline` (center) | Arranger — clips, ruler, lanes | Always present |
| File browser | `browser` (right) | Media / Audio Pool, drag source | Toggled, docked |
| Clip editor strip | `clipedit` | In-place detail for the selected clip | Docked strip |
| Bottom panel | `bottom` (resizable) | **The detail view** — tabs swap content in a stable frame | Always present |
| Status bar | `status` (bottom, 24px) | Hints, readouts | Always present |

The **bottom panel is HDAW's Ableton Detail View / Bitwig device-mixer panel**:
Mixer, Piano Roll, Automation, FX Chain, MIDI FX, Audio Editor, Modulation, and
Step Seq are all *tabs in the same stable frame*. Adding a new editor or
inspector = adding a tab here, not a new window.

**Showing/hiding a region must not reflow the rest of the layout
unpredictably.** Prefer reserved/collapsible space (a region keeps its slot and
collapses to a handle) over mount/unmount that shifts everything.

### Windows & dialogs

- **No floating windows or pop-outs in the core workflow** (browse, edit, mix,
  arrange). Everything docks into the grid.
- **Modal dialogs only for genuinely modal, infrequent, or destructive acts**:
  close-with-unsaved-changes confirm, Preferences, Plugin Manager, Import,
  project startup. If a task is part of the flow of making music, it is not a
  dialog.
- Never drive a repeated workflow through a dialog.

### Audio Pool (the one Cubase convention we keep)

Project media follows **Cubase's Audio Pool model** — a centralized pool of all
audio in the project, independent of the arrangement:

- Every imported/recorded audio file appears **once**, with name, source path,
  length, and **usage/reference count** — regardless of how many clips use it.
- Clips **reference** pool items; dragging from the pool to the timeline mints
  a clip. Deleting a clip does not delete the pool item.
- The pool surfaces **unused items** for cleanup and is the single place to see
  what media the project actually carries.
- This lives in / alongside the file-browser region as the project-media side.

### Keyboard-first

- Every repeated action has a shortcut; hands rarely leave the keys.
- Transport follows DAW convention — **R = record**, return-to-zero, loop
  toggle; **Space = play/stop is the target idiom** (currently Shift+Space =
  stop, Home = rewind, Ctrl+L = loop).
- Editing follows **CUA**: Ctrl+Z / Ctrl+Shift+Z / Ctrl+Y undo/redo, plus
  save/copy/paste/duplicate (file shortcuts live in `FileMenu.tsx`).
- Shortcuts are discoverable (status-bar hints / tooltips) and don't collide
  with the host browser or OS.

### Visual language

- **Dark, calm, neutral base** (`--bg-window`/`--bg-panel` charcoal). The
  canvas recedes so content (clips, waveforms, meters) reads.
- **Color = meaning, never decoration.** Track/clip identity, state, and signal:

  | Token | Meaning |
  |-------|---------|
  | `--accent` (amber `#d97706`) | focus / active / selection / interaction |
  | `--mute-color` (amber) / `--solo-color` (green) | mutually-exclusive track states — kept far apart in hue on purpose |
  | `--vu-green/yellow/red` | signal level |
  | `--danger/--warning/--success/--info` | status semantics |

- **Hierarchy from weight, size, and spacing — not ornament.** Dense but
  organized; no gratuitous borders, shadows, or gradients.
- **Tabular numerals** (`font-variant-numeric: tabular-nums`) for any
  time/position/level readout so digits don't jitter. The transport position
  display is the one place a distinctive display treatment is welcome.
- **13px system-ui base** for density; step up size/weight deliberately for
  emphasis.

### Motion & feedback

- The UI is **alive but never theatrical**: every control gives immediate,
  perceptible feedback (hover, pressed, focus states).
- **Meters are living elements** — they move with the audio and are always on.
- Micro-transitions (~100–150 ms) for hover/resize/reveal; nothing slower that
  would lag the feel of an instrument.
- Motion signals state changes, it doesn't entertain. Respect
  `prefers-reduced-motion`.

### Known deviation to reconcile

- `.clip-editor-container` mounts/unmounts with selection, reflowing the
  timeline. Long-term it should collapse into reserved space (or merge into the
  bottom-panel tabs) so showing it never shifts the arrange view.

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
