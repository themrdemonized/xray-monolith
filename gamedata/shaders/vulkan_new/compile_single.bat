@echo off
REM ============================================================================
REM Compile Single GLSL Shader to SPIR-V
REM ============================================================================
REM
REM Usage: compile_single.bat shader_name.vert
REM        compile_single.bat shader_name.frag
REM
REM ============================================================================

if "%~1"=="" (
    echo Usage: compile_single.bat shader_name.vert
    echo        compile_single.bat shader_name.frag
    pause
    exit /b 1
)

set SHADER=%~1
set GLSLC=glslc
set TARGET=vulkan1.2

REM Determine shader stage from extension
set STAGE=
if "%~x1"==".vert" set STAGE=vertex
if "%~x1"==".frag" set STAGE=fragment

if "%STAGE%"=="" (
    echo ERROR: Unknown shader extension: %~x1
    echo Supported: .vert, .frag
    pause
    exit /b 1
)

echo Compiling %SHADER% (stage: %STAGE%)...
echo.

%GLSLC% -fshader-stage=%STAGE% --target-env=%TARGET% "%SHADER%" -o "%SHADER%.spv"

if %ERRORLEVEL% EQU 0 (
    echo SUCCESS: %SHADER%.spv created
) else (
    echo FAILED: Compilation error
)

pause
