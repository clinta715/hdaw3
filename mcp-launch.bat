@echo off
:: mcp-launch.bat — copy-on-launch wrapper for HDAW MCP server.
:: Copies HDAW_headless.exe, hdaw_plugin_host.exe AND hdaw_plugin_scanner.exe
:: to %TEMP% before running, so the original files are never locked and
:: cmake --build can overwrite them freely. The plugin host too:
:: getHostExePath() resolves it as a sibling of the running exe, and a stale
:: host in %TEMP% breaks the READY handshake for every isolated plugin. The
:: scanner likewise: EngineMCP spawns the scanner from %TEMP% at scan time,
:: so a stale scanner in %TEMP% silently downgrades every plugin scan.
:: The next MCP session automatically picks up the freshly built binaries.
::
:: Engine-source resolution is GENERATOR-AWARE (lesson 21: never launch a
:: stale engine): the active build directory is Ninja single-config
:: (build\HDAW_headless.exe, RelWithDebInfo, release Qt DLLs); the VS
:: generator would output to build\Debug\. Each payload is resolved as:
::   1. build\<name>       (Ninja — the canonical build-fast.bat output)
::   2. build\Debug\<name> (VS generator fallback, with a loud warning)
::   3. neither            -> error
:: If you rebuild with the Visual Studio generator, be aware the stale
:: build\*.exe from a previous Ninja build will WIN this resolution —
:: delete build\*.exe or update this script in that world.
::
:: DLLs (Qt, JUCE, etc.) stay in the build directories; PATH is extended so
:: the temp copy can find them without duplicating 100+ MB of DLLs. Both
:: config dirs are prepended (release first): DLL names do not collide
:: across configs (Qt6Core.dll vs Qt6Cored.dll).
::
:: Usage (in opencode.jsonc mcpServers):
::   "command": "D:\\pdf\\roo projects\\hdaw3\\mcp-launch.bat"
::   "args": []

setlocal

set "NINJA_DIR=%~dp0build"
set "VSDBG_DIR=%~dp0build\Debug"
set "SRC=%NINJA_DIR%\HDAW_headless.exe"
set "DST=%TEMP%\HDAW_headless_mcp.exe"

:: Kill stale engines before copying: a lingering MCP engine holds the target
:: exe locked -> copy /Y fails silently -> the session reuses the pre-fix
:: binary. Killing the parent does NOT kill children on Windows, so the
:: plugin hosts must be killed explicitly (lesson 20). Any holder of the
:: target at launch time is by definition stale (one engine per session).
taskkill /F /IM HDAW_headless_mcp.exe >nul 2>&1
taskkill /F /IM hdaw_plugin_host.exe >nul 2>&1

call :resolve_engine_src "HDAW_headless.exe" SRC
if errorlevel 1 exit /b 1

copy /Y "%SRC%" "%DST%" >nul 2>&1
if errorlevel 1 (
    echo ERROR: Failed to copy HDAW_headless.exe to temp. >&2
    exit /b 1
)
for %%A in ("%SRC%") do set "SRCSZ=%%~zA"
for %%A in ("%DST%") do set "DSTSZ=%%~zA"
if not "%SRCSZ%"=="%DSTSZ%" (
    echo ERROR: Size mismatch copying HDAW_headless.exe: source %SRCSZ% bytes, destination %DSTSZ% bytes. Stale engine holding the target? >&2
    exit /b 1
)

call :resolve_engine_src "hdaw_plugin_host.exe" HOST_SRC
if errorlevel 1 exit /b 1
set "HOST_DST=%TEMP%\hdaw_plugin_host.exe"

copy /Y "%HOST_SRC%" "%HOST_DST%" >nul 2>&1
if errorlevel 1 (
    echo ERROR: Failed to copy hdaw_plugin_host.exe to temp. >&2
    exit /b 1
)
for %%A in ("%HOST_SRC%") do set "HOST_SRCSZ=%%~zA"
for %%A in ("%HOST_DST%") do set "HOST_DSTSZ=%%~zA"
if not "%HOST_SRCSZ%"=="%HOST_DSTSZ%" (
    echo ERROR: Size mismatch copying hdaw_plugin_host.exe: source %HOST_SRCSZ% bytes, destination %HOST_DSTSZ% bytes. Stale engine holding the target? >&2
    exit /b 1
)

call :resolve_engine_src "hdaw_plugin_scanner.exe" SCAN_SRC
if errorlevel 1 exit /b 1
set "SCAN_DST=%TEMP%\hdaw_plugin_scanner.exe"

copy /Y "%SCAN_SRC%" "%SCAN_DST%" >nul 2>&1
if errorlevel 1 (
    echo ERROR: Failed to copy hdaw_plugin_scanner.exe to temp. >&2
    exit /b 1
)
for %%A in ("%SCAN_SRC%") do set "SCAN_SRCSZ=%%~zA"
for %%A in ("%SCAN_DST%") do set "SCAN_DSTSZ=%%~zA"
if not "%SCAN_SRCSZ%"=="%SCAN_DSTSZ%" (
    echo ERROR: Size mismatch copying hdaw_plugin_scanner.exe: source %SCAN_SRCSZ% bytes, destination %SCAN_DSTSZ% bytes. Stale engine holding the target? >&2
    exit /b 1
)

:: Prepend the build directories to PATH so the copied exe finds its DLLs
:: (release first — both configs' DLL names coexist without collision).
set "PATH=%NINJA_DIR%;%VSDBG_DIR%;%PATH%"

:: Crash capture (the §3 abort class: debug-CRT heap asserts / std::terminate
:: used to die with only an MSVC dialog). DEFAULT ON: when procdump is on
:: PATH and HDAW_NO_CRASH_CAPTURE is not set, start the engine normally then
:: ATTACH procdump as a watcher — procdump's UTF-16 banner goes to a log
:: file, NOT the engine's stdout, so the MCP stdio contract stays clean.
:: Opt-out with HDAW_NO_CRASH_CAPTURE=1.
:: The gtest runner %TEMP%\hdaw_capture\run_with_capture.ps1 remains the
:: default crash-capture path for tests. HDAW_CRASH_DUMP_TYPE=mini (-> -mm)
:: is honored by mcp-launch-capture.ps1 (default -ma).
:: procdump invocation is delegated to PowerShell (mcp-launch-capture.ps1):
:: (1) cmd's own argument quoting is a minefield for paths with spaces (the
:: engine path lives in %TEMP% and the build dirs have spaces); (2) an inline
:: `-Command "..."` string cannot survive embedded double quotes like
:: "$($engine.Id)" -- cmd's quote pairing breaks and the whole batch file
:: aborts with '... was unexpected at this time' before the engine starts.
:: The capture logic therefore lives in a .ps1 invoked via -File; it starts
:: the engine with inherited stdio, then attaches procdump to the engine PID
:: as a watcher. Procdump's banner/error streams go to log files in the
:: capture dir, NOT the engine's stdout, so the MCP stdio contract stays clean.
if not "%HDAW_NO_CRASH_CAPTURE%"=="1" (
    setlocal EnableDelayedExpansion
    set "ENGINE=!DST!"
    powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0mcp-launch-capture.ps1"
    exit /b !ERRORLEVEL!
)

"%DST%" --mcp-stdio %*
exit /b %ERRORLEVEL%

:: Resolves the newest-preferred source path for an engine payload.
::   %1 = exe file name, %2 = name of the variable to receive the path.
:: Preference: Ninja root (build\) over VS-generator (build\Debug\).
:: Warns once when falling back to the VS-generator tree (likely stale).
:resolve_engine_src
set "RES_NAME=%~1"
set "RES_NINJA=%NINJA_DIR%\%RES_NAME%"
set "RES_VSDBG=%VSDBG_DIR%\%RES_NAME%"
if exist "%RES_NINJA%" (
    set "%~2=%RES_NINJA%"
    exit /b 0
)
if exist "%RES_VSDBG%" (
    if not defined VSDBG_WARNED (
        echo WARNING: %RES_NINJA% not found - falling back to VS-generator Debug output. Run build-fast.bat to refresh the Ninja build. >&2
        set "VSDBG_WARNED=1"
    )
    set "%~2=%RES_VSDBG%"
    exit /b 0
)
echo ERROR: Neither %RES_NINJA% nor %RES_VSDBG% found. Build first (build-fast.bat, or cmake --build build --target HDAW_headless hdaw_plugin_host hdaw_plugin_scanner). >&2
exit /b 1
