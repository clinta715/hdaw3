# Handoff: Incremental routing — standing follow-up, design complete

Date: 2026-08-15. Deferred from handoff
`docs/handoffs/2026-08-15-drain-seam-and-docs-archive.md` §3 item 5.

## What this is

`rebuildRoutingGraph()` is O(project) per call — it tears down and
re-instantiates every clip/plugin processor on every edit. At 128+ clips
this stalls ~30s (AGENTS.md lesson 6). The fix is incremental graph
mutation: add/remove/move only the changed nodes instead of rebuilding
everything.

## Current state

- **Design doc complete:** `docs/plans/2026-08-09-incremental-routing-design.md`
  — three candidate approaches (tree-diff, targeted commands, hybrid),
  dependency map, pitfall gates, success criteria. Approach C (hybrid)
  is recommended: hot paths (clip add/remove/move/duplicate) use narrow
  `RoutingManager::addClip/removeClip/moveClip`; structural changes
  (track add/remove, tempo, load) keep full rebuild.
- **No implementation started.** The design doc's Task 1 (spike on
  clip-add path, benchmarked) is the next step.
- **Coalescing already works:** `AsyncUpdater` merges burst edits into
  one rebuild per message-loop tick. The problem is that ONE rebuild is
  still O(project).
- **`ProjectModel::sliceClipAtTimes` exists** as a model-level batch op
  that does NOT rebuild (used by ripple delete, merge). The incremental
  path should follow this pattern: model-level ops that bypass the full
  rebuild.

## What to do next

1. **Read the design doc** (`docs/plans/2026-08-09-incremental-routing-design.md`)
   — it has the full analysis, candidate approaches, and Task 1 spec.
2. **Task 1: Spike.** Implement Approach C on the clip-add path only.
   Benchmark: 128-clip project, add one clip, measure time vs full
   rebuild. Write recommendation.
3. **If spike succeeds:** write the full implementation plan (Task 2+),
   breaking into incremental migrations of each command site.
4. **If spike fails:** fall back to Approach A (tree-diff) or accept
   the O(project) cost with better coalescing.

## Key files

| File | Role |
|------|------|
| `src/engine/MainAudioProcessor.cpp` | `rebuildRoutingGraph()` — the O(project) entry point |
| `src/engine/RoutingManager.{h,cpp}` | `rebuildFromValueTree()` + `addTrack()` — full rebuild logic |
| `src/engine/AudioEngineCommands_Clips.cpp` | Clip add/remove/move commands — hot-path callers |
| `src/engine/AudioEngineCommands_Slicing.cpp` | Slice/merge/ripple — batch callers |
| `src/engine/AudioEngine.cpp` | `handleAsyncUpdate()` — coalesced rebuild trigger |

## Constraints

- Gate 1/6/10: incremental add must not reset sibling state.
- Gate 3/12: graph mutation must park the pump under `graphLock`.
- Lessons 7/8: latency/quality must not change.
- Kill-switch: `HDAW_FORCE_INCREMENTAL_ROUTING=1` env var, defaulting
  to full rebuild until proven.
