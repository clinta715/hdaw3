@echo off
setlocal

REM build-fast.bat — incremental build script for HDAW
REM Usage:
REM   build-fast              Build HDAW.exe only (skip tests/headless)
REM   build-fast test         Build hdaw_tests.exe only
REM   build-fast all          Build everything
REM   build-fast ninja        Reconfigure with Ninja (one-time, much faster)
REM   build-fast frontend     Build frontend only

set BUILD_DIR=%~dp0build
set CONFIG=Debug

if "%1"=="ninja" goto :ninja
if "%1"=="frontend" goto :frontend
if "%1"=="test" goto :test
if "%1"=="all" goto :all
if "%1"=="" goto :hdaw
echo Unknown target: %1
echo Usage: build-fast [ninja|test|all|frontend]
exit /b 1

:hdaw
cmake --build "%BUILD_DIR%" --config %CONFIG% --target HDAW -- /m /v:minimal 2>&1
if %errorlevel% neq 0 exit /b %errorlevel%
echo [build-fast] HDAW.exe up to date.
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
goto :eof

:frontend
cd /d "%~dp0frontend"
call npm run build
if %errorlevel% neq 0 exit /b %errorlevel%
echo [build-fast] Frontend built. Run 'build-fast' to rebuild C++ with new frontend.
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
