@echo off
setlocal enabledelayedexpansion

:: ─────────────────────────────────────────────────────────────────────────────
:: HDAW catch-all build.
:: Builds every element of the application and guarantees consistency:
::   1. React SPA + Electron main/preload (npm run build  ->  dist/ , dist-electron/)
::   2. C++ engine: HDAW_lib, HDAW.exe (browser, embeds SPA), HDAW_headless.exe
::                  (Electron engine), hdaw_plugin_scanner.exe, hdaw_tests.exe
::   3. Runs the gtest suite so a green build means the engine actually works
::   4. Packages the Electron app (release/win-unpacked/)
::
:: GUARANTEE against the stale-bundle trap: HDAW.exe embeds the frontend via
:: Qt resources (src/resources/frontend.qrc). CMake's AUTORCC under the VS
:: generator does NOT treat the referenced dist/ files (or even the .qrc mtime)
:: as rebuild triggers, so a plain rebuild silently serves an outdated SPA.
:: When frontend/dist/ is newer than HDAW.exe we force a full clean C++ rebuild
:: (per-target --clean-first is unsafe here: HDAW/HDAW_headless/hdaw_tests share
:: HDAW_lib, so cleaning one wipes the others). Slower, but never stale.
::
:: Control flow uses goto/labels and top-level (non-parenthesized) execution of
:: the heavy programs. cmd's block parser is fragile when long-running programs
:: run inside "if (...) {...}" blocks (a stray token in their output, or a
:: leading "(" in an echo inside a block, raises ". was unexpected at this
:: time"). Keeping heavy work at the top level avoids that class of bug.
::
:: Run from anywhere; it cd's to its own location (frontend/).
:: ─────────────────────────────────────────────────────────────────────────────

cd /d "%~dp0"

set "ROOT=%CD%\.."
set "BUILD_DIR=%ROOT%\build"

:: ── 1. Frontend (React SPA + Electron main/preload). prebuild regenerates
::    src/version.ts automatically; this single command covers the whole FE.
echo === [1/4] Building frontend (dist/ + dist-electron/) ===
call npm run build
if !errorlevel! neq 0 goto :fail_fe

:: ── 2. Decide whether the C++ tree needs a forced clean rebuild. We force it
::    if configure hasn't run, OR if the frontend dist/ is newer than the
::    browser-mode binary that embeds it.
set "FORCE_CLEAN=0"
:: CMakeCache.txt is the generator-independent marker that configure has run
:: (VS 18 2026 emits an .slnx, not the legacy .sln, so don't key off .sln).
if not exist "%BUILD_DIR%\CMakeCache.txt" set "FORCE_CLEAN=1"
if not exist "%BUILD_DIR%\Debug\HDAW.exe" set "FORCE_CLEAN=1"

if "!FORCE_CLEAN!"=="0" (
    rem PowerShell gives a locale-independent LastWriteTime comparison.
    rem Returns 1 (dist newer) or 0 (binary already current). The [int] cast
    rem must wrap the WHOLE boolean expression, not the DateTime operands.
    for /f "delims=" %%r in ('powershell -NoProfile -Command "[int]((Get-Item -LiteralPath 'dist\index.html').LastWriteTime -gt (Get-Item -LiteralPath '%BUILD_DIR%\Debug\HDAW.exe').LastWriteTime)"') do set "DIST_NEWER=%%r"
    if "!DIST_NEWER!"=="1" set "FORCE_CLEAN=1"
)

echo === [2/4] Building C++ engine (all targets) ===
if "!FORCE_CLEAN!"=="1" goto :cpp_clean
call cmake --build "%BUILD_DIR%" --config Debug
if !errorlevel! neq 0 goto :fail_cpp
goto :cpp_done

:cpp_clean
echo Forced clean rebuild ^(frontend dist newer than embedded SPA, or first build^)
call cmake --build "%BUILD_DIR%" --config Debug --clean-first
if !errorlevel! neq 0 goto :fail_cpp

:cpp_done

:: ── 3. Run the gtest suite. Run at the top level (not in an if-block) so the
::    program's output can't confuse cmd's block parser. A build that compiles
::    but fails tests is not "done".
echo === [3/4] Running engine tests ===
set "TEST_RC=0"
if not exist "%BUILD_DIR%\Debug\hdaw_tests.exe" goto :no_tests
call "%BUILD_DIR%\Debug\hdaw_tests.exe" --gtest_brief=1
set "TEST_RC=!errorlevel!"
if !TEST_RC! neq 0 goto :fail_tests
goto :tests_done

:no_tests
echo WARNING: hdaw_tests.exe not found - test run skipped. >&2

:tests_done

:: ── 4. Package the Electron app. electron-builder.yml copies HDAW_headless.exe,
::    hdaw_plugin_scanner.exe and DLLs from build/Debug/ as extraResources.
echo === [4/4] Packaging Electron app ===
call npx electron-builder --win --x64 --dir
if !errorlevel! neq 0 goto :fail_pkg

echo.
echo === Done ===
echo Browser mode:    %BUILD_DIR%\Debug\HDAW.exe
echo Electron engine: %BUILD_DIR%\Debug\HDAW_headless.exe
echo Electron app:    release\win-unpacked\HDAW.exe
echo Tests:           %BUILD_DIR%\Debug\hdaw_tests.exe
exit /b 0

:: ── Failure exits ──────────────────────────────────────────────────────────
:fail_fe
echo ERROR: frontend build failed. >&2
exit /b 1

:fail_cpp
echo ERROR: C++ build failed. >&2
exit /b 1

:fail_tests
echo ERROR: engine tests failed ^(exit !TEST_RC!^). >&2
exit /b !TEST_RC!

:fail_pkg
echo ERROR: Electron packaging failed. >&2
exit /b 1
