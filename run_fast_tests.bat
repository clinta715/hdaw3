@echo off
REM run_fast_tests.bat - builds hdaw_tests then runs the suite EXCLUDING the
REM known-long render/recipe/spawn-timeout suites (PsytranceComposition +
REM ExportAutomation render real 8s+ deliverables; CrashRecovery/PluginIsolation
REM spawn isolated children with 30s READY waits). This tier is NOT ~3 minutes:
REM the remaining ~1900 tests still cost ~0.5-0.7 s EACH on this box, because
REM per-test cost is dominated by engine construction, not test logic (measured
REM 2026-09-25: a no-engine DSP test is ~1 ms, MasterGain ~0.66 s/test,
REM BusSendRpcTest ~0.55 s/test, McpCoverageTest ~0.46 s/test; there are 584
REM engine.initialize() call sites across tests/). An unscoped run of this tier
REM did NOT finish in 40 min (measured 2026-09-25). The practical fast path is
REM the shard runner, run-tests-sharded.ps1. Full coverage still requires the
REM unfiltered suite - run that before delivery/commit. See AGENTS.md "Testing" and
REM docs/plans/2026-09-02-seven-failure-baseline-fix.md.
cd /d "D:\pdf\roo projects\hdaw3"
call build-fast.bat test
if errorlevel 1 exit /b 1
.\build\hdaw_tests.exe --gtest_filter=-PsytranceComposition.*:ExportAutomation.*:CrashRecovery.*:PluginIsolation.*:RenderSequenceRelease.*
