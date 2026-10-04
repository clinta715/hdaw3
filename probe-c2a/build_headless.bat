@echo off
REM Build only HDAW_headless.exe (Ninja single-config). Instrumentation logging only.
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cd /d E:\build\hdaw3
cmake --build build --target HDAW_headless 2>&1
exit /b %errorlevel%
