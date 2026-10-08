# Independent level residency assessment

The active architecture is one host process. Location-worker hosting has been removed at the user's request; the earlier distributed-host proposal is superseded. Entity bindings now contain level and native object identity only.

The user selected shared location residency on 2026-10-08: players gather in one exit and the whole party moves together. Independent active maps are deferred. The engine constraints below remain research for a future expansion; they are not required by the current party-travel scope.

## Engine constraints

| Constraint | Source | Required change |
| --- | --- | --- |
| One active gameplay level | IGame_Level.cpp; Level.h; x_ray.cpp | Isolate level context and lifecycle inside the host |
| One local actor global | Actor_Network.cpp | Keep camera/input ownership independent of remote player actors |
| One physics world | xrPhysics/PHWorld.cpp | Isolate physics creation, stepping and teardown by level |
| One active AI level graph and script engine | ai_space.h/.cpp | Make AI and script world access contextual |
| One ALife actor/active-level registry | alife_graph_registry.h/.cpp | Support multiple active player regions while preserving one persistent world |
| Online switch requires the active level | alife_switch_manager.cpp | Extend activation rules beyond the primary actor's location |
| Transition broadcast and reconnect | xrServer.cpp; Level_network_messages.cpp | Scope transitions to the travelling player |
| World save reads active level state | alife_storage_manager.cpp | Checkpoint every active context consistently |

## Verification required

1. Establish normal offline play, combat, inventory, save/load and transition regression coverage.
2. Demonstrate canonical same-map NPC, item and combat state with passive client replicas.
3. Isolate two level contexts, including AI, physics and script scheduling, within the host process.
4. Transfer a player and owned item subtree with a durable checkpoint, destination readiness and failure rollback.
5. Advance two maps simultaneously, then save/reload without duplicated entities or items.
6. Demonstrate distant same-map players have independent online/relevance regions.

Measure host frame-time median/p95/p99, simulation tick duration, input-to-result latency, bandwidth, memory, transition readiness and recovery interruption with two and four players. No performance advantage or independent residency is currently verified.
