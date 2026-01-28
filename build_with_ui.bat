@echo off
REM Full build script: Compile UI shaders + Build engine + Deploy

echo ============================================================
echo X-Ray Vulkan UI Renderer - Full Build Script
echo ============================================================
echo.

REM ============================================================
REM Step 1: Compile UI Shaders
REM ============================================================
echo [1/3] Compiling UI shaders...
echo.

cd gamedata\shaders

set VULKAN_SDK=C:\VulkanSDK\1.3.296.0
set GLSLC=%VULKAN_SDK%\Bin\glslc.exe

if not exist "%GLSLC%" (
    echo ERROR: glslc.exe not found at %GLSLC%
    echo Please install Vulkan SDK or update VULKAN_SDK path
    pause
    exit /b 1
)

echo   Compiling ui.vert...
"%GLSLC%" -fshader-stage=vertex ui.vert -o ui.vert.spv
if %ERRORLEVEL% NEQ 0 (
    echo   FAILED to compile ui.vert
    cd ..\..
    pause
    exit /b 1
)
echo   [OK] ui.vert.spv

echo   Compiling ui.frag...
"%GLSLC%" -fshader-stage=fragment ui.frag -o ui.frag.spv
if %ERRORLEVEL% NEQ 0 (
    echo   FAILED to compile ui.frag
    cd ..\..
    pause
    exit /b 1
)
echo   [OK] ui.frag.spv

echo.
echo   Deploying shaders to D:\anomaly\gamedata\shaders\vulkan\...

if not exist "D:\anomaly\gamedata\shaders\vulkan\" (
    mkdir "D:\anomaly\gamedata\shaders\vulkan\"
)

copy /Y ui.vert.spv "D:\anomaly\gamedata\shaders\vulkan\ui.vert.spv" > nul
copy /Y ui.frag.spv "D:\anomaly\gamedata\shaders\vulkan\ui.frag.spv" > nul
echo   [OK] Shaders deployed

cd ..\..

echo.
echo ============================================================
echo [2/3] Building engine...
echo ============================================================
echo.

call do_build2.bat
if %ERRORLEVEL% NEQ 0 (
    echo.
    echo ERROR: Build failed!
    pause
    exit /b 1
)

echo.
echo ============================================================
echo [3/3] Deploying executable...
echo ============================================================
echo.

if exist "bin\x64\VerifiedDX11\xrEngine.exe" (
    copy /Y "bin\x64\VerifiedDX11\xrEngine.exe" "D:\anomaly\bin\" > nul
    echo   [OK] xrEngine.exe deployed to D:\anomaly\bin\
) else (
    echo   ERROR: xrEngine.exe not found in bin\x64\VerifiedDX11\
    pause
    exit /b 1
)

echo.
echo ============================================================
echo BUILD COMPLETE!
echo ============================================================
echo.
echo Next steps:
echo 1. Launch game: D:\anomaly\bin\xrEngine.exe
echo 2. Check logs: D:\anomaly\appdata\logs\xray_egorb.log
echo 3. Check UI status: UI_RENDERER_STATUS.md
echo.
pause
