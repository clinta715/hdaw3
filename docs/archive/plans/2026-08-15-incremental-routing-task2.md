# Incremental Routing — Task 2+ Implementation Plan

> Builds on the Task 1 spike (`docs/plans/2026-08-15-incremental-routing-spike-task1.md`)
> and its decision document (`docs/plans/2026-08-15-incremental-routing-spike-recommendation.md`).
> Approach C is confirmed and measured (~8× faster clip-add on the graphLock-hold
> surface). This plan implements the recommendation's migration order: **Task 2**
> (incremental core + sibling state hardening), **Task 3** (wire the hot paths
> through the AudioEngine coalescing seam), **Task 4** (latency/quality gates +
> flip default). Each task has independent success gates; the feature stays
> behind `HDAW_FORCE_INCREMENTAL_ROUTING` (default OFF) until Task 4 flips it.

> **Status (2026-08-15):** Tasks 2-4 complete. Default is now incremental **ON**;
> `HDAW_FORCE_INCREMENTAL_ROUTING=0` is the full-rebuild escape hatch. All gates
> green: 13 equivalence tests, real-device latency 0-shift, bit-identical A/B
> render, 848/848 suite with default flipped.

## Goal

Give HDAW an incremental clip-mutation path on the live `AudioProcessorGraph`
(clip add / remove / placement change) that is behaviorally identical to
`rebuildRoutingGraph()` but avoids the full teardown, so batch clip edits stop
rebuilding the whole graph per command — wired through the existing
`AudioEngine` coalescing seam, gated behind a read-once env flag, and only
flipped on after latency/quality equivalence is proven.

## Success Gates (all must pass to declare done)

### Task 2 — incremental core + sibling state hardening
- [x] T2-G1 **`removeClip` incremental path.** `RoutingManager::removeClip(trackIndex, clipIndex)` removes the live node + connections, drops the identity-map entries, and re-applies crossfades to the remaining siblings via the shared `computeTrackCrossfades`/`computeMergedEnvelopeForClip` helpers. Render-equivalence test: full rebuild of layout `L` vs. full rebuild of `L` minus one clip then incremental remove of that clip must match < 1e-6, with equal node/connection counts.
- [x] T2-G2 **Placement-change (move) incremental path.** `RoutingManager::updateClipPlacement(trackIndex, clipIndex)` re-reads the clip's `startTime`/`duration`/`offset`/`gain`/fades from the ValueTree, re-pushes them to the live processor, and re-applies crossfades to the moved clip + overlapping siblings. Equivalence test mirrors T2-G1 (move a clip into a new overlap; render must match a full rebuild < 1e-6).
- [x] T2-G3 **Undo/redo flows through the incremental paths.** Undo of a clip-add removes incrementally; undo of a remove re-adds incrementally (append-restore only). Structural undo (middle insert, reorder, track ops) falls back to full rebuild. Test: add → undo → redo → renders all equivalent < 1e-6 to a full rebuild, and the live `getAudioClipSources()` map matches.
- [x] T2-G4 **Live-processor assertions, not ReadModel.** All new equivalence tests assert on live `RoutingManager` processors (`getAudioClipSources()`, `getMidiClipSources()`) — sibling envelope bit-identical when untouched, matching a full-rebuild reference when touched (Gate 1/10).
- [x] T2-G5 **Mutation discipline.** Every new `RoutingManager` mutation is called under `graphLock` + (when off the message thread) pump-park, exactly mirroring `MainAudioProcessor::rebuildRoutingGraph` (`MainAudioProcessor.cpp:611-637`). No mutation without the lock (Gate 12/13).
- [x] T2-G6 **Full suite green.** `build\Debug\hdaw_tests.exe` → 800+ PASSED including `IncrementalRoutingSpike.*`, `AudioGraphSurface.*`, `TrackMixerState.*`, `ClipSlicing.*`, `MergeClips.*`, `ClipStreamingE2E.*`.

### Task 3 — wire the hot paths through the coalescing seam
- [x] T3-G1 **Pending-op queue in AudioEngine.** `valueTreeChildAdded`/`valueTreeChildRemoved` (CLIP) record `{op, trackIndex, clipIndex, clipID}` into a mutex-guarded pending queue instead of (only) `triggerAsyncUpdate()`; structural events (TRACK add/remove, clip not at end, any non-append) set a `forceFullRebuild` flag. Queue is drained in `handleAsyncUpdate` (`AudioEngine.cpp:1289`).
- [x] T3-G2 **Incremental drain path.** When the flag is OFF (default) `handleAsyncUpdate` calls `rebuildRoutingGraph()` exactly as today — **zero behavior change**. When ON and the queue holds only incremental-safe ops, it applies `addClip`/`removeClip`/`updateClipPlacement` under `graphLock` (message-thread no-park path, mirroring `rebuildRoutingGraph`), then recomputes `projectEndSample` (auto-stop correctness, lessons 5/15). Any structural op → full `rebuildRoutingGraph`.
- [x] T3-G3 **One bake per tick, one lock hold.** N batched clip ops coalesced into one `handleAsyncUpdate` produce one `graphLock` hold and one pump bake (matching today's coalescing; no per-clip rebuild loops — performance rule 1/lesson 6).
- [x] T3-G4 **Flag plumbing.** `HDAW_FORCE_INCREMENTAL_ROUTING` read once at engine startup (`AudioEngine::initialize()`, `AudioEngine.cpp:50`), precedent `FrontendTreeWatcher.cpp:25-29` / `PluginManager.cpp:34,68`. Default OFF.
- [x] T3-G5 **Coalescing equivalence under the real command layer.** Gtest drives real commands (`AudioEngineCommands::addClips`, `removeClips`, `moveClips`, `undo`/`redo`) with the flag ON vs OFF and asserts identical `getAudioClipSources()` maps + identical rendered output < 1e-6 after `engine.drainPendingRoutingRebuild()` (deterministic drain, `AudioEngine.cpp:1298`).
- [x] T3-G6 **MCP parity + no new surface.** The incremental path is transparent to RPC/MCP (same commands, same RPC layer) — verify `add_clips`/`remove_clips`/`move_clips` MCP tools exercise the incremental path with flag ON (no new tools needed). Frontend/ReadModel unchanged (both rebuild from ValueTree).
- [x] T3-G7 **Full suite green** (same command as T2-G6) + the `IncrementalRoutingSpike` suite still passes.

### Task 4 — latency/quality gates + flip default
- [x] T4-G1 **Latency measured on real device I/O.** `getTotalLatency()`/`getLatencySamples()` recorded before/after a burst of incremental adds on the real audio device — equal to the full-rebuild path (lessons 7). No topology latency shift (spike already showed 0→0 on the standalone graph). **Measured (Focusrite, active console): device output latency `3840→3840` (and `441→441` on rerun), graph latency `0→0`, output channels `2→2`, OFF-path graph latency `0`** — `IncrementalRoutingAB.RealDeviceLatencyStable`.
- [x] T4-G2 **Quality A/B on real device.** Critical-listening A/B between incremental and full-rebuild paths on reference material: no clicks/pops/DC/phase artifacts (lesson 8). **Rendered both paths through the production `ExportManager` pipeline (overlapping crossfaded clips + move + last-remove): max diff = 0 (bit-identical); user A/B-confirmed on Focusrite.**
- [x] T4-G3 **Default flipped.** Default behavior becomes incremental (flag default ON or removed), `HDAW_FORCE_FULL_SYNC`-style kill-switch retained to force the old path. **`HDAW_FORCE_INCREMENTAL_ROUTING` default flipped to ON (read once at startup); `=0`/`false`/`FALSE` restores the full-rebuild path.**
- [x] T4-G4 **Suite + build green with the default flipped.** Same commands as T2-G6 plus `cmake --build build --config Debug`. **848/848 PASSED (162 suites) with the default flipped; incremental suites 15/15 (incl. AB latency + A/B).**

## Dependency Map

Blast radius: the audio routing graph only. No ReadModel, frontend, or RPC-shape changes — both projections rebuild from the ValueTree, which is unaffected by *how* the graph is mutated.

- **Upstream (who mutates the tree / triggers rebuilds):**
  - Coalescing seam (Task 3 target): `AudioEngine::valueTreeChildAdded` (`AudioEngine.cpp:1035`, CLIP branch `:1129-1133`, TRACK branch `:1134-1138`), `valueTreeChildRemoved` (`:1141`, CLIP `:1244-1280`, TRACK `:1282-1286`) → `triggerAsyncUpdate()` → `handleAsyncUpdate()` (`:1289-1296`) → `MainAudioProcessor::rebuildRoutingGraph()`.
  - Direct callers of `rebuildRoutingGraph` (stay full-rebuild; do NOT touch): `AudioEngineCommands_Clips.cpp:511,574,670` (ripple/insert-silence/duplicate-region), `:767` (generateArrangement); `AudioEngineCommands_Slicing.cpp:23,81,101,126,173,244,279`; `AudioEngineCommands_Undo.cpp:53` (newProject), `:112` (loadProject), `:241` (relink single), `:288` (relink all); `AudioImport.cpp:121`, `MidiImport.cpp:144`; `McpTools_Project.cpp:1280` (load_project); `Router_Composition.cpp:84`; `Router_AudioGraph.cpp:15` (`rebuildRoutingGraph` RPC); `AudioEngine.cpp:74` (StretchCache entryReady), `:762` (tempo), `:888` (stretchMode/stretchRatio prop).
  - SPSC clip-param path (NOT a rebuild today — placement changes ride this): `AudioEngine::valueTreePropertyChanged` CLIP branch `:845-879` pushes `ParamUpdate{trackIndex, paramID, value, clipIndex}` for gain/fadeIn/fadeOut/startTime/duration/offset/looping/muted → live `ClipSourceProcessor`/`MidiClipProcessor`. **Note:** crossfades are NOT recomputed on this path today — a move that creates a new overlap stays uncrossfaded until the next rebuild. Task 2's `updateClipPlacement` closes this gap behind the flag.
- **Downstream (consumers of the mutated graph):** audio thread `MainAudioProcessor::processBlock` (`MainAudioProcessor.cpp:290-306`, `graphLock.tryEnter`), `ExportManager::renderThreadFunc` (`ExportManager.cpp:158-269`), frontend via ValueTree (unaffected), `projectEndSample` recompute (`MainAudioProcessor.cpp:639-663`).
- **God nodes in scope:** `RoutingManager` (high-degree: owns graph nodes, maps, tracks, buses, sends). All new methods live on it; mutations stay inside its existing lock contract.
- **Community boundaries crossed:** none beyond engine-internal (RoutingManager ↔ AudioEngine ↔ MainAudioProcessor). No frontend/MCP/RPC contract changes.
- **Projections affected:** Audio graph only. ReadModel/frontend snapshot rebuild from ValueTree and are identical regardless of graph mutation strategy.
- **SPSC paths touched:** clip param bridge (moves ride it today); Task 2 adds crossfade recompute on top of placement changes but writes only via the existing `setGainEnvelopePoints`/processor setters (no new SPSC surface).
- **Delta vs fullSync:** frontend delta path is unaffected (clip add/remove already deltas). Incremental graph mutation is invisible to the frontend.

## Pitfall Gates Triggered

- **Gate 1 / Gate 6 / Gate 10 (state restore on rebuild):** every new incremental mutation must leave sibling processors in the exact state a full rebuild would. Covered by T2-G4 (live-processor assertions, crossfade bit-identical when untouched).
- **Gate 2 (silent no-op paths):** the SPSC path currently skips crossfade recompute on placement change — `updateClipPlacement` must actually recompute and re-push, else the incremental path silently diverges. Equivalence tests (T2-G2, T3-G5) prove the full chain.
- **Gate 3 (audio-thread safety):** none of the new code runs on the audio thread. `updateClipPlacement` writes are identical to what `buildClipNode`/SPSC already do (setters on prepared processors under `graphLock`).
- **Gate 12 (graph mutation from non-message threads):** every mutation holds `graphLock`; off-message-thread callers also pump-park. The Task 3 drain runs on the message thread (`handleAsyncUpdate`) → no-park path, same as `rebuildRoutingGraph` (`MainAudioProcessor.cpp:611-622`). T2-G5/T3-G2 codify this.
- **Gate 13 (DSP-state writes):** crossfade envelope writes go through `ClipSourceProcessor::setGainEnvelopePoints` under the same locking the rebuild path uses (no new listener writes to DSP internals).
- **Gate 15 (stale flags/binaries):** the env flag is read once at startup; tests verify the *binary* (run `hdaw_tests.exe` after rebuild, not source). `drainPendingRoutingRebuild` gives deterministic settlement for assertions.
- **Gate 11 (message pump):** unchanged — tests/harness already run on `MessagePumpThread`; the spike harness `shutdown()` (parked teardown) is the model for any new test harness in Task 2/3.

## Anti-Pattern Scan

- ❌ No `rebuildRoutingGraph()` per-clip in a loop — Task 3 coalesces into one lock hold + one bake (T3-G3).
- ❌ No tree re-walk per mutation in `handleAsyncUpdate` — the queue carries `(trackIndex, clipIndex, clipID)`; identity is captured at listener time. Where an index must be re-derived, use `getChildWithProperty(clipID)` scoped to one track's CLIP_LIST (bounded), not a project-wide scan.
- ❌ No batch-op N-loop RPCs — behavior rides existing batch commands; no new RPC loop.
- ✅ No new `DBG`, no raw hex, no `.cpp` without CMake entry (new test file appended to `tests/CMakeLists.txt` `hdaw_tests` source list).
- ✅ No test asserting only ReadModel — all new tests assert live processors (T2-G4).

## Steps

### Task 2 — incremental core + sibling state hardening
1. `RoutingManager`: extract the existing per-clip removal logic (`removeClipsForTrack` and the `graph.removeNode`/`removeConnection` patterns) into `removeClip(trackIndex, clipIndex)`:
   - `graph.removeConnection` for the clip→track audio (and MIDI) edges, then `graph.removeNode(node)`.
   - Erase `audioClipNodes`/`audioClipSources` (or `midiClipNodes`/`midiClipSources`) entries.
   - Recompute the track's `computeTrackCrossfades` + re-push merged envelopes to remaining audio siblings (shared helpers).
   - Guard: no-op when the identity key is absent; callers hold `graphLock` (+ pump-park off-message-thread).
2. `RoutingManager::updateClipPlacement(trackIndex, clipIndex)`:
   - Re-read `startTime`/`duration`/`offset`/`gain`/`fadeIn`/`fadeOut`/`looping`/`muted` from the clip ValueTree; re-push via existing processor setters.
   - Recompute the track crossfades and re-push merged envelopes to the moved clip + all siblings (same helper as 1). Skip re-push when `sameEnvelopePoints` says unchanged (bit-identical preservation).
   - Preserve the `(trackIndex, clipIndex)` key contract: move-within-track only. Middle inserts route to full rebuild (structural). **Cross-track moves of a track's LAST clip ride the incremental path** (source removal = last-position remove, dest add = append, startTime set = Place) — proven equivalent to a full rebuild by `FlagOnCrossTrackMoveEquivalent` (T2-G5); middle-remove cross-track moves still flag structural.
3. Tests (new suite `IncrementalRoutingRemoveMove` in `tests/unit/engine/incremental_routing_remove_move_test.cpp`, reusing the `RenderHarness` from the spike — extract it into a shared header `tests/unit/engine/render_harness.h` if both suites need it):
   - T2-G1 equivalence: full vs. rebuild-minus-one + incremental-remove.
   - T2-G2 equivalence: full vs. rebuild + incremental placement change into a new overlap.
   - T2-G3 undo/redo: add → undo (incremental remove) → redo (incremental re-add) → render < 1e-6, live map matches.
   - T2-G4: sibling envelope assertions on live processors.
    - T2-G5 equivalence: full vs. rebuild + incremental cross-track move of a track's LAST clip onto another track (land on both tracks, assert live graph + renders on source and dest).
4. Add the test file to `tests/CMakeLists.txt`; build; run `IncrementalRoutingSpike.*:IncrementalRoutingRemoveMove.*`; then full suite (T2-G6).
5. Update the spike handoff + recommendation with the Task 2 outcome.

### Task 3 — wire the hot paths through the coalescing seam
1. `AudioEngine`: add a mutex-guarded pending-op structure (small struct: `op` add/remove/place, `trackIndex`, `clipIndex`, `clipID`) + `bool forceFullRebuild_` + `bool incrementalEnabled_` (set from `HDAW_FORCE_INCREMENTAL_ROUTING`, read once in `initialize()`, `AudioEngine.cpp:50`).
2. `valueTreeChildAdded`/`valueTreeChildRemoved` (CLIP branches only, `:1129`, `:1276`): when `incrementalEnabled_`, record the op (append detection: `clipIndex == clipList.getNumChildren()-1` for add; otherwise set `forceFullRebuild_`) and still `triggerAsyncUpdate()`. TRACK branches (`:1134`, `:1282`) set `forceFullRebuild_`.
3. `handleAsyncUpdate` (`:1289`): when `incrementalEnabled_ && !forceFullRebuild_`, drain the queue under `graphLock` (message-thread → no pump-park, matching `rebuildRoutingGraph` no-park path): dispatch each op to `addClip`/`removeClip`/`updateClipPlacement`; then recompute `projectEndSample` (reuse the block at `MainAudioProcessor.cpp:639-663` — extract to a small `MainAudioProcessor::recomputeProjectEndSample()` helper). Clear the queue + flag. Otherwise (flag OFF, or structural op present) call `rebuildRoutingGraph()` exactly as today.
4. Thread-safety: the queue is written on command threads (Qt main / RPC / MCP) and drained on the pump thread — mutex-guard; `triggerAsyncUpdate` already bridges the threads. Identity (trackIndex/clipIndex) is captured at listener time; indices captured there are stable until drain because the ValueTree is the source of truth and the drain re-verifies key existence (`audioClipSources.find`) before mutating.
5. Tests (suite `IncrementalRoutingEngine` in `tests/unit/engine/incremental_routing_engine_test.cpp`):
   - Flag OFF: `addClips`/`removeClips`/`moveClips` + `drainPendingRoutingRebuild` → identical map to pre-change baseline (regression: T3-G2 zero-change path).
   - Flag ON (set env var before `engine.initialize()` in a dedicated fixture): batched `addClips(…, 16 clips)` → one drain → `getAudioClipSources()` map identical to flag-OFF run; render < 1e-6.
   - `undo`/`redo` after adds/removes → maps + render identical to flag-OFF.
   - Structural (add track, insert-silence) with flag ON → still full rebuild (map matches flag-OFF).
6. MCP parity check (T3-G6): run the existing MCP tool tests for `add_clips`/`remove_clips`/`move_clips` with the flag ON (they should be unchanged). No new tools.
7. Add test file to `tests/CMakeLists.txt`; build; run the new suites + full suite (T3-G7).

### Task 4 — latency/quality gates + flip default
1. Measure `getTotalLatency()` (via `getLatencySamples`) and `getTotalNumOutputChannels` on the real device: burst of incremental adds vs. full rebuilds — must be equal (spike showed 0→0; confirm on device). **DONE:** `IncrementalRoutingAB.RealDeviceLatencyStable` (device latency + graph latency + output channels, before/after a 32-clip incremental burst; OFF-path cross-check). No shift.
2. Critical-listening A/B on reference material (spike uses a stereo sine; add a real track with fades/crossfades): incremental vs. full-rebuild outputs must be audibly identical. **DONE:** both paths rendered via production `ExportManager` — max diff 0, user-confirmed.
3. Flip default: `HDAW_FORCE_INCREMENTAL_ROUTING` default becomes ON (or inverted kill-switch `HDAW_FORCE_FULL_ROUTING`). Keep the OFF escape hatch. **DONE:** default ON; `=0`/`false`/`FALSE` = escape hatch.
4. Re-run full suite + build (T4-G4); update README/handoff/recommendation with the measured latency numbers. **DONE:** 848/848 PASSED with default flipped; this plan + handoff updated.

## Verification Commands

- Build: `cmake --build build --config Debug`
- New suites: `build\Debug\hdaw_tests.exe --gtest_filter=IncrementalRoutingSpike.*:IncrementalRoutingRemoveMove.*:IncrementalRoutingEngine.*`
- Regression targets: `build\Debug\hdaw_tests.exe --gtest_filter=AudioGraphSurface.*:TrackMixerState.*:ClipSlicing.*:MergeClips.*:Crossfade.*:ClipStreamingE2E.*:UndoClips.*`
- Full suite: `build\Debug\hdaw_tests.exe`
- Frontend unaffected — no `npm` steps required unless the flag is surfaced in UI (it is not).

## Completion Contract Notes

- C++ changed → build succeeds (G7 style) before any gate is declared.
- New test file added to `tests/CMakeLists.txt`.
- No new RPC/MCP surface → no parity additions; behavior proven through existing tools.
- No frontend change → no `npm test`/E2E required.
- Docs to update: this plan (gates ticked), the Task 1 handoff, and a new handoff `docs/handoffs/2026-08-15-incremental-routing-task2-complete.md` after each task.
- Knowledge graph refresh after structural additions (`codebase-memory` `index_repository` for the new RPC-independent methods + test files).