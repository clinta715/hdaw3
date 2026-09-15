# Plan: `project.setFaderAuthoritative` — RPC + MCP tool

## Goal
Add a one-shot tool that makes track faders authoritative again by disabling
all Volume automation lanes on a track (or the whole project). Kills the
recurring "enabled Volume automation overrides the fader in playback/export"
confusion (the "export volume bypass" rabbit hole; Polywave Shift tracks 8/10).
Non-destructive: automation points stay, only `automationEnabled` toggles.
Fully undoable.

## Design
- Engine command: `AudioEngineCommands::setFaderAuthoritative(int trackIndex, bool authoritative)`.
  - `trackIndex == -1` → all tracks (project-wide).
  - Per affected track: iterate `IDs::AUTOMATION_LIST`; for each lane where
    `IDs::name == "Volume"` OR `IDs::paramID == 1` (the volume target), set
    `IDs::automationEnabled = authoritative` under the undo manager.
  - Rebuild automation cache ONCE per affected track (not per lane).
  - Idempotent; out-of-range trackIndex → no-op (mirror `findAutomationLane`).
- RPC: `project.setFaderAuthoritative { trackIndex, authoritative }` in
  `Router_Project.cpp` (mirrors `setAutomationEnabled`).
- MCP: `set_fader_authoritative { trackId (int, -1=all), authoritative (bool) }`
  in `McpTools_Audio.cpp` (same registration function as `set_automation_enabled`,
  ~line 885). MUST call `e->getProjectCommands().setFaderAuthoritative(...)` so
  UI + MCP share ONE command path (AGENTS.md feature-parity contract).
- UI: out of scope for this change (tooling-first; a mixer button can wrap the
  RPC later).

## Files
1. `src/common/ProjectCommands.h` — add virtual (near `setAutomationEnabled`, ~line 165):
   `virtual void setFaderAuthoritative(int trackIndex, bool authoritative) = 0;`
2. `src/engine/AudioEngineCommands.h` — add declaration (~line 189).
3. `src/engine/AudioEngineCommands_Automation.cpp` — implement after
   `setAutomationEnabled` (~line 123). Iterate tracks/lanes per design; use
   `engine_.getProjectModel().getUndoManager()`; call
   `proc->rebuildAutomationCache(trackIndex)` once per affected track.
4. `src/frontend/router/Router_Project.cpp` — add dispatch case near
   `setAutomationEnabled` (~line 351):
   `if (m == "setFaderAuthoritative") { int i; bool b; requireInt trackIndex, requireBool authoritative; c.setFaderAuthoritative(i, b); return {false, Null}; }`
5. `src/mcp/McpTools_Audio.cpp` — register `set_fader_authoritative` after
   `set_automation_enabled` (~line 899), schema:
   `{{"trackId", integer}, {"authoritative", boolean}}`, required both;
   call `e->getProjectCommands().setFaderAuthoritative(trackId, authoritative)`.
6. `tests/unit/engine/automation_test.cpp` — add tests (it has `findLane` +
   `engine.getReadModel().getAutomationLanes()` patterns):
   - `SetFaderAuthoritativeDisablesVolumeLanes`: add a "Volume" (paramID 1) lane
     with a point + a custom lane (paramID 105) on track 0, both enabled;
     `setFaderAuthoritative(0, true)` → read-model shows Volume lane
     `enabled == false`, custom lane still enabled; `setFaderAuthoritative(0, false)`
     → Volume re-enabled.
   - `SetFaderAuthoritativeProjectWide`: `setFaderAuthoritative(-1, true)` →
     Volume lanes on ALL default tracks disabled.
   - `SetFaderAuthoritativeOutOfRangeIsNoOp`: trackIndex 999 → no crash, no change.
7. `tests/integration/mcp/mcp_server_test.cpp` — add an MCP tool test using the
   existing `callTool` helper: `set_fader_authoritative {trackId:0, authoritative:true}`
   → result ok, then verify via the read model that the Volume lane is disabled.

## Success Gates (all must pass with evidence)
- [ ] G1: `build/Debug/hdaw_tests.exe --gtest_filter='Automation.*'` — new tests pass.
- [ ] G2: MCP server suite (the mcp_server_test tests incl. the new
      `set_fader_authoritative` case) passes.
- [ ] G3: Full engine suite passes (exit 0; count grows from 904).
- [ ] G4: Runtime: fixed `build/Debug/HDAW.exe`, load `polywave_shift.hdaw`,
      `project.setFaderAuthoritative {trackIndex:-1, authoritative:true}` →
      `read.snapshot` shows all Volume lanes disabled; export still works.
- [ ] G5: Diff scan — no anti-patterns (no per-lane rebuild loops, one command
      path shared by UI+MCP, no unrelated changes).

## Dependency Map
- Blast radius: `ProjectCommands.h` (interface), `AudioEngineCommands_Automation.cpp`,
  `Router_Project.cpp`, `McpTools_Audio.cpp`, `automation_test.cpp`, `mcp_server_test.cpp`.
- Upstream callers: `dispatchProject` (RPC), the MCP tool. Both call the same
  engine command (no duplicate logic).
- Downstream: ReadModel `getAutomationLanes` (surfaces `enabled`); `rebuildAutomationCache`
  → `AutomationManager` (live processor enabled state).
- Projections: ReadModel (automation change → fullSync, per AGENTS.md — correct).
- SPSC paths: `rebuildAutomationCache` is the existing message→audio path; no new
  audio-thread work.
- Undo: one undo unit via the undo manager (loop of `setProperty` with the same
  manager coalesces).

## Pitfall Gates
- Gate 2 (unimplemented path): RPC → command → ValueTree → rebuildAutomationCache
  → read-model, covered by G1/G2/G4.
- Gate 4/15 (stale binary): G4 verifies the binary.
- Gate 9 (validation): trackIndex bounds-checked (no-op on invalid).
- Lesson 2 (setProperty no-op on unchanged): idempotent — value already at target
  is a no-op, no side-effect reliance.

## Anti-patterns
- Rebuild once per track, not per lane.
- One command path for UI + MCP.
