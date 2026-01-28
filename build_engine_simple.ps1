# Build xrEngine WITHOUT old renderers (R1/R2/R3/R4)
# Simplified version - only essential projects
$env:VULKAN_SDK = "C:\VulkanSDK\1.4.335.0"
$MSBuild = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\amd64\MSBuild.exe"
$Solution = "src\engine-vs2022.sln"
$Config = "Release"
$Platform = "x64"

Write-Host "============================================" -ForegroundColor Green
Write-Host "  Building X-Ray Engine - Vulkan Only" -ForegroundColor Green
Write-Host "  (Simple build - essential projects)" -ForegroundColor Green
Write-Host "============================================" -ForegroundColor Green
Write-Host ""

# Build essential projects
$projects = @(
    "xrCore",
    "xrCDB",
    "xrSound",
    "xrRender_Vulkan",
    "xrEngine"
)

foreach ($proj in $projects) {
    Write-Host ">>> Building $proj..." -ForegroundColor Yellow
    & $MSBuild $Solution /t:$proj /p:Configuration=$Config /p:Platform=$Platform /p:BuildProjectReferences=false /v:minimal /nologo /m:4

    if ($LASTEXITCODE -ne 0) {
        Write-Host ""
        Write-Host "ERROR: Failed to build $proj" -ForegroundColor Red
        Write-Host ""
        exit $LASTEXITCODE
    }
    Write-Host "    OK" -ForegroundColor Green
}

Write-Host ""
Write-Host "============================================" -ForegroundColor Green
Write-Host "  Build SUCCESS - Vulkan Only!" -ForegroundColor Green
Write-Host "============================================" -ForegroundColor Green
Write-Host ""

# Check output
$binPath = "_build\_game\bin_dbg\x64\$Config"
if (Test-Path "$binPath\xrEngine.exe") {
    Write-Host "xrEngine.exe created:" -ForegroundColor Cyan
    Get-Item "$binPath\xrEngine.exe" | Select-Object Name, @{N='Size (MB)';E={[math]::Round($_.Length/1MB, 2)}}, LastWriteTime
}
if (Test-Path "$binPath\xrRender_Vulkan.dll") {
    Write-Host "xrRender_Vulkan.dll created:" -ForegroundColor Cyan
    Get-Item "$binPath\xrRender_Vulkan.dll" | Select-Object Name, @{N='Size (KB)';E={[math]::Round($_.Length/1KB, 2)}}, LastWriteTime
}

Write-Host ""
Write-Host "Ready to test Vulkan renderer!" -ForegroundColor Green
