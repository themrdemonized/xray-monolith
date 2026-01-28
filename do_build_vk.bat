@echo off
"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\amd64\MSBuild.exe" "C:\Users\egorb\OneDrive\Documentos\GitHub\xray-monolith\src\Layers\xrRenderVulkan\xrRender_Vulkan.vcxproj" -p:Configuration=Release -p:Platform=x64 -p:SolutionDir="C:\Users\egorb\OneDrive\Documentos\GitHub\xray-monolith\src\\" -m -nologo -v:minimal
echo VK_EXIT=%ERRORLEVEL%
