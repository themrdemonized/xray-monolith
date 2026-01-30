@echo off
setlocal

echo Setting up build environment...
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64

echo Building xrRender_Vulkan...
cd /d "C:\Users\egorb\OneDrive\Documentos\GitHub\xray-monolith\src"
MSBuild.exe "engine-vs2022.sln" /t:xrRender_Vulkan /p:Configuration=Release /p:Platform=x64 /v:minimal /nologo

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo BUILD FAILED WITH %ERRORLEVEL% ERRORS
    exit /b 1
)

echo.
echo BUILD SUCCESS!
