# Сборка xrRender_Vulkan
$env:VULKAN_SDK = "C:\VulkanSDK\1.4.335.0"

$MSBuild = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\amd64\MSBuild.exe"
$Solution = "src\engine-vs2022.sln"
$Project = "xrRender_Vulkan"
$Config = "VerifiedDX11"
$Platform = "x64"

Write-Host "Building $Project..." -ForegroundColor Green
Write-Host "VULKAN_SDK = $env:VULKAN_SDK" -ForegroundColor Cyan
Write-Host ""

& $MSBuild $Solution /t:$Project /p:Configuration=$Config /p:Platform=$Platform /v:minimal /nologo

if ($LASTEXITCODE -eq 0) {
    Write-Host "`nBuild SUCCESS!" -ForegroundColor Green

    $dllPath = "_build\bin_dbg\VerifiedDX11\xrRender_Vulkan.dll"
    if (Test-Path $dllPath) {
        $dll = Get-Item $dllPath
        Write-Host "DLL created: $dllPath" -ForegroundColor Cyan
        Write-Host "Size: $([math]::Round($dll.Length / 1KB, 2)) KB" -ForegroundColor Cyan
        Write-Host "Modified: $($dll.LastWriteTime)" -ForegroundColor Cyan
    }
} else {
    Write-Host "`nBuild FAILED with exit code $LASTEXITCODE" -ForegroundColor Red
}
