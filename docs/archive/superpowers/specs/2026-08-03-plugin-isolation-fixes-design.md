# Plugin Process Isolation — Fixes Design

> **Date:** 2026-08-03
> **Status:** Approved
> **Goal:** Close the defects surfaced by the 2026-08-03 review of the plugin
> isolation subsystem. The headline issues are: (1) the Restart button is a
> dead no-op, (2) `~PluginProxySlot` races the audio thread over a cached
> `ShmRegion*` (use-after-free window), (3) every graceful project close
> spuriously fires the "Plugin crashed!" dialog, and (4) a handle leak on the
> no-READY spawn path.
>
> **Predecessors:**
> - `docs/superpowers/specs/2026-06-30-plugin-process-isolation-design.md` (original architecture)
> - `docs/superpowers/plans/2026-07-30-plugin-isolation-default-on.md` (default-on rollout; Tasks 5/6 left half-finished — this spec finishes them)

## 1. Findings addressed

All 20 findings from the 2026-08-03 review. Tiers:

| Tier | Findings |
|------|----------|
| Critical (crash / data loss / silent feature) | #1 Restart button dead, #3 cached-shm use-after-free, #4 audio-thread early-out race, #19 spurious crash dialog on graceful close |
| High (resource / correctness) | #2 handle leak on no-READY, #5 child `loadPluginByPath` hardcoded 44.1/512, #6 1 s lock-held `WaitForSingleObject` in `spawnPluginHost`'s defensive kill, #7 `isAlive` mutates under const-looking query, #8 `getOutputRing`/`getMidiInRing` no capacity validation, #9 `killPluginHost(true)` invalidates cached shm during respawn, #10 `isolationEnabled` not RAII, #11 `PipeServer` allocs an event per IO |
| Medium (UX / perf) | #12 `RingBuffer.h` dead code, #13 `lastHeartbeat` clock-skew vs `childAlive`, #14 global `crashCallback` never wired, #15 `ProxyEditor::onOpenEditorClicked` blocks message thread forever, #16 `CrashDialog` result ignored, #17 `audioLoop` busy-spin, #18 no per-block watchdog inside child |
| Low (spec drift) | #20 `EDITOR_CLOSED` unhandled on parent side |

## 2. Architecture

Two phases.

### Phase A — make the crash path non-leaking and correctly observable

- **`ShmRegion` becomes refcounted.** `PluginProxySlot` holds a
  `std::atomic<std::shared_ptr<ShmRegion>>` (C++20 atomic smart pointer). The
  audio thread does `std::atomic_load(...)` once at the top of `processBlock`,
  holds the resulting `shared_ptr` in a local for the duration of the callback,
  and releases it on exit. The destructor does
  `std::atomic_store(handle, nullptr)` from the message thread — the audio
  thread's local copy keeps the `ShmRegion` alive until the callback returns,
  after which the last reference drops and the region is freed. This eliminates
  the raw `cachedShm` pointer and the use-after-free window (findings #3, #4,
  #9).
- **Graceful shutdown is distinguished from crash.** A new sentinel exit code
  `0xC0DE0001` ("graceful") is introduced. `ProxyProcessManager::killPluginHost`
  gains a `KillMode` enum (`KillGraceful | KillHard`):
  - `KillGraceful` — send `SHUTDOWN` over the pipe; wait up to 2 s; if still
    alive, `TerminateProcess(handle, 0xC0DE0001)`. Always `CloseHandle`.
  - `KillHard` — existing behavior (TerminateProcess with 0, WaitForSingleObject
    1 s, CloseHandle, erase).
- **Crash detection filters on exit code.**
  `ProxyProcessManager::checkAllChildren` treats `exitCode == 0xC0DE0001` as
  graceful — no crash callback fires, no slot is marked crashed. Any other
  non-`STILL_ACTIVE` exit fires the crash callback. This kills the spurious
  "Plugin crashed!" dialog on project close (finding #19).
- **Handle leak fix.** `spawnPluginHost`'s no-READY branch closes
  `pi.hProcess` after `TerminateProcess` (finding #2).
- **RAII isolation toggle.** `ExportManager` replaces the manual
  save/restore of `PluginManager::isolationEnabled` with a scope guard
  (finding #10).
- **Bounded `ProxyEditor::onOpenEditorClicked`.** Switch from
  `sendMsg`/`receiveResp` (INFINITE) to `sendMsgBounded`/`receiveRespBounded`
  with a 2 s timeout; disable the button on timeout (finding #15).

### Phase B — auto-respawn loop and a working Restart button

- **New `CrashRecoveryManager`** (in `src/engine/`, owned by `PluginManager`).
  Tracks slots that have crashed and schedules respawns.
  ```cpp
  class CrashRecoveryManager {
  public:
      void onSlotCrashed(uint32_t slotId, juce::String pluginName,
                         juce::String pluginPath);
      void requestRespawn(uint32_t slotId, bool immediate);
      void tick();  // called from PluginManager's message-thread timer (250 ms)
      void giveUp(uint32_t slotId);  // after 3 failed attempts
  };
  ```
  State per slot: `{ pluginPath, pluginName, crashedAt, pendingRespawn,
  attemptCount }`.

- **Respawn sequence** (runs on the message thread, inside
  `AudioEngine`'s `graphLock`-guarded section so the audio callback is
  guaranteed not to be in flight):
  1. `killPluginHost(slotId, KillHard)` — fully tears down the old child and
     its shm.
  2. Allocate a new slot id from `PluginManager::nextProxySlotId`.
  3. `ProxyProcessManager::spawnPluginHost(pluginPath, newSlotId)` — creates a
     fresh child + shm + pipe.
  4. Replace the `PluginProxySlot`'s `atomic<shared_ptr<ShmRegion>>` with the
     new region. The slot's processBlock now reads the new region.
  5. `restoreStateFromTemp()` — load state blob from the manifest-keyed file
     (see below) and send `SET_STATE` to the new child.
  6. Clear `crashed` flag; resume audio.

- **Manifest file for state recovery.** Current state files are keyed by slot
  id, but respawn mints a new slot id. Fix:
  - `saveStateToTemp()` writes `hdaw_proxy_state_<slotId>.bin` AND a
    `hdaw_proxy_manifest_<slotId>.json` containing
    `{ "pluginPath": "...", "pluginName": "...", "savedAt": <epoch> }`.
  - `CrashRecoveryManager` carries the *old* slot id into the respawn; after
    successful restore, both files are deleted.

- **Restart button path.**
  - `CrashDialog::accept()` (Restart Plugin) calls
    `recoveryMgr->requestRespawn(slotId, /*immediate=*/true)` then closes.
  - `CrashDialog::reject()` (Dismiss) just closes; the auto-respawn timer
    still fires after the 500 ms grace period if the user did nothing.
  - `ProxyEditor::onCrashRestart` (the in-DAW "Restart?" button) calls
    `slot->requestRespawn()` which delegates to the same
    `CrashRecoveryManager::requestRespawn` path.

- **Bounded retry.** 3 attempts, exponential backoff (1 s, 2 s, 4 s). After
  the 3rd failure, `giveUp(slotId)` fires:
  - Log to `hdaw_debug.log`.
  - Slot stays silent (`crashed` remains true, `processBlock` returns).
  - Surface a one-shot "Plugin unrecoverable — bypassed, please reload project
    to recover" dialog.

## 3. Components and data flow

```
                  ┌──────────────────────────────────────────────┐
                  │ CrashRecoveryManager (in PluginManager)      │
                  │  onSlotCrashed / requestRespawn / tick       │
                  └────────────────┬─────────────────────────────┘
                                   │ message thread, under graphLock
        ┌──────────────────────────┴────────────────────────────┐
        │                                                          │
        ▼                                                          ▼
┌────────────────────┐  spawn/kill    ┌────────────────────────┐
│ ProxyProcessManager│<───────────────│  PluginProxySlot       │
│  KillGraceful      │                │  atomic<shared_ptr<    │
│  KillHard          │───────────────>│   ShmRegion>> shm      │
│  exitCode filter   │  new ShmRegion │  crashed (atomic)      │
└────────────────────┘                │  requestRespawn()      │
        │                             └────────────┬───────────┘
        │ child exit code                          │ audio thread
        ▼                                          ▼
┌────────────────────┐  SHM rings   ┌────────────────────────┐
│ checkAllChildren   │<─────────────│ processBlock:          │
│ 0xC0DE0001 = OK    │              │  atomic_load(shm)      │
│ other     = crash  │              │  hold local shared_ptr │
└────────────────────┘              │  release on exit       │
                                    └────────────────────────┘
```

**Crash → respawn timeline (happy path):**
1. Child dies (bug, hang, external kill). Exit code != `0xC0DE0001`.
2. `checkAllChildren` (2 s tick) detects it, fires per-slot crash callback.
3. `PluginProxySlot::onChildCrashed` runs on health-monitor thread:
   - `crashed.store(true)`, `saveStateToTemp()` (writes bin + manifest).
   - `MessageManager::callAsync` → `CrashDialog` shown on message thread.
   - `CrashRecoveryManager::onSlotCrashed` registers the slot.
4. Audio thread sees `crashed==true` in next `processBlock`, returns silence.
5. After 500 ms grace, `tick()` runs the respawn sequence under `graphLock`.
6. New child sends READY, new shm installed atomically, state restored.
7. `crashed.store(false)`. Audio resumes on next callback.
8. CrashDialog auto-dismisses if still open (the slot it referenced is gone).

**User-clicks-Restart timeline:**
1. User clicks Restart in CrashDialog (or in ProxyEditor's crash button).
2. `requestRespawn(slotId, immediate=true)` — skips the 500 ms grace.
3. Same respawn sequence as steps 5-8 above.

## 4. Per-finding fix map

| # | Finding | Fix | Files |
|---|---------|-----|-------|
| 1 | Restart button dead | `CrashRecoveryManager` + real respawn | new `src/engine/CrashRecoveryManager.{h,cpp}`, `PluginProxySlot.cpp`, `PluginManager.cpp` |
| 2 | Handle leak on no-READY | `CloseHandle(pi.hProcess)` in fail branch | `ProxyProcessManager.cpp` |
| 3 | Cached-shm use-after-free | `atomic<shared_ptr<ShmRegion>>` | `PluginProxySlot.{h,cpp}`, `ProxyProcessManager.{h,cpp}` |
| 4 | Audio early-out race | Same as #3 (audio holds local shared_ptr) | `PluginProxySlot.cpp` |
| 5 | Hardcoded 44.1/512 in child `loadPluginByPath` | **Not a bug — clarified as intentional.** `loadPlugin()` runs from `PluginHost::run()` *before* the `PREPARE` message arrives (see `PluginHost.cpp:180`), so the target rate is unknown at instantiation time. The 44100/512 passed to `createPluginInstance` is a placeholder for JUCE's constructor; the real rate/block arrive via `PREPARE` and override via `prepareToPlay`. Add a comment documenting this; no behavior change. | `PluginHost.cpp` |
| 6 | 1 s lock-held wait in defensive kill | Drop the lock before `WaitForSingleObject` in `killPluginHost` (re-lock to erase) | `ProxyProcessManager.cpp` |
| 7 | `isAlive` mutates under const query | Split into `isAliveConst` (read-only) vs `markDead` (mutate); const-qualify the query | `ProxyProcessManager.{h,cpp}` |
| 8 | No capacity validation in shm getters | Validate `hdr->capacity != 0 && capacityMatches(hdr)` before pointer arithmetic; return nullptr on mismatch | `ProxySharedMemory.cpp` |
| 9 | `killPluginHost(true)` invalidates cached shm | Same as #3 (refcounted shm survives the kill) | covered by #3 |
| 10 | `isolationEnabled` not RAII | Scope guard in `ExportManager` | `ExportManager.cpp` |
| 11 | Event allocated per IO in `PipeServer` | Cache one `OVERLAPPED`+event per `PipeServer` instance; reset per call | `ProxyPipe.{h,cpp}` |
| 12 | `RingBuffer.h` dead code | Delete the file and its test; document that the shm-backed inline ring in `PluginHost::audioLoop`/`PluginProxySlot::processBlock` is the canonical implementation | `src/proxy/ProxyRingBuffer.h`, `tests/unit/proxy/RingBufferTest.cpp`, `tests/CMakeLists.txt` |
| 13 | Heartbeat clock skew | Drop `ChildInfo::lastHeartbeat` (parent-side struct, not in shm — no binary compat concern). `checkAllChildren` uses only `childAlive` atomic (already updated by the child on each heartbeat, see `PluginHost.cpp:330`) plus the process exit code. `sendHeartbeat` no longer writes `lastHeartbeat`. | `ProxyProcessManager.{h,cpp}` |
| 14 | Global `crashCallback` never wired | Delete the global callback API; keep only per-slot callbacks | `ProxyProcessManager.{h,cpp}` |
| 15 | `onOpenEditorClicked` blocks forever | Bounded IO with 2 s timeout; disable button on failure | `ProxyEditor.cpp` |
| 16 | CrashDialog result ignored | Wire accept/reject to `CrashRecoveryManager` | `CrashDialog.{h,cpp}`, `PluginProxySlot.cpp` |
| 17 | `audioLoop` busy-spin | `SwitchToThread()` once every 64 underrun iterations; `Sleep(0)` otherwise | `PluginHost.cpp` |
| 18 | No per-block watchdog in child | Add `audioFramesProduced` atomic to `ShmHeader`. **Bump `SHM_MAGIC` from `0x48444157` to `0x48444158`** so a stale child binary fails the magic check rather than read garbage from the new field's offset. Parent's health check treats a stalled counter as a crash. | `ProxyCommon.h`, `PluginHost.cpp`, `ProxyProcessManager.cpp` |
| 19 | Spurious crash on graceful close | Sentinel exit code `0xC0DE0001` + `KillGraceful` path | `ProxyProcessManager.{h,cpp}`, `PluginHost.cpp` (exit with sentinel after SHUTDOWN) |
| 20 | `EDITOR_CLOSED` unhandled | Wire `EDITOR_CLOSED` response → `TrackFXSlot::remoteEditorOpen = false` + notify editor host | `TrackFXSlot.{h,cpp}`, `PluginProxySlot.cpp` |

## 5. Error handling

| Scenario | Behavior |
|----------|----------|
| `CreateProcess` fails | Existing path: `errorMessage` returned, slot not created. |
| Child doesn't send READY within 8 s | Existing `kReadyTimeoutMs` in `PipeServer` handles this. `spawnPluginHost` returns false; slot is destroyed. |
| Child hangs in `processBlock` | `audioFramesProduced` atomic in shm stalls; parent's health check detects it within 2 s. Slot marked crashed; respawn scheduled. |
| Child exits gracefully (SHUTDOWN) | `exitCode == 0xC0DE0001`; no crash callback; no spurious dialog. |
| Child exits due to bug or external kill | Crash callback fires; recovery loop starts. |
| Respawn fails (CreateProcess fails again) | Increment `attemptCount`. Retry with exponential backoff (1 s, 2 s, 4 s). After 3rd failure: `giveUp()`. |
| Respawn succeeds but state restore fails | Slot loads with default state; log warning; user can re-author. |
| Temp state file corrupt or missing | Slot loads default; no user-facing error. |
| `~PluginProxySlot` runs while audio thread is mid-`processBlock` | Audio thread holds local `shared_ptr<ShmRegion>` for the callback duration; destructor's `atomic_store(nullptr)` doesn't free the region until the audio thread's local copy drops. Safe. |
| Two respawns collide for the same slot | `pendingRespawn` flag serializes; second request is a no-op. |
| `isolationEnabled` left off after export crash | RAII guard restores on scope exit, including exception paths. |
| Audio-thread `HDAW_LOG` allocation | Gate the per-500-calls log behind `HDAW_PROXY_DEBUG` (compiles out in release). |
| `getOutputRing`/`getMidiInRing` called before capacity set | Returns nullptr; callers already null-check. |

## 6. Testing

### C++ unit/integration (`tests/`)

| Test | Verifies |
|------|----------|
| `PipeServerGracefulShutdownTest` | Spawn passthrough child, send `SHUTDOWN`, assert exit code `0xC0DE0001` and no crash callback. |
| `CrashRecoveryTest` | Spawn `__crash__` slot, trigger crash via audio callback, assert `onSlotCrashed` fires within 2 s, `CrashRecoveryManager` schedules respawn, respawn succeeds, audio resumes. Uses injected clock for determinism. |
| `ShmRegionHandleRaceTest` | N respawn loops × M audio-callback threads; run under TSan/ASan; assert no use-after-free. |
| `RespawnStateRestoreTest` | Set plugin state, kill child, assert respawned child receives the same state blob. |
| `RespawnExhaustionTest` | Respawn fails 4× (non-existent plugin); assert `giveUp` callback fires after 3rd retry. |
| `KillModeTest` (extends existing ProxyProcessManager suite) | `KillGraceful` produces `0xC0DE0001` and no crash callback; `KillHard` produces a crash callback. |
| `EditorClosedRelayTest` | Child closes editor window; assert parent receives `EDITOR_CLOSED` and `TrackFXSlot::remoteEditorOpen` flips to false. |
| `Modified PluginProxySlot suite` | Destructor runs while a (mocked) audio callback is mid-flight; assert no crash. |
| `isolationEnabled RAII` | ExportManager throw path restores the toggle. |

### Frontend E2E (`frontend/e2e/`)

| Test | Verifies |
|------|----------|
| `plugin-isolation.spec.ts` | Load project with isolated plugin; kill child by PID via `window.rpc`; assert CrashDialog appears; click Restart; assert slot resumes; screenshot the dialog. |
| `plugin-close-no-crash-dialog.spec.ts` | Load project with isolated plugin; close project; assert NO CrashDialog appears. |

### Verification gates

- `cmake --build build --config Debug` succeeds.
- `build\Debug\hdaw_tests.exe --gtest_filter=Proxy.*:PluginIsolation.*:CrashRecovery.*` passes.
- `cd frontend && npm test && npm run test:e2e` passes.
- Manual: load HDAW, add a crashing test plugin, click Restart in the dialog, verify audio resumes; close project, verify NO "Plugin crashed!" dialog appears.

## 7. Out of scope (deferred)

- Refactoring `PluginHost::audioLoop` to use a shared ring-buffer abstraction
  (the inline shm-backed ring stays canonical; `RingBuffer.h` is deleted, not
  promoted).
- Distinguishing 3rd-party plugin freezes from legitimate high CPU use. A
  frozen plugin in `processBlock` currently hangs the parent's audio for ~2 s
  (the watchdog timeout) before being marked crashed. A shorter timeout or a
  progress-counter heuristic is a future tuning exercise.
- Cross-process latency compensation: the isolation path adds one block of
  round-trip latency. PDC alignment is handled by the existing
  `getTotalLatency()` reporting, but a per-plugin latency negotiation protocol
  (so the host can compensate for plugin-reported latency across the IPC
  boundary) is not in scope.

## 8. Reconcile with in-flight changes

`git status` shows uncommitted modifications to several files this spec
touches:

- `src/proxy/PluginProxySlot.{h,cpp}` — likely related to cached-shm; the
  implementation plan must reconcile against the current working tree, not
  the last commit.
- `src/proxy/ProxyPipe.{h,cpp}` — likely related to bounded IO; same.
- `src/proxy/ProxyProcessManager.{h,cpp}` — likely related to kill/health.
- `src/engine/PluginManager.cpp` — likely related to isolation wiring.
- `tests/integration/proxy/isolation_integration_test.cpp` — existing tests
  may already cover some of the new scenarios; dedupe before adding.

**Plan-step 0 (mandatory first step of the implementation plan):** `git stash`
or commit the in-flight changes on a WIP branch, then re-read each file before
editing. Do not assume the file contents in this spec's references (which are
against the last commit) match the working tree.
