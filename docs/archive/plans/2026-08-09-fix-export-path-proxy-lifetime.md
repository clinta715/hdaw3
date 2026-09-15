# Fix export-path proxy lifetime + respawn-during-export race

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Close the export-path use-after-free that crashes `McpServer.DiagnosticClapExportMatrix` (and any export where an isolated plugin's child crashes), so the test can be re-enabled. Root cause: export's transient plugin proxies are registered in the **shared** `PluginManager::liveProxySlots` map and share the **shared** `CrashRecoveryManager`, but the proxy destructor never deregisters — so export teardown leaves dangling entries a pending respawn then dereferences.

**Tech Stack:** C++17, JUCE 8, GTest, plugin process-isolation

---

## Root Cause Summary

`ExportManager::renderThreadFunc` builds its own local `renderGraph` + `RoutingManager` (`ExportManager.cpp:76,119`) but passes the **shared** `PluginManager*`. So when the export graph instantiates isolated plugins, `createPluginInstance` (`PluginManager.cpp:540`) registers each `PluginProxySlot*` in the shared `liveProxySlots` and wires crash callbacks to the shared `crashRecovery` (`:523,525,535,537`).

The crash chain:
1. During export a child crashes → `crashRecovery->onSlotCrashed(sid)` enqueues a respawn entry.
2. Export ends → `renderGraph.releaseResources()` + stack unwind destroy `routingManager`/`renderGraph` → each `PluginProxySlot` destructor runs (`PluginProxySlot.cpp:44-53`). It removes the *process* crash callback and kills the host — but **never erases itself from `liveProxySlots`** and **never cancels the pending recovery entry**.
3. The live `PluginManager` Timer fires `crashRecovery->tick()` (`PluginManager.cpp:717`) → `attemptRespawn` → `respawnIsolatedSlot(sid)` → `liveProxySlots.find(sid)` returns the **dangling** pointer → dereference → `0xC0000005`.

A second, latent race: `ExportManager::renderThreadFunc`'s `renderGraph.processBlock` (`:189`) takes no lock, so a mid-export respawn's `killPluginHost`+`migrateToNewSlot` (free+swap) can race the in-flight export read — the export-path twin of the bug closed in `2026-08-09-fix-respawn-migrate-uaf.md`.

A third issue (no crash): some CLAP instruments (Odin2) render silence on isolated export — handled as its own investigation task.

### Why the design is wrong (the invariant)
`liveProxySlots` is an **observer registry** (raw pointers to objects the graph owns), but it's never kept consistent with proxy lifetime, has **no mutex**, and crash recovery has **no cancellation**. Three independent defects all stem from that.

---

## The fix (three layers)

**Layer 1 — make `liveProxySlots` a correct, thread-safe registry:**
- Add `std::mutex liveProxySlotsMutex_` to `PluginManager` (private).
- Guard every access: the insert in `createPluginInstance` (`:540`), and the find/erase/insert in `respawnIsolatedSlot` (`:731-732,784-785`). (Concurrent insert from export's render thread + read/erase from the message-thread Timer is already a latent data race — this fixes it.)
- Add a destruction callback. The proxy already uses a callback-injection pattern (`setCrashRecoveryNotifier`, `setRespawnRequestFn`); add `setSlotDestroyedFn(std::function<void(uint32_t)>)`. `~PluginProxySlot` calls it. PluginManager's callback (set at `createPluginInstance`): lock `liveProxySlotsMutex_`, `liveProxySlots.erase(slotId)`, then `crashRecovery->cancel(slotId)`.
- With the entry erased, the stale respawn hits the existing safety net `respawnIsolatedSlot` `:731-732` (`find()→not found→return false`) → **no UAF**. `cancel()` prevents pointless retry churn.

**Layer 2 — give `CrashRecoveryManager` a cancel path:**
- Add `void CrashRecoveryManager::cancel(uint32_t slotId)` — `{ std::lock_guard<std::mutex> lock(mutex); entries.erase(slotId); }`. Called from the destruction callback.

**Layer 3 — suppress respawn *during export*** (closes the render-thread race by design, no export graphLock plumbing):
- Add `std::atomic<bool> respawnEnabled{true}` to `CrashRecoveryManager`; `attemptRespawn` returns early (leaves entry pending) when false.
- `ExportManager` sets `crashRecovery.respawnEnabled = false` at `renderThreadFunc` entry and restores `true` **after the render thread joins** (i.e., after stack unwind has destroyed the proxies and the destruction callback has canceled their entries). Locate the join site in `ExportManager` and place the restore there. Rationale: a crashed plugin during offline export should **fail the export**, not respawn into a half-rendered file; and with respawn suppressed there is no kill+swap to race the export `processBlock`.

**Layer 4 (separate, may not fully resolve) — Odin2 isolated-export silence:** investigate whether state/preset round-trips to the isolated child for Odin2 specifically (state-truncation class, AGENTS.md lesson 14) or whether it's a plugin init quirk. Non-blocking: if not root-caused, `DiagnosticClapExportMatrix` should **documented-skip** Odin2 with a comment + a `TODO` rather than fail the suite.

---

## Dependency Map

- **Blast radius:** `PluginManager` (shared by live + export), `CrashRecoveryManager`, `PluginProxySlot` destructor, `ExportManager`. Affects both live playback crash-recovery and export.
- **Upstream:** graph-node destruction (live `rebuildRoutingGraph` and export `renderGraph` unwind) → `~PluginProxySlot`. `PluginManager` Timer → `crashRecovery->tick()`.
- **Downstream:** `respawnIsolatedSlot` now reads a consistent map; `CrashRecoveryManager::attemptRespawn` gains a suppression gate.
- **God nodes in scope:** `PluginManager` (high-degree), `~PluginProxySlot` (runs on two threads: message thread live, render thread export — hence the mutex).
- **Projections affected:** audio graph only. No ValueTree / ReadModel / frontend change.
- **SPSC / threads crossed:** proxy destructor now mutates `liveProxySlots` from the export render thread — the mutex is mandatory, not optional.

---

## Files to Modify

| File | Change |
|------|--------|
| `src/engine/PluginManager.h` | Add `std::mutex liveProxySlotsMutex_`; declare the destruction-callback setter wiring |
| `src/engine/PluginManager.cpp` | Guard `liveProxySlots` access (`:540,731-732,784-785`); set `setSlotDestroyedFn` in `createPluginInstance` (erase + `crashRecovery->cancel`); pass the existing pattern |
| `src/proxy/PluginProxySlot.h` | Add `setSlotDestroyedFn` member + setter |
| `src/proxy/PluginProxySlot.cpp` | `~PluginProxySlot` calls the destruction callback (after existing cleanup) |
| `src/engine/CrashRecoveryManager.h/.cpp` | Add `cancel(slotId)`; add `std::atomic<bool> respawnEnabled{true}` + early-return in `attemptRespawn` |
| `src/engine/ExportManager.cpp` | `crashRecovery.respawnEnabled=false` at render entry; `=true` after render thread joins |
| `tests/integration/mcp/mcp_server_test.cpp` | Remove `DISABLED_` from `DiagnosticClapExportMatrix`; Odin2 documented-skip if still silent |
| `tests/unit/...` | New: deregistration unit test; export-teardown-after-crash no-UAF test |

---

## Pitfall Gates Triggered

| Gate | Why | Mitigation |
|------|-----|------------|
| **Gate 3: Audio-Thread Safety** | Destructor now mutates a map | The map mutation is on the **message/render** thread (proxy destructors run after `processBlock`), not the audio thread. Guarded by `liveProxySlotsMutex_` (a `std::mutex`, not the audio-thread `graphLock`). No audio-thread change. |
| **Lesson 12 (graph thread-safety)** | Touching `liveProxySlots` which respawn reads under `graphLock` | `liveProxySlots` access is **not** under `graphLock` today and must not be (graphLock is for the audio thread). The new `liveProxySlotsMutex_` is a separate, finer-grained lock. Do **not** nest `graphLock` inside it or vice-versa to avoid lock-ordering issues. |
| **Lesson 13 (DSP-state writes)** | Not applicable — no DSP-state write here | — |
| **Gate 2: Unimplemented path** | The destruction callback must actually fire | Unit test: destroy a proxy → assert `liveProxySlots` no longer contains it and `respawnIsolatedSlot` returns false |
| **Lesson 6 / perf** | No new full rebuilds | None introduced |

## Anti-Pattern Scan
- No `rebuildRoutingGraph()` added.
- No graph mutation outside the `graphLock` pattern (respawn still parks under graphLock as in the preceding fix).
- No DSP-state write without `stateLock`.
- The destruction callback must be **null-safe** (the proxy can be destroyed during PluginManager shutdown before/without a callback set) — guard `if (slotDestroyedFn) slotDestroyedFn(slotId);`.

---

## Tasks

### Task 1 — Thread-safe `liveProxySlots` + destruction deregistration
- [ ] `PluginManager.h`: add `std::mutex liveProxySlotsMutex_;`.
- [ ] `PluginProxySlot.{h,cpp}`: add `std::function<void(uint32_t)> slotDestroyedFn` + `setSlotDestroyedFn`; call it at the **end** of `~PluginProxySlot` (after `shmHandle.reset()`), null-guarded.
- [ ] `PluginManager.cpp createPluginInstance`: set the callback to `[this](uint32_t sid){ std::lock_guard<std::mutex> lk(liveProxySlotsMutex_); liveProxySlots.erase(sid); if (crashRecovery) crashRecovery->cancel(sid); }`. Guard the `:540` insert under the same mutex.
- [ ] `respawnIsolatedSlot`: guard the find (`:731-732`) and the erase/insert (`:784-785`) under `liveProxySlotsMutex_`. **Lock ordering note:** acquire `liveProxySlotsMutex_` only around map ops, never together with `graphLock`.

### Task 2 — `CrashRecoveryManager::cancel` + respawn-suppression gate
- [ ] Add `void cancel(uint32_t slotId)` (erase under `mutex`).
- [ ] Add `std::atomic<bool> respawnEnabled{true}`; in `attemptRespawn`, if `!respawnEnabled.load()` return early (leave entry pending).
- [ ] `ExportManager.cpp`: `pluginManager->recovery().respawnEnabled.store(false)` at `renderThreadFunc` entry (after `setRenderMode(true)`); restore `true` **after the render thread is joined** (find the join site; must be after stack unwind so the destruction callbacks have canceled export's entries).

### Task 3 — Re-enable `DiagnosticClapExportMatrix`
- [ ] Remove `DISABLED_`. Update the comment to past-tense (lifetime fixed; respawn suppressed during export).
- [ ] Odin2: if still renders silence, add a documented skip set (e.g. `static const char* kKnownSilent[] = {"Odin2"};`) with a `TODO` referencing the silence investigation — do NOT weaken the per-plugin `EXPECT_GT` for present plugins.

### Task 4 — Regression tests
- [ ] **Unit:** create an isolated proxy via `PluginManager`, destroy it (release the `unique_ptr`), assert `respawnIsolatedSlot(slotId, path)` returns `false` (map entry gone) and no crash.
- [ ] **Integration:** export a project containing a plugin whose child is killed mid-export (use `killProxyForTesting` or a crash-injection seam right before teardown); assert export completes/tears down without `0xC0000005` (this is the gate that proves the dangling-entry UAF is closed).

---

## Success Gates (all must pass to declare done)

- [ ] `McpServer.DiagnosticClapExportMatrix` re-enabled and **passes** (present plugins non-silent; Odin2 either fixed or documented-skip).
- [ ] New deregistration unit test passes; new export-teardown-after-crash test passes (no UAF).
- [ ] `CrashRecovery.*`, `ProxyHealth.*`, `McpServer.ExportAudio*` all green (no regression to live crash-recovery).
- [ ] Full `hdaw_tests.exe`: no new failures vs. the `1959c9c` baseline (631 passed / 4 known `PluginIsolation.*`).
- [ ] `cmake --build build --config Debug` succeeds; `PluginManager.cpp`/`PluginProxySlot.cpp`/`CrashRecoveryManager.*`/`ExportManager.cpp` actually recompiled (lesson 15).

## Verification commands
```
cmake --build build --config Debug
build\Debug\hdaw_tests.exe --gtest_filter=CrashRecovery.*:ProxyHealth.*:McpServer.DiagnosticClapExportMatrix:McpServer.ExportAudio*
build\Debug\hdaw_tests.exe
```

## Open question (decide during Task 3)
If Odin2 silence is a state-round-trip defect (lesson 14), it may warrant its own fix plan rather than a documented-skip. Time-box the investigation; if not root-caused quickly, ship the skip + TODO and file a follow-up.
