# Task 1 Spike — incremental routing on the clip-add path

> Gate for all further incremental-routing work. Per
> `docs/plans/2026-08-09-incremental-routing-design.md` Task 1. No production
> command migration happens here; this spike proves the incremental `addClip`
> is behaviorally equivalent to a full rebuild and measures the win.

## Goal

Implement and prove a narrow `RoutingManager::addClip` (Approach C, clip-add
path only) that adds a single clip node to a live `AudioProcessorGraph` without
tearing the graph down, benchmark it against the full rebuild on a 128-clip
project, verify output equivalence via a render-diff, and write a
recommendation for the approach, identity-map design, flag name, and migration
order.

## Success Gates (all must pass to declare done)

- [x] **G1 — API exists and is shared.** `RoutingManager::addClip(trackIndex,
      clipIndex, clipTree)` added; the per-clip construction logic is
      **extracted** from `rebuildClipsForTrack` into shared helpers used by
      BOTH the full-rebuild loop and the incremental add, so equivalence holds
      by construction (single source of truth).
- [x] **G2 — Incremental add is equivalent (render-diff green).** A reference
      project (audio sine clips, some overlapping so crossfades are exercised)
      renders **sample-near-identical** output when built via full rebuild vs
      built as (N-1) full-rebuild + one incremental `addClip`. Tolerance: max
      abs sample diff < 1e-6. The overlapping case must be included to prove
      sibling crossfade re-application.
- [x] **G3 — Sibling state preserved (Gate 1/6/10).** After an incremental
      `addClip` on a track, an existing sibling clip's live processor state
      (gain envelope / crossfade points, start, duration, gain) is unchanged
      except for any crossfade points that genuinely change because the new
      clip overlaps it. Asserted on the live processor, not the ReadModel.
- [x] **G4 — Benchmark is real and reported.** A 128-clip project benchmark
      measures and PRINTS: (a) one incremental `addClip` wall-clock vs one full
      rebuild wall-clock at 128 clips; (b) cumulative 1→128 clip-add burst for
      both paths; (c) the graphLock hold time (= audio-dropout window). Test
      asserts only generous bounds (incremental < full; full completes in
      reasonable time); the numbers go in the recommendation.
- [x] **G5 — Existing suite green.** `hdaw_tests.exe` full run passes
      (especially `AudioGraphSurface.*`, `TrackMixerState.*`, `ClipSlicing.*`,
      `MergeClips.*`, `Crossfade.*`, `ClipStreamingE2E.*` — the rebuild-path
      safety net).
- [x] **G6 — Recommendation written.** `docs/plans/2026-08-15-incremental-routing-spike-recommendation.md`
      records: approach decision (Approach C confirmed or rejected), identity
      map (key scheme for clip nodes), flag name
      (`HDAW_FORCE_INCREMENTAL_ROUTING`), measured numbers, migration order for
      Task 2+, and any pitfalls found.
- [x] **G7 — `cmake --build build --config Debug` succeeds.**

## Dependency Map (verified via codebase-memory + reads)

- **Blast radius:** `RoutingManager` (rebuild + clip construction), the audio
  graph. NOT the command layer (no migration in this spike), NOT the SPSC
  bridge, NOT the frontend.
- **Upstream (who calls the rebuilt path today):** `AudioEngine::handleAsyncUpdate`
  (`AudioEngine.cpp:1289`) → `MainAudioProcessor::rebuildRoutingGraph`
  (`MainAudioProcessor.cpp:585`); plus direct callers in
  `AudioEngineCommands_Clips.cpp` (511/574/670), `AudioEngineCommands_Slicing.cpp`
  (7 sites), `AudioEngineCommands_Undo.cpp` (4 sites), `AudioImport.cpp:121`,
  `MidiImport.cpp:144`, `McpTools_Project.cpp:1280`, `Router_Composition.cpp:84`.
  None of these are changed by the spike.
- **Downstream:** `RoutingManager::rebuildFromValueTree`
  (`RoutingManager.cpp:43`), `rebuildClipsForTrack` (`:460`), `addTrack`
  (`:181`), Track processors, the audio thread (reads graph under `graphLock`).
- **God nodes in scope:** `RoutingManager::rebuildClipsForTrack` — refactored
  but the full-rebuild behavior must be bit-identical before/after. The
  existing engine suite is the regression net.
- **Community boundaries crossed:** none new (all within `src/engine`).
- **Projections affected:** audio graph only. ReadModel/frontend unchanged.
- **SPSC paths touched:** none (spike is standalone-graph tests; no live audio
  device).
- **Path integrity:** clip add today = ValueTree add → `valueTreeChildAdded`
  (`AudioEngine.cpp:1035`) → `triggerAsyncUpdate` → `handleAsyncUpdate` → full
  rebuild. The spike bypasses this seam by calling `addClip` directly; the
  recommendation documents how to feed the incremental path through the seam
  (Task 3).

## Pitfall Gates Triggered

| Gate | Why | Mitigation |
|------|-----|------------|
| Gate 1/6/10: state restore | Incremental add must not reset siblings; the twin of lesson 10 | `addClip` recomputes the affected track's crossfade map and re-pushes merged envelopes to sibling audio clips (shared helper, same math as full rebuild). G3 asserts on live processors. |
| Gate 3/12: audio-thread safety | `addNode`/`addConnection` mutate the graph | Spike tests run on the message thread (test_main pump) with no live audio callback; when the recommendation wires production (Task 3), reuse the `rebuildRoutingGraph` pump-park + `graphLock` pattern. No new mutation outside it. |
| Lesson 7/8: latency/quality | Topology change must not shift latency | Equivalence render (G2) plus `graph.getLatencySamples()` compared before/after incremental add in the benchmark test. |
| Gate 2: unimplemented path | Incremental path not wired into commands yet | Documented in the recommendation as Task 3 migration order; the spike exercises the path directly via tests so it is not dead code. |
| Gate 4: stale binaries | C++ engine changed | After building, run `build/Debug/hdaw_tests.exe` (never Release). Verify `.obj`/exe timestamps if a fix "doesn't take". |
| Gate 15: stale flags | Env-flag behavior | Spike reads the flag once at process start in the test (or not at all — direct API calls); recommendation specifies read-once semantics for production. |

## Anti-Pattern Scan (must not introduce)

- No `rebuildFromValueTree`/`graph.clear()` per clip in a loop — `addClip` is
  node-add-only.
- No DSP-state write without `stateLock` — `addClip` runs on the message
  thread pre-prepare; no live-audio writes in the spike tests.
- No full-tree walk to touch one node — clip lookup by
  `(trackIndex, clipIndex)` keys already in `audioClipSources`/
  `midiClipSources`.
- No duplication of the clip-construction logic — extract shared helpers
  (`buildClipProcessorForTree`, `computeTrackCrossfades`,
  `computeMergedEnvelopeForClip`) used by both paths.
- New `.cpp` files must be added to `tests/CMakeLists.txt` `add_executable`.

## Key Files

| File | Change |
|------|--------|
| `src/engine/RoutingManager.h` | Declare `addClip(int, int, const ValueTree&)`; private shared helpers |
| `src/engine/RoutingManager.cpp` | Extract construction/crossfade/envelope logic into helpers; implement `addClip`; `rebuildClipsForTrack` delegates to helpers |
| `tests/unit/engine/incremental_routing_spike_test.cpp` | NEW — benchmark + equivalence + sibling-state tests |
| `tests/CMakeLists.txt` | Register the new test file |
| `docs/plans/2026-08-15-incremental-routing-spike-recommendation.md` | NEW — recommendation deliverable |

## Steps

1. **Refactor `RoutingManager::rebuildClipsForTrack`** (`RoutingManager.cpp:460`)
   without behavior change:
   - Extract the crossfade precompute block (lines 469-501) into
     `std::unordered_map<int, std::vector<ClipSourceProcessor::GainPoint>>
     computeTrackCrossfades(int trackIndex, const ValueTree& trackTree)`.
   - Extract the per-clip build+connect body (lines 503-644, both audio and
     midi branches) into a private helper that constructs ONE node from a clip
     ValueTree and connects it to the track, given a crossfade map:
     `void buildClipNode(int trackIndex, int clipIndex, const ValueTree&
     clipTree, const CrossfadeMap&)` — this is the shared construction
     used by full rebuild AND incremental add.
   - `rebuildClipsForTrack` becomes: compute crossfades → loop children →
     `buildClipNode`.
   - Run the engine suite to confirm the refactor is bit-neutral.
2. **Implement `RoutingManager::addClip(int trackIndex, int clipIndex, const
   ValueTree& clipTree)`**:
   - Compute `computeTrackCrossfades` for the track (now includes the new
     clip — crossfades against siblings are correct).
   - `buildClipNode(trackIndex, clipIndex, clipTree, crossfadeMap)` for the new
     clip.
   - Re-push merged envelopes to sibling audio clips whose crossfade points
     changed: iterate `audioClipSources` for `trackIndex`, compute each
     sibling's merged envelope via a shared `computeMergedEnvelopeForClip`
     helper, and call `setGainEnvelopePoints` only when the points changed
     (avoids redundant work and preserves sibling state otherwise).
   - No `graph.clear()`, no `prepareToPlay`, no `reconnectMasterToOutput`.
3. **Benchmark test** (`IncrementalRoutingSpike` suite):
   - Write a sine WAV (AudioFormatWriter, 1s, 440Hz) to a temp file.
   - Build a `ProjectModel` with track 0 containing 128 audio clips referencing
     the sine WAV (all non-overlapping), plus a few overlapping pairs.
   - Standalone-graph harness (mirror ExportManager): `AudioProcessorGraph` +
     `RoutingManager` + local `TransportManager` + `setBusesLayout(stereo)` +
     `setPlaybackInfo` + `rebuildFromValueTree` + `reconnectMasterToOutput` +
     `prepareToPlay(44100, 512)` + `setNonRealtime(true)`.
   - Measure (std::chrono, print with `std::cout` / HDAW_LOG):
     (a) one full `rebuildFromValueTree` at 128 clips;
     (b) one `addClip` at 128 clips (both wrapped to measure the same
         mutation surface; time only the call);
     (c) cumulative 1→128: N full rebuilds vs N addClip calls;
     (d) `graph.getLatencySamples()` before and after an incremental add.
   - Assert generous bounds only (inc < full; no crash).
4. **Equivalence test** (`IncrementalRoutingSpike.EquivalentToFullRebuild`):
   - Project P = N audio clips (mix of non-overlapping and overlapping pairs,
     sine WAV) on track 0.
   - Graph A: full `rebuildFromValueTree`, render N blocks to buffer A.
   - Graph B: full rebuild of P minus the last clip, then
     `addClip(track, lastIndex, lastClipTree)`, render same N blocks to buffer B.
   - Assert max abs diff < 1e-6; assert node count and connection count equal.
5. **Sibling-state test** (`IncrementalRoutingSpike.SiblingStatePreserved`):
   - Track with two non-overlapping audio clips + one overlapping pair;
     rebuild full. Read sibling processor state (envelope points, start,
     duration, gain). `addClip` a new clip that does NOT touch the observed
     sibling → assert sibling processor state unchanged. Then add a clip that
     DOES overlap the sibling → assert the sibling's crossfade points changed
     as the full rebuild would produce (compare against a full-rebuild
     reference processor).
6. **Run the full test suite**; fix any regressions (the refactor must be
   behavior-neutral).
7. **Write the recommendation** (`docs/plans/2026-08-15-incremental-routing-spike-recommendation.md`):
   - Approach decision (C confirmed / A fallback).
   - Identity map: `(trackIndex, clipIndex)` keys in
     `audioClipNodes`/`midiClipNodes` already serve as the map; document how
     Task 3 feeds add/remove/move through the coalescing seam (pending-op
     queue in `AudioEngine` driven by `valueTreeChildAdded/Removed`, drained in
     `handleAsyncUpdate`; structural changes → full rebuild).
   - Flag: `HDAW_FORCE_INCREMENTAL_ROUTING`, read once at engine startup,
     default OFF (full rebuild) until all Task 4 gates pass.
   - Measured numbers: the benchmark output, transcribed.
   - Migration order: Task 2 (incremental core + sibling state), Task 3
     (wire hot paths behind flag), Task 4 (latency/quality + flip default).
   - Pitfalls found during the spike.

## Verification commands

```
cmake --build build --config Debug
build\Debug\hdaw_tests.exe --gtest_filter=IncrementalRoutingSpike.*
build\Debug\hdaw_tests.exe --gtest_filter=AudioGraphSurface.*:TrackMixerState.*:ClipSlicing.*:MergeClips.*:Crossfade.*:ClipStreamingE2E.*
build\Debug\hdaw_tests.exe
```