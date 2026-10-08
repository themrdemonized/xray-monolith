# Gameplay authority checkpoint

The implementation is a native gameplay fixture, not complete shared-world co-op. It runs on one host; no location-worker processes are involved.

## Inventory transactions

Protocol 7 adds reliable ordered item state, inventory requests and transaction results. Wire requests identify a logical actor, actor generation, level, logical item, expected item revision, sequence and take/drop action. Native engine object IDs stay in the owner-thread adapter.

The host authenticates the sending player against the actor binding and current level. It bounds request rate and queue size, keeps 256 recent outcomes per connection, rejects changed payloads reusing a sequence and returns the original outcome for an exact replay. Old requests outside the retained journal cannot mutate state. Reconnecting creates a new native guest binding, so old requests cannot control its replacement.

The native adapter checks actor life, item incarnation, current native/server ownership, a two-metre pickup distance and inventory capacity. It uses ordinary native ownership events and verifies the resulting server parent. The engine probe separately verifies that the native item becomes an actual child of the guest and later becomes independent. Catalog revisions follow both transactions and ownership changes performed by native gameplay. Item baselines drain in bounded batches instead of filling the reliable queue at late join.

Only explicitly created transient session fixtures currently enter this adapter. Ordinary ALife loot, corpse containers, equipment, consumption, ammunition and character inventory import/export are not connected. Transient detachment skips ALife import; destruction clears the session item binding, and guest/level teardown removes fixtures. This boundary avoids representing an ephemeral actor as the persistent owner of ordinary saved-world loot.

## Damage results

Host actor condition snapshots carry health, stamina and radiation with actor generation, level and sequenced tick. Clients discard stale ticks and retired bindings before applying owned actor conditions. Damage originates in the host's native hit-event path; the client does not choose damage values. The fixture sends one nonlethal native hit and verifies reduced host health reaches the guest.

Normal weapon inputs, host weapon/ammunition state, bullet and projectile lifecycle, NPC world replication, hit effects and lethal/death/rejoin behavior remain unfinished. The fixture does not verify those features.

## Verification

Run `test-coopnet.cmd` for six standalone suites. GameplayTests covers strict decoding, competing pickups, replay, sequence wrap, stale actor bindings, forged ownership, ordered condition results and a baseline larger than the reliable queue.

Run `./test-coopnet-engine.ps1 -GameplayProbe -Seconds 90` for two isolated game clients. It checks native movement, rendering, take/drop ownership, exactly two native mutations for three accepted responses (including replay), host damage applied on the client, normal shutdown and original-save hashes. All fixtures use separate copied appdata.

The canonical host-world baseline, passive client world, normal combat and inventory controls, durable character/item ownership, quests and world persistence are still required before calling the project playable co-op.
