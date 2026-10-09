# Respawn at a living teammate

In a shared CoopNet session, a player reaching zero health enters a downed state and receives a **Respawn** popup. Clicking Respawn requests revival at a living connected teammate's current XYZ position on the same map. The host chooses and validates the teammate; guests cannot supply teleport coordinates. Host players use the same validation locally. Equipment stays with the player, and revival resets condition and velocity. Escape opens the main menu.

If everyone is dead, the button is disabled. Players can wait for a living teammate or use the menu to load a save. Disconnected sessions and location loading cannot approve a respawn. A living player cannot request revival, and repeated network requests cannot revive a player twice. Pre-respawn condition/position packets are ignored after approval.

The native character shell and inventory remain intact while downed. CoopNet reports zero health, stops movement and firing, blocks damage/inventory actions, and suppresses single-player death teardown. This avoids attempting to revive an already-destroyed actor or ragdoll. It does not implement dropped death inventories or a spectator camera. Guest downed condition is retained by the existing character save mechanism; host primary downed state is session-local.

Protocol 15 requires matching builds on all peers. `coop_respawn` invokes the same request backend as the button. `coop_respawn_probe` is an explicit development-only stimulus; it is never enabled in ordinary play.
