# HDAW Envelope Generation — Session Handoff Prompt

Paste the block below into a new context to resume this work. It assumes a
fresh agent with no memory of the original conversation.

---

```
You are resuming HDAW (JUCE 8 DAW, React 19 frontend, C++20 engine) work on the
envelope-generation feature. Working directory: D:\pdf\roo projects\hdaw3.

MANDATORY first steps:
1. Load the hdaw-guard skill (AGENTS.md requires it before ANY code change).
2. Read docs/plans/2026-08-10-envelope-generation-tools.md — it is the
   canonical plan with success gates (G0–G9), dependency map, pitfall gates,
   and the finalized design decisions. Everything below is a summary of it.
3. Per hdaw-guard §Execution Model: the orchestrating session plans, analyzes,
   and verifies; ALL implementation is dispatched to `general` subagent tasks
   with full briefs (plan, dependency map, pitfall gates, exact files,
   verification commands, report contract). Verify each subagent's diff and
   test evidence yourself before closing a gate.

## Status (verified, gates already closed)
- Unit A0 DONE (G0): beats↔seconds fix on the automation-lane and clip-gain-
  envelope point paths. Files: src/engine/AudioEngineCommands_Automation.cpp
  (3 commands), src/engine/AudioEngineCommands_GainEnvelope.cpp (3 commands),
  src/engine/ReadModelImpl.cpp (3 read sites), src/mcp/McpTools_Audio.cpp
  (add_automation_point handler). Tests: tests/unit/engine/automation_units_test.cpp
  (5 tests). Full suite was 643/0 green.
- Unit A DONE (G1): HDAW::EnvelopeGenerator core. Files:
  src/engine/EnvelopeGenerator.{h,cpp} (pure std, no JUCE, namespace HDAW),
  tests/unit/engine/envelope_generator_test.cpp (12 tests, EnvelopeGenerator.*).
  Full suite was 655/0 green.
- Unit B DONE (G2–G5): 3 envelope generation commands + setClipCcPoints bulk
  writer. Files: src/engine/AudioEngineCommands_Envelope.cpp (4 methods),
  src/engine/AudioEngineCommands.h (3 overrides + private helper),
  src/common/ProjectCommands.h (3 pure virtuals),
  src/frontend/FrontendRouter.cpp (3 dispatch entries + parseShape),
  src/engine/ClipSourceProcessor.h (getGainEnvelopePoints accessor),
  CMakeLists.txt (source registration). Tests:
  tests/unit/engine/envelope_generation_test.cpp (7 tests, EnvelopeGeneration.*),
  tests/unit/frontend/envelope_generation_rpc_test.cpp (5 tests,
  EnvelopeGenerationRpc.*). Full suite 667/0 green.
- Unit C DONE (G6): 4 MCP tools in McpTools_Audio.cpp (list_envelope_shapes,
  generate_automation_envelope, generate_clip_gain_envelope,
  generate_clip_cc_lane). Tests: mcp_functionality_test.cpp (4 tests). Full
  suite 672/0 green.
- Unit D DONE (G7): EnvelopeGenerateControl.tsx + envelopeShapes.ts + CSS,
  wired into AutomationPanel, GainEnvelopeEditor, CCLane. automationStore
  generateEnvelope method. Vitest 294/0 green.
- Finalize DONE (G8–G9): Version bumped to 0.16.0 in CMakeLists.txt and
  frontend/package.json. All gates checked. Knowledge graph refreshed
  (7011 nodes, 18732 edges). Engine 16/16 envelope tests, Vitest 294/0,
  frontend build success.

## Authoritative design decisions (deviate only with justification)
1. Units: RPC/MCP boundary = beats; ValueTree = seconds; automation lane points
   = timeline-absolute seconds; gain envelope = clip-relative seconds;
   CC points = clip-relative BEATS (documented exception — NO conversion).
   beatsToSeconds lives in src/engine/AudioEngineCommands_Helpers.h:41.
2. shapeName() strings (MCP tool depends on them):
   "ramp","adsr","sine","triangle","saw","square","pulse","staircase","sCurve",
   "randomWalk","noise".
3. Per-target value domains for the RPC startValue/endValue params (command
   scales into the normalized generator and back out):
   - generateAutomationEnvelope: 0..1, stored as-is.
   - generateClipGainEnvelope: 0..2 (editor domain); /2 into generator, x2 out.
     Applies to AUDIO clips only (MainAudioProcessor.cpp:269-282 iterates
     getAudioClipSources()).
   - generateClipCcLane: 0..127 int; /127 in, x127 round out.
4. generateAutomationEnvelope: replace the lane's POINTs with startTime in
   [start,end] seconds (inclusive); points outside range untouched; one
   um.beginNewTransaction("generate envelope"); all adds with &um; ONE
   proc->rebuildAutomationCache(trackIndex) at the end; mirror the by-index
   removal style of removeAutomationPoint (AudioEngineCommands_Automation.cpp:88).
5. generateClipGainEnvelope: generate then call the EXISTING bulk writer
   setClipGainEnvelope(clipId, points) (AudioEngineCommands_GainEnvelope.cpp:58,
   already one transaction + notifyClipGainEnvelopeChanged).
6. generateClipCcLane: NEW bulk writer setClipCcPoints(clipId, controllerNumber,
   points) mirroring setClipGainEnvelope: one transaction, remove CC_POINTs for
   that controller in [start,end], add with ProjectModel::allocateCcID()
   (ccID property with nullptr um, the rest with &um — see addCcPoint,
   AudioEngineCommands_Midi.cpp:235-240). CC times stay in beats — pass
   params.startTime/endTime into the generator unmodified (time axis is
   unit-agnostic).
7. G3 (live gain-envelope assertion after rebuildRoutingGraph) needs a NEW read
   accessor: const getGainEnvelopePoints() on ClipSourceProcessor.h — SpinLock-
   guarded copy (envelopeLock), message-thread/test use only.
8. RPC defaults: start=0, end=16 (beats), domain-native values, cycles=1,
   steps=8, phase=0, density=8, smooth=0, seed=0. Unknown shape string ->
   makeError(-32602, "unknown shape ...") — never silent.
9. Interface: add the 3 generate methods as pure virtuals to
   src/common/ProjectCommands.h (#include "../engine/EnvelopeGenerator.h" —
   pure std, no engine dependency leak). AudioEngineCommands overrides.
10. MCP: registerEnvelopeTools in src/mcp/McpTools_Audio.cpp, called from
    registerAudioDomain (McpTools_Audio.cpp:532): list_envelope_shapes,
    generate_automation_envelope, generate_clip_gain_envelope,
    generate_clip_cc_lane. Tools call e->getProjectCommands().generate* and
    surface "lane not found"/errors (findLane helper pattern at
    McpTools_Audio.cpp:311-331).

## Verified reference patterns (file:line)
- RPC dispatch (string->enum + optDouble/optInt<uint64_t> defaults):
  FrontendRouter.cpp:1276-1311 (generatePhrase).
- RPC-layer test harness: frontend::dispatch(engine, method, params) —
  tests/unit/frontend/ghost_clips_rpc_test.cpp:22-30 (mirror this file).
- Live-processor rebuild test: tests/unit/engine/track_mixer_state_test.cpp:6-22
  (engine.getMainProcessor()->rebuildRoutingGraph(); getTrack(0)).
- Automation live cache: Track::getAutomation(i) (getAutomationTree().getProperty
  (IDs::name) to match the lane, getPoints() for values) — AutomationManager.h:90-91.
- Gain-envelope restore on rebuild: RoutingManager.cpp:487-509.
- Command-level test harness: tests/unit/engine/automation_test.cpp:27-41.
- Audio clip creation without a real file: cmds.addAudioClip(0, 0.0, 4.0,
  "test.wav", "Audio") — merge_clips_test.cpp:156.
- CMakeLists: HDAW_lib source list CMakeLists.txt:73-119 (add new .cpp after
  AudioEngineCommands_Automation.cpp at :83); tests/CMakeLists.txt
  add_executable(hdaw_tests ...) explicit list.

## Remaining units (sequential; verify gates before the next)
- Unit B (commands + RPC; gates G2–G5): NEW src/engine/AudioEngineCommands_Envelope.cpp
  (3 generate commands + setClipCcPoints); declarations in AudioEngineCommands.h
  and ProjectCommands.h; 3 dispatch entries in FrontendRouter.cpp;
  ClipSourceProcessor.h accessor; CMakeLists registrations; NEW
  tests/unit/engine/envelope_generation_test.cpp (G2: range replacement +
  one-undo-step + live AutomationManager cache after rebuildRoutingGraph;
  G3: gain envelope live ClipSourceProcessor assertion after rebuild — needs
  the accessor; G4: CC points replaced with real ccIds + one undo) and NEW
  tests/unit/frontend/envelope_generation_rpc_test.cpp (G5: happy path via
  frontend::dispatch + invalid shape -> error).
- Unit C (MCP; gate G6): registerEnvelopeTools + tests in
  tests/integration/mcp/mcp_functionality_test.cpp. Parallelizable with D.
- Unit D (frontend; gate G7): automationStore.generateEnvelope (one RPC, then
  fetchForTrack — no point loops); shared EnvelopeGenerateControl.tsx +
  envelopeShapes.ts; docked Generate row in AutomationPanel.tsx (per lane),
  GainEnvelopeEditor (ClipEditor.tsx:245), CCLane.tsx (PianoRoll);
  CSS theme tokens only (no raw hex, Gate 8); Vitest + one Playwright test
  driving the automation-panel generate flow (poll read.getAutomationPoints).
- Finalize (G8, G9): version bump 0.16.0 in BOTH CMakeLists.txt and
  frontend/package.json; full suites; plan checklist; codebase-memory
  index_repository fast refresh (structural changes).

## Verification commands (Windows PowerShell)
- cmake --build build --config Debug
- build\Debug\hdaw_tests.exe --gtest_filter=<suite>.*
- cd frontend; npm test
- cd frontend; npm run test:e2e
- NEVER test against build/Release/HDAW.exe (stale). Frontend changes reach the
  packaged app only via frontend\build.bat (see AGENTS.md stale-frontend table).

## Pitfall gates in force (from the plan)
- Gates 1/6 (state restore on rebuild): G2/G3 tests assert LIVE processor state
  (AutomationManager cache, ClipSourceProcessor envelope) after
  rebuildRoutingGraph — never only the ReadModel.
- Gate 2 (full path): each gate test asserts tree, ReadModel, AND live
  processor/undo behavior; invalid inputs error, never no-op silently.
- Gate 3 (audio-thread safety): generator + commands are message-thread only;
  zero processBlock changes; the new ClipSourceProcessor accessor must not be
  callable from the audio thread.
- Gate 9: CC points use allocateCcID only; getMainProcessor() null-guarded;
  shape strings validated.
- Anti-patterns: ONE RPC per generate (no addAutomationPoint loops); one undo
  transaction; one rebuildAutomationCache; new .cpp files registered in
  CMakeLists; no DBG (use HDAW_LOG if needed); no floating windows in UI.
```

---

Notes for the resuming agent:
- The original context also verified: default "Volume" lane ships seed points
  at 0.0 and 16.0 SECONDS (ProjectModel.cpp:43-44) — count assertions must use
  a dedicated lane; `AutomationManager::addPoint/updatePoint/removePoint` are
  dead code (zero callers) — do not touch; FrontendTreeWatcher/TreeDeltaAccumulator
  never surface gainEnvelope (fullSync path) — no conversion needed there.
