@echo off
call "%~dp0R1_env.cmd"
if errorlevel 1 exit /b 1
cd /d "%~dp0"
if not exist build mkdir build
if not exist package\gamedata\plugins\R1_XREAL_Native mkdir package\gamedata\plugins\R1_XREAL_Native
cl /nologo /O2 /MT /W3 /I vendor\minhook\include /c vendor\minhook\src\buffer.c vendor\minhook\src\hook.c vendor\minhook\src\trampoline.c vendor\minhook\src\hde\hde64.c /Fo:build\
if errorlevel 1 exit /b 1
cl /nologo /EHsc /std:c++17 /O2 /MT /W4 /I vendor\minhook\include /LD native\R1_native.cpp build\buffer.obj build\hook.obj build\trampoline.obj build\hde64.obj /Fo:build\ /Fe:package\gamedata\plugins\R1_XREAL_Native\R1_XREAL_Native.dll /link bcrypt.lib d3d11.lib dxgi.lib d3dcompiler.lib /IMPLIB:build\R1_XREAL_Native.lib /INCREMENTAL:NO /DYNAMICBASE /NXCOMPAT
