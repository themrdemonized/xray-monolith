@echo off
chcp 65001 >nul

set MSBUILD="C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe"
set SOLUTION=src\engine-vs2022.sln

echo [1/3] Checking MSBuild...
if not exist %MSBUILD% (
    echo ERROR: MSBuild not found!
    exit /b 1
)

echo [2/3] Building Vulkan configuration...
%MSBUILD% %SOLUTION% /p:Configuration=Vulkan /p:Platform=x64 /m /v:minimal
if %ERRORLEVEL% NEQ 0 (
    echo BUILD FAILED!
    exit /b 1
)

echo.
echo [3/3] Copying to D:\anomaly\bin...
if not exist "_build\_game\bin_dbg\AnomalyVulkan.exe" (
    echo ERROR: Build output not found!
    exit /b 1
)

if exist "D:\anomaly\bin\AnomalyVulkan.exe" (
    echo Creating backup...
    copy /Y "D:\anomaly\bin\AnomalyVulkan.exe" "D:\anomaly\bin\AnomalyVulkan.exe.backup" >nul
)
if exist "D:\anomaly\bin\AnomalyVulkan.pdb" (
    copy /Y "D:\anomaly\bin\AnomalyVulkan.pdb" "D:\anomaly\bin\AnomalyVulkan.pdb.backup" >nul
)

echo Copying AnomalyVulkan.exe...
copy /Y "_build\_game\bin_dbg\AnomalyVulkan.exe" "D:\anomaly\bin\"
echo Copying AnomalyVulkan.pdb...
copy /Y "_build\_game\bin_dbg\AnomalyVulkan.pdb" "D:\anomaly\bin\"

echo.
echo ========================================
echo SUCCESS! Files deployed
echo ========================================
dir "D:\anomaly\bin\AnomalyVulkan.*" | find "AnomalyVulkan"
