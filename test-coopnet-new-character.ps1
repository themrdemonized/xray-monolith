param([ValidateRange(60,240)][int]$Seconds=120)
$ErrorActionPreference='Stop'
$testRoot=Join-Path $PSScriptRoot ('_build/coopnet-new-character-'+[Guid]::NewGuid().ToString('N'))
$client=Join-Path (Split-Path $PSScriptRoot) 'Anomaly-1.5.3'
$sourceFiles=@(Get-ChildItem "$client/appdata/savedgames/player - autosave.*" -File)+@(Get-Item "$client/gamedata/configs/axr_options.ltx")
$sourceHashes=@{}
foreach ($file in $sourceFiles) {$sourceHashes[$file.FullName]=(Get-FileHash $file.FullName).Hash}
& "$PSScriptRoot/prepare-coopnet-engine-test.ps1" -LoadFixture -TestDirectory $testRoot
# Isolate writable new-game options from the installed client and its saves.
foreach ($role in @('host','guest')) {
    $root=Join-Path $testRoot $role
    Copy-Item "$client/gamedata" $root -Recurse
    New-Item "$root/db" -ItemType Junction -Target "$client/db" | Out-Null
    $fsLines=Get-Content "$root/fsgame.ltx" | Where-Object {$_ -notmatch '^\$fs_root\$'}
    @('$fs_root$ = false | false | '+$root+'\')+@($fsLines) | Set-Content "$root/fsgame.ltx" -Encoding ascii
}
$guestRoot=Join-Path $testRoot 'guest'
$optionsPath=Join-Path $guestRoot 'gamedata/configs/axr_options.ltx'
$options=Get-Content $optionsPath -Raw
foreach ($entry in @{new_game_faction='stalker';new_game_money='1234';new_game_loadout='wpn_pm,novice_outfit,bandage';new_game_map='factory_complex';new_game_character_name='ArrivalGuest'}.GetEnumerator()) {
    $options=[regex]::Replace($options,'(?m)^\s*'+$entry.Key+'\s*=.*$',('        '+$entry.Key+' = '+$entry.Value))
}
Set-Content $optionsPath $options -Encoding ascii
# Normalize only the disposable host copy, using stock faction initialization.
$hostOptionsPath=Join-Path $testRoot 'host/gamedata/configs/axr_options.ltx'
$hostOptions=Get-Content $hostOptionsPath -Raw
foreach ($entry in @{new_game_faction='stalker';new_game_map='rookie_village'}.GetEnumerator()) {
    $hostOptions=[regex]::Replace($hostOptions,'(?m)^\s*'+$entry.Key+'\s*=.*$',('        '+$entry.Key+' = '+$entry.Value))
}
Set-Content $hostOptionsPath $hostOptions -Encoding ascii
$guestLines=Get-Content "$guestRoot/appdata/user.ltx" | Where-Object {$_ -notmatch '^(coop_|start )'}
@('coop_character_probe 127.0.0.1:27889')+@($guestLines)+@('start server(all/single/alife/new) client(localhost)') | Set-Content "$guestRoot/appdata/user.ltx" -Encoding ascii
$owned=@()
try {
    foreach ($role in @('host','guest')) {
        $root=Join-Path $testRoot $role
        $arguments=@('-silent_error_mode','-noprefetch')
        if ($role -eq 'guest') {$arguments+='-coop_new_character_probe'}
        $owned+=Start-Process "$root/bin/AnomalyDX11.exe" -WorkingDirectory $root -ArgumentList $arguments -WindowStyle Hidden -PassThru
    }
    $watch=[Diagnostics.Stopwatch]::StartNew()
    while ($watch.Elapsed.TotalSeconds -lt $Seconds) {
        foreach ($process in $owned) {if ($process.HasExited) {throw "New-character fixture exited early: PID $($process.Id), code $($process.ExitCode)"}}
        Start-Sleep -Milliseconds 500
    }
} finally {
    foreach ($process in $owned) {
        if (!$process.HasExited) {$process.CloseMainWindow() | Out-Null; if (!$process.WaitForExit(30000)) {Stop-Process -Id $process.Id; throw 'Owned new-character fixture did not close normally'}}
    }
    foreach ($path in $sourceHashes.Keys) {if ((Get-FileHash $path).Hash -ne $sourceHashes[$path]) {throw "Installed source file changed: $path"}}
}
$texts=@{}
foreach ($role in @('host','guest')) {
    $log=Get-ChildItem "$testRoot/$role/appdata/logs" -Filter *.log | Sort-Object LastWriteTime -Descending | Select-Object -First 1
    $texts[$role]=Get-Content $log.FullName -Raw
    if ($texts[$role] -match 'FATAL ERROR|! CoopNet update failed|\[SCRIPT ERROR\]') {throw "Native $role fixture reported an error; logs: $testRoot"}
}
$guestText=$texts.guest
if ($guestText -notmatch 'New game is successfully created' -or $guestText -notmatch 'selected character initialization completed' -or $guestText -notmatch 'canonical baseline loaded and acknowledged') {throw "Fresh stock character initialization/join did not complete; logs: $testRoot"}
if ($guestText.IndexOf('selected character initialization completed') -gt $guestText.IndexOf('joining with loaded character')) {throw 'Character connected before initialization'}
if ($texts.host -notmatch 'selected character imported: items \d+ rubles 1234' -or $guestText -notmatch 'guest inventory view applied: items \d+ active rounds -?\d+ rubles 1234') {throw "Fresh character loadout/money did not transfer; logs: $testRoot"}
$spawn=[regex]::Match($texts.host,'native guest requested: object \d+ position (-?[\d.]+) (-?[\d.]+) (-?[\d.]+)')
$arrival=[regex]::Match($guestText,'guest arrival placed: level \d+ position (-?[\d.]+) (-?[\d.]+) (-?[\d.]+)')
if (!$spawn.Success -or !$arrival.Success) {throw "No authoritative arrival placement; logs: $testRoot"}
if ($guestText -notmatch 'owned native snapshots applied: [1-9]\d*') {throw "Guest native movement did not resume after arrival; logs: $testRoot"}
$sourcePosition=[regex]::Match($guestText,'selected character initialization completed: incarnation \d+ position (-?[\d.]+) (-?[\d.]+) (-?[\d.]+)')
if (!$sourcePosition.Success) {throw 'No initialized source character position recorded'}
$sourceDistanceSquared=0
for ($axis=1;$axis -le 3;$axis++) {$sourceDistanceSquared+=[math]::Pow(([double]$sourcePosition.Groups[$axis].Value-[double]$spawn.Groups[$axis].Value),2)}
if ($sourceDistanceSquared -lt 25*25) {throw 'Fresh character fixture did not exercise a distant staging spawn'}
for ($axis=1;$axis -le 3;$axis++) {if ([math]::Abs([double]$spawn.Groups[$axis].Value-[double]$arrival.Groups[$axis].Value) -gt .25) {throw 'Guest arrival did not match the native host spawn'}}
foreach ($role in @('host','guest')) {
    $announcements=[regex]::Matches($texts[$role],'radio join announcement: ArrivalGuest has joined the session\.');
    if ($announcements.Count -ne 1) {throw "Expected exactly one named join notification on $role; logs: $testRoot"}
}
Write-Host "PASS: actual stock new character initialized, transferred selected loadout/1234 rubles, and arrived at the host-assigned native spawn. Installed saves/options unchanged. Logs: $testRoot"
Write-Host 'PASS: host and guest each posted the named join notification exactly once.'
