# Plan: Respawn-storm fixes (Fix A–D) — 2026-08-16

Source handoff: `docs/handoffs/2026-08-16-respawn-storm-root-cause.md` (diagnosis
complete + live-verified; zero code changes). HEAD `730839c`, tree clean. This plan
dispatches the handoff §5 fix set to ONE implementation subagent.

## Goal

Stop the plugin respawn-storm by (A) releasing the stale render sequence after a full
routing rebuild, (B) adding a global respawn circuit breaker, (C) validating/re-resolving
respawn plugin paths, (D) logging flag reasons — with regression/unit tests and a lesson 21.

## Success Gates (all must pass with evidence)

- [ ] G1 Build: `cmake --build build --config Debug` succeeds AND binaries relinked
      (lesson 15 — check `hdaw_tests.exe`/`HDAW.exe` timestamps newer than the edited `.obj`s).
- [ ] G2 Fix A / T1: new gtest passes — after `rebuildRoutingGraph(true)` the previous
      graph's isolated `hdaw_plugin_host.exe` children are released (count returns to
      baseline) WITHOUT needing to play.
- [ ] G3 Fix B / T2: `CrashRecoveryManager` storm test — 20 distinct slot crashes, one
      `tick()` → respawn attempts ≤ budget (default 8) and remaining entries still pending.
- [ ] G4 Fix C / T3: `resolveRespawnPath` (extracted helper) resolves a known identifier
      to a file path and rejects an unknown identifier (no spawn / failure).
- [ ] G5 Fix D: `checkAllChildren` emits an `HDAW_LOG("CrashRecovery", ...)` reason for
      each flagged slot (exit-code / GetExitCodeProcess-failed / stalled). Verified by review.
- [ ] G6 Full suite: `build/Debug/hdaw_tests.exe` passes after killing stale engines
      (lesson 20). The known-flaky five (lesson 20 list) must be re-run after cleanup
      before being blamed.
- [ ] G7 Docs: lesson 21 appended to `AGENTS.md` (≤15 lines, style of lesson 20).
- [ ] G8 Hygiene: no logging/locks/allocs added to the audio thread; Fix A OUTSIDE
      `graphLock` and OUTSIDE the pump-park; `HDAW_LOG` not `DBG`; any new test `.cpp`
      added to `tests/CMakeLists.txt`; no RPC/frontend changes.

## Dependency Map (verified via grep + trace_path + reads)

- **Fix A** — `MainAudioProcessor::rebuildRoutingGraph` (`src/engine/MainAudioProcessor.cpp:585`).
  - Blast radius: HIGH — `rebuildRoutingGraph` is a hub (~45 refs). Upstream callers:
    `AudioEngine.cpp` (97/785/930/1392/1433), `AudioEngineCommands_Undo.cpp:112` (loadProject),
    `AudioEngineCommands_Clips.cpp`, `AudioEngineCommands_Slicing.cpp`, `AudioImport.cpp:121`,
    `MidiImport.cpp:144`, `McpTools_Project.cpp:1280`, `Router_AudioGraph.cpp:15`,
    `Router_Composition.cpp:84`.
  - Downstream: `routingManager->rebuildGraph()` → `juce::AudioProcessorGraph::rebuild()`.
  - Projection affected: audio graph (render sequence). No ReadModel/frontend change.
  - God-node risk: rebuildRoutingGraph is high-degree; keep the change to a single added
    call + park release, no restructuring.
- **Fix B** — `CrashRecoveryManager.{h,cpp}`. Upstream: `PluginManager::timerCallback`→`tick()`,
  proxy crash callbacks→`onSlotCrashed`. Downstream: `respawnFn`→`PluginManager::respawnIsolatedSlot`.
- **Fix C** — `PluginManager::respawnIsolatedSlot` (`src/engine/PluginManager.cpp:877`);
  mirror `resolveIdentifierToPath` (L736). Upstream: `CrashRecoveryManager` respawnFn.
  Downstream: `proxyProcessMgr->spawnPluginHost`.
- **Fix D** — `ProxyProcessManager::checkAllChildren` (`src/proxy/ProxyProcessManager.cpp:228`),
  health-monitor thread (`startHealthMonitor` L319). NOT the audio thread — logging safe.
- SPSC paths touched: none new. Cross-community: engine↔proxy seam (Fixes C/D) — interface
  is the existing spawn/kill/callback API, unchanged.

## Pitfall Gates Triggered

- **Gate 10/1 (rebuild seam):** Fix A edits `rebuildRoutingGraph`. T1 must assert LIVE
  observable behavior (child-process count), not just no-crash.
- **Gate 3 (audio-thread):** add NO logging/locks/allocs/string-ops to `processBlock` or
  anything it calls. Fix D logs only on the health thread.
- **Gate 12 (graph mutation off message thread):** Fix A calls `graph.rebuild()` via
  `rebuildGraph()`. Off-message-thread this is `triggerAsyncUpdate()` (non-blocking); on-thread
  it is synchronous `handleAsyncUpdate()`. Do NOT add a `MessageManagerLock`; never take one
  on the message thread.
- **Gate 18 (no plugin work while parked):** Fix A MUST run OUTSIDE the pump-park section.
  Release the park (`pumpPark.reset()`) before calling `rebuildGraph()`.
- **Gate 14 (respawn/isolation):** Fix C must let `__`-prefixed test sentinels
  (`__passthrough__`) pass through untouched; only re-resolve real identifier strings.
- **Gate 15 (stale binaries):** verify relink before trusting test results.
- **Gate 20 (stale engines):** kill stale `HDAW*`/`hdaw_plugin_host` before proxy/crash suites.

## Fix A placement (exact)

In `rebuildRoutingGraph`, after `graphLock.exit()` + `graphRebuildPending.store(false)`,
release the park then re-bake:

```cpp
graphLock.exit();
graphRebuildPending.store(false, std::memory_order_release);

// Release the pump park BEFORE re-baking (lesson 18): the bake may touch node
// processors; plugin work must not run while the pump is parked.
pumpPark.reset();

// Fix A (lesson 21): force a render-sequence re-bake so the sequence baked during
// the PREVIOUS playback (pinning old Node::Ptr -> Tracks -> FX slots -> plugin
// proxies -> child processes) is released. graph.clear() alone leaves it pinned
// until the next processBlock re-bake, so a load with stopped transport leaked the
// whole previous graph's plugin children. Outside graphLock + outside the park.
// Mirrors the drain-path precedent (AudioEngine.cpp:1491).
if (routingManager != nullptr)
    routingManager->rebuildGraph();

recomputeProjectEndSample();
```

## ⚠ T1 mechanism risk (orchestrator analysis — READ before implementing T1)

JUCE releases the OLD render sequence only via a two-step handshake in
`RenderSequenceExchange` (`juce_AudioProcessorGraph.cpp:1629`):
1. `handleAsyncUpdate()` bakes a NEW sequence → `set()` → `mainThreadState`, `isNew=true`.
2. The AUDIO thread's `processBlock`→`updateAudioThreadState()` swaps it into
   `audioThreadState` (the old sequence lands in `mainThreadState`), then a 500 ms
   `Timer` clears `mainThreadState` when `!isNew`.

The leak lives in `audioThreadState`, which is swapped ONLY by the audio thread running
`graph.processBlock`. `MainAudioProcessor::processBlock` early-outs (before
`graph.processBlock`) when transport is stopped, so with stopped transport the swap never
happens. **`graph.rebuild()` alone updates `mainThreadState`, not `audioThreadState`** —
so if T1 fails with Fix A as written, the release is not happening and you must close the
handshake. Fallbacks to investigate (pick the minimal, thread-safe one):
- After `rebuildGraph()`, drive one `graph.processBlock` on a scratch buffer while the
  sequence swap can occur (respect graphLock + transport state), OR
- Force the exchange to install the new sequence without waiting for playback, OR
- Confirm whether the bake's `set(nullptr)`/signature path already clears the pin in this
  JUCE build (measure, don't assume).
T1 is the arbiter: it measures real `hdaw_plugin_host.exe` counts before/after
`rebuildRoutingGraph(true)`. If counts don't return to baseline, the fix is incomplete —
do NOT weaken T1 to make it pass.

## Steps (subagent)

1. Fix A (MainAudioProcessor.cpp) per placement above.
2. Fix B (CrashRecoveryManager.{h,cpp}): sliding-window budget (default 8/30000ms; env
   `HDAW_RESPAWN_BUDGET`/`HDAW_RESPAWN_WINDOW_MS`). When exhausted: skip `respawnFn`,
   leave entry pending (`nextRetryMs=now+2000`), `HDAW_LOG` once per tick batch. Record a
   timestamp only for respawns actually attempted. Keep `respawnEnabled` gate first.
3. Fix C (PluginManager.cpp): extract `resolveRespawnPath`; in `respawnIsolatedSlot`, if
   path doesn't end `.vst3`/`.clap` (case-insensitive) and doesn't start `__`, re-resolve
   via `matchesIdentifierString` (mirror `resolveIdentifierToPath`). Resolved→spawn+log;
   unresolvable→log+`return false`.
4. Fix D (ProxyProcessManager.cpp): log reason per flagged slot in `checkAllChildren`.
5. Tests: T1 (new file, add to tests/CMakeLists.txt), T2, T3. Model T1 on
   `tests/unit/common/commands_test.cpp` / `tests/unit/engine/audioengine_read_facade_test.cpp`
   (construct `AudioEngine`) + `tests/unit/proxy/crash_recovery_test.cpp` (isolated
   `__passthrough__` child, Toolhelp32 process count).
6. Lesson 21 in AGENTS.md.
7. Build (G1), run new tests, then full suite (G6) after killing stale engines.

## Verification commands

- `cmake --build build --config Debug`
- `build/Debug/hdaw_tests.exe --gtest_filter=*RenderSequenceRelease*` (T1),
  `--gtest_filter=CrashRecovery.*` (T2/others), `--gtest_filter=*RespawnPath*` (T3)
- `Get-Process HDAW_headless,HDAW,hdaw_plugin_host -EA SilentlyContinue | Stop-Process -Force`
  (before proxy/crash suites — lesson 20)
- `build/Debug/hdaw_tests.exe` (full)

Do NOT commit.
