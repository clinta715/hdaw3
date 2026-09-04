@echo off
REM run_fast_tests.bat - fast iteration tier: builds hdaw_tests then runs the
REM suite EXCLUDING the known-long render/recipe/spawn-timeout suites
REM (PsytranceComposition + ExportAutomation render real 8s+ deliverables;
REM CrashRecovery/PluginIsolation spawn isolated children with 30s READY
REM waits). Full coverage still requires the unfiltered suite - run that
REM before delivery/commit. See AGENTS.md "Testing" and
REM docs/plans/2026-09-02-seven-failure-baseline-fix.md.
cd /d "D:\pdf\roo projects\hdaw3"
call build-fast.bat test
if errorlevel 1 exit /b 1
.\build\hdaw_tests.exe --gtest_filter=-PsytranceComposition.*:ExportAutomation.*:CrashRecovery.*:PluginIsolation.*:RenderSequenceRelease.*
