param([string]$Output)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if (-not $Output) { $Output = Join-Path $root '_build\R1_PdaMapMath' }
$Output = [IO.Path]::GetFullPath($Output)
[IO.Directory]::CreateDirectory($Output) | Out-Null
$install = & 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -latest -products '*' -property installationPath
$vc = Get-ChildItem -LiteralPath (Join-Path $install 'VC\Tools\MSVC') -Directory | Sort-Object Name -Descending | Select-Object -First 1
$sdk = Get-ChildItem -LiteralPath 'C:\Program Files (x86)\Windows Kits\10\Include' -Directory | Sort-Object Name -Descending | Select-Object -First 1
$env:INCLUDE = @((Join-Path $vc.FullName 'include'),(Join-Path $sdk.FullName 'ucrt'),(Join-Path $sdk.FullName 'shared'),(Join-Path $sdk.FullName 'um')) -join ';'
$sdkLib = Join-Path 'C:\Program Files (x86)\Windows Kits\10\Lib' $sdk.Name
$env:LIB = @((Join-Path $vc.FullName 'lib\x64'),(Join-Path $sdkLib 'ucrt\x64'),(Join-Path $sdkLib 'um\x64')) -join ';'
$exe = Join-Path $Output 'R1_PdaMapMath_Test.exe'
$obj = Join-Path $Output 'R1_PdaMapMath_Test.obj'
& (Join-Path $vc.FullName 'bin\Hostx64\x64\cl.exe') /nologo /std:c++17 /EHsc /O2 /MT ('/Fo' + $obj) ('/Fe' + $exe) (Join-Path $PSScriptRoot 'R1_PdaMapMath_Test.cpp') *> (Join-Path $Output 'R1_Math_Compile.log')
if ($LASTEXITCODE -ne 0) { Get-Content -LiteralPath (Join-Path $Output 'R1_Math_Compile.log'); exit 1 }
& $exe *> (Join-Path $Output 'R1_Math_Result.log')
$code = $LASTEXITCODE
Get-Content -LiteralPath (Join-Path $Output 'R1_Math_Result.log')
exit $code
