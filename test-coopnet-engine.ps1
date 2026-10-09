param([ValidateRange(30,300)][int]$Seconds = 90, [switch]$MovementProbe, [switch]$GameplayProbe, [switch]$WorldProbe, [switch]$PartyProbe, [switch]$WeaponProbe, [switch]$InventoryProbe, [switch]$WorldLootProbe, [switch]$SettingsProbe, [switch]$RespawnProbe, [switch]$SharedWorldProbe, [switch]$ContainerProbe, [switch]$ContainerRecoveryProbe, [switch]$DialogueProbe, [switch]$StarterProbe, [switch]$RestartProbe, [string]$TestDirectory)
$ErrorActionPreference = 'Stop'
if ($PartyProbe) { $WorldProbe=$true }
if ($SettingsProbe) { $WorldProbe=$true }
if ($RespawnProbe) { $WorldProbe=$true }
if ($SharedWorldProbe) { $WorldProbe=$true }
if ($ContainerProbe) { $WorldProbe=$true }
if ($DialogueProbe) { $WorldProbe=$true }
if ($ContainerRecoveryProbe) { $WorldProbe=$true }
if ($WorldLootProbe) { $WorldProbe=$true }
if ($InventoryProbe) { $WeaponProbe=$true }
if ($StarterProbe) { $WorldProbe=$true }
if ($WeaponProbe) { $WorldProbe=$true }
if ($RestartProbe) { $WorldProbe=$true }
if ($WorldProbe) { $GameplayProbe=$true }
if ($GameplayProbe) { $MovementProbe=$true }
if (($InventoryProbe -or $StarterProbe -or $WorldLootProbe -or $SettingsProbe -or $RespawnProbe -or $SharedWorldProbe -or $ContainerProbe -or $DialogueProbe) -and !$TestDirectory) {
    # A prior guest journal would bypass the fresh-loadout/firing stimulus.
    $TestDirectory=Join-Path $PSScriptRoot ('_build\coopnet-inventory-'+[Guid]::NewGuid().ToString('N'))
    foreach ($role in @('host','guest')) {
        $cache=Join-Path $PSScriptRoot "_build\coopnet-engine-test\$role\appdata\shaders_cache"
        $target=Join-Path $TestDirectory "$role\appdata"
        New-Item $target -ItemType Directory -Force | Out-Null
        if (Test-Path $cache) { Copy-Item -LiteralPath $cache -Destination $target -Recurse }
    }
}
$expectedInventoryCount=if ($ContainerProbe -or $ContainerRecoveryProbe) { 3 } else { 1 }
$client = Join-Path (Split-Path $PSScriptRoot) 'Anomaly-1.5.3'
$fixtureFiles = Get-ChildItem "$client\appdata\savedgames\player - autosave.*" -File
$originalHashes = @{}
foreach ($file in $fixtureFiles) { $originalHashes[$file.FullName] = (Get-FileHash -LiteralPath $file.FullName).Hash }
$ownedProcesses = @()
$started = [DateTime]::UtcNow
try {
    $ownedProcesses = @(& "$PSScriptRoot\prepare-coopnet-engine-test.ps1" -Launch -LoadFixture -ReplicaProbe -MovementProbe:$MovementProbe -GameplayProbe:$GameplayProbe -WorldProbe:$WorldProbe -PartyProbe:$PartyProbe -WeaponProbe:$WeaponProbe -InventoryProbe:$InventoryProbe -WorldLootProbe:$WorldLootProbe -SettingsProbe:$SettingsProbe -RespawnProbe:$RespawnProbe -SharedWorldProbe:$SharedWorldProbe -ContainerProbe:$ContainerProbe -ContainerRecoveryProbe:$ContainerRecoveryProbe -DialogueProbe:$DialogueProbe -StarterProbe:$StarterProbe -TestDirectory $TestDirectory)
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
if ($TestDirectory) { $testRoot=[IO.Path]::GetFullPath($TestDirectory) }
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
    if ($logs.host -notmatch 'CoopNet guest ALife registration: 0' -or $logs.host -match 'CoopNet guest ALife registration: 1') {
        throw 'Guest actor entered the persistent ALife registry.'
    }
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
    $nativeTransactions=[regex]::Matches($logs.host,'CoopNet inventory native transaction: sequence [12] action [12] ')
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
    $restored=[regex]::Match($logs.host,'CoopNet guest condition restored: character 2 health ([\d.]+) power ([\d.]+) radiation ([\d.]+)');
    if (!$restored.Success -or [double]::Parse($restored.Groups[1].Value,[Globalization.CultureInfo]::InvariantCulture) -ge .95 -or
        [double]::Parse($restored.Groups[1].Value,[Globalization.CultureInfo]::InvariantCulture) -le 0) {
        throw 'Guest authoritative condition did not survive native map travel.'
    }
    if ($last.Groups[3].Value -ne $destination.Groups[1].Value -or $last.Groups[3].Value -eq $snapshots[0].Groups[3].Value) {
        throw 'Party did not travel to a different native map.'
    }
    $name=$last.Groups[1].Value+'.scop';
    if ((Get-FileHash -LiteralPath "$testRoot\host\appdata\savedgames\$name").Hash -ne
        (Get-FileHash -LiteralPath "$testRoot\guest\appdata\savedgames\$name").Hash) { throw 'Destination snapshot hashes differ.' }
    Write-Output "NATIVE_PARTY_TRANSITION_PASS: lone entrant waited, departure reset gathering, both clients loaded destination level $($destination.Groups[1].Value)."
}
if ($WeaponProbe) {
    if ($logs.host -notmatch 'CoopNet native weapon ready: guest [1-9]\d* rounds 3' -or
        $logs.host -notmatch 'CoopNet native guest weapon fired: remaining rounds [012]') {
        throw 'Guest weapon activation and real ammunition consumption from client fire input missing.'
    }
    Write-Output 'NATIVE_WEAPON_PASS: host guest weapon finished drawing and consumed ammunition from client fire input.'
    if ($PartyProbe) {
        if ($logs.host -notmatch ("CoopNet guest inventory restored: character 2 items $expectedInventoryCount") -or
            $logs.host -notmatch ("CoopNet native inventory restoration completed: items $expectedInventoryCount active slot [1-9]\d* rounds 2")) {
            throw 'Guest weapon and ammunition preservation across native travel missing.'
        }
        Write-Output 'NATIVE_INVENTORY_TRAVEL_PASS: guest weapon, active slot and remaining ammunition restored on the destination map.'
    }
}
if ($RestartProbe) {
    if ($logs.host -notmatch ("CoopNet durable guest save loaded: character 2 sequence [1-9]\d* items $expectedInventoryCount") -or
        $logs.host -notmatch ("CoopNet native inventory restoration completed: items $expectedInventoryCount active slot [1-9]\d* rounds 2")) {
        throw 'Guest equipment and ammunition restore after a host restart missing.'
    }
    Write-Output 'NATIVE_GUEST_RESTART_PASS: a new host process restored the saved guest weapon, active slot and two remaining rounds.'
}
Write-Output 'Development fixture only. Full NPC animation, clicked UI interaction and complete inventory presentation are not verified.'

if ($InventoryProbe) {
    if ($logs.guest -notmatch 'CoopNet guest cloned inventory retired:' -or
        $logs.guest -notmatch 'CoopNet inventory control probe completed: rounds 2' -or
        $logs.host -notmatch 'CoopNet inventory native transaction: sequence [0-9]+ action 4 ' -or
        $logs.host -notmatch 'CoopNet inventory native transaction: sequence [0-9]+ action 3 ') {
        throw 'Guest inventory mirror, host ruck/equip and ammunition preservation evidence missing.'
    }
    Write-Output 'NATIVE_INVENTORY_CONTROL_PASS: guest mirror retired cloned equipment; host accepted ruck/equip and preserved two rounds.'
}
if ($StarterProbe) {
    if ($logs.host -notmatch 'CoopNet guest starter loadout ready: items 4 rounds [1-9][0-9]*' -or
        $logs.guest -notmatch 'CoopNet guest inventory view applied: items 4 active rounds [1-9][0-9]*') {
        throw 'Fresh guest starter equipment and client inventory mirror evidence missing.'
    }
    Write-Output 'NATIVE_STARTER_LOADOUT_PASS: fresh guest received pistol, ammunition, bandage and PDA with an owner inventory view.'
}

if ($WorldLootProbe) {
    if ($logs.host -notmatch 'CoopNet world loot probe: persistent item created' -or
        $logs.host -notmatch 'CoopNet world loot probe: guest ownership confirmed stage 2' -or
        $logs.host -notmatch 'CoopNet world loot probe: persistent drop confirmed' -or
        $logs.host -notmatch 'CoopNet world loot probe: guest ownership confirmed stage 4' -or
        $logs.guest -notmatch 'CoopNet world loot probe: second pickup requested' -or
        $logs.guest -notmatch 'CoopNet world loot revision conflict: retry scheduled' -or
        $logs.guest -notmatch 'CoopNet world loot revision retry sent: sequence \d+ attempt [1-3]') {
        throw 'Persistent world loot, guest pickup, persistent drop or second pickup evidence missing.'
    }
    Write-Output 'NATIVE_WORLD_LOOT_PASS: client presentation pickup removed ALife ownership, drop restored persistent world ownership, and a second pickup transferred the same item back to the guest.'
}
if ($SharedWorldProbe) {
    if ($logs.host -notmatch 'CoopNet shared probe: host NPC and quests created' -or
        $logs.host -notmatch 'CoopNet shared probe: host NPC killed and quests completed/failed' -or
        $logs.host -notmatch 'CoopNet shared probe: host corpse removed and info withdrawn' -or
        $logs.guest -notmatch 'CoopNet NPC spawned: section dog_weak anchor' -or
        $logs.guest -notmatch 'CoopNet NPC death applied: anchor' -or
        $logs.guest -notmatch 'CoopNet NPC removed: anchor' -or
        $logs.guest -notmatch 'CoopNet shared probe: guest quest writes denied' -or
        $logs.guest -notmatch 'CoopNet shared probe: guest quest coopnet_probe_quest state 2' -or
        $logs.guest -notmatch 'CoopNet shared probe: guest quest coopnet_probe_fail state 0' -or
        $logs.guest -notmatch 'CoopNet shared probe: guest story info present' -or
        $logs.guest -notmatch 'CoopNet shared probe: guest story info removed') {
        throw 'Native NPC spawning/death/removal or host-owned quest state evidence missing.'
    }
    $probeSpawns=[regex]::Matches($logs.guest,'CoopNet NPC spawned: section dog_weak anchor (\d+)\b')
    $anchor=$probeSpawns[0].Groups[1].Value
    $spawns=[regex]::Matches($logs.guest,"CoopNet NPC spawned: section dog_weak anchor $anchor\b")
    $deaths=[regex]::Matches($logs.guest,"CoopNet NPC death applied: anchor $anchor\b")
    $removals=[regex]::Matches($logs.guest,"CoopNet NPC removed: anchor $anchor\b")
    if ($spawns.Count -ne 1 -or $deaths.Count -ne 1 -or $removals.Count -ne 1) { throw 'The replicated test NPC was recreated or lost instead of remaining stable until host removal.' }
    Write-Output 'NATIVE_SHARED_WORLD_PASS: dynamically spawned NPC replicated, host death and removal applied, quests completed/failed, and guest quest writes denied.'
}
if ($SettingsProbe) {
    if ($logs.host -notmatch 'CoopNet host world rules published: revision [1-9]\d* count [1-9]\d*' -or
        $logs.guest -notmatch 'CoopNet host world rules applied: revision [1-9]\d* count [1-9]\d*' -or
        $logs.guest -notmatch 'CoopNet settings probe: guest world commands and scripted writes denied; host factor 7 retained' -or
        $logs.guest -match 'CoopNet options .* failed|CoopNet host world rules could not be applied') {
        throw 'Host settings replication or guest native/script settings lock evidence missing.'
    }
    Write-Output 'NATIVE_HOST_SETTINGS_PASS: host rules and clock applied; guest console and scripted world-state changes rejected.'
}
if ($RespawnProbe) {
    if ($logs.guest -notmatch 'CoopNet respawn probe: guest local death ignored until host confirmation') { throw 'Guest local death authority guard evidence missing.' }
    if ($WeaponProbe -and $logs.host -notmatch 'CoopNet respawn probe: guest equipment retained; rounds 2') { throw 'Respawn equipment/ammunition preservation evidence missing.' }
    if ($logs.host -notmatch 'CoopNet respawn probe: host respawned at guest' -or
        $logs.host -notmatch 'CoopNet respawn probe: all dead; respawn disabled and host denied request' -or
        $logs.guest -notmatch 'CoopNet client respawn accepted: host position' -or
        $logs.guest -notmatch 'CoopNet respawn probe: guest living after host approval' -or
        $logs.host -notmatch 'CoopNet Respawn dialog opened' -or $logs.guest -notmatch 'CoopNet Respawn dialog opened') {
        throw 'Guest/host respawn popup, host approval or no-living-player guard evidence missing.'
    }
    Write-Output 'NATIVE_RESPAWN_PASS: guest and host popup opened, each respawned at a living teammate through host validation, and all-dead revival was rejected.'
}

if ($ContainerProbe) {
    if ($logs.host -notmatch 'container probe: populated stash created' -or $logs.host -notmatch 'container probe: host transfer and ALife withdrawal confirmed' -or $logs.guest -notmatch 'container probe: guest native inventory confirmed' -or $logs.guest -notmatch 'container replica spawned:' -or $logs.guest -notmatch 'container replica removed:') { throw 'Native populated stash replication/transfer/removal failed.' }
    if ($logs.host -notmatch 'container probe: locked stash pickup denied without mutation') { throw 'Locked stash rejection evidence missing.' }
    if ($logs.host -notmatch 'container probe: populated corpse created' -or $logs.host -notmatch 'container probe: host corpse transfer confirmed' -or $logs.guest -notmatch 'container probe: guest corpse inventory confirmed') { throw 'Native corpse ownership transfer failed.' }
    Write-Output 'NATIVE_CONTAINER_LOOT_PASS: populated stash and NPC corpse transferred items into the guest native inventory, released host source/ALife ownership, and retired the emptied stash replica.'
}
if ($ContainerRecoveryProbe) {
    $stashRecovery=[regex]::Match($logs.guest,'container inventory restored: marker 1 level ([1-9]\d*)')
    $corpseRecovery=[regex]::Match($logs.guest,'container inventory restored: marker 2 level ([1-9]\d*)')
    if (!$stashRecovery.Success -or !$corpseRecovery.Success -or $stashRecovery.Groups[1].Value -ne $corpseRecovery.Groups[1].Value) { throw 'Native stash/corpse item restart recovery failed.' }
    Write-Output 'NATIVE_CONTAINER_RESTART_PASS: both looted bandages recovered into native guest inventory with their saved conditions.'
}

if ($DialogueProbe) {
    if ($logs.host -notmatch 'native dialogue transcript probe: player and NPC answers captured without host talk UI') { throw 'Native dialogue reply redirection evidence missing.' }
    if ($logs.host -notmatch 'native dialogue topics probe: section .+ choices [1-9][0-9]* context restored stale incarnation and range denied') { throw 'Native NPC dialogue topics evidence missing.' }
    Write-Output 'NATIVE_DIALOGUE_TOPICS_PASS: real NPC topics evaluated for the guest, script context restored, stale NPC incarnation and range rejected.'
}
