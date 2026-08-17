# Plan: Respawn-storm termination + attribution (follow-up to fixes A–D)

Date: 2026-08-16. Owner: orchestrator session. Status: investigation complete, ready for implementation.

## Root cause (evidence-based, replaces the two hypotheses in the handoff)

Fixes A–D **were running** in today's 20:22 UTC Debug engine (spawn log 20:22:54–20:23:12
used real resolved paths; `build\Debug\HDAW_headless.exe` built 11:52 contains all fix
strings, verified by binary string search). The "persisting storm" lines (20:26:59–20:30:12)
were written by a **different, orphaned PRE-FIX engine process** interleaving into the shared
`%TEMP%\hdaw_debug.log`:

- Storm entries reference slots 1–168 then 1–60; today's engine only ever created slots 1–27.
  Slots 28–168 cannot exist in today's engine — they belong to a long-lived orphan that
  accumulated children across repeated loads.
- ~31 ms respawn cadence and 20+ attempts per 2 s — impossible under fix B's 8/30 s budget.
- Zero occurrences of `respawn breaker tripped`, `unresolvable identifier`, `resolved '`,
  or fix-D `checkAllChildren ... flagged` lines in the whole 48 MB log (tag `CrashRecovery`
  itself IS visible elsewhere — not tag filtering; `HDAW_LOG` is synchronous+flushed — no dropping).
- Storm entries carry unresolved identifier paths; today's load resolved every spawn to real
  paths (so its proxies' `pluginPathForRecovery` are real paths — identifier entries must come
  from a process whose `resolveIdentifierToPath` failed at load, i.e. an engine with an empty
  `knownPluginList`).
- `%TEMP%\HDAW_headless_mcp.exe` (mcp-launch.bat's copy target) is dated **8/13 9:07 PM** and
  contains **zero fix strings** — the bat's `copy /Y` has silently failed ever since (a lingering
  engine holds the file locked), so any bat-launched MCP session reuses the pre-fix binary.

**But** the investigation also found real perpetuator bugs that make ANY engine (fixed ones
included) churn forever once children die. These are the code fixes below:

- **E1 re-flag storm:** `ProxyProcessManager::checkAllChildren`
  (src/proxy/ProxyProcessManager.cpp:229-304) never removes or dedups flagged children — a
  dead child is re-flagged **every sweep**, re-firing `perSlotCrashCallbacks` →
  `onChildCrashed` → `onSlotCrashed` forever.
- **E2 attempt-ladder reset:** `CrashRecoveryManager::onSlotCrashed`
  (src/engine/CrashRecoveryManager.cpp:25-37) unconditionally sets `attemptCount = 0`, so
  re-flags defeat `kMaxAttempts` — give-up can never terminate a slot.
- **E3 silent proxy-miss:** `PluginManager::respawnIsolatedSlot`
  (src/engine/PluginManager.cpp:926) returns false at `proxy == nullptr` with **no log and no
  entry cancel** — dead slots retry 3× per wave forever and look identical to a live storm
  (this exact silence wasted the previous session's diagnosis).
- **E4 log ambiguity:** log lines carry no date and no PID (`DebugLog.h` has `currentPid()`
  unused) — multi-process/multi-day misattribution is what derailed this investigation.
- **E5 orphan breeding:** `mcp-launch.bat` copy fails silently when an old engine holds the
  target; no cleanup of orphaned engines/plugin-host children (lesson 20's standing follow-up).

## Goal

Respawn storms terminate deterministically (one flag, one recovery ladder, real give-up), and
every log line is attributable to a process and date.

## Success Gates (all must pass)

- [ ] G1: `cmake --build build --config Debug` succeeds.
- [ ] G2: New gtests pass: attempt-ladder preservation (T4), flag-once re-sweep (T5),
      entry-cancel on proxy-miss (T6); existing `CrashRecovery.*`, `RenderSequenceRelease.*`,
      and proxy suites still pass (`build/Debug/hdaw_tests.exe --gtest_filter=...`).
- [ ] G3: Log-line check: new lines emit `"date"` and `"pid"` fields (unit-assertable via a
      DebugLog format test or by inspecting the file written during tests).
- [ ] G4: No anti-patterns: `HDAW_LOG` (not DBG), no locks taken while already held
      (cancel is NOT called under CrashRecoveryManager::mutex — respawnFn runs outside it),
      no audio-thread logging added.

## Dependency Map

- `DebugLog::log` / `logAlways` (src/common/DebugLog.h): called from everywhere (god node) —
  change is **additive fields only**, no signature change; consumers are humans/docs (verified:
  no code parses the format; docs describe it as "JSON lines, UTC" — keep `ts`, add fields).
- `checkAllChildren`: sole caller = health-monitor thread (`startHealthMonitor`). Downstream:
  crash callbacks → `PluginProxySlot::onChildCrashed` → notifier → `CrashRecoveryManager`.
- `onSlotCrashed`: sole caller = the crash-recovery notifier lambda set in
  `PluginManager::createPluginInstance` (PluginManager.cpp:643-659).
- `respawnIsolatedSlot`: sole caller = respawnFn lambda (PluginManager.cpp:92-93), invoked by
  `CrashRecoveryManager::attemptRespawn` on the message thread via `timerCallback`/`tick`
  (timer 250 ms) — **outside** the manager's mutex (verified CrashRecoveryManager.cpp:99-129).
- `mcp-launch.bat`: sole caller = opencode MCP config (`~/.config/opencode/opencode.jsonc`).
- Projections affected: none (ReadModel / frontend snapshot / audio graph untouched).
- SPSC paths: none. Audio thread: none of the touched functions run on it
  (checkAllChildren = monitor thread; DebugLog change affects it only via existing callers).

## Pitfall Gates Triggered

- **Gate 14 (cross-process):** the entire bug family is cross-process (orphans, shared log,
  shared %TEMP% state). Fixes E4/E5 attack it directly.
- **Gate 15 (stale binaries/flags):** THE trap of this incident — verification-by-string-search
  of the actual running binary, not the source. mcp-launch.bat must fail loudly on copy failure.
- **Gate 12 (graph mutation off-message-thread):** no new graph mutations added; respawn path
  already parks via graphLockPtr — unchanged.
- **Gate 3 (audio-thread safety):** no new audio-thread code; DebugLog change must not add
  overhead beyond two extra small field writes under the existing mutex.

## Changes

1. **src/common/DebugLog.h** — in both `log()` and `logAlways()`: add `"date":"YYYY-MM-DD"`
   (computed from the same `std::tm utc`) and `"pid":<currentPid()>` to the JSON line.
   Keep `"ts"` unchanged (docs and humans rely on it).

2. **src/proxy/ProxyProcessManager.{h,cpp}** — flag-once semantics in `checkAllChildren`:
   add `bool crashNotified = false;` to `ChildInfo`. In all three flag paths
   (GetExitCodeProcess-failure, non-zero exit code, stall), only push to `crashedSlots`
   (and log) when `!info.crashNotified`, and set `info.crashNotified = true` when pushed.
   Do NOT erase from `children` inside the iteration (collect-then-mutate pattern is already
   used for callbacks; the respawn path's `killPluginHost` performs the erase). Reset
   `crashNotified` where a child is (re)created for a slot id (`spawnPluginHost` success path
   inserts/refreshes `ChildInfo` — make sure a fresh spawn clears it).

3. **src/engine/CrashRecoveryManager.cpp** — `onSlotCrashed`: when an entry for `slotId`
   already exists AND `pendingRespawn` is true, refresh `pluginPath`/`pluginName`/`crashedAtMs`
   but **preserve `attemptCount`**; reset `attemptCount = 0` only for brand-new entries
   (after a successful respawn the entry is erased, so a later `onSlotCrashed` legitimately
   starts a fresh ladder).

4. **src/engine/PluginManager.cpp** — `respawnIsolatedSlot` at the `proxy == nullptr` early
   return (line ~926): log
   `HDAW_LOG("CrashRecovery", "respawnIsolatedSlot: no live proxy for slot N - slot torn down, canceling recovery")`
   and call `crashRecovery->cancel(oldSlotId)` before `return false`. Safe (no manager mutex
   held by caller). Note the log message must avoid characters that break greppability.

5. **mcp-launch.bat** — before the copies: kill stale holders
   (`taskkill /F /IM HDAW_headless_mcp.exe >nul 2>&1` and
   `taskkill /F /IM hdaw_plugin_host.exe >nul 2>&1` — lesson 20: killing the parent does NOT
   kill children on Windows). After each `copy /Y`, verify success explicitly (copy's own
   errorlevel is already checked — keep it, and ADD a size comparison via `for %%A in (file)
   do set SZ=%%~zA` against the source, failing loudly with a clear message if mismatched).
   Rationale comment: MCP stdio engines are one-per-client-session; any holder of the target
   exe at launch time is by definition stale.

6. **Tests (tests/unit/proxy/crash_recovery_test.cpp + nearest existing proxy test file):**
   - T4 `CrashRecovery.RepeatedCrashNotificationsKeepAttemptLadder`: failing respawnFn;
     call `onSlotCrashed(same slot)` repeatedly (simulating re-flag sweeps); assert total
     respawnFn invocations ≤ kMaxAttempts before giveUpFn fires.
   - T5 flag-once: using the existing ProxyProcessManager test seams (see
     `CrashRecovery.AutoRespawnAfterCrash` style; spawn sentinel child via `__`-prefixed
     test plugin or killProxyForTesting): assert the crash callback fires exactly once across
     two `checkAllChildren` sweeps for the same dead child.
   - T6 `RespawnCancelsEntryWhenProxyGone`: CrashRecoveryManager entry whose respawnFn
     cancels the entry and returns false (mirroring the new PluginManager behavior); assert
     `numEntries() == 0` afterwards and no further attempts occur on subsequent `tick()`s.
   - T7 DebugLog format: minimal test writing via `HDAW_LOG` to the real temp file and
     asserting the line contains `"date":"` and `"pid":` (or assert format via a tiny
     refactor seam if direct file reading is flaky — prefer reading the actual file; it is
     flushed synchronously).
   - NOTE (lesson 20): proxy tests that spawn real children collide with any live engine —
     kill stale engines before running: `Get-Process HDAW*,hdaw_plugin_host -EA SilentlyContinue |
     Stop-Process -Force`.

7. **Docs:** update `docs/handoffs/2026-08-16-respawn-storm-followup.md` with a resolution
   section (orphan attribution + the E-fixes), and extend AGENTS.md lesson 21 with: (a) the
   orphan/interleaved-log attribution trap — log lines now carry pid/date, always attribute
   before concluding a fix failed; (b) flag-once + ladder-preservation + cancel-on-proxy-miss
   as the storm-termination contract; (c) mcp-launch.bat now kills stale holders.

## Verification commands

```
cmake --build build --config Debug
build\Debug\hdaw_tests.exe --gtest_filter=CrashRecovery.*:RenderSequenceRelease.*:PluginIsolation.*:ProxyProcessManager.*
build\Debug\hdaw_tests.exe --gtest_filter=DebugLog.*
```
Plus (manual, optional live check): kill stale engines, start MCP session, load
`polyrhythm_aminor_120bpm.hdaw3`, force-kill a child via the test seam, confirm ONE flag log,
≤3 respawn attempts, give-up/cancel log, then silence — all lines carrying pid+date.

## Out of scope

- Why the orphan's children died originally (lesson 21 causes; fixed by A).
- Per-machine global respawn budget across processes (E1+E2 make storms terminate per-engine;
  a cross-process budget would need a global semaphore — not warranted now).
