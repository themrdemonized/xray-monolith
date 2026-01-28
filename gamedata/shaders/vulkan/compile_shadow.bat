@echo off
REM ============================================================================
REM Compile Shadow Map Shaders to SPIR-V
REM ============================================================================

echo Compiling shadow map shaders...

REM Check if glslangValidator exists
where /q glslangValidator
if errorlevel 1 (
    echo ERROR: glslangValidator not found in PATH
    echo Please install Vulkan SDK
    pause
    exit /b 1
)

REM Compile vertex shader
echo Compiling shadow_depth.vert...
glslangValidator -V shadow_depth.vert -o shadow_depth.vert.spv
if errorlevel 1 (
    echo ERROR: Failed to compile shadow_depth.vert
    pause
    exit /b 1
)

REM Compile fragment shader
echo Compiling shadow_depth.frag...
glslangValidator -V shadow_depth.frag -o shadow_depth.frag.spv
if errorlevel 1 (
    echo ERROR: Failed to compile shadow_depth.frag
    pause
    exit /b 1
)

echo.
echo Shadow shaders compiled successfully!
echo.
pause
