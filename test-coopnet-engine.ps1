param([ValidateRange(30,300)][int]$Seconds = 90, [switch]$MovementProbe, [switch]$GameplayProbe, [switch]$WorldProbe, [switch]$PartyProbe)
$ErrorActionPreference = 'Stop'
if ($PartyProbe) { $WorldProbe=$true }
if ($WorldProbe) { $GameplayProbe=$true }
if ($GameplayProbe) { $MovementProbe=$true }
$client = Join-Path (Split-Path $PSScriptRoot) 'Anomaly-1.5.3'
$fixtureFiles = Get-ChildItem "$client\appdata\savedgames\player - autosave.*" -File
$originalHashes = @{}
foreach ($file in $fixtureFiles) { $originalHashes[$file.FullName] = (Get-FileHash -LiteralPath $file.FullName).Hash }
$ownedProcesses = @()
$started = [DateTime]::UtcNow
try {
    $ownedProcesses = @(& "$PSScriptRoot\prepare-coopnet-engine-test.ps1" -Launch -LoadFixture -ReplicaProbe -MovementProbe:$MovementProbe -GameplayProbe:$GameplayProbe -WorldProbe:$WorldProbe -PartyProbe:$PartyProbe)
    if ($ownedProcesses.Count -ne 2) { throw 'Expected exactly two owned engine probe processes.' }
    $watch = [System.Diagnostics.Stopwatch]::StartNew()
    while ($watch.Elapsed.TotalSeconds -lt $Seconds) {
        foreach ($process in $ownedProcesses) {
            if ($process.HasExited) { throw "Probe engine exited early: PID $($process.Id), code $($process.ExitCode)" }
        }
        Start-Sleep -Milliseconds 500
    }
} finally {
    $cleanupProblems = @()
    # Only processes returned by this launch are eligible for test cleanup.
    foreach ($process in $ownedProcesses) {
        if (!$process.HasExited) {
            $process.CloseMainWindow() | Out-Null
            if (!$process.WaitForExit(10000)) {
                Stop-Process -Id $process.Id
                $cleanupProblems += "Probe engine PID $($process.Id) did not close normally."
            }
        }
    }
    foreach ($path in $originalHashes.Keys) {
        if ((Get-FileHash -LiteralPath $path).Hash -ne $originalHashes[$path]) {
            $cleanupProblems += "Original fixture source changed: $path"
        }
    }
    if ($cleanupProblems.Count) { throw ($cleanupProblems -join "`n") }
}
$testRoot = Join-Path $PSScriptRoot '_build\coopnet-engine-test'
$logs = @{}
foreach ($role in @('host','guest')) {
    $file = Get-ChildItem "$testRoot\$role\appdata\logs" -Filter '*.log' |
        Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1
    if (!$file -or $file.LastWriteTimeUtc -lt $started) { throw "Fresh $role log missing" }
    $logs[$role] = Get-Content -LiteralPath $file.FullName -Raw
    if ($logs[$role] -match 'FATAL ERROR|! CoopNet update failed:') { throw "$role engine reported a runtime failure" }
    if ($logs[$role] -notmatch '\* CoopNet session stopped') { throw "$role session shutdown evidence missing" }
}
if ($logs.host -notmatch 'CoopNet host ready participants: 2' -or
    $logs.host -notmatch 'CoopNet host actor bound: generation [1-9]\d* level [1-9]\d*') {
    throw 'Host admission/actor capture evidence missing'
}
$renderEvidence = [regex]::Match($logs.guest,'CoopNet remote actor visual removed: updates ([1-9]\d*) render submissions ([1-9]\d*)')
if ($logs.guest -notmatch 'CoopNet client state: connected' -or
    $logs.guest -notmatch 'CoopNet remote actor visual created:' -or
    !$renderEvidence.Success) {
    throw 'Guest admission, model update, rendering or cleanup evidence missing'
}
if ($MovementProbe) {
    $motion = [regex]::Match($logs.host,'CoopNet guest simulation removed: inputs ([1-9]\d*) distance ([\d.]+)')
    if ($logs.host -notmatch 'CoopNet native guest bound:' -or !$motion.Success -or
        $logs.guest -notmatch 'CoopNet owned native snapshots applied: [1-9]\d*' -or
        [double]::Parse($motion.Groups[2].Value,[Globalization.CultureInfo]::InvariantCulture) -lt 0.5) {
        throw 'Native guest spawn, real client input and physical displacement evidence missing.'
    }
    Write-Output "NATIVE_MOVEMENT_PASS: $($motion.Groups[1].Value) inputs; $($motion.Groups[2].Value) metres displacement."
}
Write-Output "ENGINE_PROBE_PASS: guest model updated $($renderEvidence.Groups[1].Value) times, submitted $($renderEvidence.Groups[2].Value) times, and removed; original saves unchanged."
if ($GameplayProbe) {
    $nativeTransactions=[regex]::Matches($logs.host,'CoopNet inventory native transaction:')
    $health=[regex]::Matches($logs.guest,'CoopNet authoritative guest health applied: (-?[\d.]+)')
    $damaged=$false
    foreach ($match in $health) {
        $value=[double]::Parse($match.Groups[1].Value,[Globalization.CultureInfo]::InvariantCulture)
        if ($value -gt 0 -and $value -lt .95) { $damaged=$true }
    }
    if ($nativeTransactions.Count -ne 2 -or !$damaged -or
        $logs.host -notmatch 'CoopNet native inventory take confirmed:' -or
        $logs.host -notmatch 'CoopNet native inventory drop confirmed:' -or
        $logs.guest -notmatch 'CoopNet gameplay results: inventory accepts 3 phase 3 condition updates [1-9]\d*') {
        throw 'Host native loot ownership, exactly-once replay, drop or authoritative damage evidence missing.'
    }
    Write-Output 'NATIVE_GAMEPLAY_FIXTURE_PASS: native take/drop, replay without duplicate mutation, host damage and client condition correction.'
}
if ($WorldProbe) {
    $baseline=[regex]::Match($logs.host,'CoopNet canonical baseline captured: (coopnet-[0-9a-f]{16}) bytes ([1-9]\d*) level ([1-9]\d*)')
    if (!$baseline.Success -or $logs.guest -notmatch 'CoopNet canonical baseline SHA-256 verified:' -or
        !$logs.guest.Contains("CoopNet canonical baseline loaded and acknowledged: $($baseline.Groups[1].Value) level $($baseline.Groups[3].Value)")) {
        throw 'Host-created baseline transfer, checksum validation or native loading barrier evidence missing.'
    }
    $baselineName=$baseline.Groups[1].Value+'.scop'
    $hostHash=(Get-FileHash -LiteralPath "$testRoot\host\appdata\savedgames\$baselineName").Hash
    $guestHash=(Get-FileHash -LiteralPath "$testRoot\guest\appdata\savedgames\$baselineName").Hash
    if ($hostHash -ne $guestHash) { throw 'Transferred canonical baseline differs from the host snapshot.' }
    Write-Output "CANONICAL_BASELINE_PASS: guest loaded the verified host snapshot $baselineName; file hashes match."
    if ($logs.host -notmatch 'CoopNet host NPC states: objects [1-9]\d*' -or
        $logs.guest -notmatch 'CoopNet authoritative NPC states applied: objects [1-9]\d*' -or
        $logs.guest -notmatch 'CoopNet NPC state updates: [1-9]\d*' -or
        $logs.guest -notmatch 'CoopNet passive world stopped: frame updates [1-9]\d* scheduled updates [1-9]\d*') {
        throw 'Host NPC state application or passive client frame/schedule dispatch evidence missing.'
    }
    Write-Output 'NATIVE_NPC_REPLICATION_PASS: host NPC states applied to passive client world objects; native frame and schedule dispatch bypassed local simulation.'
}
if ($PartyProbe) {
    $destination=[regex]::Match($logs.host,'CoopNet party probe destination arrived: level ([1-9]\d*)');
    $snapshots=[regex]::Matches($logs.host,'CoopNet canonical baseline captured: (coopnet-[0-9a-f]{16}) bytes ([1-9]\d*) level ([1-9]\d*)');
    if (!$destination.Success -or $logs.host -notmatch 'CoopNet party probe lone entrant held' -or
        $logs.host -notmatch 'CoopNet party probe departure reset' -or $snapshots.Count -lt 2 -or
        $logs.guest -notmatch "CoopNet party travel: stage 3 present 2 required 2 destination $($destination.Groups[1].Value)") {
        throw 'Native lone entrant, departure reset, second snapshot and whole-party destination evidence missing.'
    }
    $last=$snapshots[$snapshots.Count-1];
    if ($last.Groups[3].Value -ne $destination.Groups[1].Value -or $last.Groups[3].Value -eq $snapshots[0].Groups[3].Value) {
        throw 'Party did not travel to a different native map.'
    }
    $name=$last.Groups[1].Value+'.scop';
    if ((Get-FileHash -LiteralPath "$testRoot\host\appdata\savedgames\$name").Hash -ne
        (Get-FileHash -LiteralPath "$testRoot\guest\appdata\savedgames\$name").Hash) { throw 'Destination snapshot hashes differ.' }
    Write-Output "NATIVE_PARTY_TRANSITION_PASS: lone entrant waited, departure reset gathering, both clients loaded destination level $($destination.Groups[1].Value)."
}
Write-Output 'Development fixture only. Dynamic NPC lifecycles, item replication, normal combat/inventory controls and persistence are not verified.'
