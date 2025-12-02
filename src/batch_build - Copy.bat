REM vswhere is required, install it via `choco install vswhere`
@echo off
setlocal enabledelayedexpansion

for /f "usebackq tokens=*" %%i in (`vswhere -latest -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe`) do (
  "%%i" engine-vs2022.sln /p:Configuration=DX10
    pause
  exit /b !errorlevel!
)
