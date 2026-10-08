# CoopNet development

Source audited: `a91b22ce`, xray-monolith, 2026-10-08. Development branch: `coopnet`. Local recovery tag: `coopnet-build-baseline`. Upstream remote remains separate from the user's `origin` fork.

The current implementation uses one host world and group location transitions. Guests load a validated canonical host snapshot and receive native actor and existing NPC updates. Protocol 11 adds host-side guest firing and guest condition/equipment preservation. Full ordinary gameplay remains incomplete: guest inventory presentation/controls, dynamic world lifecycles and shared quests need further work. See [current verification evidence](PROGRESS.md).

## Documents

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
