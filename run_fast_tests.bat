@echo off
setlocal
REM run_fast_tests.bat - builds the four test exes then runs the suite EXCLUDING
REM the known-long render/recipe/spawn-timeout suites (PsytranceComposition +
REM ExportAutomation render real 8s+ deliverables; CrashRecovery/PluginIsolation
REM spawn isolated children with 30s READY waits). This tier is NOT ~3 minutes:
REM the remaining ~1900 tests still cost ~0.5-0.7 s EACH on this box, because
REM per-test cost is dominated by engine construction, not test logic (measured
REM 2026-09-25: a no-engine DSP test is ~1 ms, MasterGain ~0.66 s/test,
REM BusSendRpcTest ~0.55 s/test, McpCoverageTest ~0.46 s/test; there are 584
REM engine.initialize() call sites across tests/). An unscoped run of this tier
REM did NOT finish in 40 min (measured 2026-09-25). The practical fast path is
REM the shard runner, run-tests-sharded.ps1. Full coverage still requires the
REM unfiltered suite - run that before delivery/commit. See AGENTS.md "Testing"
REM and docs/plans/2026-09-02-seven-failure-baseline-fix.md.
REM
REM 2026-09-28 test-time split: the suite is four exes (hdaw_tests_engine /
REM _mcp / _frontend / _platform). The same exclusion filter is applied to each;
REM a gtest filter that matches nothing in an exe is a no-op, so one filter fits
REM all four. The exes run SEQUENTIALLY here (conservative tier); use
REM run-tests-sharded.ps1 for parallel execution.
cd /d "%~dp0"
call build-fast.bat test
if errorlevel 1 exit /b 1
set "HDAW_FAST_FILTER=--gtest_filter=-PsytranceComposition.*:ExportAutomation.*:CrashRecovery.*:PluginIsolation.*:RenderSequenceRelease.*"
for %%E in (hdaw_tests_engine hdaw_tests_mcp hdaw_tests_frontend hdaw_tests_platform) do (
    echo [run_fast_tests] %%E
    .\build\%%E.exe %HDAW_FAST_FILTER%
    if errorlevel 1 exit /b 1
)
