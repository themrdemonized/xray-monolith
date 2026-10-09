@echo off
setlocal
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "COOP_VS=%%i"
if not defined COOP_VS exit /b 1
call "%COOP_VS%\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44
if errorlevel 1 exit /b 1
cd /d "%~dp0"
if not exist "_build\coopnet-tests" mkdir "_build\coopnet-tests"
cl /nologo /std:c++17 /EHsc /W4 /WX "tests\coopnet\ProtocolTests.cpp" /Fo"_build\coopnet-tests\ProtocolTests.obj" /Fe"_build\coopnet-tests\ProtocolTests.exe"
if errorlevel 1 exit /b 1
"_build\coopnet-tests\ProtocolTests.exe"
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /EHsc /W4 /WX "tests\coopnet\SessionTests.cpp" /Fo"_build\coopnet-tests\SessionTests.obj" /Fe"_build\coopnet-tests\SessionTests.exe"
if errorlevel 1 exit /b 1
"_build\coopnet-tests\SessionTests.exe"
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /EHsc /W4 /WX "tests\coopnet\SnapshotTests.cpp" /Fo"_build\coopnet-tests\SnapshotTests.obj" /Fe"_build\coopnet-tests\SnapshotTests.exe"
if errorlevel 1 exit /b 1
"_build\coopnet-tests\SnapshotTests.exe"
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /EHsc /W4 /WX "tests\coopnet\EntityTests.cpp" /Fo"_build\coopnet-tests\EntityTests.obj" /Fe"_build\coopnet-tests\EntityTests.exe"
if errorlevel 1 exit /b 1
"_build\coopnet-tests\EntityTests.exe"
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /EHsc /W4 /WX "tests\coopnet\InputTests.cpp" /Fo"_build\coopnet-tests\InputTests.obj" /Fe"_build\coopnet-tests\InputTests.exe"
if errorlevel 1 exit /b 1
"_build\coopnet-tests\InputTests.exe"
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /EHsc /W4 /WX "tests\coopnet\GameplayTests.cpp" /Fo"_build\coopnet-tests\GameplayTests.obj" /Fe"_build\coopnet-tests\GameplayTests.exe"
if errorlevel 1 exit /b 1
"_build\coopnet-tests\GameplayTests.exe"
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /EHsc /W4 /WX "tests\coopnet\WorldBaselineTests.cpp" /Fo"_build\coopnet-tests\WorldBaselineTests.obj" /Fe"_build\coopnet-tests\WorldBaselineTests.exe"
if errorlevel 1 exit /b 1
"_build\coopnet-tests\WorldBaselineTests.exe"
if errorlevel 1 exit /b 1
for %%T in (WorldState PartyTransition GuestSave InventoryView JoinProfile WorldSettings) do (
cl /nologo /std:c++17 /EHsc /W4 /WX "tests\coopnet\%%TTests.cpp" /Fo"_build\coopnet-tests\%%TTests.obj" /Fe"_build\coopnet-tests\%%TTests.exe"
if errorlevel 1 exit /b 1
"_build\coopnet-tests\%%TTests.exe"
if errorlevel 1 exit /b 1
)
exit /b 0
