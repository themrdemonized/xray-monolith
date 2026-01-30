@echo off
REM ============================================================================
REM Compile All GLSL Shaders to SPIR-V
REM ============================================================================
REM
REM Phase 0: Shader Porting - Compilation Script
REM
REM Usage: compile_all.bat
REM
REM Compiles all .vert and .frag files in current directory to .spv
REM ============================================================================

echo.
echo ========================================================================
echo Compiling GLSL Shaders to SPIR-V
echo ========================================================================
echo.

set GLSLC=glslc
set TARGET=vulkan1.2
set ERRORS=0

REM Check if glslc is available
where glslc >nul 2>nul
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: glslc not found!
    echo Please install Vulkan SDK: https://vulkan.lunarg.com/sdk/home
    pause
    exit /b 1
)

echo Using glslc from:
where glslc
echo.
echo Target: %TARGET%
echo.

REM ============================================================================
REM Compile Vertex Shaders
REM ============================================================================

echo Compiling vertex shaders...
echo.

for %%f in (*.vert) do (
    echo [VERT] %%f
    %GLSLC% -fshader-stage=vertex --target-env=%TARGET% "%%f" -o "%%f.spv"
    if %ERRORLEVEL% NEQ 0 (
        echo ERROR: Failed to compile %%f
        set /a ERRORS+=1
    ) else (
        echo   OK: %%f.spv
    )
    echo.
)

REM ============================================================================
REM Compile Fragment Shaders
REM ============================================================================

echo Compiling fragment shaders...
echo.

for %%f in (*.frag) do (
    echo [FRAG] %%f
    %GLSLC% -fshader-stage=fragment --target-env=%TARGET% "%%f" -o "%%f.spv"
    if %ERRORLEVEL% NEQ 0 (
        echo ERROR: Failed to compile %%f
        set /a ERRORS+=1
    ) else (
        echo   OK: %%f.spv
    )
    echo.
)

REM ============================================================================
REM Summary
REM ============================================================================

echo ========================================================================
if %ERRORS% EQU 0 (
    echo SUCCESS: All shaders compiled!
    echo.
    echo SPIR-V files created:
    dir /b *.spv 2>nul
) else (
    echo FAILED: %ERRORS% shader(s) failed to compile!
    echo Please fix errors above and try again.
)
echo ========================================================================
echo.

pause
