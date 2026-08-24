@echo off
setlocal
set "BUILD_DIR=%~dp0build\Debug"
set "SRC=%BUILD_DIR%\HDAW_headless.exe"
set "DST=%TEMP%\HDAW_headless_mcp.exe"

echo [debug] SRC=%SRC%
echo [debug] DST=%DST%
echo [debug] Checking if SRC exists...
if not exist "%SRC%" (
    echo ERROR: %SRC% not found
    exit /b 1
)
echo [debug] SRC exists, size:
for %%A in ("%SRC%") do echo   %%~zA bytes

echo [debug] Copying to temp...
copy /Y "%SRC%" "%DST%"
if errorlevel 1 (
    echo ERROR: Copy failed
    exit /b 1
)

echo [debug] Setting PATH...
set "PATH=%BUILD_DIR%;%PATH%"

echo [debug] Launching MCP server...
"%DST%" --mcp-stdio %*
