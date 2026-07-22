@echo off
setlocal

:: 1. Build C++ engine (HDAW_lib.lib + HDAW_headless.exe + plugin scanner)
echo === Building C++ engine ===
call cmake --build ..\build --config Debug --target HDAW_lib HDAW_headless hdaw_plugin_scanner --parallel
if %errorlevel% neq 0 exit /b %errorlevel%

:: 2. Build React frontend + Electron main process
echo === Building frontend ===
call npm run build
if %errorlevel% neq 0 exit /b %errorlevel%

:: 3. Optionally build the browser-mode HDAW.exe (bundles frontend via qrc)
echo === Building HDAW.exe (browser mode) ===
call cmake --build ..\build --config Debug --target HDAW --parallel
if %errorlevel% neq 0 exit /b %errorlevel%

:: 4. Package Electron app (copies engine binaries from build/Debug/ via electron-builder.yml extraResources)
echo === Packaging Electron app ===
call npx electron-builder --win --x64 --dir
if %errorlevel% neq 0 exit /b %errorlevel%

echo === Done ===
echo Electron package: release\win-unpacked\HDAW.exe
echo Browser mode:    ..\build\Debug\HDAW.exe
