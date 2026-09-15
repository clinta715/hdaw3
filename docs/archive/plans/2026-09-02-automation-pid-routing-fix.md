# Plan: Fix automation/LFO pid routing (audio-FX lanes ≥ pid 200 dead since e917c1f1)

Date: 2026-09-02 · Engine touch: YES (user-approved discussion 2026-09-02)

## Goal
Restore automation-lane and LFO-target routing to AUDIO FX slots. Since commit
e917c1f1 (2026-08-08, carried as in-flight WIP from docs/plans/2026-08-06-midi-fx-modulation.md),
Track.cpp routes pids >= 200 into midiFxChain, shadowing the audio compound
mapping pid = 100 + slotIndex*100 + paramIndex used by the frontend
(AutomationPanel.tsx:94), by tests (FilterCutoff lane pid 100 works only
because slot 0 < 200), and by recipes (BassSweep pid 400, Riser pid 200,
jungle BreakHP pid 400 — all inert today).

## Root cause
The 2026-08-06 MIDI-FX plan assigned pids 200+ to MIDI FX without noticing
the audio compound for slot 1 param 0 IS 200. The two ranges collide; the
engine checks pid >= 200 first, so every audio-FX lane on slot >= 1
decodes as midiFxChain[(pid-200)/100] (usually empty -> silently dropped).
MIDI-FX lanes via the UI never worked either: AutomationPanel applies the
audio formula to every param (computing garbage pids for MIDI-FX entries);
only ModulationPanel passes the overloaded 200+ pid through (those worked).
No saved project can rely on pid >= 200 meaning MIDI FX except
ModulationPanel-created LFO targets, which keep working under the new range
after the ReadModel change (see below).

## Fix (midiFx range 200+ -> 1000+)
pid semantics after fix:
  1/2/3 = volume/pan/mute · 100..999 = fxChain slot*100+param compound
  1000..1999 = midiFxChain slot*100+param compound (NEW RANGE)

Engine (child A):
1. Track.cpp ~441 (record-currentVal), ~468 (apply lane), ~612 (LFO mod):
   `pid >= 200` -> `pid >= 1000`; `(pid - 200)` -> `(pid - 1000)` (both
   decode lines per branch). Branch order already evaluates the midiFx
   check before `pid >= 100` — keep that order (1000 >= 100 must decode as
   midiFx, not fx slot 10).
2. ReadModelImpl.cpp ~806 (getAutomatableParams, midiFx loop):
   `aps.paramIndex = 200 + si * 100 + p.index` -> `1000 + si * 100 + p.index`
   and update the surrounding comment (paramIndex carries the full pid for
   midiFx entries; bare index for audio slots).
3. New test file tests/unit/engine/automation_pid_routing_test.cpp (ADD TO
   tests/CMakeLists.txt source list — anti-pattern guard):
   - AudioLaneDrivesLiveFilterCutoff: track with filter at slot 1, lane pid
     200 sweeping cutoff, FixedPlayHead + processBlock pattern copied from
     InternalFx.FilterCutoffAutomationSweeps (stress test lines 1124+);
     assert live fxChain[1] cutoff param moved AND band-energy contrast
     (low vs high) like the seam test, AND a coexisting arpeggiator's
     midiFx param 0 is untouched.
   - MidiFxLanePid1000DrivesLiveArp: lane pid 1000 -> live midiFxChain[0]
     getAutomationParam(0) follows lane points; fxChain untouched.
   - LfoTarget1000ModulatesLiveArp: setLfoParam targetParamID 1000, run
     processBlock, assert midiFx param != base.
4. Docs: one short paragraph in docs/adr-automation-model.md documenting the
   three pid ranges (replacing the implicit convention), noting the
   2026-08-06 plan's 200+ range is superseded.

Frontend (child B):
5. AutomationPanel.tsx (lane creation ~line 94 + bound-set ~line 136):
   `paramID = paramIndex >= 1000 ? paramIndex : 100 + slotIndex*100 + paramIndex`
   (midiFx snapshots carry the full pid in paramIndex).
6. ModulationPanel.tsx (~line 52-55 fetchTargets map): same rule so audio-FX
   plugin params compose the 100+ compound (today they emit bare indexes —
   inert targets) and midiFx entries pass 1000+ through.
7. FXChain.tsx lines 663/694 setLastClickedParamID: FIRST verify the param
   list source; if it can include midiFx entries (same getAutomatableParams
   snapshot), apply the same rule; if audio-only, leave and note it.
8. Run cd frontend && npm test; update ModulationPanel.test.tsx /
   AutomationPanel fixtures if they pin old formulas; add/extend a unit test
   covering the new pid composition rule if a helper/component test exists.

## Success gates
- G1: automation_pid_routing_test PASSES (3 tests) on build/hdaw_tests.exe.
- G2: --gtest_filter=*Automation*:*MidiFx*:InternalFx.* all PASS (no
  regression in automation/midi-fx/internal-fx suites).
- G3: PsytranceComposition.DarkForestV5 PASSES; orchestrator re-runs metric
  gates on the fresh render (lanes pid 200/400 now ACTIVE — recipe lane
  values may need retuning; content-only change).
- G4: frontend npm test green.
- G5: full hdaw_tests suite: no new failures vs pre-change baseline (check
  stale engines first, lesson 20).

## Dependency map (grep-exhaustive — literal arithmetic, grep = full enumeration)
Decode: Track.cpp:441/468/612 (only sites reading pid ranges).
Encode: ReadModelImpl.cpp:806; AutomationPanel.tsx:94/136; ModulationPanel.tsx:52;
        FXChain.tsx:663/694 (verify param source). No other compound users.
Storage: lane paramID is an int in the ValueTree — persisted projects
         transparent; audio-FX lanes heal, ModulationPanel midiFx LFO targets
         keep working (ReadModel emits the new range; old saved 200+ LFO
         targets on midiFx params would need re-creation — acceptable: UI
         lanes via AutomationPanel never worked, ModulationPanel midiFx LFOs
         are rare and self-heal on next target edit).
No SPSC/graph topology changes; processBlock branch structure unchanged
(same number of branches, constants only). State-lock idiom untouched.

## Pitfall gates
- Lesson 13 (DSP-state write races): unchanged — same calls, same locks.
- Anti-pattern "new .cpp not in CMakeLists": addressed in step 3.
- PowerShell 5.1: no && in any command; use build-fast.bat path.
- Engine-stability rule: constants-only change in existing branches + new
  test; no new engine complexity; user explicitly approved.


## Outcome (2026-09-02)
All gates GREEN. Engine: Track.cpp 3 decode sites + ReadModelImpl encode site
moved to the 1000-range; ADR documents the pid address space; new suite
AutomationPidRouting (3 tests) pins the decode on LIVE processors. Frontend:
composePid rule in AutomationPanel + ModulationPanel (+4 unit tests);
FXChain verified audio-only, unchanged. Full suite: 1288 tests, 1271 passed,
7 failed — all 7 PROVEN PRE-EXISTING via stash-baseline run (McpServer.
ExportAudioStreamsLongClipWithoutDropouts, McpServer.DiagnosticClapExportMatrix,
PluginManagerInProcessVst3.InstantiatesRealVst3ByIdentifier, RealtimeSafety.
{NaN,Infinite,DCOffset}TripsDetection, RealtimeSafety.DrainProducesLogString).
DarkForestV5 metric gates hold with lanes active (A +8.77 / B 14.3 / C 16.5).
Follow-ups found, not done: MidiFxSlot stores RAW default in initParamCache
but NORMALIZED in loadParamsFromTree (baseline mismatch 0.25 vs 0.1206);
rebuildRoutingGraph scratch processBlock drive runs modulation with no
playhead (pre-saturates LFO targets during rebuild — lesson-21 adjacent).
