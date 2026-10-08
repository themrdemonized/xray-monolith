$ErrorActionPreference = 'Stop'
$dependencyRoot = Join-Path $PSScriptRoot '_build\coopnet-deps'
$installed = Join-Path $dependencyRoot 'installed\x64-windows'
if (!(Test-Path "$installed\include\GameNetworkingSockets\steam\steamnetworkingsockets.h")) {
    throw 'Run setup-coopnet-deps.ps1 and wait for it to finish first.'
}
$cmake = Get-ChildItem "$dependencyRoot\vcpkg\downloads\tools" -Recurse -Filter cmake.exe |
    Select-Object -First 1 -ExpandProperty FullName
if (!$cmake) { throw 'Dependency CMake tool missing' }
$build = Join-Path $PSScriptRoot '_build\coopnet-network-tests'
& $cmake -S "$PSScriptRoot\tests\coopnet" -B $build -G 'Visual Studio 18 2026' -A x64 "-DCMAKE_PREFIX_PATH=$installed"
if ($LASTEXITCODE) { throw 'Network test configure failed' }
& $cmake --build $build --config Release --parallel 2
if ($LASTEXITCODE) { throw 'Network test build failed' }
$exe = Join-Path $build 'Release\GnsSmoke.exe'
$savedPath = $env:PATH
$hostProcess = $null
$clientProcess = $null
try {
    $env:PATH = "$installed\bin;$savedPath"
    $hostProcess = Start-Process -FilePath $exe -ArgumentList 'host','27888' -WindowStyle Hidden -PassThru -RedirectStandardOutput "$build\host.log" -RedirectStandardError "$build\host-error.log"
    # The client tolerates asynchronous connection establishment; no readiness sleep.
    $clientProcess = Start-Process -FilePath $exe -ArgumentList 'client','127.0.0.1:27888' -WindowStyle Hidden -PassThru -RedirectStandardOutput "$build\client.log" -RedirectStandardError "$build\client-error.log"
    if (!$clientProcess.WaitForExit(25000)) { throw 'Client test timed out' }
    if (!$hostProcess.WaitForExit(25000)) { throw 'Host test timed out' }
    if ($clientProcess.ExitCode -ne 0 -or $hostProcess.ExitCode -ne 0) {
        Get-Content "$build\host-error.log", "$build\client-error.log"
        throw 'Real-process transport test failed'
    }
    if (!(Select-String -Path "$build\host.log" -Pattern '^HOST_PASS ') -or
        !(Select-String -Path "$build\client.log" -Pattern '^CLIENT_PASS ')) {
        throw 'Transport success evidence missing'
    }
    Get-Content "$build\host.log", "$build\client.log"
} finally {
    foreach ($ownedProcess in @($clientProcess, $hostProcess)) {
        if ($ownedProcess -and !$ownedProcess.HasExited) { Stop-Process -Id $ownedProcess.Id }
    }
    $env:PATH = $savedPath
}
