$ErrorActionPreference = 'Stop'
$revision = '2750401336fb7c95f6619657a46a7e798661341c'
$dependencyRoot = Join-Path $PSScriptRoot '_build\coopnet-deps'
$registry = Join-Path $dependencyRoot 'vcpkg'
if (!(Test-Path "$registry\.git")) {
    git clone https://github.com/microsoft/vcpkg.git $registry
    if ($LASTEXITCODE) { throw 'vcpkg clone failed' }
}
git -C $registry checkout --detach $revision
if ($LASTEXITCODE) { throw 'Pinned vcpkg revision unavailable' }
& "$registry\bootstrap-vcpkg.bat" -disableMetrics
if ($LASTEXITCODE) { throw 'vcpkg bootstrap failed' }
& "$registry\vcpkg.exe" install --triplet x64-windows "--x-manifest-root=$PSScriptRoot\coopnet-deps" "--x-install-root=$dependencyRoot\installed"
if ($LASTEXITCODE) { throw 'CoopNet dependency build failed' }
