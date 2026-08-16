# Incremental Routing Spike — Recommendation (Task 1 result)

> Outcome of `docs/plans/2026-08-15-incremental-routing-spike-task1.md`. Proves
> Approach C on the clip-add path and recommends how to proceed with Task 2+.
> This is a **decision document**, not a live spec — the code lives in
> `RoutingManager::addClip` and the `IncrementalRoutingSpike` suite; further
> tasks will be planned separately.

## Approach decision: C confirmed

**Approach C (incremental clip-add on a live graph, no teardown) is confirmed**
and measured on the clip-add path. `RoutingManager::addClip` adds one clip node
to a prepared, non-realtime `AudioProcessorGraph` without `graph.clear()`,
`prepareToPlay`, or `reconnectMasterToOutput`, and re-applies crossfades to the
affected siblings only. It is behaviorally equivalent to a full rebuild
(render-diff < 1e-6, identical node/connection counts) and ~8× faster at 128
clips on the measured mutation surface.

### Measured numbers (benchmark output, transcribed)

```
[IncrementalRoutingSpike.Benchmark] at 128 clips
  single op  addClip=1.0752ms  fullRebuild=7.7634ms
  cumulative 1->128:  addClip=63.9014ms  fullRebuild=588.22ms
  graph latency before=0 after=0 samples
```

- **Single op** (one mutation against a fully-baked 128-clip graph):
  `addClip` ≈ 1.1 ms vs full rebuild ≈ 7.8 ms — **~7.5–8× faster** on the
  graphLock-hold surface.
- **Cumulative 1→128** (fresh harness per path, one mutation per clip):
  `addClip` ≈ 64 ms vs full rebuild ≈ 588 ms — **~9× faster**.
- **Latency unchanged**: `graph.getLatencySamples()` is 0 before and after an
  incremental add (no topology latency shift).
- Run-to-run variance is real (±1 ms on single ops) but the *ratio* is stable
  across all runs (single op 7–8×, cumulative 7–9×).

**Interpretation caveat — the timed surface is the mutation, not the bake.**
Per-iteration wall-clock in the benchmark is dominated (~124 s of the ~125 s
test) by JUCE's *async* render-sequence bake on the message thread: every
topology change — for **both** paths — makes `RenderSequenceBuilder::build`
(`build/_deps/juce-src/.../juce_AudioProcessorGraph.cpp:1012-1028`) rebuild the
full O(N) render sequence. `NodeStates::applySettings` (:431-488) prepares only
*new* nodes for incremental adds, but the full-sequence bake is inherent to JUCE
and is paid identically by both paths. The production win is therefore the
**graphLock-hold window** (the audio-dropout surface), not total pump time.
MessageManagerLock acquisition itself costs ~10–30 ms per round-trip and must be
excluded from any timing of the mutation surface.

## Identity map

`(trackIndex, clipIndex)` keys in `RoutingManager::audioClipSources` /
`audioClipNodes` (and `midiClipSources` / `midiClipNodes`) already serve as the
identity map; no new structure was needed. `addClip(trackIndex, clipIndex,
clipTree)` looks up existing siblings by these keys and re-applies merged
crossfade envelopes only to siblings whose points genuinely changed.

**Constraint discovered:** `addClip` is **append-at-end** — the new clip must
already be the last child of `CLIP_LIST` (clipIndex == last position), because
the identity keys assume clip position == clipIndex and the clip's index in the
track's CLIP_LIST. Middle inserts require sibling key shifting; the
recommendation for Task 3 is that the pending-op queue only feeds *appends*
through the incremental path and routes structural changes (insert-silence,
ripple delete, move) to a full rebuild.

## Flag

`HDAW_FORCE_INCREMENTAL_ROUTING`, read once at engine startup, default **OFF**
(full rebuild) until all Task 4 gates pass. **DONE (2026-08-15):** default is
now **ON** (Task 4 flip); `HDAW_FORCE_INCREMENTAL_ROUTING=0`/`false`/`FALSE`
restores the full-rebuild path. Precedent for read-once env-flag
semantics: `HDAW_FORCE_FULL_SYNC` in `src/frontend/FrontendTreeWatcher.cpp:26`.
The spike does not read the flag (it calls `addClip` directly).

## Migration order (Task 2+)

1. **Task 2 — incremental core + sibling state hardening.** Keep `addClip`
   behind the flag; extend the spike to the remove/move/undo paths using the
   same shared construction helpers. Every new mutation holds the pump park +
   `graphLock` exactly as `rebuildRoutingGraph` does (`MainAudioProcessor.cpp:620-635`).
2. **Task 3 — wire the hot paths through the coalescing seam.** `AudioEngine`
   accumulates pending clip adds from `valueTreeChildAdded` into a queue drained
   in `handleAsyncUpdate`; each drained op calls `addClip` (one park per tick,
   one bake), matching today's coalescing of N clip ops into one rebuild.
   Structural changes (track add/remove, reorder, insert-silence, ripple,
   move-to-middle) → full rebuild. MCP feature parity: expose the incremental
   behavior through the same RPC layer (no new surface needed).
3. **Task 4 — latency/quality gates + flip default.** Measure `getTotalLatency()`
   before/after on real device I/O and A/B against the full-rebuild path (lessons
   7/8); only then flip the default and remove the flag.

## Pitfalls found during the spike

1. **Pump-park is mandatory for EVERY graph mutation — including teardown.**
   The graph's internal `LockingAsyncUpdater` dispatches topology changes on the
   message thread and iterates the **live node list**. Repeated
   `rebuildFromValueTree()`/`addClip` on a prepared graph without a
   `MessageManagerLock` produced an access violation in `NodeStates::applySettings`
   (`feeefeee` freed-node pattern, verified under cdb). Production already does
   this (`rebuildRoutingGraph`, `needsPark` + `MessageManagerLock`); the spike
   must mirror it. The harness `shutdown()` also parks: `routing.reset()` +
   `graph.clear()` under the lock, then a bake-wait to drain the pump to idle
   before the graph goes out of scope. Without the parked teardown the full
   suite shows **delayed heap corruption** (`_CrtIsValidHeapPointer`) in tests
   that run *later* in the suite (e.g. `McpServer.*`, `SessionModel.*`) — the
   corruption is real but detected far from the cause.
2. **MessageManagerLock does NOT drain the pump.** It spinlocks the message
   thread; queued async-rebuild messages are still serviced after the lock is
   released. A bake-wait probe (post a `CallbackMessage` after the graph's
   queued updater messages, FIFO, bounded wait) is required after any mutation
   before the first `processBlock`, and at teardown. This is the same contract
   `ExportManager` enforces.
3. **Timing methodology:** measure only the mutation call, *inside* the held
   lock. Including lock acquisition (~10–30 ms round-trip) or the async bake
   swamps the comparison and produced a spurious "incremental slower" result
   (2509 ms vs 1134 ms) in an early run.
4. **Time-unit convention (lesson 1) held:** clip ValueTree `startTime`/`duration`
   are seconds; the transport speaks samples; no beats crossed any boundary in
   the spike.
5. **`valueTreeChildAdded` ordering:** the clip must be appended to the ValueTree
   *before* `addClip` is called, because `addClip` reads crossfades back from the
   model tree and the identity keys assume the append position.
6. **Teardown in tests must be explicit and parked** (see 1). Implicit
   destructor ordering destroys `RoutingManager`'s node refs and then the graph
   on the test thread with the pump still able to dispatch — the corruption
   source behind pitfall 1.

## What this does NOT cover

- Live audio device I/O (the spike is a standalone non-realtime graph). Task 4
  must measure real `getTotalLatency()` and A/B quality before the default
  flips.
- The remove / move / insert-silence / ripple paths (Task 2+).
- The frontend / ReadModel (unchanged; both projections rebuild from the
  ValueTree and are unaffected by how the graph is mutated).

## Gates

- [x] G1 — `RoutingManager::addClip` exists; per-clip construction extracted
  into shared helpers (`computeTrackCrossfades`, `computeMergedEnvelopeForClip`,
  `buildClipNode`) used by both paths.
- [x] G2 — render-diff green: `EquivalentToFullRebuild` max abs diff < 1e-6,
  node/connection counts equal.
- [x] G3 — sibling state preserved: `SiblingStatePreserved` asserts on live
  processors; untouched siblings bit-identical, overlapped sibling's crossfade
  matches a full-rebuild reference.
- [x] G4 — benchmark real and reported (numbers above).
- [x] G5 — full suite green: **836 tests / 159 suites PASSED**
  (`hdaw_tests.exe`, includes `AudioGraphSurface`, `TrackMixerState`,
  `ClipSlicing`, `MergeClips`, `Crossfade`, `ClipStreamingE2E`).
- [x] G6 — this document.
- [x] G7 — `cmake --build build --config Debug` succeeds.