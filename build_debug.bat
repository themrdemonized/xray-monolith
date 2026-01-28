@echo off
cd /d "C:\Users\egorb\OneDrive\Documentos\GitHub\xray-monolith"
"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe" "src\engine-vs2022.sln" /p:Configuration=VerifiedDX11 /p:Platform=x64 /t:xrEngine /m /v:minimal
echo BUILD_EXIT_CODE=%ERRORLEVEL%
