@echo off
"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe" "src\engine-vs2022.sln" /p:Configuration=Release /p:Platform=x64 /t:xrRender_Vulkan /nologo /v:minimal
