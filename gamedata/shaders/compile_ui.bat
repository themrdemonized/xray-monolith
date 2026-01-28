@echo off
REM Compile UI shaders to SPIR-V

set VULKAN_SDK=C:\VulkanSDK\1.3.296.0
set GLSLC=%VULKAN_SDK%\Bin\glslc.exe

if not exist "%GLSLC%" (
    echo ERROR: glslc.exe not found at %GLSLC%
    echo Please install Vulkan SDK or update VULKAN_SDK path
    pause
    exit /b 1
)

echo Compiling UI shaders...

echo   - ui.vert -^> ui.vert.spv
"%GLSLC%" -fshader-stage=vertex ui.vert -o ui.vert.spv
if %ERRORLEVEL% NEQ 0 (
    echo FAILED to compile ui.vert
    pause
    exit /b 1
)

echo   - ui.frag -^> ui.frag.spv
"%GLSLC%" -fshader-stage=fragment ui.frag -o ui.frag.spv
if %ERRORLEVEL% NEQ 0 (
    echo FAILED to compile ui.frag
    pause
    exit /b 1
)

echo.
echo UI shaders compiled successfully!
echo.
echo Deploying to D:\anomaly\gamedata\shaders\vulkan\...

if not exist "D:\anomaly\gamedata\shaders\vulkan\" (
    mkdir "D:\anomaly\gamedata\shaders\vulkan\"
)

copy /Y ui.vert.spv "D:\anomaly\gamedata\shaders\vulkan\ui.vert.spv"
copy /Y ui.frag.spv "D:\anomaly\gamedata\shaders\vulkan\ui.frag.spv"

echo.
echo Deployment complete!
pause
