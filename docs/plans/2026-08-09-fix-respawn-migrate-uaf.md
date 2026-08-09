# Fix respawn→migrate use-after-free (re-enable DiagnosticClapExportMatrix)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Close the use-after-free in `PluginManager::respawnIsolatedSlot` where `killPluginHost` frees the old `ShmRegion` while the audio thread can still read the proxy's dangling `shmHandle`. Re-enable the `McpServer.DiagnosticClapExportMatrix` stress test.

**Tech Stack:** C++17, JUCE 8, GTest, plugin process-isolation (proxy host)

---

## Root Cause Summary

The graphLock fix in commit `82f2d78` protected the shared_ptr *assignment* in `migrateToNewSlot`, but **not the free**. In `src/engine/PluginManager.cpp` the order is:

1. `:736` `killPluginHost(oldSlotId, KillHard)` → frees the old `ShmRegion` memory
2. `:740`–`:749` spawn new host + build a non-owning `shared_ptr<ShmRegion>` (no-op deleter)
3. `:756`–`:758` acquire `graphLock` only here, around `migrateToNewSlot`

Between step 1 and step 3 the audio thread can still run `processBlock`. The proxy's `shmHandle` still points at the **already-freed** old region (the shared_ptr's no-op deleter means refcount does not keep the memory alive). `PluginProxySlot::processBlock` copies `shmHandle` (`:329`) and dereferences `shm->getHeader()` (`:330`, `:335`) → **use-after-free**.

The single-plugin export path (`ExportAudioWithClapPluginDoesNotHang`) never triggers respawn, so it stays green. The bug only surfaces under crash recovery during active playback — exactly the path that must be reliable.

The disabled-test comment (`tests/integration/mcp/mcp_server_test.cpp:561-568`) documents this exactly.

---

## Dependency Map

- **Blast radius:** crash-recovery path only. Normal playback, single-plugin export, and non-isolated plugins are unaffected.
- **Upstream:** `CrashRecoveryManager` timer → `PluginProxySlot::onChildCrashed` → `PluginManager::respawnIsolatedSlot` (`PluginManager.cpp:47`).
- **Downstream (the race participants):**
  - Audio thread: `MainAudioProcessor::processBlock` → `graph.processBlock` (`:215-226`, `graphLock.tryEnter()`, on failure → silence) → `Track::processBlock` → `PluginProxySlot::processBlock` (`:329` reads `shmHandle`).
  - Message thread: `respawnIsolatedSlot` (`:721`) → `killPluginHost` / `migrateToNewSlot`.
- **The shared lock:** `graphLock` lives on `MainAudioProcessor`. The audio thread does `tryEnter()` (non-blocking, skips to silence on contention); `PluginManager` calls `graphLockPtr->enter()` (blocking) — set via `AudioEngine` → `PluginManager::setGraphLock` (commit `82f2d78`). This is the intended serialization mechanism.
- **Projections affected:** audio graph only (no ValueTree / ReadModel / frontend change).
- **SPSC paths touched:** none new. The fix narrows an existing race window.

---

## Files to Modify

| File | Change |
|------|--------|
| `src/engine/PluginManager.cpp` | Reorder `respawnIsolatedSlot`: spawn+getShm **before** lock; acquire `graphLock` **before** `killPluginHost`; hold across `migrateToNewSlot`; release; then state-restore + `prepareToPlay` after unlock |
| `tests/integration/mcp/mcp_server_test.cpp` | Remove `DISABLED_` from `DiagnosticClapExportMatrix` (line 569) |
| `tests/unit/proxy/crash_recovery_test.cpp` | Add focused UAF regression: respawn while sustained `processBlock` is running; assert no crash + audio resumes on the new region |

## The fix (core diff shape)

Current (`PluginManager.cpp:734-758`):
```cpp
auto* proxy = it->second;
proxyProcessManager.killPluginHost(oldSlotId, KillMode::KillHard);   // FREES OLD REGION (UAF window opens)
auto newSlotId = nextProxySlotId.fetch_add(1, std::memory_order_relaxed);
if (!proxyProcessManager.spawnPluginHost(pluginPath.toStdString(), newSlotId)) return false;
proxyProcessManager.setSlotCrashCallback(newSlotId, [proxy](uint32_t){ proxy->onChildCrashed(); });
auto* rawShm = proxyProcessManager.getShm(newSlotId);
if (!rawShm) return false;
auto newShm = std::shared_ptr<proxy::ShmRegion>(rawShm, [](proxy::ShmRegion*){});
if (graphLockPtr) graphLockPtr->enter();                            // lock acquired TOO LATE
proxy->migrateToNewSlot(newSlotId, newShm);
if (graphLockPtr) graphLockPtr->exit();
```

New order:
```cpp
auto* proxy = it->second;

// 1. Spawn the NEW host first (process creation can be slow — never under graphLock,
//    which would starve the audio callback for the spawn duration).
auto newSlotId = nextProxySlotId.fetch_add(1, std::memory_order_relaxed);
if (!proxyProcessManager.spawnPluginHost(pluginPath.toStdString(), newSlotId)) return false;
proxyProcessManager.setSlotCrashCallback(newSlotId, [proxy](uint32_t){ proxy->onChildCrashed(); });
auto* rawShm = proxyProcessManager.getShm(newSlotId);
if (!rawShm) { proxyProcessManager.killPluginHost(newSlotId, KillMode::KillHard); return false; }
auto newShm = std::shared_ptr<proxy::ShmRegion>(rawShm, [](proxy::ShmRegion*){});

// 2. Under graphLock: kill the old host (frees old region) + migrate (swap shmHandle
//    to the new region). The audio thread's MainAudioProcessor::processBlock does
//    graphLock.tryEnter(); on failure it returns silence (MainAudioProcessor.cpp:227),
//    so it cannot read the freed old shmHandle between kill and migrate.
if (graphLockPtr) graphLockPtr->enter();
proxyProcessManager.killPluginHost(oldSlotId, KillMode::KillHard);
proxy->migrateToNewSlot(newSlotId, newShm);
if (graphLockPtr) graphLockPtr->exit();

// 3. After unlock: state restore + prepareToPlay use bounded IPC (up to 5s) and must
//    NOT hold graphLock (would starve audio). The capacity==0 guard in
//    PluginProxySlot::processBlock (:337) returns silence safely until PREPARE lands.
auto stateBlock = proxy::PluginProxySlot::loadStateForOldSlotId(oldSlotId);
if (stateBlock.getSize() > 0)
    proxy->setStateInformation(stateBlock.getData(), (int)stateBlock.getSize());
proxy->prepareToPlay(lastSampleRate, lastBlockSize);
```

**Why `KillHard` under the lock is safe:** `KillGraceful` is now non-blocking (`TerminateProcess` + sentinel, commit `0d6f9f4`), and `KillHard` is an immediate `TerminateProcess`. The sentinel exit code is recognized by the health monitor as intentional, so `onChildCrashed` does not re-fire during the kill.

---

## Pitfall Gates Triggered

| Gate | Why | Mitigation |
|------|-----|------------|
| **Gate 3: Audio-Thread Safety** | Extending the `graphLock` critical section | The critical section is kill (immediate TerminateProcess) + migrate (two atomic stores + shared_ptr move) — microseconds. No allocation/IPC inside the lock. Spawn is deliberately **outside** the lock. |
| **Gate 1: State Restored on Rebuild** | Respawn recreates the processor | Already handled: `setStateInformation` + `prepareToPlay` restore plugin state on the new child. Test asserts audio resumes non-silent on the new region. |
| **Gate 2: Unimplemented Code Path** | The new `getShm` failure branch kills the new host | New early-return also kills the just-spawned host to avoid a leaked child process. |

## Anti-Pattern Scan

- No new `rebuildRoutingGraph()` calls (this path never rebuilds).
- No new `setProperty` reliance.
- No graph mutation outside the established `graphLock` pattern (this *fixes* a graph-mutation race — AGENTS.md lesson 12).
- No frontend / CSS changes.

---

## Tasks

### Task 1: Reorder `respawnIsolatedSlot` so the lock covers the free
- [ ] Edit `src/engine/PluginManager.cpp:734-758` to the new order above (spawn+getShm before lock; kill+migrate under lock; state+prepare after).
- [ ] In the new `getShm` failure branch, `killPluginHost(newSlotId, KillHard)` before returning false (no leaked child).
- [ ] Verify `graphLockPtr` is non-null in the test environment (it is wired from `AudioEngine`; the existing `CrashRecovery.*` tests already exercise respawn).

### Task 2: Re-enable the stress test
- [ ] `tests/integration/mcp/mcp_server_test.cpp:569` — remove `DISABLED_` prefix → `TEST(McpServer, DiagnosticClapExportMatrix)`.
- [ ] Keep the fail-on-silence `EXPECT_GT` per-plugin so the summary doubles as the discovery table.

### Task 3: Add a focused UAF regression test
- [ ] In `tests/unit/proxy/crash_recovery_test.cpp`, add `CrashRecovery.RespawnDuringActiveProcessing`:
  - Build a proxy slot, spawn host, `prepareToPlay`, drive `processBlock` on a background thread (sustained loop writing input / reading output).
  - Trigger respawn (simulate crash / call `respawnIsolatedSlot` directly) **while** the background `processBlock` loop is running.
  - Assert: no access violation (test completes), and after respawn the next `processBlock` produces non-silent output from the new region.
- [ ] This is the gate that proves the window is closed — without it the fix is unverifiable.

---

## Success Gates (all must pass to declare done)

- [ ] `CrashRecovery.RespawnDuringActiveProcessing` passes (no crash, audio resumes on new region).
- [ ] `McpServer.DiagnosticClapExportMatrix` passes (re-enabled) — non-silence per available CLAP plugin.
- [ ] `CrashRecovery.*` and `ProxyHealth.*` suites green (existing: `AutoRespawnAfterCrash`, `GivesUpAfterThreeFailures`, `IdleChildNotKilledByStallDetector`).
- [ ] `McpServer.ExportAudioWithClapPluginDoesNotHang` still green (no regression in the single-plugin path).
- [ ] Full `build/Debug/hdaw_tests.exe` run: no new failures vs. baseline (only the known `PluginIsolation.*` set tracked separately in the triage plan).
- [ ] `cmake --build build --config Debug` succeeds.

## Verification commands

```
cmake --build build --config Debug
build\Debug\hdaw_tests.exe --gtest_filter=CrashRecovery.*:ProxyHealth.*:McpServer.DiagnosticClapExportMatrix:McpServer.ExportAudioWithClapPluginDoesNotHang
build\Debug\hdaw_tests.exe
```
