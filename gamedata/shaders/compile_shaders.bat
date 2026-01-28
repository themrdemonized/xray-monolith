@echo off
REM Компиляция GLSL шейдеров в SPIR-V
REM Требуется установленный Vulkan SDK

echo ===============================================
echo    X-Ray Monolith - Shader Compilation
echo ===============================================
echo.

REM Проверка VULKAN_SDK
if "%VULKAN_SDK%"=="" (
    echo ERROR: VULKAN_SDK environment variable not set
    echo Please install Vulkan SDK: https://vulkan.lunarg.com/
    pause
    exit /b 1
)

set GLSLANG=%VULKAN_SDK%\Bin\glslangValidator.exe
set TARGET=vulkan1.3

REM Проверка glslangValidator
if not exist "%GLSLANG%" (
    echo ERROR: glslangValidator.exe not found
    echo Path: %GLSLANG%
    pause
    exit /b 1
)

echo Found glslangValidator: %GLSLANG%
echo Target environment: %TARGET%
echo.

REM Компиляция vertex shaders
echo Compiling vertex shaders...
for %%f in (*.vert) do (
    echo   - %%f
    "%GLSLANG%" -V --target-env %TARGET% "%%f" -o "%%f.spv"
    if errorlevel 1 (
        echo ERROR: Failed to compile %%f
        pause
        exit /b 1
    )
)

REM Компиляция fragment shaders
echo Compiling fragment shaders...
for %%f in (*.frag) do (
    echo   - %%f
    "%GLSLANG%" -V --target-env %TARGET% "%%f" -o "%%f.spv"
    if errorlevel 1 (
        echo ERROR: Failed to compile %%f
        pause
        exit /b 1
    )
)

echo.
echo ===============================================
echo    Shader compilation complete!
echo ===============================================
echo.

REM Список скомпилированных файлов
echo Generated SPIR-V files:
dir /b *.spv

echo.
pause
