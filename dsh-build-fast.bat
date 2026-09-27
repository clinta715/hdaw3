@echo off
setlocal enabledelayedexpansion

REM dsh-build-fast.bat - build wrapper for HDAW
REM
REM Usage (same as build-fast.bat):
REM   dsh-build-fast.bat              Build HDAW.exe (RelWithDebInfo)
REM   dsh-build-fast.bat debug        Build HDAW.exe (Debug)
REM   dsh-build-fast.bat test         Build hdaw_tests.exe only
REM   dsh-build-fast.bat all          Build everything
REM   dsh-build-fast.bat frontend     Build frontend
REM   dsh-build-fast.bat ninja        Reconfigure with Ninja (recommended)
REM
REM Compiler selection (env var HDAW_COMPILER):
REM   HDAW_COMPILER=msvc   (default) Uses cl.exe, build dir: build/
REM   HDAW_COMPILER=clang  Uses clang-cl, build dir: build-clang/
REM
REM Clang-cl targets (configure once with 'clang-ninja', then use normally):
REM   dsh-build-fast.bat clang-ninja  Configure build-clang/ with clang-cl
REM   HDAW_COMPILER=clang dsh-build-fast.bat test   (etc.)
REM
REM Key differences from build-fast.bat:
REM   1. Uses Ninja as default generator (faster incremental builds)
REM   2. Adds explicit error reporting with exit codes
REM   3. Skips the Electron stale-asar check (irrelevant for engine builds)
REM   4. Optional Clang-cl compiler (better diagnostics, sanitizers)

set "ROOT=%~dp0"
set CONFIG=RelWithDebInfo

REM -- Compiler selection -----------------------------------------------------
set "COMPILER=msvc"
if /i "%HDAW_COMPILER%"=="clang" set "COMPILER=clang"
if /i "%1"=="clang-ninja" set "COMPILER=clang"

REM Build dir: separate per compiler (never mix object files)
set "BUILD_DIR=%ROOT%build"
if "%COMPILER%"=="clang" set "BUILD_DIR=%ROOT%build-clang"
if not "%HDAW_BUILD_DIR%"=="" set "BUILD_DIR=%HDAW_BUILD_DIR%"

REM -- Build scratch TMP/TEMP (B8) --------------------------------------------
REM The low-integrity sandbox denies the real %TEMP% and link.exe dies on
REM lnk{GUID}.tmp (LNK1104). Point the toolchain's temp at a workspace scratch
REM (gitignored via the .tmp_* glob) so ninja, cl and link all inherit a
REM writable TMP. setlocal scopes this to this script's process tree; an
REM inherited HDAW_BUILD_TMP overrides the default location.
set "BUILD_SCRATCH=%ROOT%.tmp_build_scratch\lnk"
if not "%HDAW_BUILD_TMP%"=="" set "BUILD_SCRATCH=%HDAW_BUILD_TMP%"
if not exist "%BUILD_SCRATCH%" mkdir "%BUILD_SCRATCH%" 2>nul
set "TMP=%BUILD_SCRATCH%"
set "TEMP=%BUILD_SCRATCH%"
echo [dsh-build] build scratch TMP/TEMP: %BUILD_SCRATCH%

REM -- WSL time-sync hook (no-op on native Windows) ---------------------------
call "%ROOT%scripts\time-sync.cmd" 2>nul

REM -- MSVC bootstrap --------------------------------------------------------
where cl.exe >nul 2>nul
if errorlevel 1 (
    set "VSB="
    if exist "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" set "VSB=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
    if exist "C:\Program Files\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" set "VSB=C:\Program Files\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
    if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" set "VSB=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
    if exist "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" set "VSB=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
    if defined VSB (
        call "!VSB!" >nul
        if errorlevel 1 (
            echo [dsh-build] ERROR: vcvars64.bat failed ^(rc=!errorlevel!^)
            exit /b 1
        )
    ) else (
        echo [dsh-build] ERROR: cl.exe not found and no VS install detected.
        exit /b 1
    )
)

REM -- Clang-cl discovery (only when COMPILER=clang) --------------------------
if "%COMPILER%"=="clang" (
    set "CLANG_CL=clang-cl"
    where clang-cl.exe >nul 2>nul
    if errorlevel 1 (
        set "CLANG_CL="
        REM VS ships clang-cl as "C++ Clang tools for Windows"
        if exist "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\Llvm\bin\clang-cl.exe" set "CLANG_CL=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\Llvm\bin\clang-cl.exe"
        if exist "C:\Program Files\Microsoft Visual Studio\18\BuildTools\VC\Tools\Llvm\bin\clang-cl.exe" set "CLANG_CL=C:\Program Files\Microsoft Visual Studio\18\BuildTools\VC\Tools\Llvm\bin\clang-cl.exe"
        if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\Llvm\bin\clang-cl.exe" set "CLANG_CL=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\Llvm\bin\clang-cl.exe"
        if exist "C:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Tools\Llvm\bin\clang-cl.exe" set "CLANG_CL=C:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Tools\Llvm\bin\clang-cl.exe"
        if exist "C:\Program Files\LLVM\bin\clang-cl.exe" set "CLANG_CL=C:\Program Files\LLVM\bin\clang-cl.exe"
        if defined CLANG_CL (
            echo [dsh-build] clang-cl not on PATH - using !CLANG_CL!
        ) else (
            echo [dsh-build] ERROR: clang-cl not found. Install "C++ Clang tools for Windows" from VS Installer.
            echo [dsh-build]        Or install LLVM: winget install LLVM.LLVM
            exit /b 1
        )
    )
)

REM -- CMake discovery -------------------------------------------------------
set "CMAKE_EXE=cmake"
where cmake.exe >nul 2>nul
if errorlevel 1 (
    set "CMAKE_EXE="
    if exist "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" set "CMAKE_EXE=C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
    if exist "C:\Program Files\Microsoft Visual Studio\18\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" set "CMAKE_EXE=C:\Program Files\Microsoft Visual Studio\18\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
    if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" set "CMAKE_EXE=C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
    if exist "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" set "CMAKE_EXE=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
    if exist "C:\Qt\Tools\CMake_64\bin\cmake.exe" set "CMAKE_EXE=C:\Qt\Tools\CMake_64\bin\cmake.exe"
    if defined CMAKE_EXE echo [dsh-build] cmake not on PATH - using !CMAKE_EXE!
)
if not defined CMAKE_EXE (
    echo [dsh-build] ERROR: cmake.exe not found.
    exit /b 1
)

REM -- Ninja discovery -------------------------------------------------------
set "NINJA_EXE=ninja"
where ninja.exe >nul 2>nul
if errorlevel 1 (
    set "NINJA_EXE="
    if exist "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe" set "NINJA_EXE=C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
    if exist "C:\Program Files\Microsoft Visual Studio\18\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe" set "NINJA_EXE=C:\Program Files\Microsoft Visual Studio\18\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
    if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe" set "NINJA_EXE=C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
    if exist "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe" set "NINJA_EXE=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
    if exist "C:\Qt\Tools\Ninja\ninja.exe" set "NINJA_EXE=C:\Qt\Tools\Ninja\ninja.exe"
    if defined NINJA_EXE echo [dsh-build] ninja not on PATH - using !NINJA_EXE!
)
if not defined NINJA_EXE (
    echo [dsh-build] ERROR: ninja.exe not found. Run 'build-fast ninja' from a VS prompt first.
    exit /b 1
)

REM -- Target dispatch -------------------------------------------------------
if "%1"=="debug" set CONFIG=Debug
if "%1"=="ninja" goto :ninja
if "%1"=="clang-ninja" goto :clang_ninja
if "%1"=="frontend" goto :frontend
if "%1"=="package" goto :package
if "%1"=="test" goto :test
if "%1"=="all" goto :all
if "%1"=="" goto :hdaw
echo Unknown target: %1
echo Usage: dsh-build-fast [debug^|ninja^|clang-ninja^|test^|all^|frontend^|package]
exit /b 1

:hdaw
call :build_target HDAW
if !errorlevel! neq 0 exit /b !errorlevel!
echo [dsh-build] HDAW.exe up to date (config: %CONFIG%, compiler: %COMPILER%).
goto :eof

:test
call :build_target hdaw_tests
if !errorlevel! neq 0 exit /b !errorlevel!
echo [dsh-build] hdaw_tests.exe up to date (config: %CONFIG%, compiler: %COMPILER%).
goto :eof

:all
call :build_target
if !errorlevel! neq 0 exit /b !errorlevel!
echo [dsh-build] All targets up to date (config: %CONFIG%, compiler: %COMPILER%).
goto :eof

REM -- Build via Ninja (or fall back to MSBuild) ------------------------------
REM Ninja: single-config, direct mtime tracking, no .sln overhead.
REM /Z7 embedded debug info (CMakeLists.txt) eliminates C1041 PDB contention.
:build_target
if exist "%BUILD_DIR%\build.ninja" (
    if "%1"=="" (
        "!CMAKE_EXE!" --build "%BUILD_DIR%" 2>&1
    ) else (
        "!CMAKE_EXE!" --build "%BUILD_DIR%" --target %1 2>&1
    )
) else (
    if "%1"=="" (
        "!CMAKE_EXE!" --build "%BUILD_DIR%" --config %CONFIG% -- /m /v:minimal 2>&1
    ) else (
        "!CMAKE_EXE!" --build "%BUILD_DIR%" --config %CONFIG% --target %1 -- /m /v:minimal 2>&1
    )
)
exit /b !errorlevel!

:ninja
echo [dsh-build] Configuring with Ninja (one-time setup)...
if "%CMAKE_PREFIX_PATH%"=="" if exist "C:\Qt\6.11.2\msvc2022_64" set "CMAKE_PREFIX_PATH=C:\Qt\6.11.2\msvc2022_64"
"!CMAKE_EXE!" -S "%ROOT%." -B "%BUILD_DIR%" -G Ninja -DCMAKE_BUILD_TYPE=%CONFIG% -DCMAKE_PREFIX_PATH=%CMAKE_PREFIX_PATH% -DCMAKE_MAKE_PROGRAM="!NINJA_EXE!"
if %errorlevel% neq 0 (
    echo [dsh-build] ERROR: Ninja configure failed ^(rc=!errorlevel!^).
    exit /b 1
)
echo [dsh-build] Ninja configured. Now run 'dsh-build-fast' for fast incremental builds.
goto :eof

:clang_ninja
echo [dsh-build] Configuring build-clang/ with clang-cl + lld-link (one-time setup)...
if "%CMAKE_PREFIX_PATH%"=="" if exist "C:\Qt\6.11.2\msvc2022_64" set "CMAKE_PREFIX_PATH=C:\Qt\6.11.2\msvc2022_64"
"!CMAKE_EXE!" -S "%ROOT%." -B "%BUILD_DIR%" -G Ninja ^
    -DCMAKE_BUILD_TYPE=%CONFIG% ^
    -DCMAKE_PREFIX_PATH=%CMAKE_PREFIX_PATH% ^
    -DCMAKE_MAKE_PROGRAM="!NINJA_EXE!" ^
    -DCMAKE_C_COMPILER="!CLANG_CL!" ^
    -DCMAKE_CXX_COMPILER="!CLANG_CL!" ^
    -DCMAKE_C_FLAGS="-fuse-ld=lld" ^
    -DCMAKE_CXX_FLAGS="-fuse-ld=lld" ^
    -DCMAKE_EXE_LINKER_FLAGS="-fuse-ld=lld" ^
    -DCMAKE_SHARED_LINKER_FLAGS="-fuse-ld=lld" ^
    -DCMAKE_MODULE_LINKER_FLAGS="-fuse-ld=lld"
if %errorlevel% neq 0 (
    echo [dsh-build] ERROR: clang-cl configure failed ^(rc=!errorlevel!^).
    exit /b 1
)
echo [dsh-build] clang-cl configured in build-clang/.
echo [dsh-build] Build with: set HDAW_COMPILER=clang ^&^& dsh-build-fast.bat [target]
goto :eof

:frontend
echo [dsh-build] Building frontend...
cd /d "%ROOT%frontend"
call npm run build
if !errorlevel! neq 0 (
    echo [dsh-build] ERROR: frontend build failed ^(rc=!errorlevel!^)
    cd /d "%ROOT%"
    exit /b !errorlevel!
)
cd /d "%ROOT%"
echo [dsh-build] Frontend built (dist/).
goto :eof

:package
echo [dsh-build] Building full package (C++ + frontend + Electron)...
call :build_target
if !errorlevel! neq 0 exit /b !errorlevel!
cd /d "%ROOT%frontend"
call npm run build
if !errorlevel! neq 0 exit /b !errorlevel!
call npm run package:dir
if !errorlevel! neq 0 exit /b !errorlevel!
cd /d "%ROOT%"
echo [dsh-build] Electron app repackaged: frontend\release\win-unpacked\HDAW.exe
goto :eof
