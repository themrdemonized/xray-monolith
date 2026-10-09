param([switch]$Launch, [switch]$LoadFixture, [switch]$ReplicaProbe, [switch]$MovementProbe, [switch]$ManualControls, [switch]$GameplayProbe, [switch]$WorldProbe, [switch]$PartyProbe, [switch]$WeaponProbe, [switch]$InventoryProbe, [switch]$WorldLootProbe, [switch]$SettingsProbe, [switch]$RespawnProbe, [switch]$StarterProbe, [string]$TestDirectory)
$ErrorActionPreference = 'Stop'
if ($PartyProbe) { $WorldProbe=$true }
if ($SettingsProbe) { $WorldProbe=$true }
if ($RespawnProbe) { $WorldProbe=$true }
if ($WorldLootProbe) { $WorldProbe=$true }
if ($InventoryProbe) { $WeaponProbe=$true }
if ($StarterProbe) { $WorldProbe=$true }
if ($WeaponProbe) { $WorldProbe=$true }
if ($WorldProbe) { $GameplayProbe=$true }
if ($GameplayProbe) { $MovementProbe=$true; $LoadFixture=$true }
if ($ReplicaProbe -and !$LoadFixture) { throw 'ReplicaProbe requires LoadFixture and isolated copied worlds.' }
if ($MovementProbe) { $ReplicaProbe = $true; if (!$LoadFixture) { throw 'MovementProbe requires LoadFixture.' } }
if ($ManualControls -and !$MovementProbe) { throw 'ManualControls requires MovementProbe.' }
$client = Join-Path (Split-Path $PSScriptRoot) 'Anomaly-1.5.3'
$testRoot = Join-Path $PSScriptRoot '_build\coopnet-engine-test'
if ($TestDirectory) {
    $testRoot=[IO.Path]::GetFullPath($TestDirectory)
    $allowed=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '_build'))+[IO.Path]::DirectorySeparatorChar
    if (!$testRoot.StartsWith($allowed,[StringComparison]::OrdinalIgnoreCase)) { throw 'TestDirectory must be inside this repository _build directory.' }
}
$output = Join-Path $PSScriptRoot '_build\_game\bin_dbg'
$dependencyBin = Join-Path $PSScriptRoot '_build\coopnet-deps\installed\x64-windows\bin'
if (!(Test-Path "$output\GameNetworkingSockets.dll")) {
    throw 'Finish build.ps1 -CoopNet successfully before preparing the engine test.'
}
$fsTemplate = Get-Content "$client\fsgame.ltx"
$userTemplate = Get-Content "$client\appdata\user.ltx" |
    Where-Object { $_ -notmatch '^(coop_|rs_screenmode |vid_mode |snd_volume_eff |snd_volume_music |r__framelimit |g_always_active )' }
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
    # Hidden probe processes must render without desktop focus; only these copied configs change.
    @($userTemplate) + @('rs_screenmode windowed','vid_mode 1280x720',
        'snd_volume_eff 0','snd_volume_music 0','r__framelimit 30','g_always_active on',$startup,'coop_status') |
        Set-Content "$data\user.ltx" -Encoding ascii
    if ($ReplicaProbe) { Add-Content "$data\user.ltx" 'coop_replica_probe' -Encoding ascii }
    if ($MovementProbe) {
        $controls = if ($ManualControls) { 'coop_movement_probe' } else { 'coop_movement_probe auto' }
        Add-Content "$data\user.ltx" $controls -Encoding ascii
    }
    if ($GameplayProbe) { Add-Content "$data\user.ltx" 'coop_gameplay_probe' -Encoding ascii }
    if ($WorldLootProbe) { Add-Content "$data\user.ltx" 'coop_loot_probe' -Encoding ascii }
    if ($RespawnProbe) { Add-Content "$data\user.ltx" 'coop_respawn_probe' -Encoding ascii }
    if ($SettingsProbe) { Add-Content "$data\user.ltx" 'coop_settings_probe' -Encoding ascii }
    if ($WorldProbe) { Add-Content "$data\user.ltx" 'coop_world_probe' -Encoding ascii }
    if ($WeaponProbe) { Add-Content "$data\user.ltx" 'coop_weapon_probe' -Encoding ascii }
    if ($InventoryProbe -and $role -eq 'guest') { Add-Content "$data\user.ltx" 'coop_inventory_probe' -Encoding ascii }
    if ($StarterProbe -and $role -eq 'host') { Add-Content "$data\user.ltx" 'coop_starter_probe' -Encoding ascii }
    if ($PartyProbe -and $role -eq 'host') { Add-Content "$data\user.ltx" 'coop_party_probe' -Encoding ascii }
    if ($LoadFixture -and ($role -eq 'host' -or $ReplicaProbe) -and !($WorldProbe -and $role -eq 'guest')) {
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
    Write-Host 'Development fixture only. Use coop_status in each console; close both clients after testing.'
}
