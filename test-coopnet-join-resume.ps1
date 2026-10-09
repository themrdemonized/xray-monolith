param([ValidateRange(60,180)][int]$Seconds=90)
$ErrorActionPreference='Stop'
$probeRoot=Join-Path $PSScriptRoot ('_build\coopnet-join-'+[Guid]::NewGuid().ToString('N'))
foreach ($role in @('host','guest')) {
    $cache=Join-Path $PSScriptRoot "_build\coopnet-engine-test\$role\appdata\shaders_cache"
    $target=Join-Path $probeRoot "$role\appdata"
    New-Item $target -ItemType Directory -Force | Out-Null
    if (Test-Path -LiteralPath $cache) { Copy-Item -LiteralPath $cache -Destination $target -Recurse }
}
$client=Join-Path (Split-Path $PSScriptRoot) 'Anomaly-1.5.3'
$hashes=@{}
Get-ChildItem "$client\appdata\savedgames\player - autosave.*" -File | ForEach-Object { $hashes[$_.FullName]=(Get-FileHash -LiteralPath $_.FullName).Hash }
& "$PSScriptRoot\prepare-coopnet-engine-test.ps1" -LoadFixture -WorldProbe -GameplayProbe -MovementProbe -SettingsProbe -TestDirectory $probeRoot
$guestConfig=Join-Path $probeRoot 'guest\appdata\user.ltx'
$guestSettings=Get-Content -LiteralPath $guestConfig
$owned=@()
function Start-OwnedProbe([string]$Role) {
    $root=Join-Path $probeRoot $Role
    Start-Process -FilePath (Join-Path $root 'bin\AnomalyDX11.exe') -WorkingDirectory $root -ArgumentList '-silent_error_mode','-noprefetch' -WindowStyle Hidden -PassThru
}
function Close-OwnedProbe($Process) {
    if (!$Process.HasExited) {
        $Process.CloseMainWindow() | Out-Null
        if (!$Process.WaitForExit(10000)) { Stop-Process -Id $Process.Id; throw 'Owned probe did not shut down normally.' }
    }
}
function Wait-OwnedPhase($Processes) {
    $watch=[Diagnostics.Stopwatch]::StartNew()
    while ($watch.Elapsed.TotalSeconds -lt $Seconds) {
        foreach ($process in $Processes) { if ($process.HasExited) { throw "Owned probe exited early: $($process.Id)" } }
        Start-Sleep -Milliseconds 500
    }
}
try {
    $hostProcess=Start-OwnedProbe 'host'; $owned+=$hostProcess
    $guestProcess=Start-OwnedProbe 'guest'; $owned+=$guestProcess
    Wait-OwnedPhase @($hostProcess,$guestProcess)
    Close-OwnedProbe $guestProcess
    $credentials=Join-Path $probeRoot 'guest\appdata\coopnet-connections.dat'
    if (!(Test-Path -LiteralPath $credentials) -or (Get-Item -LiteralPath $credentials).Length -eq 0) { throw 'Saved encrypted credentials missing.' }
    $firstLog=Get-Content (Join-Path $probeRoot 'guest\appdata\logs\xray_deadparrot.log') -Raw
    if ($firstLog -notmatch 'CoopNet connection credentials saved: generation 1' -or $firstLog -match 'FATAL ERROR|CoopNet options .* failed') { throw 'First admission or settings setup failed.' }
    Write-Output 'NATIVE_JOIN_CREDENTIALS_PASS: successful join wrote a Windows-encrypted connection profile.'
    # The same backend used by the menu now resolves the saved address, character and token.
    @($guestSettings | ForEach-Object { if ($_ -match '^coop_join ') { 'coop_join_menu 127.0.0.1:27889' } else { $_ } }) |
        Set-Content -LiteralPath $guestConfig -Encoding ascii
    $guestReopened=Start-OwnedProbe 'guest'; $owned+=$guestReopened
    Wait-OwnedPhase @($hostProcess,$guestReopened)
    Close-OwnedProbe $guestReopened
    Close-OwnedProbe $hostProcess
    $guestLog=Get-Content (Join-Path $probeRoot 'guest\appdata\logs\xray_deadparrot.log') -Raw
    $hostLog=Get-Content (Join-Path $probeRoot 'host\appdata\logs\xray_deadparrot.log') -Raw
    if ($guestLog -notmatch 'CoopNet connection credentials saved: generation 2' -or
        $guestLog -notmatch 'CoopNet canonical baseline loaded and acknowledged:' -or
        $guestLog -notmatch 'CoopNet settings probe: guest world commands and scripted writes denied' -or
        ([regex]::Matches($hostLog,'CoopNet native guest bound: object \d+ generation \d+').Count -lt 2) -or
        ($guestLog+$hostLog) -match 'FATAL ERROR|CoopNet update failed:|CoopNet options .* failed') { throw 'New-process menu-backend resume or host settings evidence missing.' }
    Write-Output "NATIVE_MENU_BACKEND_RESUME_PASS: reopened guest used saved credentials, resumed generation 2 against the running host, loaded its world and retained the settings lock. Test files: $probeRoot"
} finally {
    foreach ($process in $owned) { if (!$process.HasExited) { $process.CloseMainWindow() | Out-Null; if (!$process.WaitForExit(10000)) { Stop-Process -Id $process.Id } } }
    foreach ($path in $hashes.Keys) { if ((Get-FileHash -LiteralPath $path).Hash -ne $hashes[$path]) { throw "Original fixture source changed: $path" } }
}
