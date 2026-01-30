@echo off
REM Compile Hemisphere Lighting Shaders for Vulkan
REM Requires glslangValidator in PATH or Vulkan SDK installed

echo ========================================
echo Compiling Hemisphere Lighting Shaders
echo ========================================

REM Check if glslangValidator is available
where glslangValidator >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: glslangValidator not found in PATH
    echo Please install Vulkan SDK or add glslangValidator to PATH
    pause
    exit /b 1
)

REM Compile accum_sun_simple shaders
echo.
echo Compiling accum_sun_simple.vert...
glslangValidator -V accum_sun_simple.vert -o accum_sun_simple.vert.spv
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: Failed to compile accum_sun_simple.vert
    pause
    exit /b 1
)

echo Compiling accum_sun_simple.frag...
glslangValidator -V accum_sun_simple.frag -o accum_sun_simple.frag.spv
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: Failed to compile accum_sun_simple.frag
    pause
    exit /b 1
)

echo.
echo ========================================
echo Compilation successful!
echo ========================================
echo.
echo Compiled shaders:
echo   - accum_sun_simple.vert.spv
echo   - accum_sun_simple.frag.spv
echo.
echo These shaders now include:
echo   - GlobalLighting UBO (Set 0)
echo   - Hemisphere ambient lighting
echo   - Improved material-based specular
echo.

pause
