@echo off
:: mcp-launch.bat — copy-on-launch wrapper for HDAW MCP server.
:: Copies HDAW_headless.exe to %TEMP% before running it, so the original
:: file is never locked and cmake --build can overwrite it freely.
:: The next MCP session automatically picks up the freshly built binary.
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

if not exist "%SRC%" (
    echo ERROR: %SRC% not found. Run cmake --build build --config Debug --target HDAW_headless first. >&2
    exit /b 1
)

copy /Y "%SRC%" "%DST%" >nul 2>&1
if errorlevel 1 (
    echo ERROR: Failed to copy HDAW_headless.exe to temp. >&2
    exit /b 1
)

:: Prepend the build directory to PATH so the copied exe finds its DLLs
set "PATH=%BUILD_DIR%;%PATH%"

"%DST%" --mcp-stdio %*
