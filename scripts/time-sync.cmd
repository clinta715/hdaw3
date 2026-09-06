@echo off
REM ===========================================================================
REM time-sync.cmd -- Windows-side launcher for the WSL time-sync pre-build hook.
REM
REM Converts this script's location to a WSL path and runs scripts/time-sync.sh
REM inside WSL. NEVER fails a build: exits 0 whether or not WSL / ntpdate / the
REM sudo rule is available (the .sh side prints the warning; set
REM HDAW_TIME_SYNC_STRICT=1 there to turn warnings into errors).
REM ===========================================================================
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
