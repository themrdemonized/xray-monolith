param([ValidateRange(120,300)][int]$TravelSeconds=150,[ValidateRange(60,300)][int]$RestartSeconds=90,[switch]$SharedWorldProbe,[switch]$ContainerProbe)
$ErrorActionPreference='Stop'
$probeRoot=Join-Path $PSScriptRoot ('_build\coopnet-persistence-'+[Guid]::NewGuid().ToString('N'))
foreach ($role in @('host','guest')) {
    $cache=Join-Path $PSScriptRoot "_build\coopnet-engine-test\$role\appdata\shaders_cache"
    $target=Join-Path $probeRoot "$role\appdata"
    New-Item $target -ItemType Directory -Force | Out-Null
    if (Test-Path $cache) { Copy-Item -LiteralPath $cache -Destination $target -Recurse }
}
& "$PSScriptRoot\test-coopnet-engine.ps1" -WeaponProbe -PartyProbe -SharedWorldProbe:$SharedWorldProbe -ContainerProbe:$ContainerProbe -Seconds $TravelSeconds -TestDirectory $probeRoot
if ($SharedWorldProbe) {
    $travelGuestLog=Get-ChildItem (Join-Path $probeRoot 'guest\appdata\logs') -Filter '*.log' | Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1
    $travelGuestText=Get-Content -LiteralPath $travelGuestLog.FullName -Raw
    $completedMatches=[regex]::Matches($travelGuestText,'CoopNet shared probe: guest quest coopnet_probe_quest state 2')
    if ($completedMatches.Count -lt 2 -or $travelGuestText -notmatch 'CoopNet NPC catalogue received: objects \d+ level 2') { throw 'Shared quests or NPC catalogue did not resume after party travel.' }
    Write-Output 'SHARED_WORLD_TRAVEL_PASS: NPC catalogue and completed shared quest were reapplied after party travel.'
}
if ($ContainerProbe) {
    $containerTravelLog=Get-ChildItem (Join-Path $probeRoot 'guest\appdata\logs') -Filter '*.log' | Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1
    $containerTravelText=Get-Content -LiteralPath $containerTravelLog.FullName -Raw
    if ($containerTravelText -notmatch 'container inventory restored: marker 1 level 2' -or $containerTravelText -notmatch 'container inventory restored: marker 2 level 2') { throw 'Native stash/corpse loot did not survive travel.' }
    Write-Output 'NATIVE_CONTAINER_TRAVEL_PASS: both looted bandages retained their conditions in destination native inventory.'
}
$records=@(Get-ChildItem (Join-Path $probeRoot 'host\appdata\savedgames') -Filter 'coopnet-character-*-0000000000000002-*' -File)
if ($records.Count -ne 2) { throw 'Expected both guest save journal records.' }
$ordered=@($records | ForEach-Object {
    $bytes=[IO.File]::ReadAllBytes($_.FullName)
    if ($bytes.Length -lt 92) { throw 'Incomplete guest save record before corruption test.' }
    [pscustomobject]@{ Path=$_.FullName; Sequence=[BitConverter]::ToUInt64($bytes,36); Bytes=$bytes }
} | Sort-Object Sequence -Descending)
if ($ordered[0].Sequence -le $ordered[1].Sequence) { throw 'Distinct journal sequences missing.' }
$newest=$ordered[0]
$newest.Bytes[44]=$newest.Bytes[44] -bxor 1
[IO.File]::WriteAllBytes($newest.Path,$newest.Bytes)
Write-Output "CORRUPT_RECORD_STIMULUS: changed the newest disposable guest record, sequence $($newest.Sequence); previous version retained."
& "$PSScriptRoot\test-coopnet-engine.ps1" -RestartProbe -ContainerRecoveryProbe:$ContainerProbe -Seconds $RestartSeconds -TestDirectory $probeRoot
$hostLog=Get-ChildItem (Join-Path $probeRoot 'host\appdata\logs') -Filter '*.log' |
    Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1
$text=Get-Content -LiteralPath $hostLog.FullName -Raw
if ($text -notmatch 'CoopNet ignored invalid guest save: character 2 slot [01]') { throw 'Corrupt newest record rejection evidence missing.' }
$expectedRecoveryCount=if ($ContainerProbe) { 3 } else { 1 }
if ($text -notmatch "CoopNet durable guest save loaded: character 2 sequence $($ordered[1].Sequence) items $expectedRecoveryCount") {
    throw 'Recovery from the previous valid guest save record missing.'
}
Write-Output "NATIVE_GUEST_SAVE_RECOVERY_PASS: new host rejected corrupted newest record, restored previous valid guest state, and retained the weapon with two rounds. Test files: $probeRoot"
