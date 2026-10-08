param([ValidateRange(30,300)][int]$Seconds = 90)
$ErrorActionPreference = 'Stop'
$client = Join-Path (Split-Path $PSScriptRoot) 'Anomaly-1.5.3'
$fixtureFiles = Get-ChildItem "$client\appdata\savedgames\player - autosave.*" -File
$originalHashes = @{}
foreach ($file in $fixtureFiles) { $originalHashes[$file.FullName] = (Get-FileHash -LiteralPath $file.FullName).Hash }
$ownedProcesses = @()
$started = [DateTime]::UtcNow
try {
    $ownedProcesses = @(& "$PSScriptRoot\prepare-coopnet-engine-test.ps1" -Launch -LoadFixture -ReplicaProbe)
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
Write-Output "ENGINE_PROBE_PASS: guest model updated $($renderEvidence.Groups[1].Value) times, submitted $($renderEvidence.Groups[2].Value) times, and removed; original saves unchanged."
Write-Output 'Presentation probe only. Shared-world gameplay and visual appearance quality are not verified.'
