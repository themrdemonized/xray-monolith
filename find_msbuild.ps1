$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (Test-Path $vswhere) {
    $installPath = & $vswhere -latest -products * -property installationPath
    Write-Output "VS_INSTALL: $installPath"
    $msbuild = Join-Path $installPath "MSBuild\Current\Bin\MSBuild.exe"
    if (Test-Path $msbuild) {
        Write-Output "MSBUILD: $msbuild"
    }
    $msbuild64 = Join-Path $installPath "MSBuild\Current\Bin\amd64\MSBuild.exe"
    if (Test-Path $msbuild64) {
        Write-Output "MSBUILD64: $msbuild64"
    }
} else {
    Write-Output "vswhere not found"
    # Try common paths
    $paths = @(
        "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe",
        "C:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe",
        "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\MSBuild.exe",
        "C:\Program Files\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe"
    )
    foreach ($p in $paths) {
        if (Test-Path $p) {
            Write-Output "FOUND: $p"
        }
    }
}
