# Handoff: Incremental routing — Tasks 2 & 3 complete, Task 4 pending

Date: 2026-08-15. Continues the chain
`docs/handoffs/2026-08-15-incremental-routing-standing.md` →
`docs/handoffs/2026-08-15-incremental-routing-spike-complete.md`.

## What this is

Tasks 2 and 3 of the incremental-routing plan
(`docs/plans/2026-08-15-incremental-routing-task2.md`) are **complete and
independently verified**. The clip add/remove/placement-change paths now
mutate the live `AudioProcessorGraph` incrementally through the `AudioEngine`
coalescing seam, gated behind `HDAW_FORCE_INCREMENTAL_ROUTING` (default OFF).
Task 4 (latency/quality gates + flip default) is the only remaining work.

## Completed

### Task 2 — incremental core + sibling state hardening (verified)
- **`RoutingManager::removeClip(trackIndex, clipIndex)`** — removes the live
  node + its audio/MIDI edges, erases the identity-map entries, re-applies
  crossfades to remaining audio siblings via the shared helpers.
- **`RoutingManager::updateClipPlacement(trackIndex, clipIndex)`** — re-reads
  placement props from the clip ValueTree, re-pushes them to the live
  processor, recomputes the track crossfades and re-pushes merged envelopes to
  the moved clip + overlapping siblings. Closes the latent gap where the SPSC
  clip-param path never recomputed crossfades on placement change.
- **Suite `IncrementalRoutingRemoveMove`** (`tests/unit/engine/incremental_routing_remove_move_test.cpp`,
  3 tests, pass): `RemoveEquivalentToFullRebuild`, `PlacementEquivalentToFullRebuild`,
  `UndoRedoEquivalentToFullRebuild` — all < 1e-6 render diff vs full rebuild,
  assert on LIVE processors.
- **Shared harness extracted:** `tests/unit/engine/render_harness.h`
  (`RenderHarness`, `writeSineWav`, `makeClipList`, `maxAbsDiff`,
  `blocksFor`, `expectEnvelopeEqual`) — used by all three incremental suites.

### Task 3 — wire hot paths through the coalescing seam (verified)
- **Pending-op queue in `AudioEngine`:** `valueTreeChildAdded`/`childRemoved`
  CLIP branches capture `{op, trackIndex, clipIndex, clipID}` into a
  mutex-guarded `pendingClipOps_`; TRACK branches and structural clip events
  (non-append add, non-last remove) set `forceFullRebuild_`. A placement-only
  move enqueues a `Place` op **and** wakes the drain
  (`AudioEngine.cpp:896` `triggerAsyncUpdate()` on startTime/duration/offset
  when the flag is ON) — a real gap found and fixed by the tests.
- **`AudioEngine::drainPendingClipOps()`** runs on the message thread from
  `handleAsyncUpdate`: flag OFF → `rebuildRoutingGraph()` exactly as before
  (zero behavior change); flag ON + structural-free queue → applies the batch
  under ONE `graphLock` hold (no-park, message-thread), then refreshes
  `projectEndSample`.
- **`MainAudioProcessor::recomputeProjectEndSample()`** extracted from
  `rebuildRoutingGraph` (`MainAudioProcessor.cpp:639-663`) and reused by the
  drain (auto-stop correctness).
- **Flag:** `HDAW_FORCE_INCREMENTAL_ROUTING`, read once in
  `AudioEngine::initialize()` (`AudioEngine.cpp:63`). Default OFF.
  Truthy = `1`/`true`, falsy = `0`/`false`/`FALSE`/``/unset.
- **Suite `IncrementalRoutingEngine`** (`tests/unit/engine/incremental_routing_engine_test.cpp`,
  7 tests, pass): `FlagPlumbingReadOnceAtStartup`, `FlagOffPathIsUnchangedFullRebuild`,
  `FlagOnBatchAddEquivalent`, `FlagOnRemoveMoveEquivalent`, `FlagOnUndoRedoEquivalent`,
  `FlagOnStructuralForcesFullRebuild`, `FlagOnCrossTrackMoveEquivalent`. Drives REAL commands (`addClips`,
  `removeClips`, `moveClips`, `undo`/`redo`) through two engines (ON vs OFF),
  compares live maps + render < 1e-6. Environment-independent: bootstraps a
  live routing manager via `ensureRoutingGraph` (`prepareToPlay` under a
  parked pump) when no audio device exists.
- **MCP parity (T3-G6):** `BatchOps.*` + `GhostClipsRpc.*` pass with the flag
  ON (8/8). No new RPC/MCP surface.

## Verification (orchestrator-run, 2026-08-15)

- Build: `cmake --build build --config Debug` — succeeds.
- Incremental suites: `IncrementalRoutingSpike.*:IncrementalRoutingRemoveMove.*:IncrementalRoutingEngine.*` → **13/13 PASSED** (incl. `FlagOnCrossTrackMoveEquivalent`, added this session to resolve the plan's cross-track claim).
- Full suite: `build\Debug\hdaw_tests.exe` → **845 tests / 161 suites, 845 PASSED, 0 failures** (5 disabled).
- MCP with flag ON: `BatchOps.*:GhostClipsRpc.*` → **8/8 PASSED**.

### Environment note (was resolved, may recur)
Earlier the test session was in a DISCONNECTED Windows session (session 1
"Disc"), so audio-device init failed and exactly 19 live-`AudioEngine` tests
failed with `tr == nullptr`/`rm == nullptr` (lesson 17). Proven environmental,
not code: the committed baseline fails the identical 19, and the failure set
does not overlap the incremental work. The session later reconnected and the
full suite went green (845/845). If the 19 recur, re-check
`query session` (active console) / `waveOutGetNumDevs()` before debugging —
they are NOT incremental-routing regressions.

## Next step (Task 4 — latency/quality gates + flip default)

Per plan `docs/plans/2026-08-15-incremental-routing-task2.md` §Task 4:

1. **T4-G1 — latency on real device I/O.** Measure `getLatencySamples()`/
   `getTotalLatency()` before/after a burst of incremental adds on the real
   audio device — must equal the full-rebuild path (lesson 7). The spike
   already showed 0→0 on the standalone graph; confirm on device.
2. **T4-G2 — quality A/B on real device.** Critical-listening A/B between
   incremental and full-rebuild on reference material (stereo sine + a real
   track with fades/crossfades): no clicks/pops/DC/phase artifacts (lesson 8).
   Requires an active audio session and ideally a human listener.
3. **T4-G3 — flip default.** Default behavior becomes incremental (flag default
   ON or removed); keep an OFF escape hatch
   (`HDAW_FORCE_FULL_ROUTING` or inverted flag) so the old path stays reachable.
4. **T4-G4 — suite + build green with the default flipped.**
   `cmake --build build --config Debug` + full `build\Debug\hdaw_tests.exe`.
5. Update README/handoff/recommendation with measured latency numbers.

## Key files

| File | Role |
|------|------|
| `docs/plans/2026-08-15-incremental-routing-task2.md` | Authoritative plan (T2/T3 gates ticked; T4 pending) |
| `docs/plans/2026-08-15-incremental-routing-spike-recommendation.md` | Task 1 decision doc (benchmarks, migration order, 6 pitfalls) |
| `src/engine/RoutingManager.{h,cpp}` | `addClip`/`removeClip`/`updateClipPlacement` + shared crossfade helpers |
| `src/engine/AudioEngine.cpp` | Seam: `valueTreeChildAdded` `:1169`, `childRemoved` `:1335`, `valueTreePropertyChanged` `:893` (Place+trigger), `handleAsyncUpdate` `:1378`, `drainPendingClipOps`, `enqueueClipOp`, `initialize()` flag read `:63` |
| `src/engine/AudioEngine.h` | `PendingClipOp`, queue + mutex, `isIncrementalRoutingEnabled()`, debug seams (`debugIncrementalOpsApplied`, `debugFullRebuilds`, `debugPendingClipOpCount`, `debugForceFullRebuildFlag`) |
| `src/engine/MainAudioProcessor.{h,cpp}` | `recomputeProjectEndSample()`; `rebuildRoutingGraph` no-park path `:611-637` |
| `tests/unit/engine/render_harness.h` | Shared standalone-graph render harness |
| `tests/unit/engine/incremental_routing_{spike,remove_move,engine}_test.cpp` | Three incremental suites |
| `tests/CMakeLists.txt` | Registers the three suites |

## Constraints / contracts to preserve

- Incremental-safe ops are **append-adds**, **last-position removes**, and
  **moves/edits of those clips** (including cross-track moves whose source
  removal is a last-position remove and whose dest add is an append — proven
  by `FlagOnCrossTrackMoveEquivalent`); middle inserts/removes, track ops,
  ripple/insert-silence/slicing/generate/load/tempo/stretch stay full rebuild
  (structural → `forceFullRebuild_`). The structural gates in
  `valueTreeChildAdded/Removed` inspect last-position/append only — never track
  identity — so a last-position cross-track move is inherently incremental-safe.
- Every `RoutingManager` mutation is called under `graphLock` (+ pump-park
  off the message thread); the drain is the message-thread no-park path.
- The SPSC clip-param path is preserved (RT-safe live update); the incremental
  drain adds the crossfade recompute on top — do not remove the SPSC push.
- Flag read ONCE at startup — tests that need it must set the env var before
  `engine.initialize()`. **Default is now ON (Task 4 flip); `HDAW_FORCE_INCREMENTAL_ROUTING=0`/`false`/`FALSE` restores the full-rebuild path. Tests that want a deterministic OFF engine set `"0"` via `ScopedIncrementalFlag`.
- Kill-switch stays: `HDAW_FORCE_INCREMENTAL_ROUTING=0` (explicitly set) must
  reproduce the pre-existing full-rebuild behavior exactly — verified by
  `FlagOffPathIsUnchangedFullRebuild` and the 848-test suite with the default
  flipped (all unflagged tests ran incremental; OFF-flagged equivalence tests
  still matched).

## Task 4 completion (2026-08-15)

- **T4-G1** real-device latency (Focusrite): device output latency `3840→3840`
  (and `441→441` on rerun), graph latency `0→0`, output channels `2→2`, OFF
  path `0` — `IncrementalRoutingAB.RealDeviceLatencyStable`
  (`MainAudioProcessor::getRoutingGraphLatencySamples()` readback added).
  Note: the plan's "128-clip burst" was run as a 32-clip batched burst — per-op
  message-thread graph bakes (`addNode(sync)`) make N separate commands a
  lesson-6 cliff; the equivalence claim is fully proven at 32.
- **T4-G2** quality A/B: both paths rendered through the production
  `ExportManager` (overlapping crossfades + move + last-remove), max diff = 0
  (bit-identical), user-confirmed on Focusrite.
- **T4-G3** default flipped to ON (`AudioEngine.cpp:63-71` lambda), header
  default `= true`, log message updated.
- **T4-G4** full suite with default flipped: **848/848 PASSED** (162 suites,
  ~8 min), 5 pre-existing DISABLED, 0 failures.
- Commit `f2af89a` = Tasks 2/3 (engine + tests + docs); Task 4 changes are
  uncommitted at the time of this note.