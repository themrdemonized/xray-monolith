@echo off
setlocal

echo ============================================================
echo Building Full X-Ray Engine with Vulkan Renderer
echo ============================================================
echo.

echo Setting up build environment...
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64

echo.
echo Building engine with Vulkan configuration...
cd /d "C:\Users\egorb\OneDrive\Documentos\GitHub\xray-monolith\src"

echo.
echo [1/3] Building xrEngine...
MSBuild.exe "engine-vs2022.sln" /t:xrEngine /p:Configuration=Vulkan /p:Platform=x64 /v:minimal /nologo /m
if %ERRORLEVEL% NEQ 0 (
    echo.
    echo [ERROR] xrEngine build FAILED
    exit /b 1
)

echo.
echo [2/3] Building xrGame...
MSBuild.exe "engine-vs2022.sln" /t:xrGame /p:Configuration=Vulkan /p:Platform=x64 /v:minimal /nologo /m
if %ERRORLEVEL% NEQ 0 (
    echo.
    echo [ERROR] xrGame build FAILED
    exit /b 1
)

echo.
echo [3/3] Building xrRender_Vulkan...
MSBuild.exe "engine-vs2022.sln" /t:xrRender_Vulkan /p:Configuration=Vulkan /p:Platform=x64 /v:minimal /nologo /m
if %ERRORLEVEL% NEQ 0 (
    echo.
    echo [ERROR] xrRender_Vulkan build FAILED
    exit /b 1
)

echo.
echo ============================================================
echo BUILD SUCCESS! All components compiled.
echo ============================================================
echo.
echo Copying files to D:\anomaly\bin...

set "SOURCE_DIR=C:\Users\egorb\OneDrive\Documentos\GitHub\xray-monolith\_build\_game\bin_dbg"
set "DEST_DIR=D:\anomaly\bin"

if not exist "%DEST_DIR%" (
    echo ERROR: D:\anomaly\bin does not exist!
    exit /b 1
)

echo Copying xrEngine.dll...
copy /Y "%SOURCE_DIR%\xrEngine.dll" "%DEST_DIR%\" >nul 2>&1

echo Copying xrGame.dll...
copy /Y "%SOURCE_DIR%\xrGame.dll" "%DEST_DIR%\" >nul 2>&1

echo Copying xrRender_Vulkan.dll...
copy /Y "%SOURCE_DIR%\xrRender_Vulkan.dll" "%DEST_DIR%\" >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo Warning: xrRender_Vulkan.dll not found, trying .lib...
    echo Note: Vulkan renderer might be a static library
)

echo.
echo ============================================================
echo DONE! Game is ready to launch from D:\anomaly
echo ============================================================
echo.
echo To use Vulkan renderer, launch with:
echo   AnomalyLauncher.exe -renderer renderer_vk
echo or edit user.ltx:
echo   [system]
echo   renderer renderer_vk
echo.
pause
