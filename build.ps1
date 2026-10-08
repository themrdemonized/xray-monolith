param(
    [ValidateSet('DX11','DX11-AVX','DX10','DX9','DX8','VerifiedDX11')]
    [string]$Configuration = 'DX11',
    [switch]$Deploy
)
$ErrorActionPreference = 'Stop'
$engine = $PSScriptRoot
$workspace = Split-Path $engine
$client = Join-Path $workspace 'Anomaly-1.5.3'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$msbuild = & $vswhere -latest -products '*' -requires Microsoft.Component.MSBuild -find 'MSBuild\Current\Bin\MSBuild.exe' | Select-Object -First 1
if (!$msbuild) { throw 'Install Visual Studio with Desktop development with C++ and v143 MFC/ATL components.' }
$toolsetArguments = @()
$vsroot = Split-Path (Split-Path (Split-Path (Split-Path $msbuild)))
if (Test-Path "$vsroot\MSBuild\Microsoft\VC\v180\Platforms\x64\PlatformToolsets\v145") {
    # VS2026 hosts side-by-side compilers through its v145 integration.
    $compiler = Get-ChildItem "$vsroot\VC\Tools\MSVC" -Directory |
        Where-Object { $_.Name -like '14.44.*' } | Sort-Object Name -Descending | Select-Object -First 1
    if (!$compiler -or !(Test-Path "$($compiler.FullName)\atlmfc\include\afx.h")) {
        throw 'Install C++ v14.44 (17.14) MFC and ATL components in Visual Studio Installer.'
    }
    $toolsetArguments = @('/p:PlatformToolset=v145', "/p:VCToolsVersion=$($compiler.Name)")
}
Push-Location $engine
try {
    & $msbuild "$engine\src\engine-vs2022.sln" /m:2 "/p:Configuration=$Configuration" /p:Platform=x64 @toolsetArguments /v:minimal /fl "/flp:logfile=build-$Configuration.log;verbosity=normal"
    if ($LASTEXITCODE -ne 0) { throw "Build failed. See build-$Configuration.log." }
    if ($Deploy) {
        $name = if ($Configuration -eq 'VerifiedDX11') { 'VerifiedDX11' } else { 'Anomaly' + $Configuration.Replace('-','') }
        $output = Join-Path $engine '_build\_game\bin_dbg'
        $exe = Join-Path $output "$name.exe"
        if (!(Test-Path $exe)) { throw "Expected build output missing: $exe" }
        $backup = Join-Path $workspace ('backups\' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
        foreach ($relative in @("bin\$name.exe", "bin\$name.pdb")) {
            $existing = Join-Path $client $relative
            if (Test-Path $existing) {
                $saved = Join-Path $backup $relative
                New-Item (Split-Path $saved) -ItemType Directory -Force | Out-Null
                Copy-Item -LiteralPath $existing -Destination $saved
            }
        }
        # Back up files replaced by the engine's matching scripts and shaders.
        Get-ChildItem "$engine\gamedata" -File -Recurse | ForEach-Object {
            $relative = $_.FullName.Substring((Join-Path $engine 'gamedata').Length + 1)
            $existing = Join-Path "$client\gamedata" $relative
            if (Test-Path $existing) {
                $saved = Join-Path "$backup\gamedata" $relative
                New-Item (Split-Path $saved) -ItemType Directory -Force | Out-Null
                Copy-Item -LiteralPath $existing -Destination $saved
            }
        }
        Copy-Item -LiteralPath $exe -Destination "$client\bin\$name.exe" -Force
        if (Test-Path "$output\$name.pdb") { Copy-Item "$output\$name.pdb" "$client\bin" -Force }
        Copy-Item "$engine\gamedata\*" "$client\gamedata" -Recurse -Force
        $cache = Join-Path $client 'appdata\shaders_cache'
        if (Test-Path $cache) {
            New-Item $backup -ItemType Directory -Force | Out-Null
            Move-Item -LiteralPath $cache -Destination "$backup\shaders_cache"
        }
        Write-Host "Deployed $name. Backups: $backup"
    }
} finally { Pop-Location }
