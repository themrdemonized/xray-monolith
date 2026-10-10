param([ValidateRange(60,180)][int]$Seconds=110)
$ErrorActionPreference='Stop'
$testRoot=Join-Path $PSScriptRoot ('_build/coopnet-character-'+[Guid]::NewGuid().ToString('N'))
$client=Join-Path (Split-Path $PSScriptRoot) 'Anomaly-1.5.3'
$sourceHashes=@{}
Get-ChildItem "$client/appdata/savedgames/player - autosave.*" -File | ForEach-Object { $sourceHashes[$_.FullName]=(Get-FileHash -LiteralPath $_.FullName).Hash }
foreach ($role in @('host','guest')) {
    $cache=Join-Path $PSScriptRoot "_build/coopnet-engine-test/$role/appdata/shaders_cache"
    $target=Join-Path $testRoot "$role/appdata"
    New-Item $target -ItemType Directory -Force | Out-Null
    if (Test-Path $cache) {Copy-Item -LiteralPath $cache -Destination $target -Recurse}
}
& "$PSScriptRoot/prepare-coopnet-engine-test.ps1" -LoadFixture -ReplicaProbe -TestDirectory $testRoot
$guestConfig=Join-Path $testRoot 'guest/appdata/user.ltx'
$guestLines=Get-Content $guestConfig | Where-Object {$_ -notmatch '^coop_'}
@('coop_character_probe 127.0.0.1:27889')+@($guestLines) | Set-Content $guestConfig -Encoding ascii
$owned=@()
try {
    foreach ($role in @('host','guest')) {
        $root=Join-Path $testRoot $role
        $owned+=Start-Process -FilePath "$root/bin/AnomalyDX11.exe" -WorkingDirectory $root -ArgumentList '-silent_error_mode','-noprefetch' -WindowStyle Hidden -PassThru
    }
    $watch=[Diagnostics.Stopwatch]::StartNew()
    while ($watch.Elapsed.TotalSeconds -lt $Seconds) {
        foreach ($process in $owned) {if ($process.HasExited) {throw "Character fixture exited early: PID $($process.Id)"}}
        Start-Sleep -Milliseconds 500
    }
} finally {
    foreach ($process in $owned) {
        if (!$process.HasExited) {
            $process.CloseMainWindow() | Out-Null
            if (!$process.WaitForExit(30000)) {Stop-Process -Id $process.Id; throw 'Owned character fixture did not close normally'}
        }
    }
    foreach ($path in $sourceHashes.Keys) {if ((Get-FileHash -LiteralPath $path).Hash -ne $sourceHashes[$path]) {throw "Original save changed: $path"}}
}
$hostLog=(Get-ChildItem "$testRoot/host/appdata/logs" -Filter '*.log' | Sort-Object LastWriteTime -Descending | Select-Object -First 1).FullName
$guestLog=(Get-ChildItem "$testRoot/guest/appdata/logs" -Filter '*.log' | Sort-Object LastWriteTime -Descending | Select-Object -First 1).FullName
$hostText=Get-Content $hostLog -Raw
$guestText=Get-Content $guestLog -Raw
$selected=[regex]::Match($guestText,'joining with loaded character: items (\d+) rubles (\d+)')
if (!$selected.Success -or [int]$selected.Groups[1].Value -lt 1) {throw 'No real saved character was selected'}
$items=$selected.Groups[1].Value; $rubles=$selected.Groups[2].Value
if ($hostText -notmatch "selected character imported: items $items rubles $rubles active slot") {throw 'Native character import count/money did not match the selected save'}
if ($guestText -notmatch "guest inventory view applied: items $items active rounds -?\d+ rubles $rubles" -or $guestText -notmatch 'canonical baseline loaded and acknowledged' -or $guestText -notmatch 'guest arrival placed:') {throw 'Imported inventory/money and host arrival did not reach the guest in the host world'}
if ($hostText -match '! CoopNet update failed|FATAL ERROR' -or $guestText -match '! CoopNet update failed|FATAL ERROR') {throw 'Native character fixture reported an error'}
Write-Host "PASS: saved character transferred $items items and $rubles rubles; host world loaded; original saves unchanged."
Write-Host "Native item import checks verified ammunition, upgrades and equipped/belt placement. Logs: $testRoot"
