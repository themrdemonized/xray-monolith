# Minimal build for Vulkan testing
$env:VULKAN_SDK = "C:\VulkanSDK\1.4.335.0"
$MSBuild = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\amd64\MSBuild.exe"
$Solution = "src\engine-vs2022.sln"
$Config = "VerifiedDX11"
$Platform = "x64"

Write-Host "=== Minimal X-Ray build for Vulkan ===" -ForegroundColor Green

# Just build xrEngine (includes xrCore as dependency)
& $MSBuild $Solution /t:xrEngine /p:Configuration=$Config /p:Platform=$Platform /v:minimal /m:4 /nologo

if ($LASTEXITCODE -eq 0) {
    Write-Host "`nBuild SUCCESS!" -ForegroundColor Green
    
    $binPath = "_build\_game\bin_dbg\x64\$Config"
    if (Test-Path "$binPath\xrEngine.exe") {
        Write-Host "xrEngine.exe created!" -ForegroundColor Cyan
        Get-Item "$binPath\xrEngine.exe" | Select-Object Name, Length, LastWriteTime
    }
} else {
    Write-Host "`nBuild FAILED!" -ForegroundColor Red
}
