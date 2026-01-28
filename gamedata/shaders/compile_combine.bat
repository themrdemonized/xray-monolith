@echo off
REM ============================================================================
REM compile_combine.bat - Compile combine pass shaders to SPIR-V
REM ============================================================================
REM
REM Phase 2.18.2: Combine Shader
REM
REM Compiles:
REM - combine.vert -> combine.vert.spv
REM - combine.frag -> combine.frag.spv
REM
REM Requirements:
REM - glslangValidator in PATH (Vulkan SDK)
REM
REM ============================================================================

echo ========================================
echo Compiling Combine Pass Shaders
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
echo [1/2] Compiling combine.vert...
glslangValidator -V combine.vert -o combine.vert.spv
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: Failed to compile combine.vert
    pause
    exit /b 1
)
echo SUCCESS: combine.vert.spv created

REM Compile fragment shader
echo.
echo [2/2] Compiling combine.frag...
glslangValidator -V combine.frag -o combine.frag.spv
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: Failed to compile combine.frag
    pause
    exit /b 1
)
echo SUCCESS: combine.frag.spv created

REM Show file sizes
echo.
echo ========================================
echo Compilation Complete
echo ========================================
dir /B combine.*.spv
echo.

REM Show detailed info
for %%F in (combine.*.spv) do (
    echo %%F - %~z%%F bytes
)

echo.
echo Shaders compiled successfully!
echo Copy .spv files to: gamedata/shaders/
pause
