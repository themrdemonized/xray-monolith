# Build xrEngine bypassing R4 project dependencies
$env:VULKAN_SDK = "C:\VulkanSDK\1.4.335.0"
$MSBuild = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\amd64\MSBuild.exe"
$Solution = "src\engine-vs2022.sln"
$Config = "Release"
$Platform = "x64"

Write-Host "=== Building xrEngine (bypassing R4 dependencies) ===" -ForegroundColor Green

# Build core dependencies first
$deps = @("xrCore", "xrCDB", "xrSound", "xrRender_Vulkan")
foreach ($dep in $deps) {
    Write-Host "Building $dep..." -ForegroundColor Yellow
    & $MSBuild $Solution /t:$dep /p:Configuration=$Config /p:Platform=$Platform /v:minimal /nologo /m:4
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

# Build xrEngine WITHOUT building project references (skip R4)
Write-Host ""
Write-Host "Building xrEngine (skipping R4 references)..." -ForegroundColor Yellow
& $MSBuild $Solution /t:xrEngine /p:Configuration=$Config /p:Platform=$Platform /p:BuildProjectReferences=false /v:minimal /nologo /m:4

if ($LASTEXITCODE -eq 0) {
    Write-Host ""
    Write-Host "SUCCESS!" -ForegroundColor Green
    
    $binPath = "_build\_game\bin_dbg\x64\$Config"
    if (Test-Path "$binPath\xrEngine.exe") {
        Get-Item "$binPath\xrEngine.exe" | Select-Object Name, Length, LastWriteTime
    }
} else {
    Write-Host ""
    Write-Host "FAILED!" -ForegroundColor Red
}
