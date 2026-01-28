@echo off
REM Launch X-Ray Monolith with Vulkan Renderer
REM ============================================

echo ============================================
echo   X-Ray Monolith - Vulkan Test Launch
echo ============================================
echo.

REM Set Vulkan SDK path
set VULKAN_SDK=C:\VulkanSDK\1.4.335.0
set PATH=%VULKAN_SDK%\Bin;%PATH%

REM Enable Vulkan validation layers
set VK_LAYER_PATH=%VULKAN_SDK%\Bin
set VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation

REM Engine paths
set ENGINE_DIR=%~dp0_build\_game\bin_dbg
set GAME_DIR=%~dp0gamedata

echo Engine directory: %ENGINE_DIR%
echo Game directory: %GAME_DIR%
echo Vulkan SDK: %VULKAN_SDK%
echo.

REM Check if AnomalyDX9.exe exists
if not exist "%ENGINE_DIR%\AnomalyDX9.exe" (
    echo ERROR: AnomalyDX9.exe not found!
    echo Please build the engine first using build_engine_simple.ps1
    pause
    exit /b 1
)

REM Launch engine
echo Launching X-Ray engine with Vulkan renderer...
echo.

cd /d "%ENGINE_DIR%"
start "" AnomalyDX9.exe -r renderer_vk -nointro -designer

echo.
echo Engine launched! Check console for Vulkan output.
echo.
pause
