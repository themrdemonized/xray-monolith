@echo off
REM ============================================================================
REM compile_gbuffer.bat - Compile G-Buffer Shaders
REM ============================================================================
REM
REM Phase 2.21.1: G-Buffer Shaders
REM
REM Compiles gbuffer vertex and fragment shaders to SPIR-V format.
REM
REM ============================================================================

echo.
echo ============================================================================
echo   Compiling G-Buffer Shaders (Phase 2.21.1)
echo ============================================================================
echo.

REM Check if glslc is available
where glslc >nul 2>nul
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: glslc not found in PATH!
    echo Please install Vulkan SDK and add glslc to PATH.
    echo.
    echo Vulkan SDK: https://vulkan.lunarg.com/
    pause
    exit /b 1
)

REM ============================================================================
REM Compile Vertex Shader
REM ============================================================================
echo [1/2] Compiling gbuffer.vert...
glslc -fshader-stage=vert gbuffer.vert -o gbuffer.vert.spv
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: Failed to compile gbuffer.vert
    pause
    exit /b 1
)
echo       SUCCESS: gbuffer.vert.spv created

REM ============================================================================
REM Compile Fragment Shader
REM ============================================================================
echo [2/2] Compiling gbuffer.frag...
glslc -fshader-stage=frag gbuffer.frag -o gbuffer.frag.spv
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: Failed to compile gbuffer.frag
    pause
    exit /b 1
)
echo       SUCCESS: gbuffer.frag.spv created

echo.
echo ============================================================================
echo   G-Buffer Shaders Compiled Successfully!
echo ============================================================================
echo.
echo Output:
echo   - gbuffer.vert.spv (vertex shader)
echo   - gbuffer.frag.spv (fragment shader - 4 MRT output)
echo.
echo Next Step: Implement phase_gbuffer() method to render geometry
echo.

pause
