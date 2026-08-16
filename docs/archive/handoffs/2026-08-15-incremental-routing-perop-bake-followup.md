# Handoff — Finish incremental routing: per-op graph-bake avoidance (lesson 6)

> **Scope of this handoff:** the ONE remaining standing follow-up of the
> incremental-routing feature (Tasks 1–4 shipped, default flipped ON). This is
> the T4-G1 deviation recorded in
> `docs/handoffs/2026-08-15-incremental-routing-task4-complete.md`.
> Everything else is done and committed (`f2af89a` = Tasks 2/3, `8cdfb60` = Task 4).
> The lesson-20 pipe/shm namespace guard is a separate standing item — NOT this
> handoff.

## What is done (do not redo)

- Incremental clip routing behind `HDAW_FORCE_INCREMENTAL_ROUTING`, **default ON**
  since Task 4 (`AudioEngine.cpp:63-73`). Escape hatch: `=0`/`false`/`FALSE`.
- `AudioEngine::enqueueClipOp` / `drainPendingClipOps` (`AudioEngine.cpp:1393-1486`):
  ops captured at ValueTree-listener time, drained under ONE `graphLock` hold on
  the message thread (no-park), then `recomputeProjectEndSample()`.
- `RoutingManager::addClip` / `removeClip` / `updateClipPlacement`
  (`RoutingManager.cpp:692/735/813`) — incremental graph mutations with shared
  `buildClipNode` + crossfade recompute + sibling envelope re-push.
- 15 incremental tests (Spike 3, RemoveMove 3, Engine 7, AB 2) + 848/848 full
  suite with the default flipped.

## The problem this handoff fixes

`AudioProcessorGraph::addNode` / `removeNode` / `addConnection` / `removeConnection`
all default to `UpdateKind::sync` (JUCE 8,
`build/_deps/juce-src/modules/juce_audio_processors/processors/juce_AudioProcessorGraph.h`
lines 264/269/274/305/308 — verified). `sync` means **a full render-sequence bake
per mutation**, dispatched to the message thread. In the incremental drain
(`drainPendingClipOps` → `RoutingManager::addClip`/`removeClip`/`updateClipPlacement`
→ `buildClipNode` → `graph.addNode(...)` at `RoutingManager.cpp:639/665`, and the
`graph.addConnection` calls at 645/646/673 etc.), every op inside a batch triggers
its own bake. For a burst of N per-op commands this is the lesson-6 cliff:
N growing synchronous bakes on the message thread (the T4-G1 diagnosis measured
79.8 s CPU on the pump thread in 83 s uptime for 128 per-op adds). T4-G1 worked
around it by batching 32 clips into ONE `addClips` call — the real fix is below.

## The fix (per-op `UpdateKind::none` + one end-of-batch rebuild)

JUCE's `UpdateKind::none` ("Graph should not be rebuilt automatically. Use
rebuild() to trigger a graph rebuild") is designed exactly for this. The plan:

1. **Scope the change to the incremental path only.** The full-rebuild path
   (`rebuildRoutingGraph`) relies on per-call `sync` semantics inside its own
   rebuild sequence and must NOT be touched. Add `UpdateKind::none` to the
   `addNode`/`removeNode`/`addConnection`/`removeConnection` calls reachable from
   the incremental drain (`buildClipNode`, `addClip`, `removeClip`,
   `updateClipPlacement`). Check each call site in `RoutingManager.cpp` and pass
   `juce::AudioProcessorGraph::UpdateKind::none` only where the op is an
   incremental mutation; the full-rebuild callers of the same helpers (e.g.
   `rebuildFromValueTree` during a full rebuild) should either keep `sync` or rely
   on the end-of-rebuild `rebuild()` — **verify the full-rebuild path still bakes
   exactly once and identically to today** (bit-identical renders + latency gate).
2. **One rebuild at the end of the batch.** After `drainPendingClipOps` applies
   the full op batch under its single `graphLock` hold (`AudioEngine.cpp:1443-1481`),
   trigger ONE graph rebuild so the final topology bakes. JUCE `AudioProcessorGraph`
   has `rebuild(UpdateKind)` / `setUpdateFlag` / `handleUpdateRequest` — confirm
   the exact public API in the vendored header (lines ~200-260) and use the
   message-thread-safe variant (the drain already runs on the message thread, so
   a `rebuild()` post is fine; do NOT call `rebuild()` from a non-message thread —
   park the pump, lesson 12). Confirm whether `UpdateKind::async` per-op (bakes
   once per call-stack) is sufficient and simpler than `none` + explicit
   `rebuild()`; the header comment explicitly blesses `async` for "lots of
   separate calls to addNode, addConnection inside the same call stack" — pick
   whichever is correct and simplest, and document why.
3. **Crossfade/envelope timing invariant.** The drain already recomputes
   crossfades + sibling envelopes per op against the ValueTree (source of truth),
   so batching the *bakes* must not change WHEN envelopes are computed — only when
   the graph re-renders. Preserve the per-op crossfade recompute; only defer the
   graph bake.

## Success gates

- **G1** — A burst of N separate per-op commands (e.g. 128 `addClips` each its
  own call, as the original T4-G1 spec wanted) drains without the message-thread
  bake cliff. Prove with a test: extend `IncrementalRoutingAB.RealDeviceLatencyStable`
  (or add a companion) to run 128 per-op adds under the real device and complete
  in bounded time (well under the previous ~80 s; target the batched-32 time
  scale). This is the regression that documents the fix.
- **G2** — Behavior identical to today: the 15 incremental tests +
  `FlagOnCrossTrackMoveEquivalent` + all equivalence tests still pass, and a
  render-equivalence check (ON vs OFF) stays < 1e-6 / bit-identical.
- **G3** — Full-rebuild path untouched: `FlagOffPathIsUnchangedFullRebuild` and
  the full suite still pass; full-rebuild renders remain bit-identical.
- **G4** — Latency gate: `RealDeviceLatencyStable` still shows graph latency
  unchanged (0→0) before/after the burst.
- **G5** — Full suite green (848+ tests) + `cmake --build build --config Debug`.

## Pitfall gates to respect (hdaw-guard / AGENTS.md)

- **Lesson 6** — this IS the lesson-6 fix; do not introduce a new O(N²) anywhere.
  The 16 ms `TreeDeltaAccumulator` + `AsyncUpdater` coalescing already collapses a
  burst into one drain; the bake is the remaining cliff.
- **Lesson 12** — `AudioProcessorGraph` is not thread-safe. All graph mutation
  stays under `graphLock` + pump-park off the message thread. The drain is the
  message-thread no-park path (`AudioEngine.cpp:1384-1388`); `rebuild()` post must
  respect that.
- **Lesson 15** — after editing entry points or when a fix "doesn't take," verify
  the **binary** contains the change (timestamp / breakpoint probe) — the stale-
  `.obj` trap.
- **Gate 3/12** — realtime safety: nothing new on the audio thread.
- **Lesson 8** — every engine change must be quality-checked: the A/B
  (`IncrementalRoutingAB.ProduceWavPair`, max diff = 0) must remain bit-identical
  after the fix.
- **Lesson 10** — the drain restores track/clip state; keep the shared
  `buildClipNode`/`Track::restoreMixerState` seam. Do not break the
  `track_mixer_state_test`.
- **Gate 15** — rebuild the binary and confirm the timestamp is newer than the
  source before running tests.

## Files you will touch (likely)

- `src/engine/RoutingManager.cpp` — per-op `UpdateKind::none`/`async` on the
  incremental mutation call sites (`buildClipNode`, `addClip`, `removeClip`,
  `updateClipPlacement`, and the connection calls they reach).
- `src/engine/AudioEngine.cpp` — end-of-batch single rebuild after
  `drainPendingClipOps`'s op loop.
- `tests/unit/engine/incremental_routing_ab_test.cpp` — the 128-per-op regression
  test (G1).
- Docs: tick the follow-up in `docs/handoffs/2026-08-15-incremental-routing-task4-complete.md`
  and add a short handoff note when done.
- Do NOT touch the lesson-20 pipe/shm guard (separate item).

## Verification commands

- Build: `cmake --build build --config Debug`
- Incremental suites: `& "build\Debug\hdaw_tests.exe" --gtest_filter="IncrementalRoutingSpike.*:IncrementalRoutingRemoveMove.*:IncrementalRoutingEngine.*:IncrementalRoutingAB.*"`
- Full suite: `& "build\Debug\hdaw_tests.exe"` (~8 min)
- Frontend unaffected — no `npm` steps.

## Decision record so far (do not relitigate)

- Cross-track moves of a track's LAST clip ride the incremental path
  (Remove+Add+Place) — proven by `FlagOnCrossTrackMoveEquivalent`, docs corrected.
- Flag default flipped ON in Task 4 after latency/quality gates; escape hatch
  `HDAW_FORCE_INCREMENTAL_ROUTING=0`.
- T4-G1 used a 32-clip batched burst as a workaround; the 128-per-op goal is THIS
  handoff's job.