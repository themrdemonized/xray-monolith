# Полная сборка проекта с зависимостями
$env:VULKAN_SDK = "C:\VulkanSDK\1.4.335.0"

$MSBuild = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\amd64\MSBuild.exe"
$Solution = "src\engine-vs2022.sln"
$Config = "VerifiedDX11"
$Platform = "x64"

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "Building full solution with dependencies" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "VULKAN_SDK = $env:VULKAN_SDK" -ForegroundColor Yellow
Write-Host ""

# Сборка всего solution (включая зависимости)
Write-Host "Step 1: Building entire solution..." -ForegroundColor Green
& $MSBuild $Solution /p:Configuration=$Config /p:Platform=$Platform /v:minimal /nologo

if ($LASTEXITCODE -eq 0) {
    Write-Host "`n========================================" -ForegroundColor Green
    Write-Host "BUILD SUCCESS!" -ForegroundColor Green
    Write-Host "========================================" -ForegroundColor Green

    # Проверка созданных файлов
    Write-Host "`nChecking output files:" -ForegroundColor Cyan

    $dllPath = "_build\bin_dbg\VerifiedDX11\xrRender_Vulkan.dll"
    if (Test-Path $dllPath) {
        $dll = Get-Item $dllPath
        Write-Host "✓ xrRender_Vulkan.dll created" -ForegroundColor Green
        Write-Host "  Path: $dllPath" -ForegroundColor Gray
        Write-Host "  Size: $([math]::Round($dll.Length / 1KB, 2)) KB" -ForegroundColor Gray
        Write-Host "  Modified: $($dll.LastWriteTime)" -ForegroundColor Gray
    } else {
        Write-Host "✗ xrRender_Vulkan.dll NOT found" -ForegroundColor Red
    }

    $libPath = "_build\libraries\x64\VerifiedDX11\xrCore.lib"
    if (Test-Path $libPath) {
        Write-Host "✓ xrCore.lib found" -ForegroundColor Green
    } else {
        Write-Host "? xrCore.lib NOT found at expected location" -ForegroundColor Yellow
    }

    Write-Host "`nNext steps:" -ForegroundColor Cyan
    Write-Host "1. Test: cd _build\_game\bin" -ForegroundColor White
    Write-Host "2. Run: xrEngine.exe -renderer renderer_vk" -ForegroundColor White

} else {
    Write-Host "`n========================================" -ForegroundColor Red
    Write-Host "BUILD FAILED with exit code $LASTEXITCODE" -ForegroundColor Red
    Write-Host "========================================" -ForegroundColor Red
}
