@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" >nul 2>&1
cd /d "C:\Users\egorb\OneDrive\Documentos\GitHub\xray-monolith\src"
MSBuild.exe Layers\xrRenderVulkan\xrRender_Vulkan.vcxproj /p:Configuration=Debug /p:Platform=x64 /t:Build /v:minimal
if %ERRORLEVEL%==0 (
    echo BUILD SUCCESS
    copy /Y "..\bin\Debug\xrRender_Vulkan.dll" "D:\anomaly\bin\" >nul 2>&1
    copy /Y "..\bin\Debug\xrRender_Vulkan.pdb" "D:\anomaly\bin\" >nul 2>&1
) else (
    echo BUILD FAILED
)
