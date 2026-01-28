@echo off
set VULKAN_SDK=C:\VulkanSDK\1.4.335.0
set MSBUILD="C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\amd64\MSBuild.exe"

echo === Building xray-monolith ===
%MSBUILD% "C:\Users\egorb\OneDrive\Documentos\GitHub\xray-monolith\src\engine-vs2022.sln" /p:Configuration=VerifiedDX11 /p:Platform=x64 /m /v:minimal /nologo
echo === Build exit code: %ERRORLEVEL% ===
