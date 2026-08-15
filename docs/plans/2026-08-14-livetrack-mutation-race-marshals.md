# Live-Track Mutation Race Marshals (modulation / automation / FX editor) Implementation Plan

> **For agentic workers:** MANDATORY: invoke the `hdaw-guard` skill before any
> code change. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Close the remaining live-Track destruction races —
`MainAudioProcessor::rebuildModulation`, `rebuildAutomationCache`, and
`toggleFXEditor` — using the message-thread marshal proven in the
`rebuildTrackFX` fix (`e68bfc3`, helper `runOnMessageThread` at
`MainAudioProcessor.cpp:76`).

**Root cause (same shape as the landed fix):** all three forward to a live
`Track` on the CALLER's thread while the pump thread's async
`rebuildRoutingGraph` can destroy that Track (`routingManager = std::move(fresh)`).
Thread contexts (grep-verified 2026-08-14):

- `rebuildModulation` — `AudioEngine::initialize` (AudioEngine.cpp:80),
  MODULATION listener branches (AudioEngine.cpp:972 property, :1123 child-added,
  :1245 child-removed), modulation commands
  (AudioEngineCommands_Modulation.cpp:40,59), undo
  (AudioEngineCommands_Undo.cpp:147), RPC router (Router_AudioGraph.cpp:18).
- `rebuildAutomationCache` — automation commands
  (AudioEngineCommands_Automation.cpp: 6 sites), envelope command
  (AudioEngineCommands_Envelope.cpp:119), live-automation-touch listener during
  playback (AudioEngine.cpp:828 — fires on the thread that moved the fader,
  i.e. UI/command; the audio thread never mutates the ValueTree), MCP tools
  (McpTools_Audio.cpp:676,692), undo (:141), RPC (Router_AudioGraph.cpp:17).
- `toggleFXEditor` — RPC router (Router_AudioGraph.cpp:19), undo (:153).
  Bonus correctness: `Track::toggleFXEditor` opens/closes plugin editor
  WINDOWS (Track.cpp:656) — that must be message-thread anyway; the marshal
  fixes affinity, not just the race.

**Safety analysis (why the marshal is safe for these three):**
- No caller is the audio thread (verified above) — Gate 3 unaffected; the
  blocking cross-thread hop only ever runs on command/RPC/MCP/undo/UI threads.
- No caller holds a `MessageManagerLock` (park) — same invariant the landed
  fix relies on (`callFunctionOnMessageThread` jasserts otherwise).
- No re-entrancy under `graphLock`: `RoutingManager::rebuildFromValueTree` →
  `addTrack` calls `Track::rebuildModulation` DIRECTLY (RoutingManager.cpp:705),
  not through the marshaled `MainAudioProcessor` entry points.
- Message-thread callers (pump-thread mutations, e.g. Qt widget paths) execute
  inline via the helper's `isThisTheMessageThread` check.
- Perf: these fire at user pace (dialog edits, automation point drags,
  fader moves during playback ≤ UI rate); the µs–ms message round-trip is
  negligible, same reasoning accepted for the landed FX fix.

**Tech Stack:** C++17 (JUCE 8), gtest.

---

## Success Gates (all must pass)

- [x] **G1:** `cmake --build build --config Debug --target hdaw_tests` succeeds.
- [x] **G2 (lesson 15):** binary lists 5 `TrackFxRebuildRace` tests (2 landed + 3 new).
- [x] **G3:** Suite (5 tests) passes 3× consecutively (~25 s per run).
- [x] **G4:** Affected suites PASS: 76 tests across Automation/AutomationMode/
  AutomationUnits/ModulationFx/Commands/FxSurface/AudioGraphSurface/AudioPoolDedup.
- [x] **G5:** Full suite **815/815 PASS** (8.5 min; = 812 + 3 new). Frontend
  `npm run build` green (version.ts touched).
- [x] **G6:** Diff scan clean: no DBG, no changes beyond the three function
  bodies + one Track accessor + tests + version triple.

**Result (2026-08-14):** landed at version **0.22.3**. Deviations (intent
identical): (1) test uses `dynamic_cast<AudioEngineCommands&>` — the concrete
`addModulation`/`removeModulation` aren't on the `ProjectCommands` interface;
(2) automation asserts are **baseline-relative** — `createDefaultProject`
seeds Volume/Pan/Mute lanes on track 0, so counts assert `baseline+1` /
`baseline` (lesson-9 discipline, an improvement over the plan's guess).

## Dependency Map (verified 2026-08-14, grep)

- **Blast radius:** `src/engine/MainAudioProcessor.cpp` (3 function bodies),
  `src/engine/Track.h` (+1 read-only accessor), tests, version triple.
- **Upstream:** listed above — all non-audio, non-parked threads.
- **Downstream:** `Track::rebuildModulation` (stateLock, unchanged),
  `Track::setAutomationTrees`/`getAutomation(i).rebuildCache()` (unchanged),
  `Track::toggleFXEditor` (unchanged).
- **God nodes in scope:** `MainAudioProcessor` (hub) — confined to forwarding
  bodies; `AudioEngine` listener sites NOT modified (they call the marshaled
  entry points).
- **Projections / SPSC:** none touched.
- **Path integrity (Gate 2):** no new wiring — same call graph, different
  executing thread.

## Pitfall Gates Triggered

- **Gate 3:** verified no audio-thread callers (AudioEngine.cpp:828 fires on
  the fader-moving thread, not the audio thread).
- **Gate 12 / lesson 18:** marshal-not-park, same as the landed fix.
- **Gate 15:** G2 binary listing evidence required.
- **Lesson 9:** test assertions scoped to tracks we control; no absolute
  clip counts.
- **Lesson 20:** orphan-check before blaming hangs.
- **Anti-patterns:** none applicable (no new RPC, no loops, accessor follows
  existing getNum* pattern).

## Steps

### Task 1: Engine marshals

**Files:** `src/engine/MainAudioProcessor.cpp`, `src/engine/Track.h`

- [ ] Wrap the bodies of `rebuildModulation` (~line 527), `rebuildAutomationCache`
  (~line 540), and `toggleFXEditor` (~line 496) in `runOnMessageThread`, with
  ALL pointer re-looks (`routingManager`, `getTrackNode`, `projectModel`)
  INSIDE the callbacks (manager can swap while waiting for the pump).
  Short comment on each pointing at the helper's rationale block.
- [ ] `Track.h`: add public `int getNumModulations() const` next to
  `getNumAutomations()` (line ~70) delegating to
  `modulationManager ? modulationManager->getNumSources() : 0` — read-only
  test accessor, same pattern as the existing getNum* accessors.

### Task 2: Regression tests

**Files:** extend `tests/unit/engine/track_fx_rebuild_race_test.cpp`

- [ ] `TrackFxRebuildRace.RebuildModulationSerializedAgainstAsyncGraphRebuild`:
  ~25 iterations of `addAudioClip(0, …)` (queue async rebuild, NO sleep) then
  `addModulation(0, juce::ValueTree(IDs::MODULATION) with type="lfo" + minimal
  LFO props — check LFOModulationSource::fromValueTree, ModulationSource.h:221,
  for which properties rebuild() actually reads; type defaults to "lfo")`;
  assert live `getTrack(0)->getNumModulations() >= 1` synchronously; then
  `removeModulation(0, 0)`; assert count back to 0.
- [ ] `TrackFxRebuildRace.RebuildAutomationCacheSerializedAgainstAsyncGraphRebuild`:
  same shape: addAudioClip → `addAutomationLane(0, "vol", <paramID>)` +
  `addAutomationPoint(0, "vol", …)` (exact signatures:
  AudioEngineCommands_Automation.cpp:9,57) → assert live
  `getTrack(0)->getNumAutomations() >= 1` → `removeAutomationLane`.
- [ ] `TrackFxRebuildRace.ToggleFXEditorSerializedAgainstAsyncGraphRebuild`:
  addAudioClip → `addFxSlot(0, "eq", 0, "")` → `toggleFXEditor(0, 0)` twice
  (open/close; internal-FX slots log-and-no-op the editor itself,
  Track.cpp:667 — the marshaled lookup path is what we exercise) → assert
  live track non-null + `getNumFXSlots() == 1` → removeFxSlot.

### Task 3: Version bump 0.22.2 → 0.22.3

**Files:** `CMakeLists.txt`, `frontend/package.json`, `frontend/src/version.ts`

### Task 4: Verification

- [ ] Build, list-tests evidence, suite 3×, affected suites, report back.

## Follow-ups (out of scope)

- `rebuildMidiClipCache` (RoutingManager entry called from listeners,
  AudioEngine.cpp:958,1115,1169) has the same map-lookup-vs-swap shape —
  next candidate if this fix proves stable.
- Drain seam for the async graph bake (handoff §4.2) remains open.
