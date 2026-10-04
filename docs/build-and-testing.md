# Build & testing — traps, measurements, recipes

Moved out of AGENTS.md (2026-09-22). AGENTS.md keeps the quick commands
and one-line rules; the full narratives live here.

# Build

- **Canonical (native Windows, from a plain shell):** `build-fast.bat` builds
  `HDAW.exe` (RelWithDebInfo), `build-fast.bat test` builds the four test exes
  (`build-fast.bat test <target>` builds one), `build-fast.bat all` builds
  everything, `build-fast.bat debug` builds Debug.
  The script bootstraps MSVC (`vcvars64.bat`) and resolves the VS-bundled CMake
  when neither is on PATH — no developer prompt required.
- Raw configure/build: `cmake --build build --config Debug`
- Outputs: `build/Debug/HDAW.exe`, `build/Debug/HDAW_headless.exe`, `build/Debug/hdaw_tests_{engine,mcp,frontend,platform}.exe`
- Do NOT run `build/Release/HDAW.exe` — stale binary, contains none of the fixes.
- **Two launch modes:** Default (browser), Headless (Electron).
- **Frontend build — DEPRECATED (2026-09-23):** the Electron frontend is a separate
  project; do NOT build it as part of engine work (`npm run build`,
  `frontend\build.bat`). Engine-only verification: the `build/hdaw_tests_*.exe` gtest binaries +
  the MCP surface. See the dated banner on "How frontend changes reach the running
  app" below.
- See [`docs/architecture.md`](docs/architecture.md) for full build details.

## Linux (2026-10-04 port)

The tree builds and tests natively on Linux (verified: Ubuntu 26.04, g++ 15.2,
CMake 4.2, Ninja; JUCE 8 fetched via FetchContent, Qt 6.10 system packages).

- **Packages:** `build-essential git cmake ninja-build qt6-base-dev
  qt6-websockets-dev qt6-httpserver-dev libasound2-dev libx11-dev libxext-dev
  libxrandr-dev libxinerama-dev libxcursor-dev libfreetype-dev
  libfontconfig1-dev libgl1-mesa-dev libglib2.0-dev pkg-config`
  (note: the dev packages are `qt6-websockets-dev` / `qt6-httpserver-dev` on
  26.04 — the `libqt6*`-style names do not exist there).
- **Configure/build:** `./build-fast.sh [test|all|debug]`, or
  `cmake --preset linux` + `cmake --build build`
  (presets: `linux` = RelWithDebInfo Ninja, `linux-debug` = Debug).
- **Outputs are flat:** `build/HDAW`, `build/HDAW_headless`,
  `build/hdaw_tests_{engine,mcp,frontend,platform}`,
  `build/hdaw_plugin_host`, `build/hdaw_plugin_scanner`
  (`CMAKE_RUNTIME_OUTPUT_DIRECTORY` is set for this — child exes are located
  exe-adjacent at runtime; a nested `build/tests/` layout strands the host and
  every spawn fails its READY handshake).
- **Freshness gate:** every consumer of the child exes carries an
  `add_dependencies` edge on `hdaw_plugin_host`/`hdaw_plugin_scanner` — a
  protocol/SHM header change can no longer leave a stale child behind.
- **Plugin isolation on Linux:** `src/proxy/` is ported, not compiled out.
  Pipes → AF_UNIX SOCK_SEQPACKET (message-mode framing preserved), SHM →
  `shm_open`+mmap (same `ShmHeader` layout), spawn → fork/exec with
  `PR_SET_PDEATHSIG`, crash artifacts → async-signal-safe text reports
  (`*hung*.crash.txt`, `$TMPDIR`→`/tmp` convention) instead of dbghelp
  minidumps. The child serializes lifecycle marshaling with
  `PluginHost::lifecycleMutex_` (it runs two dispatch pumps on Linux).
  Windows code paths are unchanged (`#ifdef` branches in the same files).
- **Known deltas vs Windows:** no SEH fault containment inside plugin
  `processBlock` (faults kill the child; the parent's watchdog + respawn
  ladder handle it — lessons 21/28 semantics); crash artifacts are text
  reports, not minidumps. The four `PluginIsolation.DLL*` suites skip
  themselves (Windows-DLL targeted).
- **Concurrent `ninja` on the same `build/` corrupts `.ninja_deps`** exactly
  as the hard-kill trap below — two racing builds produced a permanent
  per-run recompile of `juce_gui_basics` + test relinks (diagnosed via
  `ninja -d explain` → "stored deps info out of date" + the
  `premature end of file` warning). Repair: `rm build/.ninja_deps
  build/.ninja_log`, one full rebuild.

### Build speed: the `.ninja_deps` trap (2026-09-21) — 285 s → 2 s

A **truncated `build/.ninja_deps`** (from a hard-killed Ninja build) makes Ninja
read every recorded target as `STALE`, which re-runs AUTOMOC → rewrites
`HDAW_lib_autogen/mocs_compilation.cpp` → dirties the **PCH** → **all ~300
`HDAW_lib` TUs recompile on every build**. Measured on this repo: a no-op build
was **285 s** (300 steps) and a one-test-TU edit was **331 s**; after the repair
the same no-op is **2 s (0 steps)** and the TU edit is **52 s (3 steps)**.

- **Symptom:** `ninja: warning: premature end of file; recovering`, and
  `ninja -t deps <target>` printing `(STALE)`.
- **Repair:** delete the deps log — `rm build/.ninja_deps` (harmless; costs at
  most one rebuild, then it settles).
- **Prevention:** never hard-kill a `cmake --build` / `ninja` process
  (`taskkill` on a *test* process is fine; killing the build mid-flight is what
  truncates the log).
- Legitimate costs that remain: a widely-included header edit rebuilds its real
  fan-out (e.g. `src/common/ProjectCommands.h` → 155 TUs, ~260 s);
  `HDAW_lib` is built with LTO (`INTERPROCEDURAL_OPTIMIZATION`);
  `windeployqt` runs as a POST_BUILD step on every test exe (and the Qt-linked engine exes).

### `cmake --build` never re-runs CMake here (the suppressed-regeneration trap)

This build tree is configured with **`CMAKE_SUPPRESS_REGENERATION=ON`**
(`build/CMakeCache.txt`, `UNINITIALIZED`), so `build.ninja` contains **no
`build build.ninja: RERUN_CMAKE` statement at all** (verify:
`Select-String build\build.ninja -Pattern ': RERUN_CMAKE'` → 0 hits). Ninja's
special "regenerate the manifest first" behaviour is therefore compiled out:
**editing `CMakeLists.txt` / `tests/CMakeLists.txt` does NOT trigger a
reconfigure when you run `cmake --build` / `ninja`.**

The failure mode is genuinely confusing, because the source is correct and only
the *build graph* is stale:

- You add a new `.cpp` **and** its `CMakeLists.txt` entry → the file is never
  compiled (no `.obj` is ever produced) → the build fails at **link** with
  `LNK2001: unresolved external symbol` for a function that plainly exists in
  the source, or (worse) an edited `main()`/entry point never takes effect —
  the same "the source says X" unreliability as lesson 15 / the `.ninja_deps`
  trap, in a different disguise.
- Diagnose: `Select-String build\build.ninja -Pattern '<NewFile>'` → **ABSENT**
  means the graph was never regenerated (`build.ninja` mtime older than
  `CMakeLists.txt` confirms it).

- **Fix (always after adding/removing/renaming a source, target, or option):**
  re-run CMake explicitly, then build:
  `cmake -S . -B build` then `cmake --build build --target hdaw_tests_mcp`
  (agents in a sandboxed shell: `dsh-build-fast.bat configure` — a bare
  `cmake -S . -B build` there dies in the configure-time try_compile with
  `C1083: Cannot open include file: 'windows.h'` because the MSVC env is not
  bootstrapped; measured 2026-09-28)
  (equivalently `cmake --build build --target rebuild_cache`, or
  `cmake --regenerate-during-build -S . -B build` — what CMake itself would run).
- **Do NOT** conclude "the tool/file is broken" from an unresolved external
  before checking the manifest — confirm the new source is in `build.ninja`.
- The suppression is deliberate (it keeps no-op builds from paying a CMake
  manifest check). Keep it; just pair every structural edit with an explicit
  configure.

### Test speed: shard the suite across processes (2026-09-21)

`run-tests-sharded.ps1 [-Shards N] [-Filter <regex>] [-SerialSuites <regex>] [-Binaries <exe[]>]`
(repo root) shards the gtest list across N
concurrent test-exe processes (the engine is a singleton *per process* and
proxy children get a unique namespace per manager instance, so concurrent runs are
safe). It shards small suites whole and large ones per test. Run it with
`powershell -File run-tests-sharded.ps1 -Shards 4` (or `pwsh`).

- **2026-09-28 test-time split:** the suite is FOUR exes (`hdaw_tests_engine` /
  `_mcp` / `_frontend` / `_platform`, built by `hdaw_test_exe()` in
  `tests/CMakeLists.txt`; plan + gates:
  `docs/plans/2026-09-28-build-test-time-split.md`). The runner defaults to all
  four, deals `(binary, unit)` pairs into the shard buckets, and runs one gtest
  process per exe per bucket; `-Binary`/`-Binaries` selects a subset. A suite
  name present in more than one exe is a hard runner error — every suite must
  live in exactly one exe. Shards run as a greedy pool (`-BucketFactor`, default 3):
  units are dealt into `-Shards × BucketFactor` buckets with no wave barriers, so one
  heavy bucket only holds its own lane. Passing MULTIPLE exes: `powershell -File` binds
  ONE value per parameter — use `-Command "& '.\run-tests-sharded.ps1' -Binaries
  'a.exe','b.exe'"` (or pass a single exe).

- **Native, no env forwarding:** the child processes are launched by PowerShell and
  inherit this shell's environment directly — `$env:HDAW_REAL_PLUGIN_TESTS=1`
  before invoking it, and the real-plugin gates run. (The retired WSL wrapper
  `scripts/run-tests-parallel.sh` needed `WSLENV=HDAW_REAL_PLUGIN_TESTS` to push
  the variable through interop; that constraint is gone with the file.)
- **Measured:** the 20-gate `FxMidiInjection` suite (the heaviest) runs **600 s
  serial → 307 s with 4 shards** (~2x; the shards contend on the CPU-heavy
  emulations, so the longest shard dominates).
- `run_fast_tests.bat` remains the fast iteration tier (excludes the
  render/recipe/spawn-heavy suites) — but see the timing note under "Testing"
  below: on this box the remaining ~1900 tests still cost ~0.5-0.7 s each, so the
  **unscoped fast tier did not finish in 40 min** (measured 2026-09-25). Sharding is
  the practical lever: a scoped 2-shard `BusSendRpcTest.*` run (32 tests) measured
  **12 s wall vs 17.5 s serial (~2.3x)**, both shards green (2026-09-25).
- **Render temp targets are process-unique (fixed 2026-09-21).** `renderTrackWindow`
  now writes `%TEMP%\hdaw_render_p<pid>_<trackIndex>_<counter>.wav`. Before the pid
  tag the counter restarted per process, so two concurrent shards rendering the same
  track index picked the same path and one export failed with `export failed: Could
  not create output file` (exactly 1 spurious failure in a 4-shard `FxMidiInjection`
  real-plugin run, 22/23; the same test passed solo in 25 s). After the fix the
  identical 4-shard sweep is **24/24**. Keep any new temp target process-unique as
  `%TEMP%\hdaw_paramtrace_<pid>.log` and the proxy state files already are;
  a per-process counter alone is not enough when one suite runs in several processes.
- **The test harness self-redirects `TMP`/`TEMP` (2026-09-25).** `tests/test_main.cpp` probes the
  resolved default temp dir (from `TMP`/`TEMP`, else Win32 `GetTempPathW`) with a
  write+read+delete of `hdaw_tmp_probe_<pid>.txt` before anything else in `main` runs. If the
  probe fails (sandboxed shell: child-process writes under `%TEMP%` are denied), it creates
  the process-unique `<repo>/.tmp_tests/tmp/<pid>` (per-pid so concurrent shards cannot race a
  suite's own `deleteRecursively` on the shared root), sets `TMP` and `TEMP` to it in BOTH the CRT env (`_putenv_s`) and the
  Win32 process env block (`SetEnvironmentVariableW` — `GetTempPath`/JUCE read the PEB, so
  `_putenv_s` alone is not enough), and prints
  `[test_main] temp dir redirected to … (default unwritable: …)`. In a normal environment the
  inherited `TMP`/`TEMP` are left untouched and it prints
  `[test_main] temp dir = … (default, verified writable)`. Set `HDAW_TEST_TMP` to override the
  choice explicitly (shard runners / CI); the resolved dir is always printed once.
  **Measured evidence:** the windowed-render scratch path
  (`src/engine/AudioEngineCommands_Composition.cpp` → `File::getSpecialLocation(File::tempDirectory)`)
  made `./build/hdaw_tests.exe --gtest_filter='VerifyPart.*'` fail 9/13 with
  `export failed: Could not create output file`; with the in-harness redirect and NO external
  `TMP`/`TEMP` override the same filter is **13/13 in 11.4 s** (`.tmp_*` is gitignored).
- **Corrupted build dir after a concurrent-build race (2026-09-27).** Editing sources WHILE a sharded run is in flight makes each shard's ninja rebuild race the others and the edit: symptom class = wandering SIGSEGV/exit-127 in heavy engine suites (death point moves between runs on the SAME binary; dying tests pass solo; raw FAILED lines 0; no crash artifacts) plus `C1083: Cannot open compiler generated file: Permission denied` on subsequent builds. Recovery: delete `build/CMakeFiles`, `.ninja_deps`, `.ninja_log`, reconfigure (`dsh-build-fast.bat ninja`), full rebuild — verified: the full suite ran 2045/2045 green afterward. Prevention: never edit sources during a sharded run.
- **The build scripts self-redirect `TMP`/`TEMP` (2026-09-27, B8).** `dsh-build-fast.bat` and
  `build-fast.bat` point the toolchain's temp at `<repo>/.tmp_build_scratch/lnk` (gitignored via
  the `.tmp_*` glob; `HDAW_BUILD_TMP` overrides) before the MSVC bootstrap, so `link.exe`'s
  `lnk{GUID}.tmp` writes never hit a denied real `%TEMP%` — the LNK1104 class hit twice on
  2026-09-26 (09-26 trap 4) is closed at the source. `setlocal` scopes the override to the
  script's process tree; the resolved scratch dir is printed once per run.
- **The harness also isolates the engine's other user-scope roots (widened 2026-09-25).**
  `tests/test_main.cpp` now probes two more roots at the top of `main` (same ordering rule:
  before `ScopedComInit`, `MessagePumpThread::start()` and `QCoreApplication`) and redirects the
  unwritable ones into `<repo>/.tmp_tests`, printing one line each:
  - *User data root.* `File::getSpecialLocation(userApplicationDataDirectory)` is
    `SHGetSpecialFolderPathW(CSIDL_APPDATA)` on Windows — the registry value
    `%USERPROFILE%\AppData\Roaming`, **not** the `APPDATA` env var
    (`build/_deps/juce-src/modules/juce_core/native/juce_Files_windows.cpp:721` →
    `getSpecialFolderPath` at `:146`, `:151`). shell32 resolves its whole folder set on FIRST
    use and never re-reads the environment afterwards (measured 2026-09-25: a fresh process
    launched with `USERPROFILE` overridden resolves to the override, an in-process override
    after shell32's first query does not) — which is why the redirect must happen here, in the
    very first statements of `main`. The engine writes `HDAW/section-templates`
    (`src/engine/AudioEngineCommands_Song.cpp:64`), `HDAW/patterns` (`:548`), `HDAW/chains`
    (`src/engine/ChainLibrary.cpp:288`), `HDAW/plugin_cache.xml` (`src/engine/PluginManager.cpp:96`),
    `HDAW/libraries` (`src/engine/FileLibraryManager.cpp:30`) and `HDAW/recordings`
    (`src/engine/MainAudioProcessor.cpp:376`) under it. On a denial the harness rewrites
    `USERPROFILE` (plus `APPDATA`/`LOCALAPPDATA`) to `<repo>/.tmp_tests/userdata` in BOTH the CRT
    and Win32 process environments (`_putenv_s`/`SetEnvironmentVariableW`, as for temp) and prints
    `[test_main] user data dir redirected to …`; a writable root prints
    `[test_main] user data dir = … (default, verified writable)` and is left alone.
    `HDAW_TEST_USERDATA` overrides the profile root outright.
  - *User-data READ mirror (2026-09-25).* The redirected root starts empty, so
    `tests/test_main.cpp` seeds it from the REAL `%APPDATA%\HDAW` (resolved **before** the
    redirect) — `plugin_cache*.xml`, `preset_cache.xml`, `plugin_blacklist.xml`,
    `preferences.ron`, `libraries/`, `chains/`, `section-templates/`, `patterns/`, `MIDI/`
    (recordings/ deliberately excluded) — printing
    `[test_main] user data mirror = 213 files, 95218271 bytes copied (0 already current) from …`
    on a first seed. Copies are best-effort and atomic (temp file + `MoveFileExW`), a missing
    source is not an error, and a file already at the source size is skipped.
    **Measured effect: neutral on every solo gate.** The 3 `FileLibraryTest.*` failures in the
    2026-09-25 4-shard run are a **temp collision, not user-data**: they reproduce only when 4
    shards share the one `.tmp_tests` temp root (each suite's `SetUp`/`TearDown`
    `deleteRecursively` the same `hdaw_file_library_test` dir) and pass **40/40** solo; **fixed
    2026-09-25 by the per-pid temp root above** — a 4-shard `FileLibraryTest.*` run is now
    **40 passed, 0 unique failed**. The 4-shard
    serial shard died mid-suite (`.tmp_tests/shard_logs/hdaw_shard_131606_serial.log` stops at
    `[ RUN ] ApplyPresetToolTest.NordRouteValidatesDumpsBeforeQueueing`, no summary), but that
    22-suite / 316-test filter completes **rc 0 in 232 s** (`[ PASSED ] 288`, 28 designed SKIPs)
    BOTH with the mirror and in a control run against an **empty** root — so the death is
    run-level (load/concurrency), not attributable to the empty root. The mirror is kept to
    restore read fidelity, not as a proven fix.
  - *QSettings.* The engine constructs `QSettings s;` with **no** format/path override
    (`src/engine/AudioEngine.cpp:198,345,445`, `src/engine/RaveService.cpp:46`,
    `src/engine/PluginManager.cpp:78`, `src/frontend/router/Router_Audio.cpp:293`,
    `src/frontend/router/Router_Project.cpp:1056`, `src/frontend/FrontendServer.cpp:119`, …), which
    on Windows means NativeFormat → `HKCU\Software\HDAW\HDAW`: a write outside the working tree
    (denied) and one store shared by every concurrent shard. The harness therefore forces the
    whole TEST PROCESS onto `QSettings::IniFormat` with `QSettings::setPath(…)` =
    `<repo>/.tmp_tests/settings/<pid>` for BOTH `UserScope` and `SystemScope` (reads fall back from
    user to system scope), before any `QSettings` object exists, and prints
    `[test_main] settings store = … (QSettings IniFormat, this process only)`. This is
    harness-only: `src/main.cpp` and `src/main_headless.cpp` keep the native (registry) format in
    production, and because the switch is process-wide the engine and the tests still share ONE
    consistent settings mechanism — just not the machine's registry.
  **Measured 2026-09-25 on this box:** the 8 settings-backed failures
  (`RaveSettings.*` ×6, `FrontendServer.SettingsNamespaceExposesMcpHttpConfig`,
  `McpServer.EngineSettingsStartMcpHttp`) go from 8 red to **13/13 PASSED** in the filter that
  contains them; the 3 `%APPDATA%` persistence failures
  (`SongPlan.TemplateRoundTripDoesNotApply`, `McpServer.ApplySongPlan`,
  `McpCoverageTest.FxChainPresetRoundTrip`) go from 3 red to **3/3 PASSED**; `VerifyPart.*` stays
  **13/13** (temp redirect unregressed). `TransportSurface.StartStopRecording` also passes now
  (3/3 solo) — its recorder target is `@userApplicationDataDirectory/HDAW/recordings`
  (`src/engine/MainAudioProcessor.cpp:376`), so the former "no capture endpoint" classification was
  really this path denial. Side effect worth knowing: the plugin cache/blacklist
  (`@userApplicationDataDirectory/HDAW/plugin_cache.xml`) is a scratch file seeded from the real
  cache by the READ mirror above, and the CLAP-/real-plugin-dependent export tests still take their
  designed SKIP path (28 SKIPs in the 316-test serial filter; e.g. `FxMidiInjection.*`,
  `ClapPresetProbe.*`) — they gate on `HDAW_REAL_PLUGIN_TESTS`, not on the cache's presence.
- **Full-suite 4-shard measurement (2026-09-25).** `powershell -File run-tests-sharded.ps1 -Shards 4`
  over the whole suite took **1398 s (23.3 min)** and reported **1957 passed, 12 unique failures**.
  Sharding is measured **NOT** to multiply failures: all 12 reproduce SOLO on this box, so they are
  environmental, not contention. Census by class: **3 persistence** (the `%APPDATA%` preset/template
  writes above — `SongPlan.TemplateRoundTripDoesNotApply`, `McpServer.ApplySongPlan`,
  `McpCoverageTest.FxChainPresetRoundTrip`), **8 settings-backed** (6× `RaveSettings.*`,
  `FrontendServer.SettingsNamespaceExposesMcpHttpConfig`, `McpServer.EngineSettingsStartMcpHttp`),
  **1 deviceless/env** (`TransportSurface.StartStopRecording`). The widened harness isolation above
  turns the first two classes (11 tests) green.
- **Unique-failure counting (fixed 2026-09-25).** gtest prints every failing test twice — a timed
  `[  FAILED  ] Name (123 ms)` line and a bare `[  FAILED  ] Name` line in the summary list, plus a
  `[  FAILED  ] N test(s), listed below:` counter that is not a test name. Summing the raw
  `^[  FAILED  ]` lines therefore double-counted: the above 12 unique failures were reported as
  "28 failed". `run-tests-sharded.ps1` now takes the total from the unique **timed** names and prints
  `TOTAL: <n> passed, <n> unique failed (raw FAILED lines: <n>)`; the per-shard detail lines keep the
  raw counts, and the `-Filter` demo `-Shards 2 -Filter "BusSendRpcTest.*"` prints
  `TOTAL: 32 passed, 0 unique failed (raw FAILED lines: 0)`.
- **Incomplete-shard detection (fixed 2026-09-25).** A shard process killed mid-run (external
  `taskkill`, OOM, hard crash) left a log that stopped at a `[ RUN ]` line with **no gtest
  completion banner**; its missing `[       OK ]` lines were silently counted as absent, so the
  runner under-reported and exited **0** — a false green. Observed 3× across 3 consecutive 4-shard
  full-suite runs, each losing one process: `.tmp_tests/shard_logs/hdaw_shard_141746_0.log` ends at
  `[ RUN      ] ExportBakeTimeout.LargeProjectExportsWithDefaultTimeout` with NO
  `[==========] N tests from M test suites ran.` line, yet the run printed
  `TOTAL: 1821 passed, 0 unique failed`. `run-tests-sharded.ps1` now asserts the banner
  (`^\[==========\] \d+ tests? from \d+ test suites? ran\.`) per shard; a log without it is printed
  as `INCOMPLETE SHARDS: shard <n> (ran <x> of <y> intended tests)`, marked `[INCOMPLETE: …]` on the
  per-shard line, and forces a **non-zero exit**. Induced proof (2026-09-25): `-Shards 2 -Filter
  "VerifyPart.*"` with its child `taskkill /F`'d reports
  `shard 0: 0 passed, 0 failed (1 units) … [INCOMPLETE: no gtest completion summary, last test 'VerifyPart.ComposedPartPasses']`
  → `INCOMPLETE SHARDS (1): … shard 0 (last test: VerifyPart.ComposedPartPasses)`, exit 1.
  (The message now also prints the ran/intended counts, e.g. the real death recorded below printed
  `shard 1 (ran 0 of 111 intended tests)`.)
  **No crash artifact exists for those deaths** (checked 2026-09-25): `%LOCALAPPDATA%\CrashDumps`
  empty, `%TEMP%\hdaw_crash_captures` holds only `engine_*` procdump dirs and **no `.dmp`**, and the
  WER `LocalDumps` registry (HKCU+HKLM) has entries for `HDAW_headless_mcp.exe` /
  `hdaw_plugin_host.exe` / `ocenaudio.exe` but **none for `hdaw_tests.exe`** — so a hard crash of a
  test shard would leave no WER dump with the current configuration. The truncated shard log (its
  last `[ RUN ]` line) is the only evidence available.
- **One `--gtest_filter` per process + intended-vs-executed coverage (fixed 2026-09-25).** gtest accepts exactly ONE
  `--gtest_filter` and **overwrites** it when the flag repeats, so the runner's per-bucket chunking (split at 6000
  chars, one `--gtest_filter` argument per chunk) ran only the **last** chunk and still exited 0 — the DEFAULT
  `-Shards 2` was a false green. Recorded evidence: `.tmp_tests/shard_logs/hdaw_shard_144808_0.log` / `_1.log` show a
  ~1575-char `Note: Google Test filter = …` and `Running 130 tests from 18 test suites` / `132 tests from 17`, printed
  `TOTAL: 550 passed, 0 unique failed` and exited 0 in **347 s**, although the suite is **287 suites / 2008 tests**
  (`--gtest_list_tests`). Replaying that old argument list (two `--gtest_filter` args at once) selects **132 of 834**
  and **131 of 859** intended tests — **1678 of 2008 (83.6%) silently never ran**. A bucket is now planned as ONE
  process, choosing the first route that applies: (a) every unit joined with `:` into a single `--gtest_filter` arg
  whenever it stays under a **24000-char** guard (`-MaxFilterArgLength`); (b) otherwise ONE
  `--gtest_flagfile=<file>` arg — GoogleTest 1.14 supports it (`gtest.cc LoadFlagsFromFile`, one flag per line, LF
  only), probed once at runtime and printed as `gtest --gtest_flagfile support: True`; `-DisableFlagfile` forces the
  fallback for measurement; (c) a bucket that is too long with no flagfile support runs as several **sequential**
  invocations, each its own single `--gtest_filter`, their logs concatenated into the one shard log (never concurrent
  with itself). The aggregate then verifies **intended-vs-executed** per shard — intended is derived from
  `--gtest_list_tests` (whole suite → its listed test count, `Suite.Test` → 1) — requiring one gtest completion banner
  per launched invocation, `SUM(ran. N) == intended`, and as a cross-check `SUM(Running N) == intended`; SKIPPED tests
  count as run in both gtest totals, and a shortfall prints `INCOMPLETE SHARDS: shard <n> (ran <x> of <y> intended
  tests)` and exits non-zero. **Measured 2026-09-25:** the full suite at `-Shards 2` now reports
  `coverage: executed 2008 of 2008 intended tests` (842 shard 0 + 850 shard 1 + 316 serial) in **1722 s (28.7 min)**,
  `1964 passed, 1 unique failed` (`PluginIsolation.LiveDropDrainsStaleOutput`, in the serial bucket — it passes solo
  in 0.6 s, so environmental, not contention) and **no INCOMPLETE shard**; the 4-shard comparison measured earlier
  today is 1398 s / 1574 s with one incomplete shard per run. The scoped
  `-Shards 2 -Filter "FileLibraryTest.*:VerifyPart.*:RaveSettings.*:McpServer.*:McpCoverageTest.*:Transport*.*:Track*.*:Bus*.*"`
  run reports `executed 324 of 324` on **all three routes** (single / flagfile / sequential with 3 invocations per
  shard, the last two forced with `-MaxFilterArgLength 1000`). The coverage check also caught a real process death the
  old code hid: a scoped `-Shards 2` run lost shard 1 at `McpServer.ExportAudioWithMultipleIsolatedInstances` (no
  banner, empty stderr) and reported `INCOMPLETE SHARDS: shard 1 (ran 0 of 111 intended tests)` + exit 1, while that
  test passes solo in 9.4 s — the same unclassified run-level death as the 4-shard losses above.
- **Memory/concurrency context for those deaths (measured 2026-09-25).** Box: 31.92 GiB visible RAM
  (`Win32_OperatingSystem.TotalVisibleMemorySize` = 33,475,520 KiB), ~14.3 GiB free at measurement.
  The runner runs up to **`-Shards` (4) concurrent test-exe processes** (from the four
  per-seam exes) plus **one serial process per exe** for the device/plugin suites, and each heavy shard additionally runs large
  offline renders and spawns its own isolated CLAP children (`hdaw_plugin_host.exe`), so a 4-shard
  run is ~5 test processes plus N plugin hosts competing for CPU and memory; the three observed
  deaths (one process lost per run) are consistent with resource pressure, but with no dump and no
  OOM/`std::bad_alloc`/SEH marker in the logs the cause is **unclassified**, not proven.
- **Pre-build time sync (WSL/Windows clock drift) — OPT-IN, no-op on a native
  box.** `scripts\time-sync.cmd` exits 0 immediately and prints nothing unless
  `HDAW_TIME_SYNC=1` is set, so the native Windows build path pays no WSL spawn.
  `build-fast.bat` and CMake (`hdaw_time_sync` ALL target, via
  `cmake/RunTimeSync.cmake`) still call it — that is intentional and free. Set
  `HDAW_TIME_SYNC=1` (cmd: `set HDAW_TIME_SYNC=1`, PowerShell:
  `$env:HDAW_TIME_SYNC=1`) only when the source tree is reached through WSL's
  drvfs/9p view; the hook then snaps the WSL clock to the Windows host
  (`sudo ntpdate -b time.windows.com`) so ninja/MSBuild never misjudge mtimes
  under WSL2 clock drift (lesson 15 / the WSL-side-edit sync recipe). WSL users
  can also run `scripts/time-sync.sh` directly. The hook NEVER fails a build on
  any path. See `docs/archive/plans/2026-09-05-time-sync-build-hook.md` and
  `docs/skills/pre-build-time-sync/SKILL.md`.
  **DEPRECATED (2026-09-23):** the `frontend\build.bat` / `npm run build` entries
  above are deprecated with the Electron frontend (see the frontend banner below);
  the rule now applies only to the live engine builds (`cmake --build`,
  `build-fast.bat`, bare `ninja`).

### Disk housekeeping: `scripts/cleanup-stale.ps1` (2026-09-22)

Reclaims disk from stale scratch on this dev box: crash dumps + debugger symbol
caches, `%TEMP%`, HDAW param traces / debug log / render WAVs / engine copies,
agent chat logs (pi / omp / opencode / codex), and re-downloadable caches
(`-Aggressive`). **Dry-run by default** — `-Apply` deletes. Files held open by a
running process cannot be deleted and are reported as `LOCKED`, which is what
protects a live engine's `hdaw_paramtrace_<pid>.log` / `hdaw_debug.log`
deliberately.

`scripts/cleanup-stale-db.mjs` (`-AgentDb`) is the sqlite companion for
`~/.local/share/opencode/opencode.db`: `VACUUM` reclaims the freelist (that DB
had grown to 9.1 GB of which **6.3 GB was free pages** — `auto_vacuum=0`), and
`--days N` prunes whole sessions with the FK cascades it needs. Both refuse to
write while another process holds the DB.

Two invariants worth keeping when editing either script: `%TEMP%\hdaw_crash_captures`
and its `wer` child are **protected dirs** (crash-diag.ps1 registers `wer` as the
WER LocalDumps folder and WER does not always recreate it), and the capture-tree
sweep globs `engine_*` only — a bare `hdaw_crash_captures\*` matched `wer` and
deleted it.

### Shell: PowerShell only (no `&&` or `&`)

This development system runs **Windows PowerShell 5.1**, where `&&` and `&` (as a command separator) are **not valid**. Every command in AGENTS.md, scripts, and docs must use PowerShell-native syntax:

| Goal | Use this | Not this |
| ------ | ---------- | ---------- |
| Run commands sequentially (fail on error) | `cmd1; if ($?) { cmd2 }` | `cmd1 && cmd2` |
| Run commands sequentially (ignore errors) | `cmd1; cmd2` | `cmd1 & cmd2` |
| Run in subshell / change dir | Use the `workdir` parameter on tool calls, or `Set-Location` | `cd dir && cmd` |
| Background jobs | `Start-Job { ... }` | `cmd &` |
| Boolean AND / OR | `if ($?) { ... }` / `if ($LASTEXITCODE -eq 0) { ... }` | `&&` / ` | | ` |

**When writing new commands in this project**, always prefer PowerShell-compatible forms. Existing references to `&&` in documentation (including this file, `README.md`, and `docs/`) are legacy from bash-originated docs and should be updated on sight.

### How frontend changes reach the running app (the stale-frontend trap)

**DEPRECATED (2026-09-23):** the Electron frontend is a separate project as of this
date — the repo no longer builds or tests it. The table and instructions below are
retained for reference only (do NOT run `frontend\build.bat`, `npm run build`,
`npm run package:dir`, or `npm run dev`). The stale-`app.asar` warning printed by
`frontend\build.bat` is therefore expected noise that can be ignored. Engine-only
verification — the four `build/hdaw_tests_*.exe` gtest binaries + the MCP surface — is the live path.

The React frontend is delivered three ways, and **a plain `cmake --build`
updates NONE of them**. If a frontend fix "doesn't take effect after
rebuilding," this is almost certainly why:

| Run mode | Binary | Frontend source | To pick up frontend changes |
| ---------- | -------- | ----------------- | ------------------------------ |
| **Packaged Electron** | `frontend/release/win-unpacked/HDAW.exe` | Frozen in `resources/app.asar` | **Repackage:** `frontend\build.bat` (or `npm run build; if ($?) { npm run package:dir }`). Ctrl+Shift+R does nothing here. |
| **Browser (standalone exe)** | `build/Debug/HDAW.exe` | Embedded via `frontend.qrc` | `frontend\build.bat` forces a clean C++ rebuild when `dist/` is newer (AUTORCC under the VS generator does NOT treat changed `dist/` as a rebuild trigger). |
| **Vite dev server** | `npm run dev` (+ engine for WS on 8766) | Live from `frontend/src` | Hard-refresh the browser (Ctrl+Shift+R). No build needed. |

**The packaged Electron app is the one users run.** Its frontend is baked into
`app.asar` at packaging time - editing source, rebuilding `dist/`, or
refreshing the window has zero effect until you repackage. `frontend\build.bat`
rebuilds the SPA, the C++ engine, runs the tests, and repackages Electron in one
command. Both build scripts detect an obsolete `app.asar` and fail/warn loudly,
so you can't silently iterate against a stale `.asar`.

**The packaged app's ENGINE comes from `build/RelWithDebInfo/`** (see
`electron-builder.yml` `extraResources`), NOT `build/Debug/`. A bare
`npm run package:dir` re-ships whatever `RelWithDebInfo` happens to contain —
on 2026-08-16 that was an 11-day-old engine (8/5) with none of the
respawn-storm fixes, so the app kept crashing plugins while every Debug-mode
verification looked clean. `frontend\build.bat` builds the engine too; if you
repackage by hand, run `cmake --build build --config RelWithDebInfo` FIRST and
verify the binary (string-search the shipped `resources\engine\HDAW_headless.exe`
for a fix marker) before trusting the package.

### Engine launch: locks and `LNK1104` (2026-09-23)

Never run live engine/tests straight out of `build/` — a running exe locks its
own file and the next link dies with **`LNK1104`** (hit twice on 2026-09-23).
Launch via the repo-root **`mcp-launch.bat`**; it copies `HDAW_headless.exe`,
`hdaw_plugin_host.exe` and `hdaw_plugin_scanner.exe` to `%TEMP%`, verifies each
copy (size + MD5 + the `audit_song_structure` sentinel), prepends the build
dirs to `PATH` so the temp copy resolves its DLLs, and `taskkill`s stale
engines first — so the build-dir exes stay free for the linker.

- **Do not re-derive a manual copy-and-launch.** A hand-rolled copy of the
  engine to `%TEMP%` died with exit `0x7FFFFFFF` even with `PATH` set: it skips
  the launcher's build-dir `PATH` prepend, so the DLLs never resolve.
- **Symptom:** `LNK1104: cannot open ...` on `HDAW_headless.exe` /
  `hdaw_plugin_host.exe` / `hdaw_plugin_scanner.exe` after an engine or test
  was left running from `build/` — kill it (the launcher does this itself) and
  rebuild.

### Build-time sink: stale `sccache` daemon + sandboxed `%TEMP%` (2026-09-25)

Every compile here goes through the **`sccache`** daemon even though sccache is
documented as OFF: `build/CMakeCache.txt` carries
`CMAKE_CXX_COMPILER_LAUNCHER:UNINITIALIZED=sccache` (line ~60) and
`CMAKE_C_COMPILER_LAUNCHER:UNINITIALIZED=sccache` (line ~84) as stale
command-line cache entries, alongside `HDAW_USE_SCCACHE:BOOL=OFF` (line ~395) —
the option's `else()` branch never clears the `UNINITIALIZED` launcher values
(`CMakeLists.txt:26-38`), so the cache wins.

Consequence on a sandboxed box: `sccache` writes temporary files for each
compile, and a daemon **started earlier with the sandbox-denied `%TEMP%`** fails
every translation unit with

```
sccache: error: failed to execute compile
sccache: caused by: Compiler not supported: "failed to write temporary file"
```

`ninja: build stopped: subcommand failed.` — measured 2026-09-25 building
`tests/test_main.cpp.obj`. Pointing `TMP`/`TEMP` at a workspace scratch dir for
the *build command alone* does **not** help: the already-running daemon keeps
its old (denied) temp dir, because the daemon outlives the client.

**Remedy (once per stale-daemon situation, not per build):**

1. `sccache --stop-server`
2. run the build with `TMP`/`TEMP` set to a workspace scratch dir — both to the
   **literal** path (`set TMP=D:\…\hdaw3\.tmp_build` then
   `set TEMP=D:\…\hdaw3\.tmp_build`; `$env:TMP=…; $env:TEMP=…` in PowerShell, or the
   `env` of the spawning process) so the daemon that is auto-restarted on the next
   compile inherits a writable temp. Setting only one, or using `%TEMP%`/`%TMP%`
   expansion, re-introduces the parse-time trap documented above;
3. verify: the build returns rc 0 (`[dsh-build] All targets up to date …`).

`build/CMakeCache.txt` is the place to look if this comes back after a
reconfigure — do not "fix" it by editing `dsh-build-fast.bat`/`build-fast.bat`.

## Testing

- **C++ engine tests (gtest):** `build/hdaw_tests_engine.exe` + `hdaw_tests_mcp.exe` + `hdaw_tests_frontend.exe` + `hdaw_tests_platform.exe` (flat Ninja RelWithDebInfo layout — there is no `build/Debug/`; 2026-09-28 test-time split, `build-fast.bat test` builds all four, `build-fast.bat all` also builds `hdaw_plugin_host.exe` which the PluginIsolation/CrashRecovery suites require)
  - Filter: `--gtest_filter=SuiteName.*`
  - Full suite: **1768 tests / 264 suites, ~44 min serial** (measured 2026-09-21 — STALE: the
    suite was 1328/216 on 2026-09-02 and keeps growing. The current reference run is the
    2026-09-24 one in `docs/testing-mcp.md` → "Testing" → "Clean reference baseline", which
    also catalogs the environmental failure classes). Fast iteration tier: `run_fast_tests.bat` — the old **~3.3 min** figure is **STALE** (measured 2026-09-25: the tier excludes the 5 heaviest suites, but the remaining ~1900 tests still cost ~0.5-0.7 s each on this box, so an unscoped run of the tier did **not finish in 40 min**). Per-test cost is dominated by engine construction, not test logic: measured 2026-09-25 a no-engine DSP test (`InternalFx.DelaySyncDivisionTracksTempo`) is ~1 ms, while `MasterGain.*` is ~0.66 s/test, `BusSendRpcTest.*` ~0.55 s/test and `McpCoverageTest.*` ~0.46 s/test — with **584 `engine.initialize()` call sites across `tests/`**. The practical fast path is the shard runner (`run-tests-sharded.ps1`): sharding pays off — a scoped 2-shard `BusSendRpcTest.*` run (32 tests) measured **12 s wall vs 17.5 s serial (~2.3x)**, both shards green (2026-09-25). Run the full suite before delivery. A native **shard runner** exists — `run-tests-sharded.ps1 [-Shards N] [-Filter ...]` (the canonical parallel runner; it replaced the WSL-only `scripts/run-tests-parallel.sh`): it splits the gtest list, keeps the device/plugin-dependent suites in ONE extra serial process, aggregates per-shard logs, and exits non-zero on failure. Its shard count is **not validated** — see the caveat below.
  - **Device-dependent suites need a working audio route; when it is missing they fail with `getTrack() == nullptr` / `tr == nullptr` even SOLO.** That is the documented deviceless pattern (lessons 9/17: no device → `rebuildRoutingGraph` no-ops → `getTrack()` nulls), and it hits `InternalFx`, `MasterGain`, `MasterBusFx`, `AudioPoolDedup`, `AudioEngineReadFacadeTest`, `AutomationPidRouting`, `RenderSequenceRelease`, `McpCoverageTest` and the export/plugin-spawn suites. **Diagnostic rule:** if every failing assertion is a null track/processor, the run is environmental (check the audio device) — do not blame parallel runs, the runner, or your change. Observed 2026-09-21: a device-healthy serial run passed all of them, and the same binary failed them an hour later.
  - Sharding caveat: the calibration runs for `run-tests-sharded.ps1` were contaminated by exactly that environmental failure (its failures were all null-track ones), so **no safe shard count has been established** — the default is a conservative 2. Re-measure on a device-healthy machine before trusting a higher `-Shards`. Update 2026-09-25: a scoped **2-shard `BusSendRpcTest.*` run is measured green** (32 tests, 12 s vs 17.5 s serial, ~2.3x), but **no full-suite shard count has been validated**. What *is* independently true: concurrent runs cannot collide on proxy pipe/shm names (unique namespace per manager instance, lesson 20) or on render temp targets (pid-tagged since 2026-09-21).
  - **The audio route also disappears in a disconnected RDP session (2026-09-23).** The deviceless pattern above can appear even when `Get-CimInstance Win32_SoundDevice` reports the hardware fine — a **disconnected** RDP session exposes no routable endpoint, so `rebuildRoutingGraph` still no-ops and every live-graph/render suite fails with the identical null `(track)`/`(rm)`/`(tr)` signature. Observed 2026-09-23: `query session` showed the active session as `> hapbt 2 Disc` (`rdp-tcp … Listen`) while Realtek/Focusrite/NVIDIA sound devices all reported Status OK, yet `TrackFxRebuildRace.*` (11), `TrackMixerState.*` (2), `MasterBusFx` (6, some as SEH `0xc0000005` inside the test body), `AudioPoolDedup.*` (3), `InternalFx.*` (7), `MasterGain.SurvivesRebuild`, `AudioEngineReadFacadeTest.GetFxProgramList*`, `RenderSequenceRelease.RebuildReleasesPreviousGraphChildren` ("no new `hdaw_plugin_host.exe` after `addFxSlot`"), `StreamingPoolDedup.EngineWires…` (`openCount 0`) and `SongCells.LockSkipsAndRerollBumpsSeed`/`HarvestNotesAndRemove` (`filled == 0`) all failed. **Diagnostic recipe:** (1) `query session` — is the active session `Disc`?; (2) run an **untouched** device-dependent control suite (`MasterGain.SurvivesRebuild`, `InternalFx.FilterLowpassAttenuatesAboveCutoff`, `AudioPoolDedup.EngineWires…`) — if those fail null-track too, the run is environmental, not change-induced; (3) if the failures land in **40–70 ms** instead of the ~**1900 ms** they take with a healthy route, the graph never settled — no route. Timing discriminator: the same binary passed live-graph tests minutes earlier (`AutomationSendBusPids` asserting on LIVE `rm->getSend`/`rm->getFxBus` + `processBlock`; `VerifyPart` renders), so a device-regression is the cause, not the batch. **Rule:** verify a batch's engine changes in a session with a live route (or compare against a pre-change log) and never attribute null-track failures to the change without that untouched control.
  - **Virus warmup watchdog no longer dumps during the intentional warmup (fixed 2026-09-25).** The child's
    hang watchdog (`src/proxy/host/PluginHost.cpp`, the thread that writes the `processBlock hung for 1s`
    minidump) fired during the INTENTIONAL Virus warmup, because the warmup pumps `processBlock` for
    `HDAW_CHILD_WARMUP_SECONDS` (default 12 s) inside `processBlockActive`. Measured cost: each Virus-family
    slot spawn wrote a 330-670 MB minidump into the child's temp dir (15 spawns = 4.5 GB in one test session;
    production writes them into `%TEMP%` on every Osirus/OsTIrus/Virus export), and one shard/process was lost
    per 4-shard run while this was active (cause unclassified — no WER dump existed for `hdaw_tests.exe`).
    Fix: while `warmupActive` the threshold is `warmupExpectedMs + 1000 ms`, `hangMs` resets on the
    warmup→audio transition, and a genuine hang still dumps (`warmupExpectedMs + 1 s`). Test hook
    `HDAW_TEST_HANG_MS` (default unset/dead) covers the real-hang path; regression tests
    `PluginIsolation.VirusWarmupWritesNoHangDump` / `PluginIsolation.RealHangWritesHangDump`.
    **Size, fixed 2026-09-30 (P2-b):** the watchdog's dump is now **stack-only** (`MiniDumpNormal` —
    thread stacks + module list, kilobytes) because the pre-fix `MiniDumpWithFullMemory` wrote
    **1.5-2 GB per dump** (twice in one ion_rift session; the emulated device's firmware image
    dominates the child's address space) — the watchdog's question is WHERE `processBlock` is stuck,
    which a stack answers. Only the SEH **crash** path still takes a full-memory dump. The 330-670 MB
    figures above are pre-fix history. Policy pinned by `DumpPolicy.*` (`tests/unit/proxy/common_test.cpp`);
    `PluginHost.cpp` also logs the measured duration/threshold next to the stable dump filename.
  - **Default device open probes for capture endpoints instead of always attempting 2-in/2-out (2026-09-25).**
    `AudioEngine::initialize` used to always attempt `initialiseWithDefaultDevices(2,2)` and fall back to
    `(0,2)`; on a capture-less box (RDP render-only, documented above) the first attempt always failed. Now
    `defaultDeviceTypeHasInputs` probes the device type once per process (memoized; `HDAW_TEST_FORCE_NO_CAPTURE`
    overrides for tests) and requests `(0,2)` directly when there are no inputs; behaviour is unchanged when
    inputs exist. Regression tests in `AudioEngineReadFacadeTest.*`
    (`ShouldRequestInputsCoversBothBranches`, `CapturelessDeviceTypeOpensOutputOnlyAndEngineIsUsable`,
    `ForcedCapturelessOpensOutputOnly`).
  - **Device caveat (2026-09-26):** this box's audio endpoint state changes across sessions — a Focusrite
    capture endpoint was present on 2026-09-26, and a "No driver" state was observed earlier the same day —
    so device-dependent suites can flip between green and environmental-failure between runs. Re-run solo
    before blaming a change.
  - The "current baseline" NUMBERS are not maintained here any more — the 2026-09-21 full
    serial run was 1768/264 -> 1728 passed, 39 skipped, 1 failed, but the live reference run
    is `docs/testing-mcp.md` → "Testing" → "Clean reference baseline" (2026-09-24:
    1971/285 — 1952 pass / 39 skipped / 11 failures, all environmental). What this file
    keeps: the environmental-failure *diagnostics*. The 2026-09-21 single failure
    `PluginIsolation.LargeStateRoundTripThroughProxy` (a 0-byte read of a 100 KB chunked state) passes solo and the whole `PluginIsolation.*:CrashRecovery.*` set (57 tests) is green solo — a load flake in the state-chunk path (lessons 14/26), not a regression. Real-plugin `FxMidiInjection.*` was verified separately: 22/23, the single failure being the cross-shard temp-file collision documented above, since fixed with a pid-tagged temp name (the identical sharded sweep is 24/24 post-fix).
  - Previous baseline (2026-09-02, post DISABLED-test rewrite pass): 0 failed; 4 RealtimeSafety detector tests SKIP in release configs (`BufferCheck` is `#if JUCE_DEBUG`-only by design); 0 DISABLED — every formerly `DISABLED_` test is either re-enabled against current contracts (PluginIsolation ×4, ExportVolumeBypass.RealProjectVolumeSensitivity, TrackFXSlotShowEditor — see `docs/archive/plans/2026-09-02-seven-failure-baseline-fix.md`) or re-enabled after its fix (`ExportAudioWithMultipleIsolatedInstances`, commit abf8a3d).
  - Build sequentially: two concurrent `build-fast` invocations on the same `build/` dir overwrite each other's `.ninja_log`, and the next build re-runs as near-full. One build at a time. Confirmed again 2026-09-24 with parallel agent slices: concurrent ninja/cmake in one `build/` tree corrupts outputs — RC1109 `manifest.res` lock, transient C1083 `Permission denied` on `.obj`s, corrupt `HDAW_lib.lib` (LNK1136, fixed by deleting it and relinking), LNK1168 on a locked `hdaw_tests.exe`. Slice-level working rule (edit-only slices, orchestrator owns the one build): `docs/testing-mcp.md` → "Parallel agent slices: build/test ownership".
  - **(WSL-only — does not apply on the native Windows dev box; there, edit the file directly and the mtimes are correct.)** WSL-side edits must be synced for the Windows compiler (drvfs/9p attribute cache shows stale content/mtimes for minutes): after editing from WSL, `cp <file> /mnt/c/temp/sync_tmp.cpp`, then from Windows `Copy-Item C:\temp\sync_tmp.cpp -> <D: path> -Force`, then touch `(Get-Item <path>).LastWriteTime = Get-Date`, and verify with PowerShell `Select-String`/`Get-Content` (never findstr through bash→cmd quoting). Symptom if skipped: ninja rebuilds "succeed" against stale sources. Verified recipe — see `docs/archive/plans/2026-09-02-seven-failure-baseline-fix.md` outcome.
  - 1015→1768 tests across 182→264 suites: MCP tools/server, transport, tracks, clips,
    notes, FX, automation, undo, save/load, phrase generation, slicing, merge,
    ripple delete, ghost clips, stretch, markers, error conditions, batch ops,
    plugin isolation, audio pool, streaming, arranger, session, library,
    note operators, tempo points, sends, MIDI FX.
  - **Engine change test discipline:** when modifying the C++ core, RPC surface,
    or JUCE interfaces, assess test impact before finishing: (1) identify gtest
    suites that exercise the changed code (RPC handlers → MCP tool tests,
    ValueTree mutations → track/clip/note tests, audio-thread logic → transport
    tests) and update them if signatures/return shapes/behavior changed; (2) if
    the change adds a new RPC method, command, or JUCE-facing interface with no
    coverage, add a gtest — the suite is the contract the frontend and MCP
    server rely on; (3) run `build/Debug/hdaw_tests.exe` to confirm no
    regression. An engine change with no test consideration is incomplete.
- **DEPRECATED (2026-09-23):** the Electron frontend is a separate project — do NOT
  run its suites (`cd frontend; npm test`, `npm run test:watch`,
  `npm run test:coverage`, `cd frontend; npm run test:e2e`). Retained below for the
  day it returns; engine-only verification is the four `build/hdaw_tests_*.exe` gtest
  binaries + the MCP surface.
- **Frontend unit tests (Vitest) — DEPRECATED (2026-09-23):** do NOT run (`cd frontend; npm test`)
  - ~177 tests: Zustand stores (transport, ui, project, notify, meter, browser),
    hooks (useTimelineDrag), utils (rowLayout, theme, grooveUtils), and
    components (StatusBar, Toaster, BottomTabs, MidiFxChain, WaveformCanvas,
    MidiThumbnailCanvas, StepSequencer, TimelineContextMenu, MixerStrip,
    TrackHeaders, Icons).
  - Watch: `npm run test:watch` · Coverage: `npm run test:coverage`
- **Frontend E2E tests (Playwright) — DEPRECATED (2026-09-23):** do NOT run (`cd frontend; npm run test:e2e`)
  - ~197 tests in `e2e/*.spec.ts`. `app.spec.ts` = render smoke; the rest are
    user-journey regressions that drive the real app (click/drag/keyboard) and
    assert on DOM/canvas/snapshot state — the layer that catches the recurring
    interaction bugs unit tests miss (drag stale-closures, rubber-band
    hit-testing, waveform display, selection→editor opening, context menus).
  - The `webServer` in `playwright.config.ts` auto-starts the engine
    (`build\Debug\HDAW.exe`, with `HDAW_NO_BROWSER=1`) plus the Vite dev server
    (port 5173); tests run against the **live** frontend, so frontend changes
    are picked up with no rebuild/repackage. Requires a current
    `build/Debug/HDAW.exe` and Playwright browsers (`npx playwright install chromium`).
  - `workers: 1` — the engine is a singleton serving one project, so tests run
    serially; each calls `startApp()` (clicks "New Project") for a clean state.
  - Test seams: `window.rpc` for RPC setup, `data-clip-id` on `.tl-clip` for
    targeting clips, `HDAW_NO_BROWSER`. Shared helpers in `e2e/helpers.ts`
    (`startApp`, `rpcCall`, `addMidiClip`/`addAudioClip`, `dragClip`, `writeSineWav`).
  - **Clip-position assertions must poll.** After an RPC that shifts clips
    (insert silence, duplicate region, move), the tree-change notification is
    debounced (~16 ms) so the DOM doesn't update immediately. Assert positions
    with Playwright's `expect.toPass()` polling, not a one-shot read:
    `await expect(async () => { expect(await clipLeft(...)).toBeGreaterThan(...); }).toPass({ timeout: 10000 });`

