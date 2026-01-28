$env:VULKAN_SDK = "C:\VulkanSDK\1.4.335.0"
$MSBuild = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\amd64\MSBuild.exe"
$Solution = "src\engine-vs2022.sln"
$Project = "xrRender_R4"
$Config = "VerifiedDX11"
$Platform = "x64"

Write-Host "Building $Project (for comparison)..." -ForegroundColor Green

& $MSBuild $Solution /t:$Project /p:Configuration=$Config /p:Platform=$Platform /v:minimal /nologo

if ($LASTEXITCODE -eq 0) {
    Write-Host "`nR4 Build SUCCESS!" -ForegroundColor Green
} else {
    Write-Host "`nR4 Build FAILED with exit code $LASTEXITCODE" -ForegroundColor Red
}
