@echo off
REM ============================================================================
REM compile_forward.bat - Compile forward pass shaders to SPIR-V
REM ============================================================================
REM
REM Phase 2.19.3: Forward Shaders
REM
REM Compiles:
REM - forward.vert -> forward.vert.spv
REM - forward.frag -> forward.frag.spv
REM
REM Requirements:
REM - glslangValidator in PATH (Vulkan SDK)
REM
REM ============================================================================

echo ========================================
echo Compiling Forward Pass Shaders
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
echo [1/2] Compiling forward.vert...
glslangValidator -V forward.vert -o forward.vert.spv
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: Failed to compile forward.vert
    pause
    exit /b 1
)
echo SUCCESS: forward.vert.spv created

REM Compile fragment shader
echo.
echo [2/2] Compiling forward.frag...
glslangValidator -V forward.frag -o forward.frag.spv
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: Failed to compile forward.frag
    pause
    exit /b 1
)
echo SUCCESS: forward.frag.spv created

REM Show file sizes
echo.
echo ========================================
echo Compilation Complete
echo ========================================
dir /B forward.*.spv
echo.

REM Show detailed info
for %%F in (forward.*.spv) do (
    echo %%F - %~z%%F bytes
)

echo.
echo Shaders compiled successfully!
echo Copy .spv files to: gamedata/shaders/
pause
