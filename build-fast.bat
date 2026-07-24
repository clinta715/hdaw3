@echo off
setlocal

REM build-fast.bat — incremental build script for HDAW
REM Usage:
REM   build-fast              Build HDAW.exe only (skip tests/headless)
REM   build-fast test         Build hdaw_tests.exe only
REM   build-fast all          Build everything
REM   build-fast ninja        Reconfigure with Ninja (one-time, much faster)
REM   build-fast frontend     Build frontend only (dist/ + dist-electron/)
REM   build-fast package      Build frontend + repackage the Electron app
REM
REM STALE-FRONTEND TRAP: the app users run is the PACKAGED Electron app
REM (frontend/release/win-unpacked/HDAW.exe), whose frontend is frozen into
REM resources/app.asar. Plain C++ builds (build-fast / build-fast all) do NOT
REM update it — after frontend changes use 'build-fast package'. The standalone
REM HDAW.exe embeds the SPA via a Qt resource that AUTORCC won't re-embed on an
REM incremental build either. See AGENTS.md "How frontend changes reach the
REM running app".

set BUILD_DIR=%~dp0build
set CONFIG=Debug

if "%1"=="ninja" goto :ninja
if "%1"=="frontend" goto :frontend
if "%1"=="package" goto :package
if "%1"=="test" goto :test
if "%1"=="all" goto :all
if "%1"=="" goto :hdaw
echo Unknown target: %1
echo Usage: build-fast [ninja^|test^|all^|frontend^|package]
exit /b 1

:hdaw
cmake --build "%BUILD_DIR%" --config %CONFIG% --target HDAW -- /m /v:minimal 2>&1
if %errorlevel% neq 0 exit /b %errorlevel%
echo [build-fast] HDAW.exe up to date.
echo [build-fast] NOTE: C++ builds do NOT refresh the frontend users see.
echo [build-fast]       After frontend changes run 'build-fast package'.
goto :eof

:test
cmake --build "%BUILD_DIR%" --config %CONFIG% --target hdaw_tests -- /m /v:minimal 2>&1
if %errorlevel% neq 0 exit /b %errorlevel%
echo [build-fast] hdaw_tests.exe up to date.
goto :eof

:all
cmake --build "%BUILD_DIR%" --config %CONFIG% -- /m /v:minimal 2>&1
if %errorlevel% neq 0 exit /b %errorlevel%
echo [build-fast] All targets up to date.
echo [build-fast] NOTE: C++ builds do NOT refresh the frontend users see.
echo [build-fast]       After frontend changes run 'build-fast package'.
goto :eof

:frontend
cd /d "%~dp0frontend"
call npm run build
if %errorlevel% neq 0 exit /b %errorlevel%
echo [build-fast] Frontend built (dist/ + dist-electron/).
echo [build-fast] To update the app users run, now use 'build-fast package'.
goto :eof

:package
cd /d "%~dp0frontend"
call npm run build
if %errorlevel% neq 0 exit /b %errorlevel%
call npm run package:dir
if %errorlevel% neq 0 exit /b %errorlevel%
echo [build-fast] Electron app repackaged: frontend\release\win-unpacked\HDAW.exe
goto :eof

:ninja
echo [build-fast] Reconfiguring with Ninja (one-time)...
cmake -S "%~dp0." -B "%BUILD_DIR%" -G Ninja -DCMAKE_BUILD_TYPE=%CONFIG% -DCMAKE_PREFIX_PATH=%CMAKE_PREFIX_PATH%
if %errorlevel% neq 0 (
    echo [build-fast] Ninja configure failed. Falling back to Visual Studio.
    echo [build-fast] Make sure ninja.exe is on PATH and a VS developer prompt is active.
    exit /b 1
)
echo [build-fast] Ninja configured. Now run 'build-fast' for fast incremental builds.
goto :eof
