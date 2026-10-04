@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cd /d E:\build\hdaw3
cmake --build build --target hdaw_plugin_host 2>&1
exit /b %errorlevel%
