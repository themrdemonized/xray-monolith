@echo off
setlocal

set "VSCMD_START_DIR=%CD%"
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul 2>&1

cd /d "C:\Users\egorb\OneDrive\Documentos\GitHub\xray-monolith\src"

echo Building xrGame with Vulkan configuration...
MSBuild.exe "engine-vs2022.sln" /p:Configuration=Vulkan /p:Platform=x64 /t:xrGame /v:minimal /m
if %ERRORLEVEL% NEQ 0 (
    echo BUILD FAILED
    exit /b 1
)

echo.
echo BUILD SUCCESS!
echo Copying to game folder...
copy /Y "..\_build\_game\bin_dbg\xrGame.dll" "D:\anomaly\bin\" >nul 2>&1

echo Done!
