@echo off
call "%~dp0R1_env.cmd"
if errorlevel 1 exit /b 1
cd /d "%~dp0"
if not exist build mkdir build
cl /nologo /std:c++17 /EHsc /W4 tests\R1_gpu_test.cpp /Fo:build\R1_gpu_test.obj /Fe:build\R1_gpu_test.exe /link d3d11.lib d3dcompiler.lib
if errorlevel 1 exit /b 1
build\R1_gpu_test.exe
