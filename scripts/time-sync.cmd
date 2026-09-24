@echo off
REM ===========================================================================
REM time-sync.cmd -- OPT-IN WSL clock-drift guard used as a pre-build hook.
REM
REM DEFAULT (native Windows 11 dev box): a no-op. Nothing in a native build
REM crosses WSL's drvfs/9p view, so there is no clock to synchronise and this
REM script exits 0 immediately -- no wsl.exe spawn, no output, no side effects.
REM That keeps the call sites (build-fast.bat, the CMake
REM `hdaw_time_sync` ALL target) free on every build.
REM
REM WHY THE GUARD EXISTS AT ALL: WSL2's virtual clock drifts away from the
REM Windows host (seconds to minutes over hours of uptime). When the source
REM tree IS reached through the WSL view, ninja/MSBuild compare file mtimes
REM through drvfs/9p and a drifted clock makes timestamps look "in the future"
REM (rebuild everything) or "in the past" (never rebuild, STALE artifacts).
REM Only that setup is worth the process spawn, so the hook is opt-in.
REM
REM ENABLE (WSL users only): set HDAW_TIME_SYNC=1 before building
REM   cmd:        set HDAW_TIME_SYNC=1
REM   PowerShell: $env:HDAW_TIME_SYNC=1
REM This script then converts its own location to a WSL path and runs
REM scripts/time-sync.sh inside WSL (that file documents the sync mechanics).
REM
REM NEVER fails a build: exits 0 on every path whether or not WSL / ntpdate /
REM the sudo rule is available (the .sh side prints the warning; set
REM HDAW_TIME_SYNC_STRICT=1 there to turn warnings into errors).
REM ===========================================================================

if not "%HDAW_TIME_SYNC%"=="1" exit /b 0

setlocal

for %%I in ("%~dp0..") do set "WIN_ROOT=%%~fI"
for %%I in ("%WIN_ROOT%") do set "DRIVE=%%~dI"
set "DRIVE_LETTER=%DRIVE:~0,1%"
for %%L in (a b c d e f g h i j k l m n o p q r s t u v w x y z) do if /i "%DRIVE_LETTER%"=="%%L" set "DRIVE_LETTER=%%L"
set "WIN_REST=%WIN_ROOT:~2%"
set "WSL_ROOT=/mnt/%DRIVE_LETTER%/%WIN_REST:\=/%"

where wsl.exe >nul 2>nul
if errorlevel 1 exit /b 0

REM Single quotes inside the argument keep the WSL path (which may contain
REM spaces) intact when wsl.exe re-parses the joined command line.
wsl.exe -e bash -lc "'%WSL_ROOT%/scripts/time-sync.sh'"
exit /b 0
