# CoopNet development

Source audited: `a91b22ce`, xray-monolith, 2026-10-08. Development branch: `coopnet`. Local recovery tag: `coopnet-build-baseline`. Upstream remote remains separate from the user's `origin` fork.

The current implementation uses one host world and group location transitions. Guests load a validated canonical host snapshot and receive native actor and NPC updates. Protocol 18 includes accessible corpse/stash loot transfers and bounded dialogue transport, alongside NPC spawning/death/removal, host task-journal/story-info synchronization, respawn, main-menu joining, saved connections, world-setting locks, host-side guest firing, saved equipment and loose world loot. Guest quest dialogue acceptance/turn-in and shared rewards remain unfinished; the dialogue transport is not connected to native quest execution yet. Complete item presentation and exact NPC animations/ragdolls also need further work. See [shared-world behavior and limits](SHARED_WORLD.md) and [current verification evidence](PROGRESS.md).

## Documents

- [Respawn](RESPAWN.md): host-validated revival, all-dead behavior and retained equipment.
- [Connecting players](CONNECTING.md): client requirements and direct host/join commands.
- [Guest weapons and saved state](GUEST_STATE.md): implementation and remaining character/inventory limits.
- [Recovered design](../../COOP_NETCODE_DESIGN.md): original text recovered through section 34; explicitly incomplete because the history tool truncates the message.
- [Engine audit](ENGINE_AUDIT.md): actual ownership, object lifecycle, networking, persistence, and update paths.
- [Multi-level assessment](MULTI_LEVEL_ASSESSMENT.md): evidence against simply loading multiple CLevels; candidate comparison and decision gates.
- [Implementation plan](IMPLEMENTATION_PLAN.md): concrete next milestones and acceptance checks.
- [Transport integration](TRANSPORT_SETUP.md): opt-in build and console commands.

## Established baseline

DX11/x64 builds with the 14.44 compiler through Visual Studio 2026's v145 integration and matching MFC/ATL libraries. `build.ps1` selects this combination. The build and deployment succeeded; the client reached its main loop. Incremental script verification completed with zero errors. Ordinary gameplay, inventory, save/load, and a level transition still require a manual baseline test. Do not describe the menu/startup smoke test as a complete single-player regression test.

Run the commands in [BUILD-AND-RUN.md](../../BUILD-AND-RUN.md). Keep Anomaly-1.5.3 beside the repository. Close the game before deployment.

## Current decision

One host simulates the active location. Every connected player must stand in the same exit before the party travels together. The networking runtime is created by host/join console commands and survives CLevel transitions. Guest actors use explicit native roles, and the host owns gameplay decisions.

Independent active locations and location workers are outside the user-selected scope. Earlier multi-level design documents are historical proposals, not current implementation requirements.
