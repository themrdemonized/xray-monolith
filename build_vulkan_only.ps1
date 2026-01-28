# Build only Vulkan renderer components
$env:VULKAN_SDK = "C:\VulkanSDK\1.4.335.0"
$MSBuild = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\amd64\MSBuild.exe"
$Solution = "src\engine-vs2022.sln"
$Config = "Release"  # Try Release instead of VerifiedDX11
$Platform = "x64"

Write-Host "=== Building Vulkan components ===" -ForegroundColor Green

# Build only necessary components in order
$projects = @("xrCore", "xrCDB", "xrSound", "xrRender_Vulkan")

foreach ($proj in $projects) {
    Write-Host "`nBuilding $proj..." -ForegroundColor Yellow
    & $MSBuild $Solution /t:$proj /p:Configuration=$Config /p:Platform=$Platform /v:minimal /nologo
    if ($LASTEXITCODE -ne 0) { 
        Write-Host "Failed to build $proj" -ForegroundColor Red
        exit $LASTEXITCODE 
    }
}

Write-Host "`n=== Vulkan components built! ===" -ForegroundColor Green
