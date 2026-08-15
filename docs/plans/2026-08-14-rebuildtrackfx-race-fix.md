# rebuildTrackFX Race Fix (marshal to message thread) Implementation Plan

> **For agentic workers:** MANDATORY: invoke the `hdaw-guard` skill before any
> code change. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Close the use-after-free window where a command-thread
`rebuildTrackFX`/`rebuildMidiTrackFX` mutates a live `Track` while the pump
thread's async `rebuildRoutingGraph` (or another thread's direct one) destroys
the same `RoutingManager`/`Track` mid-rebuild.

**Root cause:** `MainAudioProcessor::rebuildTrackFX` (MainAudioProcessor.cpp:434)
forwards directly to `routingManager->rebuildTrackFX` on the CALLER's thread
(command/RPC/undo/test — never the message thread in production). Clip/track
tree adds fire `triggerAsyncUpdate` (AudioEngine.cpp:1133) → the pump thread
runs `handleAsyncUpdate → rebuildRoutingGraph`, whose parked section does
`routingManager = std::move(fresh)` (MainAudioProcessor.cpp:519) — destroying
the old manager and every `Track` in it. The FX rebuild's map lookup
(`trackProcessors.find`) and the subsequent `Track::rebuildFXChain` mutation
(can take 100 ms+ with plugin instantiation) race that destruction.
`audio_pool_dedup_test.cpp:227-241` documents the window and dodges it with
`juce::Thread::sleep(50)`.

**Chosen fix — marshal to the message thread (NOT a pump park):**

`MainAudioProcessor::rebuildTrackFX` / `rebuildMidiTrackFX` wrap their work in
`juce::MessageManager::callFunctionOnMessageThread` (synchronous — blocks the
caller until done; executes inline when already on the message thread,
juce_MessageManager.cpp:189-190).

Why this closes the race in every direction:

1. The async rebuild path (`handleAsyncUpdate`) is itself a message callback —
   the pump delivers one message at a time, so it cannot interleave with our
   marshaled callback.
2. Any other thread's direct `rebuildRoutingGraph` must park the pump first
   (`MessageManagerLock` precedes `graphLock` in the reference
   implementation). JUCE's park posts a `BlockingMessage` that suspends the
   message thread inside the holder's callback (juce_MessageManager.cpp:302-310)
   — so (a) the park cannot be acquired while our callback runs (its acquire
   message is undelivered until we return to the loop), and (b) while another
   thread holds the park, our callback simply waits, then runs against the
   post-swap manager (fresh lookup inside the callback). No destruction can
   overlap our mutation from any direction.

Why NOT the Gate-12 park idiom (lesson 18): parking inside `rebuildTrackFX`
while `Track::rebuildFXChain` instantiates plugins IN-PROCESS self-deadlocks —
`AudioPluginFormat::createInstanceFromDescription` dispatches to the message
thread, which the park suspends (the exact trap documented at
`RoutingManager::prebuildTracks`, RoutingManager.cpp:138-157). On the message
thread that dispatch executes inline, so marshaling respects lesson 18 without
a two-phase staging refactor of `Track::rebuildFXChain`.

**Tech Stack:** C++17 (JUCE 8), gtest.

---

## Success Gates (all must pass)

- [x] **G1:** `cmake --build build --config Debug --target hdaw_tests` succeeds.
- [x] **G2 (lesson 15):** `--gtest_list_tests` on the fresh binary shows the new
  `TrackFxRebuildRace` suite (binary evidence, not source evidence).
- [x] **G3:** New regression test passes deterministically — 3× consecutive
  runs, all PASS (4.4 s / 4.3 s / 6.9 s per test).
- [x] **G4:** Affected suites PASS: `AudioPoolDedup.*` + `FxSurface.*` +
  `AudioGraphSurface.*` + `ModulationFx.*` (24 tests), `Commands.*` (40 tests).
- [x] **G5:** Full suite **812/812 PASS** (8.2 min; = 807 baseline + 3 from
  Subsystem D's gate tests + 2 new). Frontend `npm run build` also green
  (version.ts touched).
- [x] **G6:** Diff scan clean: no DBG macro, no engine-API additions beyond the
  two function bodies, comment block explains the marshal rationale.

**Result (2026-08-14):** landed at version **0.22.2** (version triple bumped:
CMakeLists.txt, frontend/package.json, frontend/src/version.ts). One compile
deviation: helper param `Fn&` → `Fn&&` (forwarding ref, MSVC C2664). Test 2
queues its async rebuilds via `addMidiClip` (MIDI clips need no wav).

## Dependency Map (verified 2026-08-14, graph + grep)

- **Blast radius:** `MainAudioProcessor.cpp` (two function bodies), new test
  file + `tests/CMakeLists.txt` registration, version triple bump. No headers
  change signature.
- **Upstream (callers):** `AudioEngineCommands_Fx.cpp:55,90,254,265,295,320,334`
  and `AudioEngineCommands_Undo.cpp:135` (rebuildTrackFX);
  MIDI-FX command sites in the same files (rebuildMidiTrackFX);
  RPC router `Router_AudioGraph.cpp:16`. All run on command/RPC/undo/test
  threads or the message thread — none on the audio thread, none inside a
  parked (`MessageManagerLock`) section. Synchronous semantics are preserved
  (`callFunctionOnMessageThread` blocks), so no caller behavior changes.
- **Downstream:** `RoutingManager::rebuildTrackFX` → `Track::rebuildFXChain` /
  `rebuildMidiFXChain` / `rebuildModulation` — unchanged. `jassert
  (!currentThreadHasLockedMessageManager())` inside
  `callFunctionOnMessageThread` is safe: no caller holds the park.
- **God nodes in scope:** `MainAudioProcessor` (hub) — change is confined to
  two small forwarding functions.
- **Projections affected:** none new. The state-harvest
  `setProperty(pluginState)` (Track.cpp:121) now fires listeners on the message
  thread — pre-existing behavior via the `StretchCache::entryReady` →
  `rebuildRoutingGraph` path (AudioEngine.cpp:67-72), so listener-thread
  diversity is not new.
- **SPSC paths touched:** none.
- **Path integrity (Gate 2):** no new wiring — identical call graph, different
  executing thread.

## Pitfall Gates Triggered

- **Gate 3 (audio-thread safety):** N/A — no `processBlock` changes; the
  marshal adds no locks on the audio path (`Track::stateLock` remains the only
  audio-adjacent lock, unchanged).
- **Gate 11 (message pump):** every process starts `MessagePumpThread` before
  JUCE construction (`test_main`), so the marshal target always exists; a
  null-`MessageManager` fallback keeps the old direct-call behavior if it ever
  doesn't.
- **Gate 12 / lesson 18:** explicitly addressed by choosing the marshal over
  the park (see above).
- **Gate 15 (stale binaries):** G2 requires `--gtest_list_tests` evidence
  from the built binary.
- **Lesson 20:** if suites hang on spawn/READY, check orphaned
  `hdaw_plugin_host.exe` before blaming code; do not kill
  `HDAW_headless_mcp.exe`.
- **Anti-pattern scan:** no new RPC, no batchable-loop pattern, no DBG, no
  CSS, new test `.cpp` registered in `tests/CMakeLists.txt`.

## Steps

### Task 1: Engine fix

**Files:** `src/engine/MainAudioProcessor.cpp` (only)

- [ ] Add a file-local helper (anonymous namespace, above the function) that
  runs a callable on the JUCE message thread, blocking until it completes:
  `MessageManager::getInstanceWithoutCreating()`; if null or
  `isThisTheMessageThread()`, run inline; else
  `callFunctionOnMessageThread` with a static trampoline + stack context.
  Wrap the callable body in try/catch → `std::exception_ptr`, rethrow on the
  calling thread after (mirror `PluginHost::runLifecycleOnMessageThread`,
  PluginHost.cpp:752-770).
- [ ] Convert `MainAudioProcessor::rebuildTrackFX` (line 434) to marshal the
  `routingManager != nullptr → routingManager->rebuildTrackFX(trackIndex)`
  body (re-checking `routingManager` INSIDE the callback — it may be swapped
  by a rebuild that completed while we waited for the pump).
- [ ] Convert `MainAudioProcessor::rebuildMidiTrackFX` (line 440) the same way.
- [ ] Comment block at the helper explaining: the race, why message-thread
  execution closes it in all directions, and why a pump park was rejected
  (lesson 18 in-process instantiation self-deadlock).

### Task 2: Regression test

**Files:** new `tests/unit/engine/track_fx_rebuild_race_test.cpp`,
`tests/CMakeLists.txt` (explicit test list, ~line 49)

- [ ] New suite `TrackFxRebuildRace.RebuildTrackFXSerializedAgainstAsyncGraphRebuild`:
  `AudioEngine engine; engine.initialize();` then loop ~25 iterations, each:
  `addAudioClip(0, 0.0, 1.0, <sine wav>, name)` (queues the coalesced async
  rebuild — NO sleep), immediately `addFxSlot(0, "eq", 0, "")` from the test
  thread, then assert synchronous semantics: `engine.getMainProcessor()
  ->getTrack(0)` is non-null and exposes ≥1 FX slot (use the actual accessor
  from Track.h) the moment the command returns; then `removeFxSlot(0, 0)` to
  exercise the remove path against a possibly-pending rebuild. Reuse the
  writeSineWav helper pattern from audio_pool_dedup_test.cpp (scope
  assertions to track 0 — lesson 9: default project has 3 tracks, empty clip
  lists).
- [ ] Second test `TrackFxRebuildRace.RebuildMidiTrackFXSerialized` — same
  shape with `addMidiFxSlot` (assert the MIDI FX accessor on the live track).

### Task 3: Version bump (repo convention: functional change → patch bump)

**Files:** `CMakeLists.txt` (`project(HDAW VERSION ...)` → 0.22.2),
`frontend/package.json` (`"version"`), `frontend/src/version.ts`

### Task 4: Verification

- [ ] Build, list-tests evidence, new suite 3×, affected suites, report back.

## Follow-ups (out of scope, documented)

- `rebuildModulation` (AudioEngine.cpp:1123 listener call site) and
  `rebuildAutomationCache` mutate live Tracks from non-message threads with
  the same destruction-race shape; same one-line marshal recipe applies.
  Deferred: no plugin instantiation involved, and they may sit on hotter paths
  (modulation edits) — measure the message round-trip cost first.
- `toggleFXEditor` (MainAudioProcessor.cpp:424) has the same race and should
  be message-thread-affine anyway (editor windows).
- No public drain seam for the async graph bake (handoff §4.2) — the sleeps
  in audio_pool_dedup_test.cpp remain.
