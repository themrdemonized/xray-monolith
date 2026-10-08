# Shared host world and party travel

The current scope is one active location in one host process. Connected players travel together. Independent active locations and location workers are outside this scope.

`coop_host` and `coop_join` enable the canonical host snapshot, native guest movement and passive baseline NPC replication directly. A guest can join from the main menu. The host must have loaded a game. The existing command arguments remain port/address, character identity and matching game/mod identities.

## Crossing an exit

The host checks the real collision shape of each enabled native level changer against its own actor and every connected guest actor. A loading or missing guest remains part of the required count. Dead actors do not count as standing in the exit. No network message allows a guest to claim exit occupancy or choose the destination.

The HUD log shows how many players have reached the selected exit. Everyone must remain in the same exit for one second. Leaving, changing exits or changing connected player identity/generation resets that dwell. There is no forced teleport or timeout that pulls absent players through the exit.

Once the group is ready, the host suspends guest map interest and invalidates the previous loading acknowledgements. It alone invokes the native level transition. Script, silent and dialog requests cannot independently change maps during a shared session. Input is gated during loading.

After the host loads the destination, it produces a fresh canonical snapshot for each guest. Guests verify and load that snapshot before acknowledging the destination map. The host creates replacement native guest actors on the new map. The party is marked arrived only when every connected player has completed the destination barrier. Loading has a 180-second deadline; failure stops the session rather than inventing readiness.

After arrival, exits remain disarmed until everyone has left the exit shapes. This prevents immediate travel back through a reciprocal arrival exit.

## Verification

`PartyTransitionTests.cpp` covers occupancy/dwell/reset behavior, invalid status payloads, late-join status delivery and a second snapshot/map acknowledgement cycle. `test-coopnet-engine.ps1 -PartyProbe -Seconds 150` is the native integration test: automatic positioning is enabled only by the explicit host test command. It checks a lone entrant waits, departure resets gathering, and both clients load a different destination with matching snapshot hashes.

Protocol 11 preserves native guest inventory, active slots, ammunition and health/power/radiation across replacement destination actors. Alternating host-local records also restore that subset after a host restart; see GUEST_STATE.md. Ordinary inventory UI synchronization, dynamic NPC lifecycles, full character conditions, shared quests and the complete playable co-op loop remain incomplete. Baseline snapshots remain in appdata after shutdown.
