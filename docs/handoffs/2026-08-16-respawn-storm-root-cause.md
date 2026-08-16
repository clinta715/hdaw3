# Handoff: Respawn-storm root cause FOUND + verified — fixes planned, NOT yet implemented

Date: 2026-08-16. User report: "recent changes seem to have introduced something
causing plugins or the plugin host to repeatedly crash and restart."

**Status: diagnosis complete and live-verified. Zero code changes made** (the
implementation subagent task was interrupted and returned nothing; `git status`
clean at `730839c`). No live engines or `hdaw_plugin_host.exe` children remain.
Next session: load `hdaw-guard`, then dispatch the fix plan in §5.

---

## 1. Root-cause chain (every link verified on current binaries)

1. **The leak (the feed).** Playing bakes the `juce::AudioProcessorGraph` render
   sequence, which holds `Node::Ptr`s → Tracks → FX slots → plugin proxies →
   child processes. A later `loadProject` runs `graph.clear()` +
   `rebuildFromValueTree`, but the render sequence is only re-baked when
   `processBlock` runs again. **With transport stopped, the ENTIRE previous
   project's plugin children leak** — each spinning its audio loop at 100% CPU,
   holding pipes/SHM. Proven live: load → play → stop → load left **12 leaked
   children** (slots 5–16) plus 4 new; loading WITHOUT prior play tore down
   cleanly (FXSlotDtor lines present); playing the new project for 2 s released
   all 12 leaked children instantly (16 → 4).
2. **The hangs (the trigger).** Under that CPU saturation, children's
   `processBlock` exceeds 1 s: 4× `Wrote minidump: processBlock hung for 1s`
   during a 9 s playback of the user's project (`%TEMP%\hdaw_plugin_host_processBlock hung for 1s.dmp`).
   The watchdog (`src/proxy/host/PluginHost.cpp:716-727`) only dumps, never kills.
3. **The storm (the loop).** `ProxyProcessManager::checkAllChildren` (2 s
   cadence, `startHealthMonitor(2000)`) flags hung (stall: `inputPending` &&
   `audioBlocksProcessed` frozen > 4 s) or dead (exit code ≠ `STILL_ACTIVE`
   and ≠ `GRACEFUL_EXIT_CODE 0xC0DE0001`) children as crashed →
   `CrashRecoveryManager` respawns. **Each new death creates a NEW recovery
   entry with `attemptCount` reset, so `kMaxAttempts=3` never trips → infinite
   ~2 s respawn loop with no global circuit breaker.** User's log: slot ids
   1→388+ in ~4 min, waves of 12–13 slots every ~2.04 s, every respawn
   succeeded (READY received) and every new child died/was flagged before the
   next sweep.
4. **Secondary defects found en route:**
   - Recovery entries carried **unresolved identifier strings** (e.g.
     `VST3-Identity-98879c0c-3d9dac4c`, `CLAP-JE8086-3429505-0`) instead of file
     paths; `PluginHost::loadPlugin()` never fails on a bad path → respawn would
     host a silent passthrough child.
   - `checkAllChildren` flags slots with **no log line saying why** (exit code
     vs stall) — cost hours of diagnosis.
   - Children accumulate across loads even without play (4 → 11 → 16) — same
     leak family.

**Why the storm itself can't be re-reproduced:** the storm session ran on the
8:51 AM binaries, which contained debug instrumentation (`SlotProc`,
`FXSlotCtor/Dtor`, "TrackFXSlot::prepare off message thread") that was removed
from source long ago — the storm binary diverges from HEAD. Its log was also
`HDAW_LOG_TAGS`-filtered (child-side death reasons suppressed; the tags came
from the user's shell env, inherited by the engine and children). Kill→respawn→
recover works cleanly on current binaries with resolved paths. The mechanism is
nonetheless proven by the individually reproduced links above.

## 2. User's storm project

`D:\pdf\roo projects\hdaw3\polyrhythm_aminor_120bpm.hdaw3` (note the **`.hdaw3`
extension** — `*.hdaw` searches miss it). Plugin census matches the storm log
exactly: Identity×5, JE8086×5, Toxic×2, WD Echo×1, 11 tracks.

## 3. Key locations (all verified)

| What | Where |
|------|-------|
| Full rebuild (fix A site): `graph.clear()` L626, `graphLock.exit()` L636 | `src/engine/MainAudioProcessor.cpp:585` `rebuildRoutingGraph` |
| `processBlock` early-out when transport stopped (~L253) — test T1 must start transport | `src/engine/MainAudioProcessor.cpp:182` |
| `RoutingManager::rebuildGraph()` (wraps juce `rebuild()`) | `src/engine/RoutingManager.cpp:133` |
| Drain-path precedent calling `rm->rebuildGraph()` after graphLock release | `src/engine/AudioEngine.cpp:1491` |
| `juce::AudioProcessorGraph::rebuild()` (synchronous) | `build/_deps/juce-src/modules/juce_audio_processors/processors/juce_AudioProcessorGraph.h:333` |
| Health monitor: `checkAllChildren` L228, `startHealthMonitor` L319, `KillGraceful` L150 (`0xC0DE0001`) | `src/proxy/ProxyProcessManager.cpp` |
| `respawnIsolatedSlot` L877 (silent `return false` on map miss L901), `resolveIdentifierToPath` L736, `createPluginInstance` L610 | `src/engine/PluginManager.cpp` |
| Entry lifecycle: grace 500 ms, backoff 1/2/4 s, `kMaxAttempts=3` per entry | `src/engine/CrashRecoveryManager.{h,cpp}` |
| Child watchdog/minidump L716-727, SEH filter L640, `loadPlugin()` never fails | `src/proxy/host/PluginHost.cpp` |
| Existing kill→respawn e2e pattern | `frontend/e2e/plugin-isolation.spec.ts` |

## 4. Repro harness (for verification after the fix)

- Bare engine: `build\Debug\HDAW_headless.exe` (default mode = headless frontend,
  WS on 8766). Drive via WebSocket: method `project.loadProject`, params
  `{ filePath }` (Node 26 has global `WebSocket`). Router: `Router_Project.cpp:643`.
- Leak repro: load project A → `transport.play` ~8 s → `transport.stop` → load
  project B → count `hdaw_plugin_host.exe` via
  `Get-CimInstance Win32_Process -Filter "Name='hdaw_plugin_host.exe'"`.
  Before fix: A's children survive. After fix A: only B's children remain,
  WITHOUT needing to play.
- Log: `%TEMP%\hdaw_debug.log` — timestamps are **UTC** (local = UTC−5).
- Helper scripts from this session (may be gone): `C:\Users\hapbt\AppData\Local\Temp\opencode\repro_load.js`, `log_offset.txt`, `engine_pid.txt`.
- **Lesson 20:** kill stale engines/children before running CrashRecovery/
  PluginIsolation suites: `Get-Process HDAW_headless,HDAW,hdaw_plugin_host -EA SilentlyContinue | Stop-Process -Force`.

## 5. The fix plan (dispatch this to ONE implementation subagent)

Four fixes, all engine-side, no frontend/RPC changes. Build
`cmake --build build --config Debug`; verify binaries relinked (lesson 15);
full suite `build/Debug/hdaw_tests.exe` at the end.

**Fix A — release stale render sequence after full rebuild (PRIMARY).**
In `MainAudioProcessor::rebuildRoutingGraph`, after `graphLock.exit()`, add
`routingManager->rebuildGraph();` with a comment: the render sequence baked
during playback pins old `Node::Ptr`s (Tracks/FX slots/proxies/child
processes); `graph.clear()` alone doesn't release them until the next
`processBlock` re-bake, so a load with stopped transport leaked the whole
previous graph's plugin children. Mirrors the drain-path precedent
(`AudioEngine.cpp:1491`). Must stay OUTSIDE the graphLock critical section and
outside the parked section (lesson 18).

**Fix B — global respawn circuit breaker.** In `CrashRecoveryManager`:
sliding-window budget (default 8 respawns / 30 s; env overrides
`HDAW_RESPAWN_BUDGET` / `HDAW_RESPAWN_WINDOW_MS`). When exhausted: don't call
`respawnFn`, leave entry pending (`nextRetryMs = now + 2s`), log
`HDAW_LOG("CrashRecovery", ...)` once per tick batch. Record timestamps only
for respawns actually attempted. Export suppression gate (`respawnEnabled=false`)
stays first, unchanged. Self-heals as old timestamps age out.

**Fix C — respawn path validation/re-resolution.** In
`PluginManager::respawnIsolatedSlot`: if path doesn't end with `.vst3`/`.clap`
(case-insensitive) and doesn't start with `__` (test sentinels pass through),
re-resolve against `knownPluginList` via `matchesIdentifierString` (mirror
`resolveIdentifierToPath` L736-767). Resolved → spawn resolved path + log.
Unresolvable → log + `return false` (no silent passthrough spawn).

**Fix D — flag-reason telemetry.** In `ProxyProcessManager::checkAllChildren`:
when adding a slot to `crashedSlots`, `HDAW_LOG("CrashRecovery", ...)` with
slot + reason: `exit code 0x…` / `GetExitCodeProcess failed err=N` /
`stalled: blocks=F frozenMs=M threshold=T`. Health thread, not audio thread —
safe. Cheap formatting only.

**Tests:**
- **T1 (Fix A regression, gtest):** fixture like existing CrashRecovery tests
  (isolation enabled, `__passthrough__` child spawns). Baseline-count
  `hdaw_plugin_host.exe` (Toolhelp32 snapshot) → spawn isolated FX → start
  transport (processBlock early-outs when stopped!) → `processBlock` ×~50 to
  bake the sequence → `rebuildRoutingGraph(true)` → poll child count back to
  baseline (2 s timeout). If a new .cpp, add it to the test target in
  `CMakeLists.txt`.
- **T2 (Fix B unit):** construct `CrashRecoveryManager` directly, `respawnFn` =
  counting lambda returning true, `onSlotCrashed` for 20 different slot ids
  (storm signature), `tick()` once → assert attempts ≤ 8 and entries still
  pending.
- **T3 (Fix C unit):** prefer extracting resolution into a pure helper
  (e.g. `resolveRespawnPath`) and unit-testing it: known identifier → resolved
  file path; unknown (`VST3-Ghost-deadbeef-0`) → no spawn / failure.
- **T4:** full suite after killing stale engines (lesson 20). Known-flaky five
  (lesson 20 list) must be re-run after cleanup before being blamed.

**Docs:** append lesson 21 to AGENTS.md (≤15 lines, style of lesson 20):
render sequence pins old graph after `graph.clear()` until next `processBlock`
re-bake → load with stopped transport leaked all previous plugin children
(100% CPU each) → saturation → `processBlock` hangs → health-flag → respawn
storm; fixes: synchronous `graph.rebuild()` at end of full rebuild, global
respawn budget, respawn path re-resolution, flag-reason logging.

**Pitfall rules for the implementer:** no logging/locks/allocs on the audio
thread; no `MessageManagerLock` on the message thread; Fix A outside graphLock;
no RPC/frontend changes; `HDAW_LOG` not `DBG`; new test file → CMakeLists.

**Report back:** files changed, key decisions per fix, new test names,
full-suite pass/fail counts, deviations. Do NOT commit.

## 6. Considered and deferred

- Watchdog minidump size (~285 MB each): same filename is overwritten, so disk
  doesn't fill — skip gating unless it recurs.
- Per-proxy death blacklisting (vs global budget): slot ids change per respawn
  so lineage tracking is complex; the token bucket covers the storm case.
- E2E Playwright leak test (enumerate child PIDs from Node): feasible but the
  gtest T1 covers the seam; add e2e only if T1 proves impractical.
