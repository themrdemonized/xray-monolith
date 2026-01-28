@echo off
REM ============================================================================
REM Compile Cascade Shadow Accumulation Shader to SPIR-V
REM ============================================================================

echo Compiling accum_sun_cascades shader...

REM Check if glslangValidator exists
where /q glslangValidator
if errorlevel 1 (
    echo ERROR: glslangValidator not found in PATH
    echo Please install Vulkan SDK or use full path
    echo.
    echo Trying C:\VulkanSDK\...
    set GLSLANG=C:\VulkanSDK\1.4.335.0\Bin\glslangValidator.exe
) else (
    set GLSLANG=glslangValidator
)

REM Compile fragment shader (vertex shader same as accum_sun_simple.vert)
echo Compiling accum_sun_cascades.frag...
%GLSLANG% -V accum_sun_cascades.frag -o accum_sun_cascades.frag.spv
if errorlevel 1 (
    echo ERROR: Failed to compile accum_sun_cascades.frag
    pause
    exit /b 1
)

echo.
echo Cascade shadow shader compiled successfully!
echo.
pause
