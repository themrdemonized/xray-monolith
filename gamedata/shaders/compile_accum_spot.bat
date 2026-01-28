@echo off
REM ============================================================================
REM compile_accum_spot.bat - Compile spot light shaders to SPIR-V
REM ============================================================================
REM
REM Phase 2.17.3: Spot Light Shaders
REM
REM Compiles:
REM - accum_spot.vert -> accum_spot.vert.spv
REM - accum_spot.frag -> accum_spot.frag.spv
REM
REM Requirements:
REM - glslangValidator in PATH (Vulkan SDK)
REM
REM ============================================================================

echo ========================================
echo Compiling Spot Light Shaders
echo ========================================

REM Check if glslangValidator exists
where glslangValidator >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: glslangValidator not found in PATH
    echo Please install Vulkan SDK and add glslangValidator to PATH
    pause
    exit /b 1
)

REM Compile vertex shader
echo.
echo [1/2] Compiling accum_spot.vert...
glslangValidator -V accum_spot.vert -o accum_spot.vert.spv
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: Failed to compile accum_spot.vert
    pause
    exit /b 1
)
echo SUCCESS: accum_spot.vert.spv created

REM Compile fragment shader
echo.
echo [2/2] Compiling accum_spot.frag...
glslangValidator -V accum_spot.frag -o accum_spot.frag.spv
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: Failed to compile accum_spot.frag
    pause
    exit /b 1
)
echo SUCCESS: accum_spot.frag.spv created

REM Show file sizes
echo.
echo ========================================
echo Compilation Complete
echo ========================================
dir /B accum_spot.*.spv
echo.

REM Show detailed info
for %%F in (accum_spot.*.spv) do (
    echo %%F - %~z%%F bytes
)

echo.
echo Shaders compiled successfully!
echo Copy .spv files to: gamedata/shaders/vulkan/
pause
