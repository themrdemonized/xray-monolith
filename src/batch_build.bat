@echo off
setlocal EnableExtensions

set "VSWHERE=%Program Files(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" set "VSWHERE=vswhere.exe"
set "MSBUILD_EXE="
for /f "usebackq tokens=*" %%i in (`%VSWHERE% -latest -products * -version "[18.0,19.0)" -requires Microsoft.Component.MSBuild Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -find MSBuild\Current\Bin\amd64\MSBuild.exe`) do set "MSBUILD_EXE=%%i"

if not defined MSBUILD_EXE (
  echo Visual Studio 2026 or Build Tools 2026 with MSVC v145 is required.
  exit /b 1
)

for %%c in (DX11 DX10 DX9 DX8 DX11-AVX DX10-AVX DX9-AVX DX8-AVX) do (
  "%MSBUILD_EXE%" "%~dp0engine-vs2026.sln" /p:Configuration=%%c /p:Platform=x64
  if errorlevel 1 exit /b 1
)
exit /b 0
