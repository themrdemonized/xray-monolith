# Independent level residency assessment

Required outcome: a player can move from Cordon to Garbage while another remains in Cordon without interruption. Same-level players may separate beyond the host's normal online radius. Party-wide transitions are not an acceptable final architecture.

## Evidence

| Constraint | Source | Consequence |
| --- | --- | --- |
| One active gameplay level | IGame_Level.cpp; Level.h; x_ray.cpp | A second CLevel replaces a global relied on across gameplay |
| One local actor global | Actor_Network.cpp | Additional single-player actor spawning can replace local actor ownership |
| One physics world | xrPhysics/PHWorld.cpp | Multiple maps cannot safely share current creation/teardown APIs |
| One active AI level graph and script engine | ai_space.h/.cpp | AI/Lua assumptions must be contextualized or isolated |
| One ALife actor/active-level registry | alife_graph_registry.h/.cpp | Offline world representation is not a second detailed active map |
| Online switch asserts same active level | alife_switch_manager.cpp | ALife hybrid needs new activation/ownership machinery |
| Transition broadcast and reconnect | xrServer.cpp; Level_network_messages.cpp | Existing transition flow affects the active world rather than one participant |
| World save reads active level state | alife_storage_manager.cpp | Worker/coordinator saves need coordinated consistent checkpoints |

## Candidate comparison

| Candidate | Assessment | Required proof |
| --- | --- | --- |
| A: multiple level contexts in one process | Largest direct engine refactor; not selected for the first milestone | Enumerate and replace level/actor/AI/physics/render/Lua globals; prove independent lifecycle and scheduling |
| B: session coordinator plus level worker processes | Strong isolation candidate for the existing singleton engine; not yet demonstrated | Run an authoritative level worker without local-player assumptions; define one canonical ALife owner, entity leases, IPC, atomic transfers, saves and crash recovery |
| C: ALife hybrid with promoted active levels | Good model for persistent world state, but current ALife is actor-centered | Extend active-level registries/online rules and preserve one world authority; show detailed simulation on two levels |

Recommendation: investigate B with a canonical ALife/persistence coordinator (ideas from C) before committing to A. This is an inference from source coupling, not proof that worker hosting is easy or already functional. Workers must not independently save competing copies of the same world.

## Performance priority approved by the user

Optimize for player frame times, control latency and smooth map transitions. Evaluate the coordinator plus location-worker design first. Its performance advantage is a hypothesis until the prototype is measured. First entry does not permanently choose authority.

- Keep one authority for each active location. Clients receive that worker's results instead of advancing competing copies of the world.
- Send controls and transient snapshots directly between players and their location worker. Keep the session coordinator off that frame-by-frame path; it owns identity, location leases, cross-location transfers and durable checkpoints.
- Select a worker machine using measured simulation headroom, available memory, player RTT/loss and upload capacity. Avoid assigning more work to a machine already missing its rendering or simulation budget. An eligible existing worker remains preferred while it meets budgets, so small metric fluctuations do not cause migrations.
- Co-locate authority with an existing player process where singleton ownership and save correctness allow it. Use separate processes for additional active locations on that machine. Do not duplicate a location's full simulation merely to satisfy a process topology.
- Activate detailed simulation only for occupied/relevant locations. Checkpoint and retire an empty location worker after a grace period; retain lightweight canonical ALife state. Preload a likely destination only when memory and CPU budgets permit it.
- Support other players' machines as optional worker hosts after lease handoff and crash recovery work. Hosting eligibility must include compatibility and capacity; first entrant is a fallback among eligible hosts.
- On overload or departure, prepare a replacement, checkpoint, revoke the old lease, install the new lease generation, and resume input. A client must never simulate a second authoritative copy while waiting for handoff.

Benchmark gates: compare host frame-time median/p95/p99, worker tick-time p95/p99, input-to-authoritative-result latency, per-client upload/download, aggregate memory, destination readiness time and recovery interruption. Exercise 2/4 players together, distant on one map, split across 2/4 maps, a constrained host, packet loss and worker departure. Choose deployment defaults from those measurements, including a dedicated worker option when a player's machine cannot sustain both rendering and authority. These gates are not yet passed.

## Prototype gates

1. Establish normal offline play/save/load/transition baseline.
2. Trace whether a level can run authoritatively without a render/local actor dependency. Existing dedicated-server branches are clues, not proof that Anomaly ALife works headlessly.
3. Demonstrate coordinator-to-worker activation with one authoritative entity owner and consistent global time.
4. Transfer one player/owned item subtree between workers with a checkpoint, ownership handoff, destination readiness, and rollback if load fails.
5. Demonstrate two different levels advancing simultaneously, then save/reload without duplicated entities/items.
6. Demonstrate distant same-map players have independent interest/online regions and neither depends on host proximity.

Keep a same-level prototype's protocol level-scoped from its first snapshot. Do not hard-code the host's map as the session map or bind durable session state to CLevel.
