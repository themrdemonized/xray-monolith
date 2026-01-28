@echo off
REM ============================================================================
REM compile_accum_point.bat - Compile Point Light Shaders
REM ============================================================================
REM
REM Phase 2.16.3: Point Light Shaders
REM
REM Compiles accum_point vertex and fragment shaders to SPIR-V.
REM
REM ============================================================================

echo.
echo ============================================================================
echo Compiling Point Light Shaders (Phase 2.16.3)
echo ============================================================================
echo.

set GLSLANG=C:\VulkanSDK\1.4.335.0\Bin\glslangValidator.exe

if not exist "%GLSLANG%" (
    echo ERROR: glslangValidator not found at %GLSLANG%
    echo Please update the path or install Vulkan SDK
    pause
    exit /b 1
)

echo Using glslangValidator: %GLSLANG%
echo.

REM ============================================================================
REM Vertex Shader
REM ============================================================================
echo [1/2] Compiling accum_point.vert...
"%GLSLANG%" -V accum_point.vert -o accum_point.vert.spv --target-env vulkan1.3

if %ERRORLEVEL% NEQ 0 (
    echo ERROR: Failed to compile accum_point.vert
    pause
    exit /b 1
)

echo OK: accum_point.vert.spv created
echo.

REM ============================================================================
REM Fragment Shader
REM ============================================================================
echo [2/2] Compiling accum_point.frag...
"%GLSLANG%" -V accum_point.frag -o accum_point.frag.spv --target-env vulkan1.3

if %ERRORLEVEL% NEQ 0 (
    echo ERROR: Failed to compile accum_point.frag
    pause
    exit /b 1
)

echo OK: accum_point.frag.spv created
echo.

REM ============================================================================
REM Summary
REM ============================================================================
echo ============================================================================
echo Compilation Complete!
echo ============================================================================
echo.
echo Output files:
echo   - accum_point.vert.spv
echo   - accum_point.frag.spv
echo.
echo Phase 2.16.3: Point Light Shaders READY
echo.

pause
