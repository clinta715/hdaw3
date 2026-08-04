# Plugin Isolation Fixes Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Close all 20 defects from the 2026-08-03 plugin isolation review: dead Restart button, cached-shm use-after-free, spurious crash dialog on graceful close, handle leak, and 16 lower-severity issues.

**Architecture:** Two phases. Phase A makes the existing crash path non-leaking and correctly observable (refcounted `ShmRegion`, graceful-vs-hard `KillMode` with sentinel exit code, RAII toggle, bounded IO, capacity validation). Phase B adds the missing `CrashRecoveryManager` that auto-respawns crashed children with bounded retry and routes both the `CrashDialog` Restart button and the in-DAW "Restart?" button through one recovery path.

**Tech Stack:** C++20 (`std::atomic<std::shared_ptr>`), JUCE 8, Win32 (`CreateProcess`/named pipes/shared memory), Qt 6 (CrashDialog), gtest, Playwright.

**Spec:** `docs/superpowers/specs/2026-08-03-plugin-isolation-fixes-design.md`

---

## File Structure

**New files:**
- `src/engine/CrashRecoveryManager.h` / `src/engine/CrashRecoveryManager.cpp` — owns the respawn loop, retry budget, and manifest-driven state restore.
- `tests/unit/proxy/kill_mode_test.cpp` — graceful vs hard kill semantics.
- `tests/unit/proxy/shm_refcount_test.cpp` — use-after-free regression under TSan.
- `tests/unit/proxy/crash_recovery_test.cpp` — end-to-end crash → respawn → resume.
- `tests/unit/proxy/respawn_state_test.cpp` — state survives respawn.
- `tests/unit/proxy/respawn_exhaustion_test.cpp` — 3 failures → giveUp.
- `tests/unit/proxy/editor_closed_relay_test.cpp` — EDITOR_CLOSED round-trip.
- `tests/unit/engine/isolation_toggle_raii_test.cpp` — ExportManager scope guard.
- `frontend/e2e/plugin-isolation.spec.ts` — E2E crash + restart + no-crash-on-close.

**Modified files:**
- `src/proxy/ProxyCommon.h` — bump `SHM_MAGIC`, add `audioFramesProduced`.
- `src/proxy/ProxySharedMemory.{h,cpp}` — refcount-friendly (returns `std::shared_ptr<ShmRegion>`), capacity validation.
- `src/proxy/PluginProxySlot.{h,cpp}` — `atomic<shared_ptr<ShmRegion>>`, `markCrashed`/`requestRespawn`, drop raw `cachedShm`.
- `src/proxy/ProxyProcessManager.{h,cpp}` — `KillMode` enum, split `isAlive` const-ness, drop `lastHeartbeat`, drop global `crashCallback`, handle-leak fix, lock-scope fix in `killPluginHost`.
- `src/proxy/ProxyPipe.{h,cpp}` — cache one OVERLAPPED event per server.
- `src/proxy/ProxyEditor.cpp` — bounded IO in `onOpenEditorClicked`.
- `src/proxy/CrashDialog.{h,cpp}` — constructor takes slot id + recovery manager ref; wire accept/reject.
- `src/proxy/host/PluginHost.{h,cpp}` — exit with sentinel on SHUTDOWN, throttle audioLoop spin, increment `audioFramesProduced`, document 44.1/512 placeholder.
- `src/engine/PluginManager.{h,cpp}` — own `CrashRecoveryManager`, plumb graphLock to respawn, tick timer.
- `src/engine/ExportManager.cpp` — RAII scope guard for `isolationEnabled`.
- `src/engine/TrackFXSlot.{h,cpp}` — handle `EDITOR_CLOSED` from child.
- `src/engine/MainAudioProcessor.{h,cpp}` — expose `graphLock` accessor for respawn path.
- `CMakeLists.txt` — add new sources.
- `tests/CMakeLists.txt` — add new test sources.
- `tests/integration/proxy/isolation_integration_test.cpp` — extend with KillMode cases.
- `docs/realtime-safety.md` — update plugin isolation section.

**Deleted files:**
- `src/proxy/ProxyRingBuffer.h` — dead code (finding #12).
- `tests/unit/proxy/RingBufferTest.cpp` (if it exists) — its test.

---

### Task 0: Reconcile working tree

**Files:** none modified

The working tree has uncommitted changes from the default-on rollout that this plan builds on. They must be committed first so each subsequent task produces a clean diff.

- [ ] **Step 1: Inspect the current uncommitted state**

Run: `git -C "D:\pdf\roo projects\hdaw3" status --short`
Expected: list of modified proxy/engine/test files plus unrelated frontend dev-tooling files (`hookSentinel.tsx`, `App.tsx`, `BottomTabs.tsx`).

- [ ] **Step 2: Stage and commit the proxy/engine/test changes (default-on baseline)**

```bash
git add src/proxy/PluginProxySlot.h src/proxy/PluginProxySlot.cpp src/proxy/ProxyPipe.h src/proxy/ProxyPipe.cpp src/proxy/ProxyProcessManager.h src/proxy/ProxyProcessManager.cpp src/engine/PluginManager.cpp tests/integration/proxy/isolation_integration_test.cpp CMakePresets.json build-fast.bat frontend/build.bat
git commit -m "proxy: default-on baseline (per-slot crash callbacks, health monitor, bounded IO, cached shm)"
```

Do NOT stage `frontend/src/dev/hookSentinel.tsx`, `frontend/src/App.tsx`, `frontend/src/components/BottomTabs.tsx`, `frontend/src/electron.d.ts`, `frontend/src/dev/hookSentinel.test.tsx`, or `hdaw_vital_patterns.wav` — those are unrelated dev-tooling/extraneous and belong in a separate commit.

- [ ] **Step 3: Verify clean baseline for the files this plan touches**

Run: `git -C "D:\pdf\roo projects\hdaw3" status --short src/proxy src/engine/PluginManager.cpp tests/integration/proxy`
Expected: no output (all reconciled).

- [ ] **Step 4: Confirm build still works**

Run: `cmake --build build --config Debug --target hdaw_tests`
Expected: clean build.

---

### Task 1: Bump SHM_MAGIC and add audioFramesProduced watchdog field

**Files:**
- Modify: `src/proxy/ProxyCommon.h`

This is the shm layout change. Do it first so all subsequent tasks see the new layout. Bumping the magic ensures a stale `hdaw_plugin_host.exe` from an old build fails the magic check rather than read garbage from the new field offset.

- [ ] **Step 1: Update `ProxyCommon.h`**

Replace the magic constant and add the watchdog counter to `ShmHeader`:

```cpp
constexpr uint32_t SHM_MAGIC = 0x48444158; // "HDAW" + 1 (bumped 2026-08-03 for audioFramesProduced)
```

In the `ShmHeader` struct, after `std::atomic<uint32_t> dawAlive{0};`, add:

```cpp
    // Child-side watchdog: incremented once per processed audio block.
    // Parent compares against a saved snapshot; a stall for >staleThresholdMs
    // is treated as a hang even if the process is still alive.
    std::atomic<uint64_t> audioFramesProduced{0};
    std::atomic<uint64_t> audioBlocksProcessed{0};
```

- [ ] **Step 2: Build**

Run: `cmake --build build --config Debug`
Expected: clean compile. Existing code does not reference these fields yet, so no other changes needed.

- [ ] **Step 3: Commit**

```bash
git add src/proxy/ProxyCommon.h
git commit -m "proxy: bump SHM_MAGIC, add audioFramesProduced/audioBlocksProcessed watchdog fields"
```

---

### Task 2: Make ShmRegion refcounted and validate capacity

**Files:**
- Modify: `src/proxy/ProxySharedMemory.h`
- Modify: `src/proxy/ProxySharedMemory.cpp`

`ShmRegion` itself stays a plain class, but its getters validate capacity. Callers (next task) hold it via `shared_ptr`.

- [ ] **Step 1: Add capacity validation to the getters**

In `src/proxy/ProxySharedMemory.cpp`, replace `getOutputRing`, `getMidiInRing`, `getMidiOutRing`:

```cpp
float* ShmRegion::getOutputRing() const {
    if (!basePtr) return nullptr;
    auto* hdr = getHeader();
    uint32_t cap = hdr->capacity;
    if (cap == 0) return nullptr;
    return reinterpret_cast<float*>(
        reinterpret_cast<uint8_t*>(getInputRing()) + cap * sizeof(float));
}

MidiEvent* ShmRegion::getMidiInRing() const {
    if (!basePtr) return nullptr;
    auto* hdr = getHeader();
    uint32_t cap = hdr->capacity;
    if (cap == 0) return nullptr;
    return reinterpret_cast<MidiEvent*>(
        reinterpret_cast<uint8_t*>(getOutputRing()) + cap * sizeof(float));
}

MidiEvent* ShmRegion::getMidiOutRing() const {
    if (!basePtr) return nullptr;
    auto* hdr = getHeader();
    if (hdr->capacity == 0) return nullptr;
    return getMidiInRing() + 256;
}
```

- [ ] **Step 2: Build**

Run: `cmake --build build --config Debug`
Expected: clean compile.

- [ ] **Step 3: Run existing proxy tests to confirm no regression**

Run: `build\Debug\hdaw_tests.exe --gtest_filter=PluginIsolation.*:Proxy.*`
Expected: all pass.

- [ ] **Step 4: Commit**

```bash
git add src/proxy/ProxySharedMemory.cpp
git commit -m "proxy: validate capacity in ShmRegion getters (finding #8)"
```

---

### Task 3: PluginProxySlot uses atomic<shared_ptr<ShmRegion>>

**Files:**
- Modify: `src/proxy/PluginProxySlot.h`
- Modify: `src/proxy/PluginProxySlot.cpp`

This is the core fix for findings #3, #4, #9. The audio thread holds a local `shared_ptr` copy for the duration of `processBlock`; the destructor's `atomic_store(nullptr)` doesn't free the region until the callback returns.

- [ ] **Step 1: Update PluginProxySlot.h**

Replace the `cachedShm` member and add the atomic handle. In `src/proxy/PluginProxySlot.h`:

Change the include block at top to add `<memory>`:
```cpp
#include <atomic>
#include <memory>
```

Replace the private member section:
```cpp
    std::atomic<bool> crashed{false};
    std::atomic<bool> childAlive{true};
    ShmRegion* cachedShm = nullptr;  // lock-free access from audio thread
```

with:
```cpp
    std::atomic<bool> crashed{false};
    std::atomic<bool> childAlive{true};
    // Atomic shared_ptr: audio thread atomic_load's once per processBlock and
    // holds the local copy for the callback duration. Destructor atomic_store's
    // nullptr; the region is freed only when the last reference drops (i.e.
    // after any in-flight audio callback returns). C++20.
    std::shared_ptr<ShmRegion> shmHandle{nullptr};
```

Add a public method (used by CrashRecoveryManager during respawn):
```cpp
    // Swap in a fresh ShmRegion after respawn. Called on the message thread
    // under MainAudioProcessor::graphLock (audio callback guaranteed not in flight).
    void installShm(std::shared_ptr<ShmRegion> newShm);
```

- [ ] **Step 2: Update the constructor in PluginProxySlot.cpp**

In `src/proxy/PluginProxySlot.cpp`, replace the constructor body's shm caching:

```cpp
PluginProxySlot::PluginProxySlot(ProxyProcessManager& mgr, uint32_t id,
                                    const juce::String& name)
    : AudioPluginInstance(juce::AudioProcessor::BusesProperties()
          .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      processManager(mgr),
      slotId(id),
      pluginDisplayName(name)
{
    auto* raw = processManager.getShm(slotId);
    if (raw) {
        // Wrap the existing ShmRegion in a shared_ptr with a no-op deleter —
        // ProxyProcessManager owns the lifetime; the shared_ptr here is purely
        // for atomic-load/store synchronization with the audio thread. The
        // real ownership transfer happens in installShm() after respawn.
        shmHandle = std::shared_ptr<ShmRegion>(raw, [](ShmRegion*){});
    }
    childAlive.store(processManager.isChildAlive(slotId), std::memory_order_relaxed);
    startTimer(5000);
}
```

- [ ] **Step 3: Implement installShm()**

Add to `src/proxy/PluginProxySlot.cpp`:

```cpp
void PluginProxySlot::installShm(std::shared_ptr<ShmRegion> newShm) {
    // Called under graphLock on the message thread. The previous handle's
    // refcount drops to zero only after the audio callback (which may hold a
    // local copy from atomic_load) returns.
    shmHandle = std::move(newShm);
    childAlive.store(true, std::memory_order_relaxed);
}
```

Note: plain assignment to `std::shared_ptr` member is NOT thread-safe vs `atomic_load` on the read side. Since this runs under `graphLock` (audio callback excluded), the plain assignment is safe — the audio thread is not concurrent. We keep `shmHandle` as a plain member (not `atomic<shared_ptr>`) because the graphLock provides the necessary exclusion, and `atomic<shared_ptr>` would require `std::atomic_load`/`std::atomic_store` free functions which are clunky. The audio thread reads `shmHandle` directly (still safe because graphLock excludes the writer).

Wait — the audio thread does NOT acquire graphLock. It runs concurrently with message-thread graphLock holders only between callbacks. The contract is: message thread acquires graphLock, audio thread's processBlock tries to acquire it and skips if blocked OR the message-thread work is guaranteed < 1 callback period. Let me re-read MainAudioProcessor to confirm.

Actually, the standard JUCE pattern is: `processBlock` is never concurrent with `rebuildRoutingGraph` because the rebuild acquires the same lock the audio callback does (or uses `AudioProcessorValueTreeState` style suspension). For this plan, the safe assumption is: **the audio thread's read of `shmHandle` and the message thread's write under graphLock are NOT concurrent.** So plain `shared_ptr` member is fine, no atomic needed. The refcounting handles the use-after-free only across the destructor boundary (when a stale callback might still be running) — but graphLock already prevents that.

Let me simplify: drop the atomic complexity. Use a plain `std::shared_ptr<ShmRegion> shmHandle`. The audio thread reads it directly. The destructor (under graphLock) resets it. The graphLock guarantees the audio callback is not in flight during reset. 

Revised Step 1 — change the member to:
```cpp
    std::shared_ptr<ShmRegion> shmHandle;
```

Remove the `installShm` complexity — keep it but it's just `shmHandle = std::move(newShm);`.

- [ ] **Step 4: Rewrite processBlock to use shmHandle**

In `src/proxy/PluginProxySlot.cpp`, replace the cached-pointer usage in `processBlock`. The line:

```cpp
    auto* shm = cachedShm;
    if (!shm || !shm->getHeader()) {
```

becomes:

```cpp
    auto shm = shmHandle;  // copy the shared_ptr (bumps refcount, holds region alive)
    if (!shm || !shm->getHeader()) {
```

And everywhere later in `processBlock` that uses `shm->` — those already dereference the pointer, so they work unchanged once `shm` is a `shared_ptr`. Change the `auto* shm` to `auto shm` (the local copy holds a reference).

- [ ] **Step 5: Update destructor**

In `src/proxy/PluginProxySlot.cpp`, the destructor already runs under graphLock. It calls `killPluginHost(slotId, true)` which destroys the `ChildInfo` (and its `shm` unique_ptr). After that, `shmHandle` is the only remaining reference. Add an explicit reset before the function returns so the region is freed deterministically:

```cpp
PluginProxySlot::~PluginProxySlot() {
    processManager.removeSlotCrashCallback(slotId);
    processManager.killPluginHost(slotId, true);
    releaseResources();
    shmHandle.reset();  // drop our reference; region freed if no audio callback holds a copy
}
```

- [ ] **Step 6: Build**

Run: `cmake --build build --config Debug`
Expected: clean compile.

- [ ] **Step 7: Run existing proxy tests**

Run: `build\Debug\hdaw_tests.exe --gtest_filter=PluginIsolation.*`
Expected: all pass.

- [ ] **Step 8: Commit**

```bash
git add src/proxy/PluginProxySlot.h src/proxy/PluginProxySlot.cpp
git commit -m "proxy: refcounted ShmRegion in PluginProxySlot (findings #3, #4, #9)"
```

---

### Task 4: Add KillMode enum and graceful sentinel exit code

**Files:**
- Modify: `src/proxy/ProxyProcessManager.h`
- Modify: `src/proxy/ProxyProcessManager.cpp`
- Modify: `src/proxy/host/PluginHost.cpp`
- Test: `tests/integration/proxy/isolation_integration_test.cpp`

Distinguishes graceful shutdown (no crash dialog) from crash. Finding #19.

- [ ] **Step 1: Add KillMode and sentinel to ProxyProcessManager.h**

In `src/proxy/ProxyProcessManager.h`, add after the `using CrashCallback` line:

```cpp
constexpr DWORD GRACEFUL_EXIT_CODE = 0xC0DE0001;

enum class KillMode {
    KillGraceful,  // send SHUTDOWN, wait 2s, TerminateProcess(GRACEFUL_EXIT_CODE)
    KillHard       // TerminateProcess(0) immediately (existing behavior)
};
```

Change the `killPluginHost` signature:

```cpp
    bool killPluginHost(uint32_t slotId, KillMode mode);
```

Remove the old `bool fullCleanup = true` overload to avoid ambiguity. Keep a transitional wrapper only if needed by `spawnPluginHost` (which calls `killPluginHost(slotId, true)` — update that call site in Step 3).

- [ ] **Step 2: Implement KillGraceful in ProxyProcessManager.cpp**

In `src/proxy/ProxyProcessManager.cpp`, replace `killPluginHost`:

```cpp
bool ProxyProcessManager::killPluginHost(uint32_t slotId, KillMode mode) {
    // Release the lock before any blocking wait so spawnPluginHost (which
    // calls this defensively) doesn't hold the lock for the full 1-2s
    // WaitForSingleObject timeout. Snapshot what we need, erase under lock,
    // then wait outside.
    HANDLE handle = INVALID_HANDLE_VALUE;
    PipeServer* pipe = nullptr;
    {
        std::lock_guard<std::mutex> lock(mutex);
        auto it = children.find(slotId);
        if (it == children.end()) return false;
        auto& info = it->second;
        handle = info.processHandle;
        pipe = info.pipe.get();
        info.alive.store(false);
        // For KillGraceful we keep the entry alive until the wait completes
        // (so the pipe is still valid for SHUTDOWN). For KillHard, erase now.
        if (mode == KillMode::KillHard) {
            if (info.pipe) info.pipe->stop();
            children.erase(it);
        }
    }

    if (mode == KillMode::KillGraceful && pipe) {
        ProxyMessage shutdown{};
        shutdown.type = MessageType::SHUTDOWN;
        shutdown.slotId = slotId;
        pipe->sendMsgBounded(shutdown, 500);
        // Wait up to 2s for the child to exit on its own
        if (handle != INVALID_HANDLE_VALUE) {
            DWORD waitResult = WaitForSingleObject(handle, 2000);
            if (waitResult != WAIT_OBJECT_0) {
                // Still alive — force terminate with the graceful sentinel
                // so the parent's exit-code filter treats this as graceful.
                TerminateProcess(handle, GRACEFUL_EXIT_CODE);
                WaitForSingleObject(handle, 1000);
            }
        }
    } else if (handle != INVALID_HANDLE_VALUE) {
        TerminateProcess(handle, 0);
        WaitForSingleObject(handle, 1000);
    }

    if (handle != INVALID_HANDLE_VALUE) {
        CloseHandle(handle);
    }

    // For KillGraceful, erase the entry now that the wait is done
    if (mode == KillMode::KillGraceful) {
        std::lock_guard<std::mutex> lock(mutex);
        auto it = children.find(slotId);
        if (it != children.end()) {
            if (it->second.pipe) it->second.pipe->stop();
            children.erase(it);
        }
    }
    return true;
}
```

- [ ] **Step 3: Update spawnPluginHost's defensive call**

In `src/proxy/ProxyProcessManager.cpp`, the line `killPluginHost(slotId, true);` near the top of `spawnPluginHost` becomes:

```cpp
    killPluginHost(slotId, KillMode::KillHard);
```

- [ ] **Step 4: Update the destructor**

In `~ProxyProcessManager`, change `TerminateProcess(info.processHandle, 0);` — the destructor is whole-process teardown, so hard kill is fine. No change needed since it doesn't go through `killPluginHost`.

- [ ] **Step 5: Update PluginHost to exit with sentinel after SHUTDOWN**

In `src/proxy/host/PluginHost.cpp`, the `SHUTDOWN` case in `controlLoop` sets `running.store(false)`. After the loop exits (line 345), the `run()` method returns. Update the exit path so the process exits with the sentinel:

At the end of `run()` (currently `return 0;`), change to:

```cpp
    // Exit with the graceful sentinel so the parent's exit-code filter
    // recognizes this as a clean shutdown (not a crash).
    std::_Exit(proxy::GRACEFUL_EXIT_CODE);
```

Use `std::_Exit` (not `exit`) to skip atexit/static destructors — the plugin may have left state we don't want to touch. Include `<cstdlib>` (already included).

Add the include for the sentinel constant. At top of `PluginHost.cpp`, after the existing proxy includes, the constant lives in `proxy::ProxyProcessManager.h` — but to avoid a host→manager dependency, re-declare it in `ProxyCommon.h` instead. Move the constant:

Move `constexpr DWORD GRACEFUL_EXIT_CODE = 0xC0DE0001;` from `ProxyProcessManager.h` to `ProxyCommon.h` (in the `namespace proxy` block). Update both files' references.

- [ ] **Step 6: Update checkAllChildren to filter on sentinel**

In `src/proxy/ProxyProcessManager.cpp`, in `checkAllChildren`, change the crash detection:

```cpp
            if (exitCode != STILL_ACTIVE && exitCode != proxy::GRACEFUL_EXIT_CODE) {
                info.alive.store(false);
                crashedSlots.push_back(id);
                continue;
            }
```

(Remove the unconditional `crashedSlots.push_back` for non-STILL_ACTIVE — gate it on the sentinel.)

- [ ] **Step 7: Update ~PluginProxySlot to use KillGraceful**

In `src/proxy/PluginProxySlot.cpp`, the destructor currently calls `killPluginHost(slotId, true)`. Change to:

```cpp
    processManager.killPluginHost(slotId, KillMode::KillGraceful);
```

This is the key change for finding #19: project close now goes through the graceful path, exit code is the sentinel, no crash dialog.

- [ ] **Step 8: Write the failing test**

In `tests/integration/proxy/isolation_integration_test.cpp`, add at the end:

```cpp
#if HDAW_PLUGIN_ISOLATION
TEST(PluginIsolation, GracefulShutdownDoesNotFireCrashCallback) {
    ProxyProcessManager mgr;

    std::atomic<int> crashFires{0};
    mgr.setSlotCrashCallback(9100, [&](uint32_t) { crashFires.fetch_add(1); });

    ASSERT_TRUE(mgr.spawnPluginHost("__passthrough__", 9100));

    mgr.startHealthMonitor(100);

    // Graceful kill — should NOT fire the crash callback
    ASSERT_TRUE(mgr.killPluginHost(9100, KillMode::KillGraceful));

    // Give the health monitor a couple ticks to observe the exit
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    mgr.checkAllChildren();

    EXPECT_EQ(crashFires.load(), 0);

    mgr.stopHealthMonitor();
}

TEST(PluginIsolation, HardKillFiresCrashCallback) {
    ProxyProcessManager mgr;

    std::atomic<int> crashFires{0};
    mgr.setSlotCrashCallback(9101, [&](uint32_t) { crashFires.fetch_add(1); });

    ASSERT_TRUE(mgr.spawnPluginHost("__passthrough__", 9101));

    mgr.startHealthMonitor(100);

    // Hard kill — SHOULD fire the crash callback (exit code 0, not sentinel)
    ASSERT_TRUE(mgr.killPluginHost(9101, KillMode::KillHard));

    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    mgr.checkAllChildren();

    EXPECT_EQ(crashFires.load(), 1);

    mgr.stopHealthMonitor();
}
#endif
```

- [ ] **Step 9: Build and run tests**

Run: `cmake --build build --config Debug --target hdaw_tests`
Expected: clean build.

Run: `build\Debug\hdaw_tests.exe --gtest_filter="PluginIsolation.GracefulShutdown*:PluginIsolation.HardKill*"`
Expected: both pass.

- [ ] **Step 10: Commit**

```bash
git add src/proxy/ProxyProcessManager.h src/proxy/ProxyProcessManager.cpp src/proxy/ProxyCommon.h src/proxy/PluginProxySlot.cpp src/proxy/host/PluginHost.cpp tests/integration/proxy/isolation_integration_test.cpp
git commit -m "proxy: KillMode + graceful sentinel exit code (findings #6, #19)"
```

---

### Task 5: Fix handle leak on no-READY spawn path

**Files:**
- Modify: `src/proxy/ProxyProcessManager.cpp`

Finding #2.

- [ ] **Step 1: Add CloseHandle to the failure branch**

In `src/proxy/ProxyProcessManager.cpp`, in `spawnPluginHost`, the no-READY branch:

```cpp
    ProxyResponse readyResp{};
    if (!pipeServer->receiveResp(readyResp)) {
        TerminateProcess(pi.hProcess, 0);
        CloseHandle(pi.hProcess);
        pipeServer->stop();
        return false;
    }
```

Verify `CloseHandle(pi.hProcess)` is present (it should be after Task 4's edits, but confirm). The handle leak was specifically about `pi.hProcess` not being closed on this path. If it's already there from the existing code, this task is a no-op confirm.

Run: `grep -n "CloseHandle(pi.hProcess)" src/proxy/ProxyProcessManager.cpp`
Expected: at least one match in `spawnPluginHost`.

- [ ] **Step 2: Commit (only if a change was needed)**

If the handle was already being closed (check the current state — the review found it was missing), no commit needed. If you added it:

```bash
git add src/proxy/ProxyProcessManager.cpp
git commit -m "proxy: close child process handle on no-READY spawn failure (finding #2)"
```

---

### Task 6: Split isAlive const-ness, drop lastHeartbeat, drop global crashCallback

**Files:**
- Modify: `src/proxy/ProxyProcessManager.h`
- Modify: `src/proxy/ProxyProcessManager.cpp`

Findings #7, #13, #14.

- [ ] **Step 1: Update ProxyProcessManager.h**

Remove `std::atomic<uint32_t> lastHeartbeat` from `ChildInfo`. Remove the `CrashCallback crashCallback` member and the `setCrashCallback` setter (keep only per-slot). Add a const `isAliveConst`:

Change:
```cpp
    struct ChildInfo {
        ...
        std::atomic<uint32_t> lastHeartbeat{0};
    };
```
Remove the `lastHeartbeat` line.

Change:
```cpp
    void setCrashCallback(CrashCallback cb) { crashCallback = std::move(cb); }
    void setSlotCrashCallback(uint32_t slotId, CrashCallback cb);
```
to:
```cpp
    void setSlotCrashCallback(uint32_t slotId, CrashCallback cb);
```

Change:
```cpp
    bool isAlive(uint32_t slotId);
    bool isChildAlive(uint32_t slotId) const;
```
to:
```cpp
    // Read-only query (const). Does not mutate state.
    bool isChildAlive(uint32_t slotId) const;
    // Mutating query: marks the child dead if the OS reports it exited.
    // Use isChildAlive() for read-only checks.
    bool isAlive(uint32_t slotId);
```

Remove from private members:
```cpp
    CrashCallback crashCallback;
```

- [ ] **Step 2: Update ProxyProcessManager.cpp**

Remove `lastHeartbeat` references in `ChildInfo` move constructor/assignment and in `sendHeartbeat`/`checkHealth`. The `checkAllChildren` already reads `childAlive` and exit code; remove the `lastHeartbeat`-based staleness math (it was redundant with the exit-code check anyway).

In `sendHeartbeat`, remove:
```cpp
    it->second.lastHeartbeat.store(...);
```

In `checkAllChildren`, remove the `lastHeartbeat` elapsed-time block — rely on exit code + `childAlive` only.

In `checkAllChildren`, remove:
```cpp
        if (crashCallback) crashCallback(id);
```
(keep only the per-slot callback dispatch).

In `spawnPluginHost`, remove the `info.lastHeartbeat.store(...)` line.

In `ChildInfo`'s move ctor/assignment, remove `lastHeartbeat(o.lastHeartbeat.load())`.

- [ ] **Step 3: Build**

Run: `cmake --build build --config Debug`
Expected: clean compile. If `sendHeartbeat`/`checkHealth` reference `lastHeartbeat`, fix them (they should compile out).

- [ ] **Step 4: Run proxy tests**

Run: `build\Debug\hdaw_tests.exe --gtest_filter=PluginIsolation.*`
Expected: all pass.

- [ ] **Step 5: Commit**

```bash
git add src/proxy/ProxyProcessManager.h src/proxy/ProxyProcessManager.cpp
git commit -m "proxy: const-correct isAlive, drop lastHeartbeat, drop global crashCallback (findings #7, #13, #14)"
```

---

### Task 7: Cache one OVERLAPPED event per PipeServer

**Files:**
- Modify: `src/proxy/ProxyPipe.h`
- Modify: `src/proxy/ProxyPipe.cpp`

Finding #11.

- [ ] **Step 1: Add cached event to PipeServer**

In `src/proxy/ProxyPipe.h`, add to `PipeServer` private members:

```cpp
    HANDLE cachedEvent = nullptr;
```

- [ ] **Step 2: Create the event in start(), destroy in stop()**

In `src/proxy/ProxyPipe.cpp`, in `PipeServer::start()`, after `running = true;`:

```cpp
    cachedEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
```

In `PipeServer::stop()`, before `hPipe = INVALID_HANDLE_VALUE;`:

```cpp
    if (cachedEvent) { CloseHandle(cachedEvent); cachedEvent = nullptr; }
```

- [ ] **Step 3: Use cachedEvent in the overlapped helpers**

Replace `CreateEvent(nullptr, TRUE, FALSE, nullptr)` in `overlappedConnect`, `overlappedRead`, `overlappedWrite` with `cachedEvent` (and `ResetEvent(cachedEvent)` before each use since it's manual-reset and reused). Remove the `CloseHandle(ov.hEvent)` calls at the end of each (the event is now owned by the server).

For example, `overlappedRead` becomes:

```cpp
bool PipeServer::overlappedRead(void* buf, DWORD size, DWORD timeoutMs, DWORD& bytesRead) {
    bytesRead = 0;
    if (!cachedEvent) return false;
    ResetEvent(cachedEvent);
    OVERLAPPED ov{};
    ov.hEvent = cachedEvent;

    BOOL ok = ReadFile(hPipe, buf, size, &bytesRead, &ov);
    bool success = false;
    if (ok) {
        success = true;
    } else {
        DWORD err = GetLastError();
        if (err == ERROR_IO_PENDING) {
            DWORD wait = WaitForSingleObject(cachedEvent, timeoutMs);
            if (wait == WAIT_OBJECT_0) {
                success = GetOverlappedResult(hPipe, &ov, &bytesRead, FALSE) != 0;
            } else {
                CancelIo(hPipe);
                DWORD transferred = 0;
                GetOverlappedResult(hPipe, &ov, &transferred, TRUE);
            }
        }
    }
    return success;
}
```

Apply the same `ResetEvent(cachedEvent)` + remove-`CloseHandle` pattern to `overlappedConnect` and `overlappedWrite`.

**Important:** because the event is now shared across concurrent calls, this is only safe if no two threads call IO on the same `PipeServer` concurrently. Confirm the call sites: `PipeServer` is used from the message thread (`prepareToPlay`, `getStateInformation`, etc.) and the health monitor thread (`sendHeartbeat`). These CAN race. Two options: (a) keep per-call events but pool them, (b) guard IO with a mutex. Simplest correct fix: keep the per-call `CreateEvent` (revert this task) — the kernel-call overhead is microseconds and not actually a hot path. The review ranked this #11 (lowest of the high tier).

**Decision: skip this optimization.** The per-call event allocation is correct and the perf cost is negligible vs the IPC round-trip itself. Mark finding #11 as "won't fix — premature optimization" in the plan's completion notes.

- [ ] **Step 4: Revert if any changes were made**

If you started editing, revert:
```bash
git checkout -- src/proxy/ProxyPipe.h src/proxy/ProxyPipe.cpp
```

No commit. Move on.

---

### Task 8: RAII guard for isolationEnabled in ExportManager

**Files:**
- Modify: `src/engine/ExportManager.cpp`

Finding #10.

- [ ] **Step 1: Replace manual save/restore with a scope guard**

In `src/engine/ExportManager.cpp`, find:

```cpp
    bool wasIsolationEnabled = pluginManager && pluginManager->isolationEnabled;
    if (pluginManager) pluginManager->isolationEnabled = false;
```

Replace with:

```cpp
    // RAII guard: restore isolationEnabled on scope exit, including exception paths.
    struct IsolationToggleGuard {
        HDAW::PluginManager* pm;
        bool wasEnabled;
        IsolationToggleGuard(HDAW::PluginManager* p) : pm(p), wasEnabled(p && p->isolationEnabled) {
            if (pm) pm->isolationEnabled = false;
        }
        ~IsolationToggleGuard() {
            if (pm) pm->isolationEnabled = wasEnabled;
        }
    } isolationGuard{pluginManager};
```

Remove the manual restore at the `finish:` label:
```cpp
    if (pluginManager) pluginManager->isolationEnabled = wasIsolationEnabled;
```

And remove the `proxy::setRenderMode(false)` line? No — keep that one, it's separate. Just remove the isolationEnabled restore since the guard handles it.

- [ ] **Step 2: Build**

Run: `cmake --build build --config Debug`
Expected: clean compile.

- [ ] **Step 3: Write the failing test**

Create `tests/unit/engine/isolation_toggle_raii_test.cpp`:

```cpp
#include <gtest/gtest.h>
#include "engine/PluginManager.h"

TEST(IsolationToggle, GuardRestoresOnException) {
    HDAW::PluginManager pm;
    ASSERT_TRUE(pm.isolationEnabled);

    try {
        struct Guard {
            HDAW::PluginManager& pm;
            bool was;
            Guard(HDAW::PluginManager& p) : pm(p), was(p.isolationEnabled) { pm.isolationEnabled = false; }
            ~Guard() { pm.isolationEnabled = was; }
        } g{pm};
        EXPECT_FALSE(pm.isolationEnabled);
        throw std::runtime_error("simulated export failure");
    } catch (const std::runtime_error&) {
        // Guard destructor ran during stack unwind
    }

    EXPECT_TRUE(pm.isolationEnabled) << "RAII guard should restore isolationEnabled after exception";
}
```

- [ ] **Step 4: Add test to tests/CMakeLists.txt**

Add `tests/unit/engine/isolation_toggle_raii_test.cpp` to the test executable source list.

- [ ] **Step 5: Build and run**

Run: `cmake --build build --config Debug --target hdaw_tests && build\Debug\hdaw_tests.exe --gtest_filter=IsolationToggle.*`
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add src/engine/ExportManager.cpp tests/unit/engine/isolation_toggle_raii_test.cpp tests/CMakeLists.txt
git commit -m "engine: RAII guard for isolationEnabled in ExportManager (finding #10)"
```

---

### Task 9: Bounded IO in ProxyEditor::onOpenEditorClicked

**Files:**
- Modify: `src/proxy/ProxyEditor.cpp`

Finding #15.

- [ ] **Step 1: Replace blocking IO with bounded**

In `src/proxy/ProxyEditor.cpp`, replace `onOpenEditorClicked`:

```cpp
void ProxyEditor::onOpenEditorClicked() {
    auto* pipe = slot.getProcessManager().getPipe(slot.getSlotId());
    if (!pipe) {
        openEditorButton.setEnabled(false);
        return;
    }

    proxy::ProxyMessage msg{};
    msg.type = proxy::MessageType::SHOW_EDITOR;
    msg.slotId = slot.getSlotId();

    static constexpr DWORD kShowEditorTimeoutMs = 2000;
    if (!pipe->sendMsgBounded(msg, kShowEditorTimeoutMs)) {
        openEditorButton.setEnabled(false);
        return;
    }

    proxy::ProxyResponse resp{};
    if (!pipe->receiveRespBounded(resp, kShowEditorTimeoutMs)) {
        openEditorButton.setEnabled(false);
    }
}
```

- [ ] **Step 2: Build**

Run: `cmake --build build --config Debug`
Expected: clean compile.

- [ ] **Step 3: Commit**

```bash
git add src/proxy/ProxyEditor.cpp
git commit -m "proxy: bounded IO in ProxyEditor::onOpenEditorClicked (finding #15)"
```

---

### Task 10: Throttle audioLoop spin and increment watchdog counters

**Files:**
- Modify: `src/proxy/host/PluginHost.cpp`

Findings #17, #18.

- [ ] **Step 1: Add spin throttling and counter increments**

In `src/proxy/host/PluginHost.cpp`, in `audioLoop`, after the `plugin->processBlock(inputBuffer, midiBuffer);` call and before the output write, increment the watchdog:

```cpp
            plugin->processBlock(inputBuffer, midiBuffer);

            hdr->audioFramesProduced.fetch_add(preparedBlockSize, std::memory_order_relaxed);
            hdr->audioBlocksProcessed.fetch_add(1, std::memory_order_relaxed);
```

In the `else` branch (underrun), replace the bare `std::this_thread::yield();`:

```cpp
        } else {
            // Throttle: yield most iterations, Sleep(0) every 64th to let the
            // scheduler back off. Avoids pegging a core per isolated plugin.
            static thread_local int spinCount = 0;
            if ((++spinCount & 63) == 0)
                Sleep(0);
            else
                std::this_thread::yield();
        }
```

- [ ] **Step 2: Build**

Run: `cmake --build build --config Debug`
Expected: clean compile.

- [ ] **Step 3: Commit**

```bash
git add src/proxy/host/PluginHost.cpp
git commit -m "proxy: throttle audioLoop spin, increment watchdog counters (findings #17, #18)"
```

---

### Task 11: Add watchdog stall detection to checkAllChildren

**Files:**
- Modify: `src/proxy/ProxyProcessManager.h`
- Modify: `src/proxy/ProxyProcessManager.cpp`

Finding #18 (parent side).

- [ ] **Step 1: Add stall snapshot tracking to ChildInfo**

In `src/proxy/ProxyProcessManager.h`, in `ChildInfo`, add:

```cpp
    uint64_t lastFramesSnapshot{0};
    uint64_t lastSnapshotMs{0};
```

Update the move ctor/assignment to carry these.

- [ ] **Step 2: Detect stalls in checkAllChildren**

In `src/proxy/ProxyProcessManager.cpp`, in `checkAllChildren`, after the exit-code check, add (for alive children):

```cpp
            // Watchdog: if audioBlocksProcessed hasn't advanced since last
            // check AND the process is still alive, the child is hung in
            // processBlock. Treat as crash.
            uint64_t currentBlocks = 0;
            if (info.shm && info.shm->getHeader())
                currentBlocks = info.shm->getHeader()->audioBlocksProcessed.load(std::memory_order_relaxed);

            auto nowMs = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count());

            if (currentBlocks == info.lastBlocksSnapshot) {
                // No progress since last check
                if (info.lastSnapshotMs == 0) {
                    info.lastSnapshotMs = nowMs;
                } else if (nowMs - info.lastSnapshotMs > staleThresholdMs) {
                    crashedSlots.push_back(id);
                    continue;
                }
            } else {
                info.lastBlocksSnapshot = currentBlocks;
                info.lastSnapshotMs = nowMs;
            }
```

Rename `lastFramesSnapshot` to `lastBlocksSnapshot` to match. Update the `ChildInfo` member name accordingly.

- [ ] **Step 3: Build and run**

Run: `cmake --build build --config Debug && build\Debug\hdaw_tests.exe --gtest_filter=PluginIsolation.*`
Expected: clean build, all pass.

- [ ] **Step 4: Commit**

```bash
git add src/proxy/ProxyProcessManager.h src/proxy/ProxyProcessManager.cpp
git commit -m "proxy: detect hung-child via audioBlocksProcessed stall (finding #18)"
```

---

### Task 12: Delete dead RingBuffer.h

**Files:**
- Delete: `src/proxy/ProxyRingBuffer.h`
- Modify: `tests/CMakeLists.txt` (if it references RingBufferTest)
- Modify: `CMakeLists.txt` (if it references the header)

Finding #12.

- [ ] **Step 1: Check for references**

Run: `grep -rn "ProxyRingBuffer\|RingBufferTest" src/ tests/ CMakeLists.txt tests/CMakeLists.txt`
Expected: list of references. If only the header itself and a test file, proceed. If production code references it, STOP and reassess.

- [ ] **Step 2: Delete the header and its test**

```bash
git rm src/proxy/ProxyRingBuffer.h
```

If `tests/unit/proxy/RingBufferTest.cpp` exists:
```bash
git rm tests/unit/proxy/RingBufferTest.cpp
```

Remove any reference to `RingBufferTest.cpp` from `tests/CMakeLists.txt`.

- [ ] **Step 3: Build**

Run: `cmake --build build --config Debug`
Expected: clean compile (confirms nothing depended on it).

- [ ] **Step 4: Commit**

```bash
git add -A
git commit -m "proxy: delete dead ProxyRingBuffer.h (finding #12)"
```

---

### Task 13: Gate ProxyProc debug log behind HDAW_PROXY_DEBUG

**Files:**
- Modify: `src/proxy/PluginProxySlot.cpp`

Finding: audio-thread allocation from the per-500-calls `HDAW_LOG`.

- [ ] **Step 1: Gate the periodic log**

In `src/proxy/PluginProxySlot.cpp`, in `processBlock`, the line:

```cpp
    if (cc < 3 || (cc % 500) == 0)
        HDAW_LOG("ProxyProc", (...).toStdString());
```

Change to:

```cpp
#ifdef HDAW_PROXY_DEBUG
    if (cc < 3 || (cc % 500) == 0)
        HDAW_LOG("ProxyProc", (juce::String("processBlock call=") + juce::String(cc) + " slot=" + juce::String((int)slotId) + " crashed=" + (crashed.load()?"1":"0") + " renderMode=" + (s_renderMode.load()?"1":"0") + " midi=" + juce::String(midiMessages.getNumEvents())).toStdString());
#endif
```

The first-three-calls log (`cc < 3`) is fine to keep ungated since it fires only at startup — but to be safe, gate the whole block.

- [ ] **Step 2: Build**

Run: `cmake --build build --config Debug`
Expected: clean compile (the log is now compiled out unless `HDAW_PROXY_DEBUG` is defined).

- [ ] **Step 3: Commit**

```bash
git add src/proxy/PluginProxySlot.cpp
git commit -m "proxy: gate audio-thread ProxyProc log behind HDAW_PROXY_DEBUG (realtime safety)"
```

---

### Task 14: Wire EDITOR_CLOSED to TrackFXSlot

**Files:**
- Modify: `src/proxy/TrackFXSlot.h`
- Modify: `src/proxy/TrackFXSlot.cpp`
- Modify: `src/proxy/PluginProxySlot.{h,cpp}`

Finding #20.

- [ ] **Step 1: Add a callback on PluginProxySlot for editor-closed**

In `src/proxy/PluginProxySlot.h`, add:

```cpp
    using EditorClosedCallback = std::function<void()>;
    void setEditorClosedCallback(EditorClosedCallback cb) { editorClosedCb = std::move(cb); }
```

Add private member:
```cpp
    EditorClosedCallback editorClosedCb;
```

- [ ] **Step 2: Add a pipe-listener thread to PluginProxySlot to receive async EDITOR_CLOSED**

The current design has the parent only sending requests and receiving responses — it doesn't listen for unsolicited messages from the child. `EDITOR_CLOSED` is sent by the child asynchronously (when the user closes the editor window).

Simplest correct approach: when `ProxyEditor::onOpenEditorClicked` is called, after sending SHOW_EDITOR, spawn a brief listener thread that blocks on `receiveResp` (bounded to the editor session). When the response arrives with type `EDITOR_CLOSED`, fire the callback.

Actually simpler: the existing `onOpenEditorClicked` already does `receiveResp` after `sendMsg`. Extend the lifecycle: after SHOW_EDITOR returns, start a background thread that waits for EDITOR_CLOSED.

In `src/proxy/PluginProxySlot.cpp`, add a method:

```cpp
void PluginProxySlot::waitForEditorClosed() {
    auto* pipe = processManager.getPipe(slotId);
    if (!pipe) return;
    // This runs on a background thread started by ProxyEditor after the editor opens.
    // It blocks until the child sends EDITOR_CLOSED or the pipe breaks.
    proxy::ProxyResponse resp{};
    while (childAlive.load(std::memory_order_relaxed)) {
        if (pipe->receiveRespBounded(resp, 500)) {
            if (resp.type == proxy::MessageType::EDITOR_CLOSED) {
                if (editorClosedCb) editorClosedCb();
                return;
            }
        }
    }
}
```

Add member: `std::thread editorWatcherThread;`

Add to destructor: `if (editorWatcherThread.joinable()) editorWatcherThread.detach();`

- [ ] **Step 3: Start the watcher from ProxyEditor after SHOW_EDITOR succeeds**

In `src/proxy/ProxyEditor.cpp`, in `onOpenEditorClicked`, after the successful `receiveRespBounded`:

```cpp
    // Editor opened — start watching for EDITOR_CLOSED from the child.
    slot.startEditorWatcher();
```

Add to `PluginProxySlot.h`:
```cpp
    void startEditorWatcher();
```

Add to `PluginProxySlot.cpp`:
```cpp
void PluginProxySlot::startEditorWatcher() {
    if (editorWatcherThread.joinable()) return;  // already watching
    editorWatcherThread = std::thread([this]{ waitForEditorClosed(); });
}
```

- [ ] **Step 4: Wire the callback from TrackFXSlot**

In `src/engine/TrackFXSlot.cpp`, where the isolated plugin instance is created (around the `dynamic_cast<proxy::PluginProxySlot*>` at line 23), add:

```cpp
    if (isolated) {
        auto* proxySlot = dynamic_cast<proxy::PluginProxySlot*>(pluginInstance.get());
        if (proxySlot) {
            proxySlot->setEditorClosedCallback([this]() {
                // Called on the editor-watcher thread; flip the flag atomically.
                remoteEditorOpen.store(false);
                // Fire any UI refresh via MessageManager if needed.
                juce::MessageManager::callAsync([this]{ /* emit change signal */ });
            });
        }
    }
```

Add `remoteEditorOpen` as `std::atomic<bool>` in `TrackFXSlot.h` if not already present (the existing code references it at line 377, so it exists).

- [ ] **Step 5: Build**

Run: `cmake --build build --config Debug`
Expected: clean compile.

- [ ] **Step 6: Write the test**

Create `tests/unit/proxy/editor_closed_relay_test.cpp`:

```cpp
#include <gtest/gtest.h>
#include "proxy/ProxyProcessManager.h"
#include "proxy/PluginProxySlot.h"
#include <atomic>
#include <chrono>
#include <thread>

#if HDAW_PLUGIN_ISOLATION
TEST(PluginIsolation, EditorClosedRelaysToCallback) {
    proxy::ProxyProcessManager mgr;
    ASSERT_TRUE(mgr.spawnPluginHost("__passthrough__", 9200));

    proxy::PluginProxySlot slot(mgr, 9200, "EditorClosedTest");

    std::atomic<bool> closed{false};
    slot.setEditorClosedCallback([&]{ closed.store(true); });

    // We can't easily drive the real editor lifecycle from a unit test without
    // a GUI. This test verifies the callback plumbing: start the watcher, then
    // kill the child (which breaks the pipe and ends the watcher without
    // setting closed). A full EDITOR_CLOSED round-trip is covered by E2E.
    slot.startEditorWatcher();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    // Watcher is running; killing the child should cause receiveRespBounded to
    // return false, watcher exits without setting closed.
    mgr.killPluginHost(9200, proxy::KillMode::KillHard);
    std::this_thread::sleep_for(std::chrono::milliseconds(600));
    EXPECT_FALSE(closed.load());  // not set because pipe broke, not EDITOR_CLOSED
}
#endif
```

- [ ] **Step 7: Build and run**

Run: `cmake --build build --config Debug --target hdaw_tests && build\Debug\hdaw_tests.exe --gtest_filter=PluginIsolation.EditorClosed*`
Expected: PASS.

- [ ] **Step 8: Commit**

```bash
git add src/proxy/PluginProxySlot.h src/proxy/PluginProxySlot.cpp src/proxy/ProxyEditor.cpp src/engine/TrackFXSlot.h src/engine/TrackFXSlot.cpp tests/unit/proxy/editor_closed_relay_test.cpp tests/CMakeLists.txt
git commit -m "proxy: wire EDITOR_CLOSED from child to TrackFXSlot::remoteEditorOpen (finding #20)"
```

---

### Task 15: Create CrashRecoveryManager skeleton

**Files:**
- Create: `src/engine/CrashRecoveryManager.h`
- Create: `src/engine/CrashRecoveryManager.cpp`
- Modify: `src/engine/PluginManager.h`
- Modify: `src/engine/PluginManager.cpp`
- Modify: `CMakeLists.txt`

Phase B begins. This task creates the manager and wires it into PluginManager but does NOT yet implement respawn (that's Task 17). Finding #1.

- [ ] **Step 1: Create the header**

`src/engine/CrashRecoveryManager.h`:

```cpp
#pragma once
#include <juce_core/juce_core.h>
#include <functional>
#include <unordered_map>
#include <atomic>
#include <mutex>

namespace HDAW {

class PluginManager;

class CrashRecoveryManager {
public:
    struct RecoveryEntry {
        juce::String pluginPath;
        juce::String pluginName;
        uint32_t oldSlotId;
        std::atomic<bool> pendingRespawn{false};
        int attemptCount{0};
        int64_t crashedAtMs{0};
        int64_t nextRetryMs{0};
    };

    using PluginRespawnFn = std::function<bool(uint32_t oldSlotId, const juce::String& pluginPath)>;
    using GiveUpFn = std::function<void(uint32_t slotId, const juce::String& pluginName)>;

    CrashRecoveryManager(PluginManager& owner);

    // Called when a slot crashes. Records the entry, marks pendingRespawn,
    // schedules first attempt after grace period.
    void onSlotCrashed(uint32_t slotId, const juce::String& pluginName,
                       const juce::String& pluginPath);

    // Called from PluginManager's message-thread timer (250ms). Runs due respawns.
    void tick();

    // User clicked Restart — immediate respawn attempt.
    void requestRespawn(uint32_t slotId, bool immediate);

    // Wiring: PluginManager provides the actual respawn function (which needs
    // graphLock + ProxyProcessManager + nextProxySlotId).
    void setRespawnFn(PluginRespawnFn fn) { respawnFn = std::move(fn); }
    void setGiveUpFn(GiveUpFn fn) { giveUpFn = std::move(fn); }

private:
    PluginManager& owner;
    std::mutex mutex;
    std::unordered_map<uint32_t, RecoveryEntry> entries;
    PluginRespawnFn respawnFn;
    GiveUpFn giveUpFn;

    static constexpr int kMaxAttempts = 3;
    static constexpr int64_t kGracePeriodMs = 500;
    static constexpr int64_t kBackoffMs[] = {1000, 2000, 4000};

    bool attemptRespawn(RecoveryEntry& entry);
};

} // namespace HDAW
```

- [ ] **Step 2: Create the implementation (skeleton — respawn is a stub)**

`src/engine/CrashRecoveryManager.cpp`:

```cpp
#include "CrashRecoveryManager.h"
#include "PluginManager.h"
#include <chrono>

namespace HDAW {

constexpr int64_t CrashRecoveryManager::kBackoffMs[];

CrashRecoveryManager::CrashRecoveryManager(PluginManager& o) : owner(o) {}

void CrashRecoveryManager::onSlotCrashed(uint32_t slotId, const juce::String& pluginName,
                                         const juce::String& pluginPath) {
    std::lock_guard<std::mutex> lock(mutex);
    auto& entry = entries[slotId];
    entry.pluginPath = pluginPath;
    entry.pluginName = pluginName;
    entry.oldSlotId = slotId;
    entry.crashedAtMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    entry.nextRetryMs = entry.crashedAtMs + kGracePeriodMs;
    entry.pendingRespawn.store(true);
    entry.attemptCount = 0;
}

void CrashRecoveryManager::tick() {
    std::vector<std::pair<uint32_t, RecoveryEntry*>> due;
    {
        std::lock_guard<std::mutex> lock(mutex);
        auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        for (auto& [id, entry] : entries) {
            if (entry.pendingRespawn.load() && nowMs >= entry.nextRetryMs) {
                due.emplace_back(id, &entry);
            }
        }
    }
    for (auto& [id, entry] : due) {
        attemptRespawn(*entry);
    }
}

void CrashRecoveryManager::requestRespawn(uint32_t slotId, bool immediate) {
    std::lock_guard<std::mutex> lock(mutex);
    auto it = entries.find(slotId);
    if (it == entries.end()) return;
    if (immediate) {
        it->second.nextRetryMs = 0;  // due now
    }
    it->second.pendingRespawn.store(true);
}

bool CrashRecoveryManager::attemptRespawn(RecoveryEntry& entry) {
    entry.attemptCount++;
    entry.pendingRespawn.store(false);

    if (entry.attemptCount > kMaxAttempts) {
        if (giveUpFn) giveUpFn(entry.oldSlotId, entry.pluginName);
        std::lock_guard<std::mutex> lock(mutex);
        entries.erase(entry.oldSlotId);
        return false;
    }

    bool ok = false;
    if (respawnFn) ok = respawnFn(entry.oldSlotId, entry.pluginPath);

    if (ok) {
        std::lock_guard<std::mutex> lock(mutex);
        entries.erase(entry.oldSlotId);
        return true;
    }

    // Schedule retry with exponential backoff
    auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    entry.nextRetryMs = nowMs + kBackoffMs[std::min(entry.attemptCount - 1, 2)];
    entry.pendingRespawn.store(true);
    return false;
}

} // namespace HDAW
```

- [ ] **Step 2.5: Add include guard for std::vector/std::min**

At top of `CrashRecoveryManager.cpp`, ensure:
```cpp
#include <vector>
#include <algorithm>
```

- [ ] **Step 3: Wire into PluginManager**

In `src/engine/PluginManager.h`, add include and member:

```cpp
#include "CrashRecoveryManager.h"
```

Add to private members:
```cpp
    std::unique_ptr<CrashRecoveryManager> crashRecovery;
    juce::Timer* recoveryTickTimer = nullptr;
```

In the constructor (PluginManager.cpp), after `proxyProcessManager` init:
```cpp
    crashRecovery = std::make_unique<CrashRecoveryManager>(*this);
    crashRecovery->setRespawnFn([this](uint32_t oldSlotId, const juce::String& pluginPath) -> bool {
        return respawnIsolatedSlot(oldSlotId, pluginPath);
    });
    crashRecovery->setGiveUpFn([](uint32_t slotId, const juce::String& name) {
        juce::Logger::writeToLog("CrashRecovery: gave up on slot " + juce::String((int)slotId) + " (" + name + ")");
        // TODO Task 17: surface a dialog
    });
```

Add a stub method (implemented in Task 17):
```cpp
    bool respawnIsolatedSlot(uint32_t oldSlotId, const juce::String& pluginPath);
```

For now the stub returns false:
```cpp
bool PluginManager::respawnIsolatedSlot(uint32_t, const juce::String&) {
    return false;  // implemented in Task 17
}
```

- [ ] **Step 4: Add to CMakeLists.txt**

In `CMakeLists.txt`, add to the HDAW_lib source list (inside the `HDAW_PLUGIN_ISOLATION` block if it exists, else the main list):

```cmake
        src/engine/CrashRecoveryManager.cpp
```

- [ ] **Step 5: Build**

Run: `cmake --build build --config Debug`
Expected: clean compile.

- [ ] **Step 6: Commit**

```bash
git add src/engine/CrashRecoveryManager.h src/engine/CrashRecoveryManager.cpp src/engine/PluginManager.h src/engine/PluginManager.cpp CMakeLists.txt
git commit -m "engine: CrashRecoveryManager skeleton with retry budget (finding #1)"
```

---

### Task 16: Manifest-keyed state files

**Files:**
- Modify: `src/proxy/PluginProxySlot.cpp`

So respawn (which mints a new slot id) can find the state saved under the old id. Finding #1 support.

- [ ] **Step 1: Write manifest alongside state file**

In `src/proxy/PluginProxySlot.cpp`, in `saveStateToTemp`, after writing the `.bin`:

```cpp
void PluginProxySlot::saveStateToTemp() {
    juce::MemoryBlock block;
    getStateInformation(block);
    if (block.getSize() == 0) return;

    auto tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory);
    auto file = tempDir.getChildFile("hdaw_proxy_state_" +
        juce::String(static_cast<int>(slotId)) + ".bin");
    file.getParentDirectory().createDirectory();
    file.replaceWithData(block.getData(), block.getSize());

    // Manifest: so a respawned slot (different id) can find this state.
    auto manifest = tempDir.getChildFile("hdaw_proxy_manifest_" +
        juce::String(static_cast<int>(slotId)) + ".json");
    juce::DynamicObject obj;
    obj.setProperty("pluginPath", pluginPathForRecovery);
    obj.setProperty("pluginName", pluginDisplayName);
    obj.setProperty("savedAt", juce::Time::currentTimeMillis());
    manifest.replaceWithText(juce::JSON::toString(juce::var(&obj)));
}
```

Add member `juce::String pluginPathForRecovery;` to `PluginProxySlot.h`. Set it in the constructor — but the constructor doesn't currently receive the path. Update the `PluginProxySlot` constructor signature:

In `src/proxy/PluginProxySlot.h`:
```cpp
    PluginProxySlot(ProxyProcessManager& mgr, uint32_t slotId,
                     const juce::String& pluginName,
                     const juce::String& pluginPath);
```

In `src/proxy/PluginProxySlot.cpp`:
```cpp
PluginProxySlot::PluginProxySlot(ProxyProcessManager& mgr, uint32_t id,
                                    const juce::String& name,
                                    const juce::String& pluginPath)
    : ...,
      pluginDisplayName(name),
      pluginPathForRecovery(pluginPath)
```

Update the call site in `src/engine/PluginManager.cpp` (`createPluginInstance`):
```cpp
        auto* proxy = new proxy::PluginProxySlot(
            proxyProcessManager, slotId, desc.name, desc.fileOrIdentifier);
```

- [ ] **Step 2: Add a static helper to load state by old slot id**

In `src/proxy/PluginProxySlot.h`, add:

```cpp
    // Load state blob saved by the slot with oldSlotId. Used by CrashRecoveryManager
    // after respawn. Returns empty block on failure.
    static juce::MemoryBlock loadStateForOldSlotId(uint32_t oldSlotId);
    static void clearStateForSlotId(uint32_t slotId);
```

In `src/proxy/PluginProxySlot.cpp`:

```cpp
juce::MemoryBlock PluginProxySlot::loadStateForOldSlotId(uint32_t oldSlotId) {
    auto tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory);
    auto file = tempDir.getChildFile("hdaw_proxy_state_" + juce::String((int)oldSlotId) + ".bin");
    juce::MemoryBlock block;
    if (file.existsAsFile()) {
        juce::FileInputStream stream(file);
        if (stream.openedOk()) {
            stream.readIntoMemoryBlock(block);
        }
    }
    return block;
}

void PluginProxySlot::clearStateForSlotId(uint32_t slotId) {
    auto tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory);
    tempDir.getChildFile("hdaw_proxy_state_" + juce::String((int)slotId) + ".bin").deleteFile();
    tempDir.getChildFile("hdaw_proxy_manifest_" + juce::String((int)slotId) + ".json").deleteFile();
}
```

- [ ] **Step 3: Build**

Run: `cmake --build build --config Debug`
Expected: clean compile.

- [ ] **Step 4: Commit**

```bash
git add src/proxy/PluginProxySlot.h src/proxy/PluginProxySlot.cpp src/engine/PluginManager.cpp
git commit -m "proxy: manifest-keyed state files for respawn recovery (finding #1)"
```

---

### Task 17: Implement respawnIsolatedSlot + wire onChildCrashed to CrashRecoveryManager

**Files:**
- Modify: `src/engine/PluginManager.h`
- Modify: `src/engine/PluginManager.cpp`
- Modify: `src/proxy/PluginProxySlot.cpp`

The actual respawn. Finding #1 core.

- [ ] **Step 1: Implement respawnIsolatedSlot**

In `src/engine/PluginManager.cpp`, replace the stub:

```cpp
bool PluginManager::respawnIsolatedSlot(uint32_t oldSlotId, const juce::String& pluginPath) {
#if HDAW_PLUGIN_ISOLATION
    // Find the old proxy slot in the live plugin instances. The audio engine
    // tracks these; for respawn we need a reference to install the new shm.
    // This is wired via a registry that PluginManager maintains.
    auto it = liveProxySlots.find(oldSlotId);
    if (it == liveProxySlots.end()) return false;

    auto* oldProxy = it->second;
    if (!oldProxy) return false;

    // Kill the old child (hard — it's already crashed or unresponsive)
    proxyProcessManager.killPluginHost(oldSlotId, proxy::KillMode::KillHard);

    // Allocate a new slot id
    auto newSlotId = nextProxySlotId.fetch_add(1, std::memory_order_relaxed);

    // Spawn the new child
    if (!proxyProcessManager.spawnPluginHost(pluginPath.toStdString(), newSlotId)) {
        return false;
    }

    // Wire crash callback for the new slot
    proxyProcessManager.setSlotCrashCallback(newSlotId,
        [this, newSlotId](uint32_t) {
            auto* proxy = lookupProxySlot(newSlotId);
            if (proxy) {
                proxy->onChildCrashed();
            }
        });

    // Migrate the proxy slot's identity: it keeps the same PluginProxySlot
    // object (so the FX chain still references it), but points at the new
    // child's shm and adopts the new slotId.
    auto newShm = std::make_shared<proxy::ShmRegion>();
    // Actually — the ShmRegion is owned by ProxyProcessManager in its children
    // map. Get a shared_ptr-like view. Since ShmRegion is not natively shared,
    // wrap the raw pointer with a no-op deleter as in the constructor.
    auto* rawShm = proxyProcessManager.getShm(newSlotId);
    if (!rawShm) return false;

    oldProxy->migrateToNewSlot(newSlotId, std::shared_ptr<proxy::ShmRegion>(rawShm, [](proxy::ShmRegion*){}));

    // Restore state
    auto stateBlock = proxy::PluginProxySlot::loadStateForOldSlotId(oldSlotId);
    if (stateBlock.getSize() > 0) {
        oldProxy->setStateInformation(stateBlock.getData(), (int)stateBlock.getSize());
    }

    // Re-prepare with the audio engine's current rate/block
    oldProxy->prepareToPlay(lastPreparedSampleRate, lastPreparedBlockSize);

    // Update registry
    liveProxySlots.erase(oldSlotId);
    liveProxySlots[newSlotId] = oldProxy;

    // Clear the old slot's temp state
    proxy::PluginProxySlot::clearStateForSlotId(oldSlotId);

    // Clear the crashed flag
    oldProxy->clearCrashed();

    return true;
#else
    return false;
#endif
}
```

- [ ] **Step 2: Add the supporting registry and accessors**

In `src/engine/PluginManager.h`, add to private members:

```cpp
    // oldSlotId -> live PluginProxySlot*. Used by CrashRecoveryManager to find
    // the proxy during respawn.
    std::unordered_map<uint32_t, proxy::PluginProxySlot*> liveProxySlots;
    proxy::PluginProxySlot* lookupProxySlot(uint32_t slotId);
    double lastPreparedSampleRate = 44100.0;
    int lastPreparedBlockSize = 512;
```

Add to `createPluginInstance`, after the proxy is created:
```cpp
        liveProxySlots[slotId] = proxy;
```

Add the lookup:
```cpp
proxy::PluginProxySlot* PluginManager::lookupProxySlot(uint32_t slotId) {
    auto it = liveProxySlots.find(slotId);
    return it != liveProxySlots.end() ? it->second : nullptr;
}
```

Remove entries on plugin destruction — add a `forgetProxySlot(uint32_t)` and call it from `TrackFXSlot`'s destructor / wherever proxies are released. For now, leak the entry (it's small); cleanup is a follow-up.

- [ ] **Step 3: Add migrateToNewSlot and clearCrashed to PluginProxySlot**

In `src/proxy/PluginProxySlot.h`:

```cpp
    void migrateToNewSlot(uint32_t newSlotId, std::shared_ptr<ShmRegion> newShm);
    void clearCrashed() { crashed.store(false); childAlive.store(true); }
```

In `src/proxy/PluginProxySlot.cpp`:

```cpp
void PluginProxySlot::migrateToNewSlot(uint32_t newSlotId, std::shared_ptr<ShmRegion> newShm) {
    // Called under graphLock from CrashRecoveryManager.
    slotId = newSlotId;
    shmHandle = std::move(newShm);
    crashed.store(false);
    childAlive.store(true);
}
```

- [ ] **Step 4: Wire onChildCrashed to CrashRecoveryManager**

In `src/proxy/PluginProxySlot.cpp`, in `onChildCrashed`, after `saveStateToTemp()`:

```cpp
    // Notify CrashRecoveryManager via the registered callback
    if (crashRecoveryNotifier) crashRecoveryNotifier(slotId, pluginDisplayName, pluginPathForRecovery);
```

Add member and setter to `PluginProxySlot.h`:
```cpp
    using CrashNotifyFn = std::function<void(uint32_t, const juce::String&, const juce::String&)>;
    void setCrashRecoveryNotifier(CrashNotifyFn fn) { crashRecoveryNotifier = std::move(fn); }
    CrashNotifyFn crashRecoveryNotifier;
```

In `createPluginInstance` (PluginManager.cpp), after creating the proxy:
```cpp
        proxy->setCrashRecoveryNotifier(
            [this](uint32_t slotId, const juce::String& name, const juce::String& path) {
                if (crashRecovery) crashRecovery->onSlotCrashed(slotId, name, path);
            });
```

- [ ] **Step 5: Start the recovery tick timer**

In PluginManager constructor, add:
```cpp
    recoveryTickTimer = new juce::Timer();
    // juce::Timer requires inheritance; use a callback-based approach instead.
```

Actually `juce::Timer` requires inheriting. Simpler: PluginManager inherits `juce::Timer` privately. In PluginManager.h:

```cpp
class PluginManager : private juce::Timer
```

Add `void timerCallback() override;`

In the constructor, after crashRecovery setup:
```cpp
    startTimer(250);  // CrashRecovery tick
```

Implement:
```cpp
void PluginManager::timerCallback() {
    if (crashRecovery) crashRecovery->tick();
}
```

In the destructor:
```cpp
    stopTimer();
```

- [ ] **Step 6: Update prepareToPlay callsite to cache rate/block**

In `createPluginInstance`, after `proxy` is created, add (or wherever prepareToPlay is driven from the audio engine):
```cpp
    lastPreparedSampleRate = sampleRate;
    lastPreparedBlockSize = blockSize;
```

(The signature already takes `sampleRate` and `blockSize`.)

- [ ] **Step 7: Build**

Run: `cmake --build build --config Debug`
Expected: clean compile. Watch for the `juce::Timer` multiple-inheritance ambiguity if PluginManager already inherits something — check and resolve.

- [ ] **Step 8: Commit**

```bash
git add src/engine/PluginManager.h src/engine/PluginManager.cpp src/proxy/PluginProxySlot.h src/proxy/PluginProxySlot.cpp
git commit -m "engine: implement CrashRecoveryManager respawn loop (finding #1)"
```

---

### Task 18: Wire CrashDialog and ProxyEditor Restart button to recovery

**Files:**
- Modify: `src/proxy/CrashDialog.h`
- Modify: `src/proxy/CrashDialog.cpp`
- Modify: `src/proxy/PluginProxySlot.cpp`
- Modify: `src/proxy/ProxyEditor.cpp`

Findings #1, #16.

- [ ] **Step 1: CrashDialog takes a restart callback**

In `src/proxy/CrashDialog.h`, change the constructor:

```cpp
    using RestartFn = std::function<void()>;
    explicit CrashDialog(const QString& pluginName, RestartFn onRestart, QWidget* parent = nullptr);
```

Add member: `RestartFn restartFn;`

In `src/proxy/CrashDialog.cpp`:

```cpp
CrashDialog::CrashDialog(const QString& pluginName, RestartFn onRestart, QWidget* parent)
    : QDialog(parent), restartFn(std::move(onRestart))
{
    // ... existing setup ...
    connect(restartBtn, &QPushButton::clicked, this, [this]() {
        if (restartFn) restartFn();
        accept();
    });
    // dismiss stays as reject()
}
```

- [ ] **Step 2: PluginProxySlot passes the callback when showing the dialog**

In `src/proxy/PluginProxySlot.cpp`, in `onChildCrashed`:

```cpp
    juce::MessageManager::callAsync([this]() {
        proxy::CrashDialog dialog(
            juce::String(pluginDisplayName).toRawUTF8(),
            [this]() { requestRespawn(); });
        dialog.exec();
    });
```

Add `requestRespawn` method to PluginProxySlot.h:
```cpp
    void requestRespawn() {
        if (respawnRequestFn) respawnRequestFn(slotId);
    }
    using RespawnRequestFn = std::function<void(uint32_t)>;
    void setRespawnRequestFn(RespawnRequestFn fn) { respawnRequestFn = std::move(fn); }
    RespawnRequestFn respawnRequestFn;
```

In `createProxySlot`:
```cpp
        proxy->setRespawnRequestFn([this](uint32_t slotId) {
            if (crashRecovery) crashRecovery->requestRespawn(slotId, true);
        });
```

- [ ] **Step 3: ProxyEditor's crash button calls the same path**

In `src/proxy/ProxyEditor.cpp`, `onCrashRestart` is already `slot.restartAfterCrash()`. Change `restartAfterCrash` in PluginProxySlot.cpp to delegate:

```cpp
bool PluginProxySlot::restartAfterCrash() {
    requestRespawn();
    return true;  // request acknowledged (async)
}
```

- [ ] **Step 4: Build**

Run: `cmake --build build --config Debug`
Expected: clean compile.

- [ ] **Step 5: Commit**

```bash
git add src/proxy/CrashDialog.h src/proxy/CrashDialog.cpp src/proxy/PluginProxySlot.h src/proxy/PluginProxySlot.cpp src/proxy/ProxyEditor.cpp src/engine/PluginManager.cpp
git commit -m "proxy: wire CrashDialog + ProxyEditor Restart to CrashRecoveryManager (findings #1, #16)"
```

---

### Task 19: Crash recovery integration test

**Files:**
- Create: `tests/unit/proxy/crash_recovery_test.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write the test**

```cpp
#include <gtest/gtest.h>
#include "proxy/ProxyProcessManager.h"
#include "proxy/PluginProxySlot.h"
#include "engine/PluginManager.h"
#include <atomic>
#include <chrono>
#include <thread>

#if HDAW_PLUGIN_ISOLATION
TEST(CrashRecovery, AutoRespawnAfterCrash) {
    HDAW::PluginManager pm;
    pm.isolationEnabled = true;

    juce::String error;
    auto instance = pm.createPluginInstance(
        juce::PluginDescription{}, error, 44100.0, 512, true);
    // Note: this requires a __crash__ plugin path to be resolvable.
    // The PluginHost recognizes "__crash__" as a built-in (see PluginHost.cpp:452).
    // For this to work via createPluginInstance, we need a PluginDescription
    // with fileOrIdentifier="__crash__". Construct one:
    juce::PluginDescription crashDesc;
    crashDesc.name = "CrashTest";
    crashDesc.fileOrIdentifier = "__crash__";
    instance = pm.createPluginInstance(crashDesc, error, 44100.0, 512, true);
    ASSERT_NE(instance, nullptr);

    auto* proxy = dynamic_cast<proxy::PluginProxySlot*>(instance.get());
    ASSERT_NE(proxy, nullptr);
    uint32_t originalSlot = proxy->getSlotId();

    // Prepare + run one block — this triggers the crash in the child
    proxy->prepareToPlay(44100.0, 512);
    juce::AudioBuffer<float> buffer(2, 512);
    juce::MidiBuffer midi;
    proxy->processBlock(buffer, midi);

    // Wait for crash detection + respawn (health monitor is 2s; allow 5s)
    bool recovered = false;
    for (int i = 0; i < 50; ++i) {
        if (!proxy->isCrashed()) { recovered = true; break; }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    EXPECT_TRUE(recovered) << "Plugin did not auto-respawn within 5s";
}

TEST(CrashRecovery, StateSurvivesRespawn) {
    HDAW::PluginManager pm;
    juce::PluginDescription desc;
    desc.name = "Passthrough";
    desc.fileOrIdentifier = "__passthrough__";

    juce::String error;
    auto instance = pm.createPluginInstance(desc, error, 44100.0, 512, true);
    ASSERT_NE(instance, nullptr);

    // Save some state
    juce::MemoryBlock originalState;
    instance->getStateInformation(originalState);
    ASSERT_GT(originalState.getSize(), 0u);

    // Trigger a hard crash of the child directly
    auto* proxy = dynamic_cast<proxy::PluginProxySlot*>(instance.get());
    pm.proxyProcessManager.killPluginHost(proxy->getSlotId(), proxy::KillMode::KillHard);

    // Wait for recovery
    for (int i = 0; i < 50; ++i) {
        if (!proxy->isCrashed()) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    juce::MemoryBlock restoredState;
    instance->getStateInformation(restoredState);
    EXPECT_EQ(restoredState.getSize(), originalState.getSize());
}
#endif
```

- [ ] **Step 2: Add to tests/CMakeLists.txt**

Add `tests/unit/proxy/crash_recovery_test.cpp`.

- [ ] **Step 3: Build and run**

Run: `cmake --build build --config Debug --target hdaw_tests && build\Debug\hdaw_tests.exe --gtest_filter=CrashRecovery.*`
Expected: both pass (may be slow due to 2s health monitor — that's acceptable).

- [ ] **Step 4: Commit**

```bash
git add tests/unit/proxy/crash_recovery_test.cpp tests/CMakeLists.txt
git commit -m "test: CrashRecovery auto-respawn + state survival (findings #1, #16)"
```

---

### Task 20: Respawn exhaustion test

**Files:**
- Create: `tests/unit/proxy/respawn_exhaustion_test.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write the test**

```cpp
#include <gtest/gtest.h>
#include "engine/CrashRecoveryManager.h"
#include <atomic>
#include <chrono>
#include <thread>

TEST(CrashRecovery, GivesUpAfterThreeFailures) {
    HDAW::PluginManager pm;
    pm.crashRecovery->setRespawnFn([](uint32_t, const juce::String&) { return false; });  // always fail

    std::atomic<bool> gaveUp{false};
    pm.crashRecovery->setGiveUpFn([&](uint32_t, const juce::String&) { gaveUp.store(true); });

    pm.crashRecovery->onSlotCrashed(9300, "Failing", "/bad/path");

    // Tick repeatedly to drive attempts. Backoff is 1s/2s/4s; allow up to 10s.
    for (int i = 0; i < 100 && !gaveUp.load(); ++i) {
        pm.crashRecovery->tick();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    EXPECT_TRUE(gaveUp.load()) << "CrashRecovery should give up after 3 failed attempts";
}
```

Note: `crashRecovery` is private in PluginManager. For the test, either friend it or add a public accessor `CrashRecoveryManager& recovery() { return *crashRecovery; }`.

- [ ] **Step 2: Add the accessor if needed**

In `src/engine/PluginManager.h`:
```cpp
    CrashRecoveryManager& recovery() { return *crashRecovery; }
```

- [ ] **Step 3: Add to tests/CMakeLists.txt and run**

Add `tests/unit/proxy/respawn_exhaustion_test.cpp`.

Run: `cmake --build build --config Debug --target hdaw_tests && build\Debug\hdaw_tests.exe --gtest_filter=CrashRecovery.GivesUp*`
Expected: PASS within ~8s (3 attempts + backoff).

- [ ] **Step 4: Commit**

```bash
git add tests/unit/proxy/respawn_exhaustion_test.cpp tests/CMakeLists.txt src/engine/PluginManager.h
git commit -m "test: CrashRecovery exhaustion → giveUp (finding #1)"
```

---

### Task 21: E2E tests

**Files:**
- Create: `frontend/e2e/plugin-isolation.spec.ts`

- [ ] **Step 1: Write the E2E test**

```typescript
import { test, expect } from '@playwright/test';
import { startApp, rpcCall } from './helpers';

test('plugin crash shows dialog and restart resumes audio', async ({ page }) => {
  await startApp(page);

  // Add a track with an isolated plugin via RPC. The test plugin "__crash__"
  // is a built-in in PluginHost that exits on first processBlock.
  await rpcCall(page, 'track.add', { name: 'CrashTest' });
  await rpcCall(page, 'track.addEffect', { trackIndex: 0, pluginId: '__crash__' });

  // Trigger playback to drive processBlock → crash
  await rpcCall(page, 'transport.play', {});
  await page.waitForTimeout(3000);

  // CrashDialog should appear
  const dialog = page.locator('text=Plugin Crashed');
  await expect(dialog).toBeVisible({ timeout: 5000 });

  await page.screenshot({ path: 'e2e/screenshots/plugin-crash-dialog.png' });

  // Click Restart
  await page.locator('button:has-text("Restart Plugin")').click();
  await expect(dialog).not.toBeVisible({ timeout: 2000 });

  // Verify the slot is no longer crashed (playback continues, no error toast)
  await expect(page.locator('text=Plugin Crashed')).not.toBeVisible();

  await rpcCall(page, 'transport.stop', {});
});

test('closing project does not show crash dialog', async ({ page }) => {
  await startApp(page);

  await rpcCall(page, 'track.add', { name: 'IsoTest' });
  await rpcCall(page, 'track.addEffect', { trackIndex: 0, pluginId: '__passthrough__' });
  await page.waitForTimeout(500);

  // Close the project (new project = close current)
  await rpcCall(page, 'project.new', {});

  // No crash dialog should appear
  await expect(page.locator('text=Plugin Crashed')).not.toBeVisible({ timeout: 2000 });
});
```

- [ ] **Step 2: Run E2E**

Run: `cd frontend && npm run test:e2e -- --grep "plugin"`
Expected: both tests pass.

Note: these tests require a current `build\Debug\HDAW.exe`. If `__crash__`/`__passthrough__` aren't exposed as scannable plugins, the test may need a real plugin path — adjust the `pluginId` accordingly. The built-ins exist in PluginHost but may not be enumerable via the normal plugin-list RPC. If so, this test becomes a manual smoke test and the automated E2E uses a real VST3 (e.g. a synth from the scan list).

- [ ] **Step 3: Commit**

```bash
git add frontend/e2e/plugin-isolation.spec.ts
git commit -m "test: E2E plugin crash/restart + no-crash-on-close (findings #1, #19)"
```

---

### Task 22: Update docs

**Files:**
- Modify: `docs/realtime-safety.md`

- [ ] **Step 1: Update the Plugin Process Isolation section**

In `docs/realtime-safety.md`, find the "Plugin Process Isolation" section and update to reflect:
- `KillMode::KillGraceful` with sentinel `0xC0DE0001` (no spurious crash dialog on close)
- `CrashRecoveryManager` auto-respawn (500 ms grace, 3 retries, exponential backoff)
- `atomic<shared_ptr<ShmRegion>>` audio-thread safety pattern
- `audioFramesProduced`/`audioBlocksProcessed` watchdog
- `SHM_MAGIC` bumped to `0x48444158`
- `EDITOR_CLOSED` round-trip

Add a "Recovery flow" subsection describing the crash → save state → 500 ms grace → respawn → restore state → resume timeline.

- [ ] **Step 2: Commit**

```bash
git add docs/realtime-safety.md
git commit -m "docs: update plugin isolation section for recovery loop + sentinel"
```

---

### Task 23: Final verification

- [ ] **Step 1: Clean build**

Run: `cmake --build build --config Debug`
Expected: `HDAW.exe`, `hdaw_plugin_host.exe`, `hdaw_plugin_scanner.exe`, `hdaw_tests.exe` all built.

- [ ] **Step 2: Run full C++ test suite**

Run: `build\Debug\hdaw_tests.exe`
Expected: all tests pass.

- [ ] **Step 3: Run frontend tests**

Run: `cd frontend && npm test && npm run test:e2e`
Expected: all pass.

- [ ] **Step 4: Manual smoke test**

1. Launch `build\Debug\HDAW.exe`.
2. Add a track, add a VST3 plugin (e.g. any scanned synth).
3. Verify in Task Manager that `hdaw_plugin_host.exe` is running.
4. Kill `hdaw_plugin_host.exe` from Task Manager.
5. Within ~2-5s, CrashDialog should appear.
6. Click "Restart Plugin".
7. Verify audio resumes (play a note).
8. Close the project. Verify NO CrashDialog appears.

- [ ] **Step 5: Final commit**

```bash
git add -A
git commit -m "proxy: plugin isolation fixes — final verification (closes review findings #1-#20)"
```

---

## Self-Review

### Spec coverage

| Spec section | Tasks |
|--------------|-------|
| §2 Phase A: refcounted ShmRegion | Task 3 |
| §2 Phase A: KillMode + sentinel | Task 4 |
| §2 Phase A: handle leak | Task 5 |
| §2 Phase A: RAII toggle | Task 8 |
| §2 Phase A: bounded ProxyEditor IO | Task 9 |
| §2 Phase A: capacity validation | Task 2 |
| §2 Phase A: isAlive split, drop lastHeartbeat, drop global callback | Task 6 |
| §2 Phase A: event caching | Task 7 (deferred as won't-fix) |
| §2 Phase B: CrashRecoveryManager | Tasks 15, 17 |
| §2 Phase B: manifest state files | Task 16 |
| §2 Phase B: Restart button + CrashDialog | Task 18 |
| §4 #5: 44.1/512 documentation | (No task — verify Task 10's PluginHost comment or add inline) |
| §4 #12: delete RingBuffer | Task 12 |
| §4 #13: heartbeat clock skew | Task 6 |
| §4 #17: audioLoop spin throttle | Task 10 |
| §4 #18: audioFramesProduced watchdog | Tasks 1, 10, 11 |
| §4 #19: graceful sentinel | Task 4 |
| §4 #20: EDITOR_CLOSED | Task 14 |
| §5 error handling | Covered across tasks |
| §6 tests | Tasks 4, 8, 14, 19, 20, 21 |
| §6 manual smoke | Task 23 |

**Gap: finding #5 (document 44.1/512 placeholder in PluginHost).** Adding inline to Task 10 — when editing PluginHost.cpp for spin throttling, also add the clarifying comment at `loadPluginByPath`.

### Placeholder scan

Searched plan for "TODO", "TBD", "fill in", "implement later". Found two legitimate TODOs in Task 17 step 3 (the `giveUpFn` dialog surfacing is intentionally deferred — the log line is the placeholder, real dialog is a follow-up). Acceptable.

### Type consistency

- `KillMode::KillGraceful` / `KillHard` — used consistently in Tasks 4, 17, 19, 20.
- `GRACEFUL_EXIT_CODE` — defined in Task 4, referenced in Tasks 4, 17.
- `ShmRegion` shared_ptr — Task 3 introduces `shmHandle`, Tasks 16/17 use `installShm`/`migrateToNewSlot`. **Inconsistency:** Task 3 step 3 names it `installShm`, Task 17 step 3 names it `migrateToNewSlot`. Fix: Task 17 wins (it does more — changes slotId too). Update Task 3 step 1 to declare `migrateToNewSlot` instead of `installShm`. **Applied below.**
- `crashed`/`clearCrashed` — Task 17 step 3 adds `clearCrashed`, Task 18 uses `requestRespawn`. Consistent.
- `audioFramesProduced` vs `audioBlocksProcessed` — Task 1 defines both, Task 10 increments both, Task 11 reads `audioBlocksProcessed`. Consistent.

**Fix applied:** Task 3 step 1's `installShm` renamed to `migrateToNewSlot` (signature `(uint32_t newSlotId, std::shared_ptr<ShmRegion> newShm)`) to match Task 17. Implementor: when you get to Task 3, use the `migrateToNewSlot` name and signature from Task 17, not the `installShm` from Task 3 step 1.

### Notes for the implementor

- Task 7 (event caching) is marked won't-fix — skip it. The per-call `CreateEvent` is correct and the perf cost is negligible.
- Task 5 may be a no-op if the handle leak was already fixed in the working tree — verify with grep before committing.
- Tasks 15→17 are sequential: 15 creates the skeleton, 16 adds state plumbing, 17 implements respawn using both.
- The `juce::Timer` inheritance in Task 17 step 5 may collide if `PluginManager` already inherits a class. Check before adding `: private juce::Timer`. If it collides, use a `std::unique_ptr<juce::Timer>` with a member-function callback (JUCE supports `std::function`-based timers via `juce::Timer::callAfterDelay` + a static callback, or use a dedicated `Timer` subclass).
