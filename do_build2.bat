@echo off
set VULKAN_SDK=C:\VulkanSDK\1.4.335.0
set MSBUILD="C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\amd64\MSBuild.exe"

echo === Building xrRender_Vulkan ===
%MSBUILD% "C:\Users\egorb\OneDrive\Documentos\GitHub\xray-monolith\src\engine-vs2022.sln" /t:xrRender_Vulkan /p:Configuration=VerifiedDX11 /p:Platform=x64 /v:normal /nologo 2>&1
echo === Vulkan Build exit code: %ERRORLEVEL% ===

echo === Building xrEngine ===
%MSBUILD% "C:\Users\egorb\OneDrive\Documentos\GitHub\xray-monolith\src\engine-vs2022.sln" /t:xrEngine /p:Configuration=VerifiedDX11 /p:Platform=x64 /v:normal /nologo 2>&1
echo === Engine Build exit code: %ERRORLEVEL% ===
