@echo off
REM Sky Rendering System - Deployment Script
REM Deploys compiled binaries and shaders to D:/anomaly

echo ========================================
echo Sky Rendering System Deployment
echo ========================================
echo.

REM Set paths
set BUILD_DIR=_build\_game\bin_dbg
set ANOMALY_BIN=D:\anomaly\bin
set ANOMALY_SHADERS=D:\anomaly\gamedata\shaders
set SOURCE_SHADERS=gamedata\shaders

echo [1/3] Checking build output...
if not exist "%BUILD_DIR%\AnomalyVulkan.exe" (
    echo ERROR: AnomalyVulkan.exe not found in %BUILD_DIR%
    echo Please build the project first.
    pause
    exit /b 1
)

echo [2/3] Deploying binaries...
echo   - Copying AnomalyVulkan.exe to %ANOMALY_BIN%
copy /Y "%BUILD_DIR%\AnomalyVulkan.exe" "%ANOMALY_BIN%\" >nul
if exist "%BUILD_DIR%\AnomalyVulkan.pdb" (
    echo   - Copying AnomalyVulkan.pdb to %ANOMALY_BIN%
    copy /Y "%BUILD_DIR%\AnomalyVulkan.pdb" "%ANOMALY_BIN%\" >nul
)

echo [3/3] Deploying shaders...
if not exist "%SOURCE_SHADERS%\sky.vert.spv" (
    echo WARNING: Sky shaders not compiled. Compiling now...
    glslc "%SOURCE_SHADERS%\sky.vert" -o "%SOURCE_SHADERS%\sky.vert.spv"
    glslc "%SOURCE_SHADERS%\sky.frag" -o "%SOURCE_SHADERS%\sky.frag.spv"
)

echo   - Copying sky.vert.spv to %ANOMALY_SHADERS%
copy /Y "%SOURCE_SHADERS%\sky.vert.spv" "%ANOMALY_SHADERS%\" >nul
echo   - Copying sky.frag.spv to %ANOMALY_SHADERS%
copy /Y "%SOURCE_SHADERS%\sky.frag.spv" "%ANOMALY_SHADERS%\" >nul

REM Also copy to vulkan subfolder for compatibility
if exist "%ANOMALY_SHADERS%\vulkan" (
    echo   - Copying to vulkan subfolder...
    copy /Y "%SOURCE_SHADERS%\sky.vert.spv" "%ANOMALY_SHADERS%\vulkan\" >nul
    copy /Y "%SOURCE_SHADERS%\sky.frag.spv" "%ANOMALY_SHADERS%\vulkan\" >nul
)

echo.
echo ========================================
echo Deployment Complete!
echo ========================================
echo.
echo Files deployed:
echo   - AnomalyVulkan.exe
echo   - AnomalyVulkan.pdb (if exists)
echo   - sky.vert.spv
echo   - sky.frag.spv
echo.
echo You can now launch the game to test sky rendering.
echo.
pause
