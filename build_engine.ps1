$ErrorActionPreference = "Stop"

Write-Host "Building engine with Vulkan configuration..." -ForegroundColor Cyan

$msbuild = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe"
$solution = "C:\Users\egorb\OneDrive\Documentos\GitHub\xray-monolith\src\engine-vs2022.sln"

& $msbuild $solution /p:Configuration=Vulkan /p:Platform=x64 /t:xrEngine /v:minimal /m

if ($LASTEXITCODE -ne 0) {
    Write-Host "BUILD FAILED" -ForegroundColor Red
    exit 1
}

Write-Host "BUILD SUCCESS!" -ForegroundColor Green
Write-Host "Copying to game folder..." -ForegroundColor Cyan

$source = "C:\Users\egorb\OneDrive\Documentos\GitHub\xray-monolith\_build\_game\bin_dbg\AnomalyVulkan.exe"
$pdb = "C:\Users\egorb\OneDrive\Documentos\GitHub\xray-monolith\_build\_game\bin_dbg\AnomalyVulkan.pdb"
$dest = "D:\anomaly\bin\"

if (Test-Path $source) {
    Copy-Item $source $dest -Force
    Copy-Item $pdb $dest -Force
    Write-Host "Done!" -ForegroundColor Green
} else {
    Write-Host "ERROR: Built EXE not found at $source" -ForegroundColor Red
    exit 1
}
