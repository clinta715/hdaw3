@echo off
setlocal enabledelayedexpansion

REM build-fast.bat — incremental build script for HDAW
REM Usage:
REM   build-fast              Build HDAW.exe only (RelWithDebInfo, optimized)
REM   build-fast debug        Build HDAW.exe with Debug (breakpoints)
REM   build-fast test         Build hdaw_tests.exe only
REM   build-fast all          Build everything
REM   build-fast ninja        Reconfigure with Ninja (one-time, much faster)
REM   build-fast frontend     Build frontend only (dist/ + dist-electron/)
REM   build-fast package      Build frontend + repackage the Electron app
REM
REM STALE-ASAR GUARD: the app users run is the PACKAGED Electron app
REM (frontend/release/win-unpacked/HDAW.exe), whose frontend is frozen into
REM resources/app.asar. Plain C++/frontend builds (build-fast / all / frontend)
REM do NOT refresh app.asar — only 'build-fast package' does. After every such
REM build this script compares frontend/dist/ against app.asar and prints a
REM loud warning when the packaged app is stale, so you never accidentally test
REM an obsolete frontend. The standalone HDAW.exe embeds the SPA via a Qt
REM resource that AUTORCC won't re-embed on an incremental build either — use
REM frontend\build.bat for a guaranteed-fresh browser build. See AGENTS.md
REM "How frontend changes reach the running app".

set "ROOT=%~dp0"
set BUILD_DIR=%ROOT%build
set CONFIG=RelWithDebInfo

if "%1"=="debug" set CONFIG=Debug
if "%1"=="ninja" goto :ninja
if "%1"=="frontend" goto :frontend
if "%1"=="package" goto :package
if "%1"=="test" goto :test
if "%1"=="all" goto :all
if "%1"=="" goto :hdaw
if "%1"=="debug" goto :hdaw
echo Unknown target: %1
echo Usage: build-fast [debug^|ninja^|test^|all^|frontend^|package]
exit /b 1

:hdaw
cmake --build "%BUILD_DIR%" --config %CONFIG% --target HDAW -- /m /v:minimal 2>&1
if !errorlevel! neq 0 exit /b !errorlevel!
echo [build-fast] HDAW.exe up to date (config: %CONFIG%).
call :check_pkg
goto :eof

:test
cmake --build "%BUILD_DIR%" --config %CONFIG% --target hdaw_tests -- /m /v:minimal 2>&1
if %errorlevel% neq 0 exit /b %errorlevel%
echo [build-fast] hdaw_tests.exe up to date (config: %CONFIG%).
goto :eof

:all
cmake --build "%BUILD_DIR%" --config %CONFIG% -- /m /v:minimal 2>&1
if !errorlevel! neq 0 exit /b !errorlevel!
echo [build-fast] All targets up to date (config: %CONFIG%).
call :check_pkg
goto :eof

:frontend
cd /d "%ROOT%frontend"
call npm run build
if !errorlevel! neq 0 exit /b !errorlevel!
cd /d "%ROOT%"
echo [build-fast] Frontend built (dist/ + dist-electron/).
call :check_pkg
goto :eof

:package
cd /d "%ROOT%frontend"
call npm run build
if !errorlevel! neq 0 exit /b !errorlevel!
call npm run package:dir
if !errorlevel! neq 0 exit /b !errorlevel!
cd /d "%ROOT%"
echo [build-fast] Electron app repackaged: frontend\release\win-unpacked\HDAW.exe
call :check_pkg
goto :eof

:check_pkg
:: Warn loudly when the packaged Electron app's app.asar is older than the
:: frontend dist/ — i.e. the app users would run is stale. Only `package`
:: refreshes app.asar; plain C++/frontend builds do not. Uses ROOT (captured at
:: top) because %~dp0 is NOT the script dir inside a called label.
set "CK_DIST=!ROOT!frontend\dist\index.html"
set "CK_ASAR=!ROOT!frontend\release\win-unpacked\resources\app.asar"
if not exist "!CK_DIST!" goto :eof
if not exist "!CK_ASAR!" goto :eof
set "CK_STALE=0"
for /f "delims=" %%r in ('powershell -NoProfile -Command "[int]((Get-Item -LiteralPath '!CK_DIST!').LastWriteTime -gt (Get-Item -LiteralPath '!CK_ASAR!').LastWriteTime)"') do set "CK_STALE=%%r"
if "!CK_STALE!"=="1" (
    echo.
    echo **********************************************************
    echo *  STALE PACKAGED APP: frontend\dist\ is newer than       *
    echo *  frontend\release\win-unpacked\resources\app.asar.      *
    echo *  The packaged Electron app will run an OUTDATED frontend *
    echo *  ^(the obsolete-.asar trap^).                            *
    echo *  Fix:  build-fast package   ^(or frontend\build.bat^)     *
    echo **********************************************************
    echo.
) >&2
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
