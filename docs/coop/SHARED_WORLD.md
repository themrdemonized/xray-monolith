# Host-owned loot, NPCs and quests

Protocol 16 extends the single-host, single-active-location model. Everyone still travels together; there are no location workers.

## Loot

Loose ordinary world items use the existing host-owned item catalogue and inventory transactions. Pickup, drop, condition and ammunition changes are reflected to guests. The host checks ownership, revision, distance and capacity; repeated requests cannot duplicate a transfer. Persistent ALife ownership is withdrawn on pickup and restored on drop. NPC inventory, stashes, story/quest items and nested item relationships remain outside this loose-loot path.

Pickup/drop revision conflicts retry at most three times, waiting for the host's newer item state. Retries expire after two seconds and require the same incarnation, location and ownership. Denial, distance and capacity failures are not retried; the host's existing checks and request replay protection remain in force.

## NPC lifecycle

The host publishes a complete reliable catalogue when NPC membership, incarnation, appearance or living/dead state changes. Each record contains a session anchor, incarnation, local section/model reference and initial pose. Positions and health continue through the existing sequenced pose channel. Guests bind matching snapshot objects, create temporary presentation objects for later spawns, retire removed/replaced objects and apply death without running native death rewards, reputation or quest callbacks.

The local engine allocates all replica object IDs. Network anchors never become native IDs. Replica creation must resolve a local NPC section and model; actor sections and path traversal are rejected. Replicas stay outside ALife persistence and AI/script scheduling. Dead replicas cannot be revived by delayed living pose packets. Corpse shells are disabled locally; exact ragdoll bones, death animation selection and equipment appearance are not replicated.

## Quests

The host owns acceptance, conditions, completion/failure and reward callbacks. Guests receive the host's native task journal and known-info registry: title, description, type, priority, status, timestamps, icon and resolvable map target. The guest PDA refreshes after an atomic update. Completed and failed tasks do not execute callbacks again. Guest task-state and info mutation paths are blocked; objective selection stays local.

Repeated quest IDs represent the latest native task incarnation. This synchronizes the native journal and story flags, rather than executing or copying arbitrary Lua functions. Guests do not independently accept/turn in quests through NPC dialogue. Mod-specific Lua task-manager tables, reward distribution, guest inventory delivery objectives and arbitrary mod script state are not synchronized by these records.

## Transport and loading

NPC and quest snapshots are separate bounded streams, at most 1 MiB each, divided into reliable 8 KiB chunks. Records have independent count/string limits; malformed, duplicate or trailing data is rejected before the native adapter sees it. Each peer completes its captured revision under backpressure before adopting a newer revision. Only guests that have acknowledged the canonical world and finished the matching level load receive these records. Baseline reload and party travel reset per-peer publication and assembly state.

Catalogue and quest changes are checked once per second. Unchanged quest snapshots are suppressed; moving NPCs do not continuously retransmit the reliable catalogue. Native catalogue reconciliation runs on catalogue changes or unfinished local spawns.

## Verification

`SharedWorldTests.cpp` exercises codecs, truncation, limits, spawn/death/removal, loading and level isolation, task completion, and large snapshots replaced during publication. `test-coopnet-engine.ps1 -SharedWorldProbe -WorldLootProbe -SettingsProbe` exercises native post-join NPC spawning, death/removal, task completion/failure and guest mutation guards alongside the loose-loot and world-setting regressions. Native verification results are recorded in PROGRESS.md after each completed run.
