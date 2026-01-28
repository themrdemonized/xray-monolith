@echo off
setlocal

echo ===============================================
echo    Building X-Ray Anomaly - Vulkan Renderer
echo ===============================================
echo.

set MSBUILD="C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe"
set SOLUTION=src\engine-vs2022.sln

if not exist %MSBUILD% (
    echo ERROR: MSBuild not found at %MSBUILD%
    echo Please install Visual Studio 2022 Build Tools
    pause
    exit /b 1
)

echo Building Vulkan configuration...
echo.

%MSBUILD% %SOLUTION% /p:Configuration=Vulkan /p:Platform=x64 /m /v:minimal

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo ===============================================
    echo    BUILD FAILED!
    echo ===============================================
    pause
    exit /b 1
)

echo.
echo ===============================================
echo    BUILD SUCCESSFUL!
echo    Output: _build\_game\bin_dbg\AnomalyVulkan.exe
echo ===============================================
echo.

pause
