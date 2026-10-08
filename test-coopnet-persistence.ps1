param([ValidateRange(120,300)][int]$TravelSeconds=150,[ValidateRange(60,300)][int]$RestartSeconds=90)
$ErrorActionPreference='Stop'
$probeRoot=Join-Path $PSScriptRoot ('_build\coopnet-persistence-'+[Guid]::NewGuid().ToString('N'))
& "$PSScriptRoot\test-coopnet-engine.ps1" -WeaponProbe -PartyProbe -Seconds $TravelSeconds -TestDirectory $probeRoot
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
& "$PSScriptRoot\test-coopnet-engine.ps1" -RestartProbe -Seconds $RestartSeconds -TestDirectory $probeRoot
$hostLog=Get-ChildItem (Join-Path $probeRoot 'host\appdata\logs') -Filter '*.log' |
    Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1
$text=Get-Content -LiteralPath $hostLog.FullName -Raw
if ($text -notmatch 'CoopNet ignored invalid guest save: character 2 slot [01]') { throw 'Corrupt newest record rejection evidence missing.' }
if ($text -notmatch "CoopNet durable guest save loaded: character 2 sequence $($ordered[1].Sequence) items 1") {
    throw 'Recovery from the previous valid guest save record missing.'
}
Write-Output "NATIVE_GUEST_SAVE_RECOVERY_PASS: new host rejected corrupted newest record, restored previous valid guest state, and retained the weapon with two rounds. Test files: $probeRoot"
