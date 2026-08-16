# Handoff: Incremental routing — Task 1 spike complete

Date: 2026-08-15. Continues
`docs/handoffs/2026-08-15-incremental-routing-standing.md`.

## What this is

Task 1 of the incremental-routing design
(`docs/plans/2026-08-09-incremental-routing-design.md`) is **complete**:
a spike proving `RoutingManager::addClip` (Approach C, clip-add path) is
behaviorally equivalent to a full rebuild and ~8× faster at 128 clips on
the graphLock-hold surface.

## Completed

- **`RoutingManager::addClip`** implemented. Per-clip construction extracted
  into shared helpers used by BOTH the full-rebuild loop and the incremental
  add: `computeTrackCrossfades`, `computeMergedEnvelopeForClip`,
  `buildClipNode` (see `docs/plans/2026-08-15-incremental-routing-spike-task1.md`).
- **Spike suite** `IncrementalRoutingSpike` (3 tests, all pass):
  - `EquivalentToFullRebuild` — render-diff < 1e-6, node/connection counts equal.
  - `SiblingStatePreserved` — untouched siblings bit-identical; overlapped
    sibling's crossfade matches full-rebuild reference (asserted on live processors).
  - `BenchmarkPrintsNumbers` — single op addClip≈1.1 ms vs full≈7.8 ms;
    cumulative 1→128 addClip≈64 ms vs full≈588 ms; latency unchanged (0→0).
- **Recommendation written:** `docs/plans/2026-08-15-incremental-routing-spike-recommendation.md`
  — Approach C confirmed, identity map `(trackIndex, clipIndex)`,
  `HDAW_FORCE_INCREMENTAL_ROUTING` flag (read-once, default OFF),
  migration order (Task 2 core, Task 3 wire seam, Task 4 latency/quality +
  flip default), and 6 pitfalls.
- **Full suite green:** 836 tests / 159 suites PASSED.

## Key findings (pitfalls that will recur in Task 2+)

1. **Every graph mutation must park the pump** (`MessageManagerLock`) —
   including **teardown**. Without it: UAF in `NodeStates::applySettings`
   (pump dispatches graph-internal async-rebuild against freed nodes) and
   **delayed heap corruption** in LATER tests (`_CrtIsValidHeapPointer`).
2. **MessageManagerLock does NOT drain the pump.** A bake-wait probe
   (post `CallbackMessage` after the graph's queued updater messages) is
   needed after any mutation and at teardown.
3. **Time only the mutation call, inside the lock.** Timing lock
   acquisition or the async bake swamps the comparison (produced a spurious
   "incremental slower" result early on).
4. JUCE's render-sequence bake is O(N) per topology change for BOTH paths;
   the production win is the graphLock-hold window, not total pump time.
5. `addClip` is **append-at-end** (clipIndex == last CLIP_LIST position);
   middle inserts need sibling key shifting → route structural changes to
   full rebuild.

## Next step (Task 2+)

Write the full implementation plan per the recommendation's migration
order. The recommendation is the authoritative starting point.