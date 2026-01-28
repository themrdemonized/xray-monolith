@echo off
REM Compile accum_sun_simple shaders to SPIR-V

echo Compiling accum_sun_simple shaders...

REM Vertex shader
glslangValidator -V accum_sun_simple.vert -o accum_sun_simple.vert.spv
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: Failed to compile vertex shader
    pause
    exit /b 1
)
echo   - accum_sun_simple.vert.spv OK

REM Fragment shader
glslangValidator -V accum_sun_simple.frag -o accum_sun_simple.frag.spv
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: Failed to compile fragment shader
    pause
    exit /b 1
)
echo   - accum_sun_simple.frag.spv OK

echo.
echo Compilation successful!
echo.
pause
