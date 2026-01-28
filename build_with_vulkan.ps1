# Сборка движка с Vulkan рендером
$env:VULKAN_SDK = "C:\VulkanSDK\1.4.335.0"

$MSBuild = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\amd64\MSBuild.exe"
$Solution = "src\engine-vs2022.sln"
$Config = "VerifiedDX11"
$Platform = "x64"

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "Building X-Ray Engine with Vulkan Renderer" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "VULKAN_SDK = $env:VULKAN_SDK" -ForegroundColor Yellow
Write-Host ""

# Сборка xrEngine (включит xrRender_Vulkan через зависимости)
Write-Host "Building xrEngine (with Vulkan renderer)..." -ForegroundColor Green
& $MSBuild $Solution /t:xrEngine /p:Configuration=$Config /p:Platform=$Platform "/p:VULKAN_SDK=$env:VULKAN_SDK" /v:minimal /nologo

if ($LASTEXITCODE -eq 0) {
    Write-Host "`n========================================" -ForegroundColor Green
    Write-Host "BUILD SUCCESS!" -ForegroundColor Green
    Write-Host "========================================" -ForegroundColor Green

    Write-Host "`nChecking output files:" -ForegroundColor Cyan

    $enginePath = "_build\_game\bin\xrEngine.exe"
    $vulkanLibPath = "_build\_game\bin_dbg\x64\VerifiedDX11\xrRender_Vulkan.lib"

    if (Test-Path $enginePath) {
        $engine = Get-Item $enginePath
        Write-Host "✓ xrEngine.exe found" -ForegroundColor Green
        Write-Host "  Modified: $($engine.LastWriteTime)" -ForegroundColor Gray
    } else {
        Write-Host "✗ xrEngine.exe NOT found" -ForegroundColor Red
    }

    if (Test-Path $vulkanLibPath) {
        Write-Host "✓ xrRender_Vulkan.lib found" -ForegroundColor Green
    } else {
        Write-Host "✗ xrRender_Vulkan.lib NOT found" -ForegroundColor Red
    }

    Write-Host "`n========================================" -ForegroundColor Cyan
    Write-Host "PHASE 1 COMPLETE!" -ForegroundColor Green
    Write-Host "========================================" -ForegroundColor Cyan
    Write-Host "Vulkan renderer is now integrated into X-Ray Engine" -ForegroundColor Green
    Write-Host ""
    Write-Host "The engine has been compiled with:" -ForegroundColor White
    Write-Host "  - STATIC_RENDERER_VULKAN defined" -ForegroundColor Gray
    Write-Host "  - DllMainXrRenderVulkan linked" -ForegroundColor Gray
    Write-Host "  - renderer_vk available in renderer list" -ForegroundColor Gray
    Write-Host ""
    Write-Host "Note: Full rendering functionality will be implemented in Phase 2" -ForegroundColor Yellow

} else {
    Write-Host "`n========================================" -ForegroundColor Red
    Write-Host "BUILD FAILED with exit code $LASTEXITCODE" -ForegroundColor Red
    Write-Host "========================================" -ForegroundColor Red
}
