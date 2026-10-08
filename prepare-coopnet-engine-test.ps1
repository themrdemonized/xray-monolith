param([switch]$Launch, [switch]$LoadFixture, [switch]$ReplicaProbe, [switch]$MovementProbe, [switch]$ManualControls)
$ErrorActionPreference = 'Stop'
if ($ReplicaProbe -and !$LoadFixture) { throw 'ReplicaProbe requires LoadFixture and isolated copied worlds.' }
if ($MovementProbe) { $ReplicaProbe = $true; if (!$LoadFixture) { throw 'MovementProbe requires LoadFixture.' } }
if ($ManualControls -and !$MovementProbe) { throw 'ManualControls requires MovementProbe.' }
$client = Join-Path (Split-Path $PSScriptRoot) 'Anomaly-1.5.3'
$testRoot = Join-Path $PSScriptRoot '_build\coopnet-engine-test'
$output = Join-Path $PSScriptRoot '_build\_game\bin_dbg'
$dependencyBin = Join-Path $PSScriptRoot '_build\coopnet-deps\installed\x64-windows\bin'
if (!(Test-Path "$output\GameNetworkingSockets.dll")) {
    throw 'Finish build.ps1 -CoopNet successfully before preparing the engine test.'
}
$fsTemplate = Get-Content "$client\fsgame.ltx"
$userTemplate = Get-Content "$client\appdata\user.ltx" |
    Where-Object { $_ -notmatch '^(coop_|rs_screenmode |vid_mode |snd_volume_eff |snd_volume_music |r__framelimit )' }
foreach ($role in @('host', 'guest')) {
    $root = Join-Path $testRoot $role
    $data = Join-Path $root 'appdata'
    $bin = Join-Path $root 'bin'
    New-Item $data,$bin -ItemType Directory -Force | Out-Null
    # Separate executable directories satisfy the release build's per-directory mutex.
    Copy-Item "$client\bin\*.dll" $bin -Force
    Copy-Item "$client\bin\alsoft.ini" $bin -Force
    Copy-Item "$dependencyBin\*.dll" $bin -Force
    Copy-Item "$output\AnomalyDX11.exe" $bin -Force
    $fs = @('$fs_root$ = false | false | ' + $client + '\')
    foreach ($line in $fsTemplate) {
        if ($line -match '^\$app_data_root\$') {
            $fs += '$app_data_root$ = true | false | ' + $data + '\'
        } else { $fs += $line }
    }
    $fs | Set-Content "$root\fsgame.ltx" -Encoding ascii
    $startup = if ($role -eq 'host') { 'coop_host 27889 1 1 1' } else { 'coop_join 127.0.0.1:27889 2 1 1' }
    @($userTemplate) + @('rs_screenmode windowed','vid_mode 1280x720',
        'snd_volume_eff 0','snd_volume_music 0','r__framelimit 30',$startup,'coop_status') |
        Set-Content "$data\user.ltx" -Encoding ascii
    if ($ReplicaProbe) { Add-Content "$data\user.ltx" 'coop_replica_probe' -Encoding ascii }
    if ($MovementProbe) {
        $controls = if ($ManualControls) { 'coop_movement_probe' } else { 'coop_movement_probe auto' }
        Add-Content "$data\user.ltx" $controls -Encoding ascii
    }
    if ($LoadFixture -and ($role -eq 'host' -or $ReplicaProbe)) {
        $fixture = Join-Path $client 'appdata\savedgames\player - autosave.scop'
        if (!(Test-Path $fixture)) { throw 'The disposable gameplay test fixture source save is missing.' }
        $saves = Join-Path $data 'savedgames'
        New-Item $saves -ItemType Directory -Force | Out-Null
        foreach ($extension in @('scop','scoc','dds')) {
            $source = Join-Path $client "appdata\savedgames\player - autosave.$extension"
            if (Test-Path $source) { Copy-Item -LiteralPath $source -Destination "$saves\coopnet-fixture.$extension" -Force }
        }
        Add-Content "$data\user.ltx" 'start server(coopnet-fixture/single/alife/load) client(localhost)' -Encoding ascii
    }
    Write-Host "Prepared $role with separate appdata: $data"
}
if ($Launch) {
    foreach ($role in @('host', 'guest')) {
        $root = Join-Path $testRoot $role
        Start-Process -FilePath "$root\bin\AnomalyDX11.exe" -WorkingDirectory $root `
            -ArgumentList '-silent_error_mode','-noprefetch' -WindowStyle Hidden -PassThru
    }
    Write-Host 'Transport test only. Use coop_status in each console; close both clients after testing.'
}
