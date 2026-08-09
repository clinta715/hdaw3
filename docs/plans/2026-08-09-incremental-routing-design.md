# Incremental routing graph (design plan)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. **This is a design plan first** — Task 1 (spike + decision) must complete before any production change.

**Goal:** Eliminate the O(project) full-teardown that `MainAudioProcessor::rebuildRoutingGraph()` performs on every clip add/remove/move, which at 128+ clips can stall ~30s (AGENTS.md lesson 6 — the explicitly "remaining follow-up"). Replace it with an **incremental** graph mutation that adds/removes/moves only the changed nodes.

**Tech Stack:** C++17, JUCE 8 `AudioProcessorGraph`, GTest

---

## Problem Statement

`MainAudioProcessor::rebuildRoutingGraph()` (`src/engine/MainAudioProcessor.cpp:475`) does, on **every** triggering edit:
1. `graph.clear()` — destroys every node
2. `routingManager = std::make_unique<RoutingManager>(...)` — reconstructs the whole manager
3. `routingManager->rebuildFromValueTree()` — re-instantiates every clip/plugin processor
4. `graph.prepareToPlay(...)` + `reconnectMasterToOutput()`

Clip add/remove is *coalesced* into one rebuild per message-loop tick (`AsyncUpdater`), but a single rebuild still tears down and re-instantiates everything. At extreme bursts this is the "black screen" cliff.

---

## Design goals & constraints

1. **Correctness over speed.** The full rebuild is the source of truth; the incremental path must produce a graph *behaviorally identical* to it.
2. **State preservation (lesson 10).** The incremental path must restore track mixer state (`Track::restoreMixerState`) and all processor state — incremental add of a track/clip must not reset siblings to constructor defaults.
3. **Latency/quality invariance (lessons 7 & 8).** The signal path length, bus layout, and PDC must not change. Measure `getTotalLatency()` before/after.
4. **Thread-safety (lessons 11–13).** Any graph mutation still parks the pump under `graphLock` (+ the `!isThisTheMessageThread()` guard). Incremental does not weaken the pump-park contract.
5. **Delta-sync compatibility.** Incremental graph edits must stay consistent with the frontend delta path (clip/track deltas) and undo (one atomic unit per batch).
6. **Kill-switch.** Ship behind a flag (env var, mirroring `HDAW_FORCE_FULL_SYNC`) defaulting to full-rebuild until the incremental path is proven; allows instant rollback.

---

## Candidate approaches (Task 1 selects one)

**Approach A — Tree-diff at rebuild time.** Keep the single `rebuildRoutingGraph` entry point but, instead of `graph.clear()`, diff the current `RoutingManager` state against the `ValueTree` and emit add/remove/move ops. Pros: localized change, preserves the coalescing/undo story. Cons: must invent a stable identity map (ValueTree node ↔ graph node) and handle reorders.

**Approach B — Targeted commands at the command layer.** Each clip/track command (add/remove/move/duplicate/slice) calls a new narrow `RoutingManager::addClip/removeClip/moveClip` instead of triggering a full rebuild; only structural changes (track add/remove, tempo, load) keep the full rebuild. Pros: minimal work per op, no diffing. Cons: every command site must be migrated; higher surface area for a missed path (lesson 10's "works live, breaks on rebuild" twin: "works on full rebuild, breaks on the incremental path").

**Approach C — Hybrid.** Approach B for the hot paths (clip add/remove/move/duplicate), keep full rebuild for the rest. Gate behind the flag.

**Task 1 deliverable:** a spike of the chosen approach on the clip-add path only, benchmarked, with a written recommendation. Do **not** migrate all paths before the spike is signed off.

---

## Dependency Map

- **Blast radius (high):** every clip/track mutation command; the audio graph; potentially the SPSC bridge and PDC.
- **Upstream:** all callers of `rebuildRoutingGraph()` — `AudioEngineCommands_Clips.cpp` (×4), `AudioEngineCommands_Slicing.cpp` (×7), `AudioEngine.cpp` (×4), `AudioEngineCommands_Undo.cpp` (×4), `AudioImport.cpp`, `MidiImport.cpp`, `MainAudioProcessor.cpp` self-trigger (×2).
- **Downstream:** `RoutingManager` (rebuilt wholesale today — this plan refactors it), `Track` processors (state-restoration contract), the audio thread (reads graph under `graphLock`).
- **God nodes in scope:** `MainAudioProcessor::rebuildRoutingGraph`, `RoutingManager::rebuildFromValueTree` / `addTrack` — high-degree hubs; a mistake cascades across every edit.
- **Community boundaries crossed:** command layer ↔ audio graph ↔ (via SPSC) frontend snapshot.
- **Projections affected:** audio graph (primary). ReadModel and frontend snapshot are unchanged in shape but benefit from faster edits.

---

## Files Likely to Change (approach-dependent; finalize after Task 1)

| File | Change |
|------|--------|
| `src/engine/RoutingManager.{h,cpp}` | Add incremental `addClip/removeClip/moveClip` (Approach B/C) or a diff-driven apply (Approach A); preserve `restoreMixerState` on every add |
| `src/engine/MainAudioProcessor.cpp` | Route hot-path triggers to incremental; keep full rebuild behind the flag / for structural ops |
| `src/engine/AudioEngineCommands_*.cpp` | (Approach B/C) call narrow routing ops instead of full rebuild where migrated |
| `tests/...` | New: incremental-equivalence tests, state-preservation-across-incremental, latency-invariance, 128-clip benchmark |

---

## Pitfall Gates Triggered

| Gate | Why | Mitigation |
|------|-----|------------|
| **Gate 1 & 6: State restored / day-one masked bugs** | Incremental add must not reset siblings; the twin of lesson 10 | Every incremental op carries `restoreMixerState` + processor state; test mutates state, does an incremental edit on a *different* node, asserts the live processor unchanged |
| **Gate 3: Audio-Thread Safety** | Graph mutation still races the pump | Reuse the exact `rebuildRoutingGraph` pump-park + `graphLock` pattern (lessons 11–12); no new mutation outside it |
| **Lessons 7 & 8: Latency/quality** | Topology/buffer changes shift latency & fidelity | Measure `getTotalLatency()` before/after; A/B the signal path; assert identical latency |
| **Gate 2: Unimplemented path** | A migrated command that silently keeps calling full rebuild (no-op incremental) | Benchmark must show the speedup; a path that doesn't get faster isn't actually incremental |

## Anti-Pattern Scan
- No `rebuildRoutingGraph()` per-clip in a loop (lesson 6) — the whole point is to remove these.
- No DSP-state write without `stateLock` (lesson 13).
- No graph mutation without the pump-park (lesson 12).
- No new full-tree walks to touch one node (AGENTS.md perf rule 3) — incremental edits use indexed/stable-reference access.

---

## Tasks

### Task 1: Spike + decision (gate for all further work)
- [ ] Prototype the chosen approach (recommend C) on **clip add** only.
- [ ] Benchmark: 128 clips added one-per-tick, full rebuild vs. incremental (wall-clock + audio-dropout count).
- [ ] Equivalence check: render a reference project with full-rebuild vs. incremental; bit-compare the output (or sample-near compare).
- [ ] Write the recommendation (approach chosen, identity-map design, flag name, migration order) and get sign-off before Task 2.

### Task 2: Incremental core + state preservation
- [ ] Implement `RoutingManager` incremental ops for the migrated paths.
- [ ] Restore full processor state on every incremental add (lesson 10).
- [ ] Add `track_mixer_state`-style tests that assert live-processor state survives an incremental edit on a sibling.

### Task 3: Migrate hot paths behind the flag
- [ ] Wire clip add/remove/move/duplicate to incremental; keep slicing/structural/load on full rebuild initially.
- [ ] Add the kill-switch flag (default = full rebuild); confirm both paths work.

### Task 4: Latency/quality + perf gates
- [ ] Measure `getTotalLatency()` before/after — must be identical.
- [ ] A/B critical-listen + render-diff reference project.
- [ ] 128-clip benchmark shows the target improvement (no ~30s stall).
- [ ] Flip the flag default to incremental once all gates pass.

---

## Success Gates (all must pass to declare done)

- [ ] Task 1 spike signed off (approach + identity map documented).
- [ ] Reference project renders **bit-identical** (or sample-near) under full-rebuild vs. incremental.
- [ ] `getTotalLatency()` unchanged before/after (lesson 7).
- [ ] Live-processor mixer/processor state survives an incremental edit on a sibling (lesson 10 regression test green).
- [ ] 128-clip burst benchmark: no ~30s stall; target improvement met.
- [ ] Full `hdaw_tests.exe` green; existing `track_mixer_state_test`, `clip_slicing_test`, `merge_clips_test` unaffected.
- [ ] Kill-switch flag verified both ways (incremental off = today's behavior).
- [ ] `cmake --build build --config Debug` succeeds.

## Verification commands
```
cmake --build build --config Debug
build\Debug\hdaw_tests.exe --gtest_filter=AudioGraphSurface.*:TrackMixerState.*:ClipSlicing.*:MergeClips.*
build\Debug\hdaw_tests.exe
```
