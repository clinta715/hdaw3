# Handoff: Respawn-storm fixes deployed but storm persists — deeper root cause

Date: 2026-08-16. Follow-up to the earlier handoff. Fixes A–D were implemented,
built, and unit-tested (all new gtest pass), but the live storm still fires when
loading the user's project.

**Status: four code fixes are in the working tree (uncommitted, HEAD still
`730839c`). The fixes address the ORIGINAL diagnosis but a deeper or parallel
root cause is now visible.** Next session: investigate the two new failure modes
below, fix, re-test.

---

## 1. What was implemented (working tree, uncommitted)

| Fix | Files | What it does |
|-----|-------|-------------|
| A (render-sequence release) | `MainAudioProcessor.cpp` (+`.h`) | After `graphLock.exit()`: releases pump park → `routingManager->rebuildGraph()` → `runOnMessageThread` drives `graph.processBlock` to force the `RenderSequenceExchange` swap and release old plugin children |
| B (respawn budget) | `CrashRecoveryManager.{h,cpp}` | Sliding-window budget (default 8/30s, env `HDAW_RESPAWN_BUDGET`/`HDAW_RESPAWN_WINDOW_MS`). When exhausted: skip `respawnFn`, leave entry pending, log once per batch |
| C (path re-resolution) | `PluginManager.{h,cpp}` | Extracted `resolveRespawnPath` — validates/re-resolves plugin paths before respawn. Unknown identifiers → `return false` |
| D (flag-reason logging) | `ProxyProcessManager.cpp` | `checkAllChildren` logs exit-code / GetExitCodeProcess error / stall stats per flagged slot |
| Tests | `render_sequence_release_test.cpp` (new), `crash_recovery_test.cpp` (extended) | T1: render-sequence release (passes); T2: storm breaker (passes); T3: path resolution (passes) |
| Docs | `AGENTS.md` | Lesson 21 |

**All new unit tests pass. The full gtest suite ran 200+ tests with zero failures
in the processed portion (timed out at ~10 min but no failures seen).**

## 2. What the user observes

Plugins keep crashing and respawning in a storm. The `hdaw_debug.log` shows:

```
respawnIsolatedSlot: oldSlot=24 path=VST3-Identity-98879c0c-3d9dac4c
respawnIsolatedSlot: oldSlot=25 path=VST3-Identity-98879c0c-3d9dac4c
respawnIsolatedSlot: oldSlot=26 path=VST3-Toxic-53097354-dc388637
...slot ids climb to 60+ in ~7 seconds...
```

All respawned children produce **silence** (`peak=0.000000`). The slot ids
exceed the 8/30s budget by 4×, meaning Fix B is not taking effect.

## 3. Two new failure modes (not covered by the original diagnosis)

### Failure Mode 1: `resolveRespawnPath` can't resolve — `knownPluginList` empty

The crash callback fires `crashRecoveryNotifier(slotId, name, pluginPathForRecovery)`.
`pluginPathForRecovery` is set in the `PluginProxySlot` constructor from the
`resolvedDesc.fileOrIdentifier` passed by `createPluginInstance`. That resolution
happens at **plugin instantiation time** (when the project loads) and SHOULD be a
real file path.

But the log shows **unresolved identifier strings** (`VST3-Identity-98879c0c-3d9dac4c`)
being passed to `respawnIsolatedSlot`. This means either:

**(a)** `resolveIdentifierToPath` at instantiation time returned the raw identifier
(knownPluginList was empty at load time — perhaps the project file didn't carry
the scan results, or the list was cleared/rebuilt between the load and the crash).

**(b)** The `pluginPathForRecovery` is NOT coming from the proxy's stored value. The
crash callback fires from the `perSlotCrashCallbacks` map (set in
`createPluginInstance` line 640: `[proxy](uint32_t id) { proxy->onChildCrashed(); }`).
`onChildCrashed()` uses `pluginPathForRecovery`. So if the proxy was constructed
with a resolved path, the callback should pass it. Unless there's a second proxy
construction path that doesn't resolve.

**(c)** Most likely: the `knownPluginList` is empty or stale when
`resolveRespawnPath` runs during crash recovery. The list is populated during
plugin scanning, but after a `loadProject` + `rebuildRoutingGraph`, the
`PluginManager` may be reconstructed or the list may not carry over. The crash
recovery's `resolveRespawnPath` then finds no entries to match against → returns
empty → but somehow the spawn still proceeds (or the function returns the input
unchanged via a code path I haven't traced).

**Investigation:** add `HDAW_LOG` inside `resolveRespawnPath` to log the
`knownList.getTypes().size()` and whether a match was found. Also log the exact
path stored in `pluginPathForRecovery` at proxy construction time. Check whether
`knownPluginList` survives a `loadProject` → `rebuildRoutingGraph` cycle.

### Failure Mode 2: Fix B budget not enforced

The log shows 36+ respawn attempts in ~7 seconds. The budget (default 8/30s)
should cap this. Possible causes:

**(a)** The `respawnTimestamps` deque in `CrashRecoveryManager` is not being
populated. The timestamp is pushed INSIDE the mutex lock in `attemptRespawn`,
AFTER the budget check. If the budget check code is not being reached (e.g.
early return before it), no timestamp is recorded.

**(b)** The `breakerTrippedInBatch` flag is reset at the start of `tick()`, and
the log message is emitted AFTER processing all due entries. If the storm
produces entries faster than one `tick()` cycle (2s), each tick might process
a fresh batch of entries that haven't hit the budget yet. But the deque
persists across ticks, so this shouldn't be the issue.

**(c)** The budget check runs inside `attemptRespawn`, which is called from
`tick()`. But `tick()` is called from `PluginManager::timerCallback()` (a
JUCE Timer on the message thread). If the timer isn't running (message pump
not started), `tick()` is never called, and the budget is never enforced.
Check: is the JUCE Timer running in the live app? `PluginManager` inherits
from `juce::Timer` and calls `startTimer(1000)` — verify this fires.

**(d)** **Most likely:** the budget check IS running but the log message is
suppressed by `HDAW_LOG_TAGS` filtering (the user's shell env inherited by
the engine). The previous handoff noted: "its log was also
`HDAW_LOG_TAGS`-filtered (child-side death reasons suppressed; the tags came
from the user's shell env)." Check whether `CrashRecovery` is in the user's
`HDAW_LOG_TAGS`. If not, the log is invisible but the budget may still be
working — the respawns might just be from entries that aged out of the window.

**Investigation:** Add a `DBG()` fallback (or write to a separate file) for the
breaker-tripped message so it can't be filtered. Also count the actual
`respawnFn` invocations in `attemptRespawn` and log the deque size.

## 4. Key locations (current code, with fixes applied)

| What | Where |
|------|-------|
| Fix A site: `pumpPark.reset()` + `rebuildGraph()` + `runOnMessageThread` drive | `MainAudioProcessor.cpp:638-682` |
| `resolveRespawnPath` (Fix C) | `PluginManager.cpp:769-792` |
| `respawnIsolatedSlot` (Fix C call site) | `PluginManager.cpp:902-970` |
| Budget check (Fix B) | `CrashRecoveryManager.cpp:92-115` |
| `attemptRespawn` (budget + kMaxAttempts) | `CrashRecoveryManager.cpp:77-108` |
| `checkAllChildren` logging (Fix D) | `ProxyProcessManager.cpp:228-295` |
| `pluginPathForRecovery` set in proxy ctor | `PluginProxySlot.cpp:34` |
| `onChildCrashed` fires crash callback | `PluginProxySlot.cpp:685-692` |
| `crashRecoveryNotifier` set in createPluginInstance | `PluginManager.cpp:643-659` |
| `respawnFn` lambda | `PluginManager.cpp:92-93` |

## 5. Repro

- Binary: `build\Debug\HDAW.exe` or `frontend\release\win-unpacked\HDAW.exe`
- Project: `D:\pdf\roo projects\hdaw3\polyrhythm_aminor_120bpm.hdaw3`
- Log: `%TEMP%\hdaw_debug.log` (timestamps are UTC, local = UTC−5)
- Helper: kill stale engines before testing: `Get-Process HDAW_headless,HDAW,hdaw_plugin_host -EA SilentlyContinue | Stop-Process -Force`

## 6. Investigation plan for next session

1. **Add diagnostic logging** to `resolveRespawnPath` (log `knownList.getTypes().size()` + match result) and to `attemptRespawn` (log deque size + budget check result). Also log `pluginPathForRecovery` at proxy construction.
2. **Verify the JUCE Timer** in `PluginManager` is firing (`startTimer(1000)` at line ~101). If not, `tick()` never runs and the budget is never enforced.
3. **Check `HDAW_LOG_TAGS`** in the user's environment — if `CrashRecovery` is filtered out, the budget log is invisible.
4. **Check `knownPluginList` lifecycle** — does it survive `loadProject` → `rebuildRoutingGraph`? Is it cleared and repopulated? If cleared, the crash recovery's `resolveRespawnPath` can't resolve.
5. **Consider storing the RESOLVED file path** directly in `CrashRecoveryManager::onSlotCrashed` (resolve at crash time, not respawn time), so even if the list changes, the stored path is correct.
6. **Consider a simpler Fix B**: instead of a sliding window, just count total respawns per `CrashRecoveryManager` lifetime and give up after N (regardless of time window). Simpler, no threading concerns with the deque.
7. **Consider making `resolveRespawnPath` resolve at crash time** (in `onSlotCrashed` or in the crash callback) rather than at respawn time, so the stored path is always a resolved file path.

---

## Resolution (2026-08-16, second follow-up)

**The storm was a pre-fix orphan engine interleaving in the shared log — fixes
A–D were in fact live in the Debug engine.** Evidence:

- Storm entries reference slot ids 28–168 (then 1–60); today's engine only
  ever created slots 1–27. Slots 28–168 cannot exist in today's engine — they
  belong to a long-lived orphan that accumulated children across repeated
  loads.
- ~31 ms respawn cadence and 20+ attempts per 2 s — impossible under fix B's
  8/30 s budget.
- Zero occurrences of `respawn breaker tripped`, `unresolvable identifier`,
  `resolved '`, or fix-D `checkAllChildren ... flagged` lines in the whole
  48 MB log (the `CrashRecovery` tag IS visible elsewhere — not tag
  filtering; `HDAW_LOG` is synchronous+flushed — no dropped lines).
- Storm entries carry unresolved identifier paths; today's load resolved
  every spawn to real paths — identifier entries must come from a process
  whose `resolveIdentifierToPath` failed at load (empty `knownPluginList`).
- `%TEMP%\HDAW_headless_mcp.exe` (mcp-launch.bat's copy target) is dated
  8/13 9:07 PM and contains zero fix strings — the bat's `copy /Y` has
  silently failed ever since (a lingering engine held the file locked), so
  any bat-launched MCP session reused the pre-fix binary.

The investigation also found real perpetuator bugs (E1–E5) that make ANY
engine — fixed ones included — churn forever once children die. All are now
fixed:

- **E1 re-flag storm:** `ProxyProcessManager::checkAllChildren` re-flagged a
  dead child on every sweep, re-firing the crash callback forever. Fix:
  flag-once (`ChildInfo::crashNotified`), reset on successful respawn.
- **E2 attempt-ladder reset:** `CrashRecoveryManager::onSlotCrashed`
  unconditionally set `attemptCount = 0`, so re-flags defeated `kMaxAttempts`
  and give-up could never terminate a slot. Fix: preserve the ladder for
  existing entries; reset only for brand-new ones.
- **E3 silent proxy-miss:** `PluginManager::respawnIsolatedSlot` returned
  false at `proxy == nullptr` with no log and no entry cancel. Fix: log +
  `crashRecovery->cancel(oldSlotId)` (plus an entry-lifetime guard in
  `attemptRespawn` so a respawnFn that cancels can't write back into the
  erased entry).
- **E4 log ambiguity:** log lines carried no date and no pid —
  multi-process/multi-day misattribution derailed this very investigation.
  Fix: `"date"` and `"pid"` fields on every `DebugLog` line.
- **E5 orphan breeding:** `mcp-launch.bat` silently reused a locked, stale
  target. Fix: `taskkill` stale `HDAW_headless_mcp.exe` /
  `hdaw_plugin_host.exe` before copying, plus a source-vs-destination size
  verification that fails loudly on mismatch.

Changes made: `src/common/DebugLog.h`, `src/proxy/ProxyProcessManager.{h,cpp}`,
`src/engine/CrashRecoveryManager.cpp`, `src/engine/PluginManager.cpp`,
`mcp-launch.bat`, tests T4–T7 in `tests/unit/proxy/crash_recovery_test.cpp`,
AGENTS.md lesson 21 extension.

## Final resolution (2026-08-16, third pass)

After E1-E5, the user still saw crashes - attributed in seconds via the new
`pid` field: the storm lines had NO pid (pre-fix binary), and the culprit was
the **packaged Electron app's engine**: `frontend\release\win-unpacked\
resources\engine\HDAW_headless.exe` was dated 8/5 - the 3:29 PM repackage
refreshed the frontend but `build/RelWithDebInfo` (the config electron-builder
ships, per `electron-builder.yml` `extraResources`) had never been rebuilt, so
the app ran an 11-day-old engine with none of the fixes.

Resolution:
1. Live verification of the fixed Debug engine against
   `polyrhythm_aminor_120bpm.hdaw3` via `project.loadProject` RPC: 23 plugin
   slots spawned with resolved paths, 90 s hold window with ZERO crashes /
   respawns / breaker trips. The fixed code does not storm.
2. `cmake --build build --config RelWithDebInfo` + verify fix-marker strings
   in the binary (breaker / unresolvable / flag-once / date field).
3. `npm run build && npm run package:dir`; verified the shipped
   `resources\engine\HDAW_headless.exe` carries the fix markers.

User-verified fixed after this. Standing rule added to AGENTS.md: a bare
`npm run package:dir` re-ships whatever `RelWithDebInfo` contains - build that
config first and string-verify the shipped engine before trusting the package.
