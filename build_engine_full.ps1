# Full engine build with Vulkan renderer
$env:VULKAN_SDK = "C:\VulkanSDK\1.4.335.0"

$MSBuild = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\amd64\MSBuild.exe"
$Solution = "src\engine-vs2022.sln"
$Config = "VerifiedDX11"
$Platform = "x64"

Write-Host "Building full X-Ray engine with Vulkan..." -ForegroundColor Green
Write-Host "VULKAN_SDK = $env:VULKAN_SDK" -ForegroundColor Cyan
Write-Host ""

# Build xrCore first (required by all)
Write-Host "=== Building xrCore ===" -ForegroundColor Yellow
& $MSBuild $Solution /t:xrCore /p:Configuration=$Config /p:Platform=$Platform /v:minimal /nologo
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

# Build xrEngine
Write-Host "`n=== Building xrEngine ===" -ForegroundColor Yellow
& $MSBuild $Solution /t:xrEngine /p:Configuration=$Config /p:Platform=$Platform /v:minimal /nologo
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

# Build xrRender_Vulkan (already done, but rebuild for safety)
Write-Host "`n=== Building xrRender_Vulkan ===" -ForegroundColor Yellow
& $MSBuild $Solution /t:xrRender_Vulkan /p:Configuration=$Config /p:Platform=$Platform /v:minimal /nologo
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "`n=== Build Complete! ===" -ForegroundColor Green

# List built files
$binPath = "_build\_game\bin_dbg\x64\$Config"
if (Test-Path $binPath) {
    Write-Host "`nBuilt files in $binPath`:" -ForegroundColor Cyan
    Get-ChildItem $binPath -Filter "*.exe" | ForEach-Object { Write-Host "  - $($_.Name)" }
    Get-ChildItem $binPath -Filter "*.dll" | ForEach-Object { Write-Host "  - $($_.Name)" }
}
