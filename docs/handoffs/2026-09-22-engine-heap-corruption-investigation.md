# Handoff: engine heap-corruption crash + "empty project after reconnect" (2026-09-22)

## What happened

During the full-track verification run, after a `set_cells` + `fill_cells` batch
(the first run of the new phrase-cell `tileBeats` tiling), the engine appeared to
"respawn with an empty project": `get_clip` returned 0 notes, then
`tone_verity` failed with "trackIndex out of range" on a 10-track project.

## Evidence chain

1. **Crash capture** (`%TEMP%\hdaw_crash_captures\engine_c5dcc247\`): procdump
   (attached by mcp-launch) caught an **unhandled `C0000374` — STATUS_HEAP_CORRUPTION**
   at 07:58:36 in `HDAW_headless_mcp.exe` (PID 17652), 207 MB dump written.
2. **Dump analysis** (cdb `!analyze -v`): the corruption was DETECTED while freeing
   the `AudioProcessorGraph::NodeID` set inside
   `juce::NodeStates::applySettings` ← `AudioProcessorGraph::Pimpl::handleAsyncUpdate`
   ← `LockingAsyncUpdater` on the message thread (`free_base` → `RtlFreeHeap`).
   Heap corruption is detected at the free — the corrupting WRITE happened earlier.
3. **The "respawn" was not a respawn**: procdump was launched with
   `Kill after dump: Disabled`, so the process continued running in a
   corrupted-heap state. The MCP proxy reconnected to the SAME brain-damaged
   process (PID 17652, created 07:56:42, still alive at investigation time) — the
   "empty project" was corrupted in-process state, not a fresh engine.
4. **Stale-engine zoo (lesson 20, live)**: the shared `hdaw_debug.log` during the
   session carried `LiveClockDiag` lines from THREE pids — a Sep-20 engine
   (31896) and another Sep-22 engine (28512) besides the crashed 17652. All have
   since exited. Unique pipe/shm namespace prefixes (v0.23.2) prevented
   collisions, but the zoo confuses log attribution — always filter by pid.
5. **Render integrity**: the v4 render completed by the corrupted process is
   **byte-identical** to a re-render on a clean engine (`mix_diff`: rmsDb 0,
   peakRatio 1, all band deltas 0). The audio path was unaffected by the
   corruption.

## Root cause status: NOT pinned to a line

Candidates, in order of suspicion:
- (a) `PhraseGenerator::ChordStab`/short-`lengthBeats` + high-density interaction
  with the new tiling call pattern (first run produced 0 notes, then the crash);
  a re-run of the exact same params produced 96 notes and no crash — nondeterministic,
  consistent with heap corruption.
- (b) The lesson-12 family: command-thread graph mutation (end-of-batch
  `graph.rebuild()` in the incremental routing path) racing the pump's
  `LockingAsyncUpdater` dispatch — the crash IS in that dispatch path. The heavier
  tiled fill changed command timing, which could surface a latent race.
- (c) Pre-existing heap bug surfaced by the heavier mutation load.

A single sample is not enough to choose. The dump is preserved at
`engine_c5dcc247\HDAW_headless_mcp.exe_260922_075836.dmp` (207 MB) for deeper
analysis (page-heap/ASAN would be the next step).

## Improvement candidates (require discussion before engine work — standing rule)

1. **Kill after dump = ON** for the procdump crash capture: a process with detected
   heap corruption must not keep serving MCP sessions. This single launcher flag
   would have converted the confusing "empty project" into a clean respawn.
2. Proxy/launcher: treat a post-dump process as dead even when procdump detaches
   (watch for the dump file in the capture dir).
3. Next-step diagnostics if it recurs: page-heap (`gflags /p /enable
   HDAW_headless_mcp.exe /full`), or an ASAN build of the test binary, then re-run
   the fill/tiling sequence to pin the corrupting write.
4. Review the incremental-routing end-of-batch rebuild against the pump-park
   idiom (lesson 12) for the heavier command payloads the tiled cells now produce.

## Recovery notes

- The saved checkpoint survived; the tileBeats re-recipes were lost with the
  corrupted process and were re-applied + saved immediately afterwards
  (checkpoint-save discipline held).
- Session aftermath: fresh engine restarted via `engine_restart`, project
  reloaded (10 tracks / 41 clips), v4b render byte-verifies the v4 output.
