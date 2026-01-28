@echo off
"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe" "src\xrGame\xrGame.vcxproj" /p:Configuration=Release /p:Platform=x64 /m /v:minimal
