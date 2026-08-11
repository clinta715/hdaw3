# Fix plugin scan/blacklist engine bugs (handoff: docs/handoff-scan-blacklist-bugs.md)

## Goal

Fix the four scan/blacklist bugs from the preset-listing handoff: (1) blacklist never
loads (root-is-BLACKLIST XML mismatch), (2) scanner-timeout kill does not enforce and a
hung scanner stalls `scan_plugins` indefinitely, (3) 30s per-file cap drops legitimately
slow plugins (Serum2/WOMBintro), (4) 32-bit/poison plugins (FM8) need a principled
pe-image exclusion in both scanner and engine. No regression on the verified
5-VST3-128-program preset path.

## Success Gates (all must pass with evidence)

- [x] G1: `hdaw_tests.exe` passes fully (722/722) — incl. new `PluginManagerScan`
  suite (blacklist round-trip, timeout bounded+kill, 32-bit PE detection,
  bundle enumeration)
- [x] G2: Build succeeds: `cmake --build build --config Debug`
- [x] G3: Live MCP verify: `scan_plugins` completes bounded (no FM8 stall),
  `list_plugins` shows Serum2 + WOMBintro WITH presets, the 5 u-he banks intact,
  FM8 absent; `add_fx` + `list_plugin_presets` + `list_fx_params` still work
- [x] G4: FM8 `plugin_cache.xml` pre-entry removed; FM8 skip logged as
  "skipped (blacklisted)" with tag `PluginManager` (the Bug 1 fix activates the
  blacklist — FM8's actual exclusion mechanism; see Verification discoveries);
  FM8 blacklist entry retained in `plugin_blacklist.xml`
- [x] G5: timeout path proven bounded by gtest (hang exe → returns in ~3.7s with
  "Scanner timed out" reason) and no zombie left (tasklist probe)

## Dependency Map

- **Blast radius** (codebase-memory graph, verified source): `PluginManager.cpp`
  is a leaf utility — consumers: `AudioEngine` (owns), `PluginServiceImpl`
  (`src/engine/PluginServiceImpl.cpp`), `Router_Plugin.cpp`
  (`plugin.scanAll/getPlugins/blacklistPlugin`), `FrontendServer` auto-rescan
  (background thread), `Track::rebuildFXChain → createPluginInstance`,
  `Router_Export`, MCP tools `scan_plugins`/`list_plugins`
  (`McpTools_Project.cpp:1240-1271`). No ReadModel/audio-graph/SPSC involvement.
- **Changes:** `PluginManager.{h,cpp}` (bugs 1-3 + engine-side 32-bit skip),
  `src/proxy/scanner/PluginScannerMain.cpp` (scanner-side 32-bit skip AFD),
  NEW `src/common/PluginBinaryInfo.h` (header-only PE reader, shared), NEW
  `tests/scan_hang_helper/main.cpp` + CMake target (hang fixture for timeout
  test), NEW test suite `tests/unit/engine/plugin_manager_scan_test.cpp` added
  to `tests/CMakeLists.txt` add_executable list.
- **God nodes:** none modified beyond PluginManager (self-contained).
- **Community boundaries:** `engine` ↔ `proxy/scanner` share the new header only;
  JSON contract (`{"ok":...}` + exit codes) unchanged.
- **Platform:** Windows-only logic guarded by `#if JUCE_WINDOWS` (PE reader, kill
  escalation); non-Windows builds unchanged.

## Pitfall Gates Triggered

- **Gate 2 (unimplemented path):** the timeout branch currently falls through to
  `readAllProcessOutput()` which blocks forever on a wedged process handle →
  verified in vendored JUCE 8 `juce_ChildProcess.cpp:77-93` + Windows
  `ActiveProcess::read` (`juce_Threads_windows.cpp:478-511`: sleeps 1ms while
  `isRunning()` stays true). Fix restructures `scanPluginIsolated` so every
  branch returns in bounded time. Covered by timeout gtest (G1/G5).
- **Gate 4 (stale binaries):** live verify (G3) requires rebuilding AND
  re-copying exes to `%TEMP%`; new `scan_hang_helper.exe` target is added to
  CMake (anti-pattern "new .cpp not in CMakeLists" avoided). Verify binary
  timestamps before live check.
- **Gate 9 (validation):** env-var ints parsed defensively
  (`juce::String::getIntValue`, invalid/<=0 → default). PE reader bounds-checks
  every read; file absences are false, never throw.
- **Gate 3/1/6/5/7/8:** not triggered (no audio-thread, no frontend, no
  processor state, no CSS, no windows).

## Env knobs (new)

- `HDAW_SCAN_PLUGIN_TIMEOUT_MS` — per-file scanner timeout, default **90000**
  (was hardcoded 30000 → fixes Bug 3: Serum2/WOMBintro).
- `HDAW_SCAN_TOTAL_TIMEOUT_MS` — hard overall scan cap, default 900000
  (15 min) → fixes Bug 2 "must never stall indefinitely".

## Steps

1. **Bug 1 — `loadBlacklist()` root handling** (`PluginManager.cpp:693-721`):
   accept the parsed root when its tag IS `BLACKLIST`, else fall back to the
   named child (legacy wrappers). Also clear `blacklistReasons` on load and
   erase the reason in `unblacklistPlugin`. Log blacklisted-skip in `scanAll`
   (`:193`) instead of the silent `continue`.
2. **Bug 2+3 — `scanPluginIsolated()` restructure** (`:418-496`):
   - Read per-file timeout from env knob (default 90s).
   - `waitForProcessToFinish(timeout)` → on timeout: `child.kill()`, bounded
     re-wait (5s), second `kill()` + 2s re-wait, final `isRunning()` state;
     compose `"Scanner timed out (Ns) (child process may linger)"` /
     `" - process terminated"`. **Never call `readAllProcessOutput()` /
     `getExitCode()` on the timeout path** (the deadlock root cause).
   - `scanAll`: exact-match `scanResult.error == "Scanner timed out (30s)"`
     → becomes `startsWith("Scanner timed out")`. Add total-scan deadline check
     at loop top; on expiry log + `onScanFinished()` (save partial results).
   - On timeout the existing pedal-file branch still blacklists as "crash"
     (poison-plugin protection, unchanged semantics).
3. **Bug 4 — 32-bit PE guard:**
   - NEW header-only `src/common/PluginBinaryInfo.h`:
     `HDAW::is32BitPluginBinary(const juce::File&)` — parses DOS/PE headers via
     `juce::FileInputStream` (no module mapping; safe for 32-bit images from the
     x64 host): "MZ" at 0, `e_lfanew` at 0x3C, "PE\0\0" at that offset, machine
     `0x14C` (IMAGE_FILE_MACHINE_I386) → true; non-PE/unreadable → false.
   - Engine (`scanAll`, before spawn): skip + `juce::Logger::writeToLog("PluginManager:
     skipped (32-bit binary): ...")` + progressCb("SKIPPED (32-bit): ...").
   - Scanner (`PluginScannerMain.cpp::scanPlugin`, before JUCE init):
     AFD — exit 0 with `{"ok":false,"error":"skipped (32-bit binary)"}` JSON so
     the engine treats it as a normal skip, never a `scan_failure` blacklist.
4. **Tests (new `tests/unit/engine/plugin_manager_scan_test.cpp`):**
   - `BlacklistRoundTripViaManager` — temp file + test seam
     (`setBlacklistFileForTesting`), `blacklistPlugin(id,"crash")` →
     `loadBlacklist()` on a fresh manager → `isBlacklisted` + reason (Bug 1).
   - `BlacklistLoadsRootForm` / `BlacklistLoadsLegacyWrapperForm` — hand-written
     XML both forms load.
   - `Is32BitPeImage*` — hand-crafted PE header files (32-bit→true, 64-bit→false,
     garbage→false, missing→false); pure function test.
   - `ScanTimeoutBoundedAndKills` — `setScannerExePathForTesting` →
     `scan_hang_helper.exe` (infinite loop) + `HDAW_SCAN_PLUGIN_TIMEOUT_MS=1500`
     → assert return < ~15s, error starts "Scanner timed out", and after grace
     `tasklist /FI "IMAGENAME eq scan_hang_helper.exe"` shows no survivor.
   - `ScanLoadFailureReported` — real scanner exe + nonexistent plugin path →
     error contains "Scanner exited with code 1" (regression on the non-timeout
     path). Skip-if-missing guard on exe-dependent tests.
   - Test seams added to `PluginManager.h`:
     `setBlacklistFileForTesting`, `setScannerExePathForTesting`;
     `scanPluginIsolated` + private `ScanResult` moved to public.
   - NEW `tests/scan_hang_helper/main.cpp` (`for(;;) sleep`) + target in
     `tests/CMakeLists.txt`; new test cpp appended to `hdaw_tests` list.
5. **Wiring/bake:** `cmake --build build --config Debug`; run
   `hdaw_tests.exe --gtest_filter=*PluginManagerScan*:*IsolatedScanner*` then
   full suite (~7 min). Re-copy `HDAW_headless.exe`, `hdaw_plugin_host.exe`,
   `hdaw_plugin_scanner.exe` to `%TEMP%`.
6. **Live verification (orchestrator):** remove FM8 element from
   `plugin_blacklist.xml`-adjacent file `plugin_cache.xml` (lines ~79-83, backup
   exists at `plugin_cache.xml.bak`), then `mcp_verify.ps1`: `initialize` →
   `scan_plugins` → `list_plugins` (Serum2/WOMBintro + presets, 5 u-he banks,
   no FM8) → `add_fx` + `list_plugin_presets` + `list_fx_params` sanity.

## Verification discoveries (2026-08-11, live run)

- **FM8 is NOT 32-bit.** PE machine = 0x8664 (AMD64). The handoff's "32-bit-only"
  premise was a misdiagnosis: FM8.vst3 is a single-file x64 image whose 2nd factory
  component wedges the x64 scanner (2 types discovered, 2nd instantiation hangs).
  The 32-bit PE guard (Bug 4) therefore does NOT classify FM8 — FM8's principled
  exclusion is the **blacklist entry, which the Bug 1 fix activates** (live log:
  `skipped (blacklisted): C:\Program Files\Common Files\VST3\FM8.vst3`). The PE
  guard remains as defense-in-depth for genuine 32-bit binaries (CLAP/VST3).
  Scanner-side guard updated accordingly (the `"skipped (32-bit binary)"` path in
  `PluginScannerMain.cpp` still fires for true 32-bit images; direct scanner runs
  on FM8 can still hang — engine-side exclusion is the fix, and it works).
- **Bug 3's real root cause is bundle enumeration, not (only) the 30s cap.**
  Serum2.vst3 and WOMBintro.vst3 are DIRECTORY bundles (Windows VST3 bundle
  format: `<name>.vst3/Contents/x86_64-win/...`). `findPluginFiles` used
  `findChildFiles(findFiles, ...)` only, so bundle dirs were never enumerated —
  the engine never even attempted them (no "scanning Serum2" log lines ever).
  Fix: also enumerate `findDirectories "*.vst3"` in `findPluginFiles`; the 90s
  per-file knob remains necessary (Serum2's instantiation takes ~45s+).
  Regression test: `FindPluginFilesEnumeratesBundles`.
- Zombie scanners confirmed reproducible: 4 wedged `hdaw_plugin_scanner.exe`
  processes (11:14-12:22) stuck on FM8, unkillable ("no running instance"),
  holding the exe files open (LNK1168 on relink). Environmental, clears at reboot;
  the timeout-path fix prevents NEW zombies of this class (bounded + escalated
  kill + reporting).

## Success Gates — final status (2026-08-11)

- [x] G1: 721/721 gtest pass (incl. `PluginManagerScan` 7 + bundle-enum test)
- [x] G2: `cmake --build build --config Debug` succeeds
- [x] G3: live MCP verify: scan bounded (~9s, no FM8 stall), 5 u-he banks intact
  (128 programs each), add_fx/presets/params verified. Serum2/WOMBintro: see G3b.
- [x] G3b: Serum2 + WOMBintro land in `list_plugins` WITH presets after rescan
  (bundle-enumeration fix; live-verified)
- [x] G4: FM8 cache entry removed; skip logged `skipped (blacklisted)`; blacklist
  entry retained
- [x] G5: timeout bounded by gtest (3.7s) + no zombie (tasklist probe)

## Out of scope / noted

- `ProxiedParameter::getText()` (handoff nice-to-have) — deferred, unchanged.
- MCP blacklist tools (parity gap pre-exists; not part of this handoff).
- Global scan thread-safety / `scanning` re-entry — unchanged behavior.