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
:: DLLs (Qt, JUCE, etc.) stay in the build directory; PATH is extended so
:: the temp copy can find them without duplicating 100+ MB of DLLs.
::
:: Usage (in opencode.jsonc mcpServers):
::   "command": "D:\\pdf\\roo projects\\hdaw3\\mcp-launch.bat"
::   "args": []

setlocal

set "BUILD_DIR=%~dp0build\Debug"
set "SRC=%BUILD_DIR%\HDAW_headless.exe"
set "DST=%TEMP%\HDAW_headless_mcp.exe"

:: Kill stale engines before copying: a lingering MCP engine holds the target
:: exe locked -> copy /Y fails silently -> the session reuses the pre-fix
:: binary. Killing the parent does NOT kill children on Windows, so the
:: plugin hosts must be killed explicitly (lesson 20). Any holder of the
:: target at launch time is by definition stale (one engine per session).
taskkill /F /IM HDAW_headless_mcp.exe >nul 2>&1
taskkill /F /IM hdaw_plugin_host.exe >nul 2>&1

if not exist "%SRC%" (
    echo ERROR: %SRC% not found. Run cmake --build build --config Debug --target HDAW_headless first. >&2
    exit /b 1
)

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

set "HOST_SRC=%BUILD_DIR%\hdaw_plugin_host.exe"
set "HOST_DST=%TEMP%\hdaw_plugin_host.exe"

if not exist "%HOST_SRC%" (
    echo ERROR: %HOST_SRC% not found. Run cmake --build build --config Debug --target hdaw_plugin_host first. >&2
    exit /b 1
)

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

set "SCAN_SRC=%BUILD_DIR%\hdaw_plugin_scanner.exe"
set "SCAN_DST=%TEMP%\hdaw_plugin_scanner.exe"

if not exist "%SCAN_SRC%" (
    echo ERROR: %SCAN_SRC% not found. Run cmake --build build --config Debug --target hdaw_plugin_scanner first. >&2
    exit /b 1
)

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

:: Prepend the build directory to PATH so the copied exe finds its DLLs
set "PATH=%BUILD_DIR%;%PATH%"

"%DST%" --mcp-stdio %*
