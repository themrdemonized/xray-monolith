# CoopNet development

Source audited: `a91b22ce`, xray-monolith, 2026-10-08. Development branch: `coopnet`. Local recovery tag: `coopnet-build-baseline`. Upstream remote remains separate from the user's `origin` fork.

The initial setup and source reconnaissance are complete. Protocol version 6, sessions, entity mapping, snapshots and input validation pass five standalone suites. Real GameNetworkingSockets loopback tests and opt-in engine host/join/presentation tests pass. Playable multiplayer is not implemented yet: the engine fixture runs two separate copied worlds. See [current verification evidence](PROGRESS.md).

## Documents

- [Recovered design](../../COOP_NETCODE_DESIGN.md): original text recovered through section 34; explicitly incomplete because the history tool truncates the message.
- [Engine audit](ENGINE_AUDIT.md): actual ownership, object lifecycle, networking, persistence, and update paths.
- [Multi-level assessment](MULTI_LEVEL_ASSESSMENT.md): evidence against simply loading multiple CLevels; candidate comparison and decision gates.
- [Implementation plan](IMPLEMENTATION_PLAN.md): concrete next milestones and acceptance checks.
- [Transport integration](TRANSPORT_SETUP.md): opt-in build and console commands.

## Established baseline

DX11/x64 builds with the 14.44 compiler through Visual Studio 2026's v145 integration and matching MFC/ATL libraries. `build.ps1` selects this combination. The build and deployment succeeded; the client reached its main loop. Incremental script verification completed with zero errors. Ordinary gameplay, inventory, save/load, and a level transition still require a manual baseline test. Do not describe the menu/startup smoke test as a complete single-player regression test.

Run the commands in [BUILD-AND-RUN.md](../../BUILD-AND-RUN.md). Keep Anomaly-1.5.3 beside the repository. Close the game before deployment.

## Current decision

Continue with explicit host-side guest roles, native actor creation/simulation and canonical world loading. The networking runtime is created only by an explicit console command, and its lifetime is above CLevel. Rendering a remote model does not supply gameplay authority. Player performance is the priority: evaluate coordinator/location workers with measured placement and direct movement traffic, using the multi-level assessment's benchmark and recovery gates.

Independent level residency remains a hard product requirement. A same-level prototype is a milestone, never the permanent party-together architecture. No multi-level architecture has yet been proven.
