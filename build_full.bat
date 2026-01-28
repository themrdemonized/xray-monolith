@echo off
echo Building full game with Vulkan support...
echo Configuration: VerifiedDX11^|x64
echo.
"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe" src\engine-vs2022.sln /p:Configuration=VerifiedDX11 /p:Platform=x64 /m /nologo /v:minimal
if %errorlevel% neq 0 (
    echo.
    echo Build FAILED!
    pause
    exit /b %errorlevel%
)
echo.
echo Build SUCCESSFUL!
echo Output: _build\_game\bin_dbg\VerifiedDX11.exe
pause
