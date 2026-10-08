param(
    [ValidateSet('DX11','DX11AVX','DX10','DX9','DX8','VerifiedDX11')]
    [string]$Renderer = 'DX11',
    [string[]]$GameArguments = @()
)
$ErrorActionPreference = 'Stop'
$client = Join-Path (Split-Path $PSScriptRoot) 'Anomaly-1.5.3'
$name = if ($Renderer -eq 'VerifiedDX11') { $Renderer } else { "Anomaly$Renderer" }
$exe = Join-Path $client "bin\$name.exe"
if (!(Test-Path $exe)) { throw "Executable missing: $exe" }
$options = @{ FilePath = $exe; WorkingDirectory = $client; PassThru = $true }
if ($GameArguments.Count) { $options.ArgumentList = $GameArguments }
Start-Process @options
