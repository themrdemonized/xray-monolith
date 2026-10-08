# Recovery status

Recovered from the conversation "Netcode For Stalker Anomaly", message 68d89c45-6ee8-4e1a-b4d3-4c5ddd19f21c. The history tool truncates the original message at 20,000 characters, during section 34. This is a partial source document, not a reconstruction of missing requirements. The companion source audit is in docs/coop/README.md. Recover the remaining original sections before claiming the complete master plan has been implemented.

# STALKER ANOMALY COOPERATIVE NETCODE

## Master Architecture and Codex Implementation Plan

**Target engine:** the STALKER Anomaly / `xray-monolith` source tree present in this repository.

**Primary objective:** implement native drop-in/drop-out cooperative multiplayer while preserving ordinary offline Anomaly gameplay.

**Important instruction to Codex:** this document describes required behavior and architectural boundaries. It does **not** guarantee that illustrative class names or pseudocode match the repository. The repository is the source of truth.

Before implementing any engine-facing subsystem, inspect the actual source and identify the real classes, globals, ownership rules, update paths, serialization paths, and threading assumptions.

---

# 1. PRODUCT REQUIREMENTS

The finished system must support:

- 2–4 players initially.
- Drop-in/drop-out multiplayer.
- Existing offline characters joining multiplayer.
- Multiplayer characters returning to offline play.
- A persistent authoritative world.
- Persistent individual character state.
- Independent player movement.
- Independent player level transitions.
- Players occupying different Anomaly levels simultaneously.
- Host-authoritative AI and world simulation.
- NPCs and mutants.
- Combat.
- Inventory.
- Loot.
- Interactions.
- World objects.
- ALife integration.
- Late joining.
- Level transitions.
- Character persistence.
- Eventually quest/script synchronization.
- Normal single-player gameplay with networking disabled.

Anti-cheat is explicitly NOT a goal.

This is cooperative networking architecture.

Correctness, stability and maintainability take priority over hostile-client validation.

---

# 2. TARGET EXPERIENCE

Example:

Player A has an existing offline character.

Player B has another existing offline character.

Player A loads their world and selects:

    HOST CO-OP

Player B selects:

    JOIN CO-OP

Player B imports their character into Player A's authoritative world.

They may initially play together.

Later:

    Player A -> Cordon
    Player B -> Garbage

This is valid.

Player A must NOT be forced into Garbage because Player B used a level transition.

Player B can later disconnect.

Their resulting character state is exported.

Player B can then load that character offline.

Player A can continue hosting, continue offline, or save normally.

---

# 3. FUNDAMENTAL WORLD RULE

DO NOT MERGE COMPLETE SINGLE-PLAYER WORLDS.

There is exactly one authoritative cooperative world.

Joining players bring their CHARACTER into that world.

They do not bring their entire offline world state.

Conceptually:

    HOST SAVE
        |
        v
    AUTHORITATIVE WORLD
        |
        +-------------------------+
        |                         |
        |      COOP SESSION       |
        |                         |
        +-------------+-----------+
                      |
              Player Registry
                      |
          +-----------+-----------+
          |                       |
      Character A             Character B
          |                       |
        Actor                   Actor

When Player B disconnects:

    Authoritative PlayerState
               |
               v
        Character Export
               |
               v
      Offline Character

Do not attempt general-purpose save-world merging.

---

# 4. ARCHITECTURAL REFERENCES

Two outside architectures are relevant.

## 4.1 TES3MP / OpenMW

TES3MP/OpenMW may be studied for architectural ideas including:

- persistent server-side player representation
- player identity
- connection-to-player mapping
- cell residency
- players occupying different cells
- world state synchronization
- object synchronization
- player state persistence
- server/client state separation
- joining an already-running world

TES3MP/OpenMW is a DESIGN REFERENCE.

Do not blindly copy its implementation.

Do not force X-Ray to internally behave like OpenMW.

Map useful concepts onto native X-Ray systems.

---

## 4.2 Legacy X-Ray Networking

Existing X-Ray multiplayer/networking code should be treated as:

- integration knowledge
- useful packet/serialization code where appropriate
- object lifecycle knowledge
- multiplayer hooks
- potentially reusable utilities

It is NOT automatically the architecture for CoopNet.

The goal is not:

    fix old STALKER multiplayer

The goal is:

    make the Anomaly single-player simulation
    capable of being authoritatively replicated

Reuse legacy code only where it helps accomplish that cleanly.

---

# 5. PRIMARY ARCHITECTURAL SEPARATION

The final system should conceptually contain:

    ANOMALY GAMEPLAY
           |
           v
    X-RAY INTEGRATION
           |
           v
        COOPNET
           |
      +----+-------------------------+
      |              |              |
      v              v              v
    Session      Replication    Persistence
      |              |              |
      +--------------+--------------+
                     |
                  Protocol
                     |
                     v
                Transport

The gameplay layer must not directly depend on socket APIs.

---

# 6. SESSION MODES

There must be three explicit modes:

    Offline
    Host
    Client

Conceptually:

    enum class CoopMode
    {
        Offline,
        Host,
        Client
    };

Exact implementation should follow repository conventions.

When:

    CoopMode::Offline

normal Anomaly behavior must remain unchanged.

This is a mandatory requirement.

---

# 7. AUTHORITY MODEL

Use host-authoritative simulation.

The host is authoritative for:

- world entity existence
- entity lifecycle
- NPCs
- mutants
- AI decisions
- ALife
- world-spawned loot
- damage results
- deaths
- world objects
- containers
- doors
- anomalies
- physics objects where necessary
- world time
- weather
- emissions
- world events
- global world state
- authoritative inventory transactions while connected

Clients are responsible for:

- input
- camera
- UI
- rendering
- sound
- local presentation
- interpolation
- eventual movement prediction

Clients communicate gameplay intentions.

Examples:

    Move
    Fire
    Reload
    UseItem
    Interact
    Pickup
    Drop
    Equip
    ChangeStance
    UseLevelTransition

The host resolves authoritative results.

---

# 8. PLAYERSTATE IS NOT CACTOR

This distinction is mandatory.

A connected player must have a persistent logical representation independent of their currently instantiated engine actor.

Conceptually:

    PlayerState != CActor

`CActor` is an engine/gameplay manifestation of a player.

`PlayerState` represents the durable cooperative player.

Illustrative structure:

    PlayerState
    {
        PlayerId
        CharacterId

        ConnectionState

        LevelId
        Position
        Rotation

        Vitals
        Inventory
        Equipment

        Money
        Reputation
        Faction
        Progression

        ActorNetEntityId
    }

DO NOT introduce this exact structure without checking existing X-Ray types.

The concept is mandatory.

The exact implementation is not.

---

# 9. WHY PLAYERSTATE EXISTS

PlayerState must survive:

- actor destruction
- temporary unloading
- level transitions
- network reconnect logic
- save/export operations
- engine object recreation

Example:

    PLAYER STATE
         |
         +----> current CActor
         |
         +----> network replication
         |
         +----> character persistence
         |
         +----> level residency

Never make persistence depend solely on the continued existence of one raw `CActor*`.

---

# 10. THREE TYPES OF STATE

Never mix these three forms.

## 10.1 Runtime Engine State

Examples:

    CActor
    NPC
    mutant
    inventory object
    physics object
    ALife object

These are actual X-Ray objects.

---

## 10.2 Network State

Small transient representations optimized for replication.

Example:

    ActorSnapshot
    {
        EntityId
        Tick

        Position
        Rotation
        Velocity

        MovementState
        Stance
    }

Network state may be lossy or quantized.

---

## 10.3 Persistent State

Durable data required for character/world reconstruction.

Example:

    CharacterState
    {
        Version
        CharacterId

        Vitals
        Inventory
        Equipment
        Money
        Reputation
        Progression
    }

Never use network snapshots as save files.

Never send complete save structures as ordinary high-frequency network updates.

---

# 11. PLAYER IDENTITY

Create stable session/player identity.

Conceptually:

    SessionId
    PlayerId
    CharacterId

These solve different problems.

`SessionId` identifies the cooperative session.

`PlayerId` identifies the connected participant.

`CharacterId` identifies the persistent character.

Do not substitute pointers or connection handles for persistent character identity.

---

# 12. NETWORK ENTITY IDENTITY

Every replicated entity needs a network-safe identity.

Conceptually:

    NetEntityId

The host allocates or authoritatively maps entity IDs.

Maintain:

    NetEntityId -> current engine object

and where useful:

    engine object -> NetEntityId

Do NOT use raw pointers as network identities.

Investigate existing X-Ray object IDs first.

Reuse or adapt existing identity systems where safe.

Do not create conflicting ID systems unnecessarily.

---

# 13. LEVEL IDENTITY

Every player and replicated world entity must have spatial residency.

At minimum:

    LevelId

Potentially:

    LevelContextId

depending on architecture discovered during investigation.

Examples:

    Player 1 -> l01_escape
    Player 2 -> l02_garbage
    Player 3 -> l05_bar

A player's current level is part of authoritative session state.

---

# 14. INDEPENDENT LEVEL RESIDENCY

This is a HARD REQUIREMENT.

Connected players must eventually be capable of occupying different Anomaly maps simultaneously.

Example:

    COOP SESSION

        Cordon
        |
        +-- Player A

        Garbage
        |
        +-- Player B

        Rostok
        |
        +-- Player C
        +-- Player D

A level transition affects only the player or players using that transition.

Never implement a permanent architecture requiring the entire party to transition together.

---

# 15. IMPORTANT MULTI-LEVEL WARNING

X-Ray may contain deep assumptions that exactly one `CLevel` or equivalent active gameplay level exists per process.

Therefore:

DO NOT immediately implement multiple loaded levels inside one process.

First investigate the repository.

The final requirement is:

    independent player level residency

The implementation mechanism is NOT predetermined.

---

# 16. MULTI-LEVEL ARCHITECTURE CANDIDATES

Codex must evaluate at least these approaches.

## Architecture A — Multiple LevelContexts in One Process

Conceptually:

    Host Process
        |
        +-- Cordon LevelContext
        +-- Garbage LevelContext
        +-- Rostok LevelContext

Potential advantages:

- single process
- direct shared state
- simple session ownership

Potential problems:

- global `CLevel`
- renderer globals
- physics globals
- AI globals
- script globals
- scheduler assumptions
- environment state
- sound state
- singleton object registries

Do not select this without proving it is viable.

---

## Architecture B — Session Coordinator + Level Workers

Conceptually:

    Coop Session
         |
         +-- Cordon Worker
         +-- Garbage Worker
         +-- Rostok Worker

Each active level may be simulated by a separate process/context.

Potential advantages:

- isolates single-level engine assumptions
- potentially easier than rewriting global engine state

Potential disadvantages:

- inter-process communication
- shared ALife ownership
- cross-level events
- save coordination
- more complicated deployment

Investigate rather than dismiss.

---

## Architecture C — ALife Hybrid

Conceptually:

    Global ALife / persistent world
                |
        +-------+-------+
        |               |
    Active Level    Offline Levels
        |
    detailed simulation

When another player enters another level:

    offline level
         |
         v
    create/promote active simulation context

This may map naturally onto X-Ray's existing online/offline ALife model.

This architecture deserves particular investigation.

Do not assume it works until the source confirms it.

---

# 17. ALIFE PRINCIPLE

The host owns authoritative ALife.

Do NOT continuously replicate the entire ALife database to every client.

Separate:

    authoritative world simulation

from:

    client-visible replicated consequences

An entity may exist in ALife without being network-relevant to a player.

---

# 18. INTEREST MANAGEMENT

Replication must be relevance based.

First filter:

    same relevant level?

If no:

    do not send ordinary entity snapshots

If yes:

    evaluate finer relevance

Potential criteria:

- distance
- entity type
- gameplay importance
- visibility
- combat relevance
- persistence importance

Conceptually:

    bool IsRelevant(Player, Entity)
    {
        if (Player.Level != Entity.Level)
            return false;

        return EvaluateLocalRelevance(Player, Entity);
    }

Global events are exceptions.

Examples:

- emissions
- global time
- session events
- certain quest/world events

---

# 19. SAME-LEVEL SPATIAL SEPARATION

Players on the same map may be arbitrarily far apart.

Do not require players to remain near the host.

Example:

    Cordon

    Player A ------------------------------ Player B

Each player gets their own interest set.

Nearby entities for A need not be sent at full frequency to B.

---

# 20. LEVEL TRANSITIONS

Transitions are per-player.

Example:

Before:

    Cordon
        Player A
        Player B

Player B uses transition.

After:

    Cordon
        Player A

    Garbage
        Player B

Player A remains uninterrupted.

---

# 21. TRANSITION STATE MACHINE

Conceptually:

    Player requests transition
             |
             v
    Host validates/resolves
             |
             v
    PlayerState enters Transitioning
             |
             v
    save/checkpoint player
             |
             v
    destination simulation prepared
             |
             v
    client loads destination
             |
             v
    client sends LevelReady
             |
             v
    actor instantiated/rebound
             |
             v
    PlayerState updated
             |
             v
    replication begins

Do not assume actor pointers survive transitions.

---

# 22. TRANSPORT

Preferred initial candidate:

    GameNetworkingSockets

But the engine must not depend directly on it everywhere.

Wrap transport behind an abstraction.

Conceptually:

    INetTransport
        |
        +-- GameNetworkingSocketsTransport

Potential future transports must remain possible.

Transport responsibilities:

- connection establishment
- connection loss
- message delivery
- reliable delivery
- unreliable delivery
- connection statistics

Transport does NOT own gameplay replication.

---

# 23. DELIVERY TYPES

Use unreliable sequenced delivery for rapidly obsolete state:

- transforms
- velocity
- aim
- frequent movement state
- frequent NPC movement
- physics snapshots

Use reliable ordered delivery for durable events:

- connect
- disconnect
- spawn
- destroy
- inventory transaction
- item pickup
- item drop
- equipment change
- level transition
- character import/export
- important world events

Never rely on unreliable delivery for persistent inventory/world mutations.

---

# 24. LOGICAL CHANNELS

Initial logical channels:

    CONTROL
    ACTOR
    COMBAT
    INVENTORY
    WORLD
    AI
    TRANSITION

Do not over-engineer transport prioritization initially.

Correctness first.

---

# 25. PROTOCOL VERSION

Define a cooperative protocol version immediately.

Conceptually:

    COOP_PROTOCOL_VERSION

Handshake:

    ClientHello
        protocolVersion
        gameBuild
        modBuild
        characterMetadata

    ServerHello
        protocolVersion
        SessionId
        PlayerId
        serverTick
        worldMetadata

Protocol mismatch must fail cleanly.

---

# 26. SIMULATION TICK

Networking must not depend on rendering FPS.

Initial target:

    20–30 Hz

for authoritative snapshot/update cadence.

Make it configurable.

Do not scatter tick constants throughout the code.

Rendering remains frame-rate independent.

---

# 27. ACTOR REPLICATION

First gameplay replication target:

- position
- rotation
- velocity
- stance
- basic movement state

Remote players must not own:

- local camera
- local input
- local UI

Do not begin with quests, inventory or ALife replication.

---

# 28. SNAPSHOT INTERPOLATION

Remote players should use buffered snapshots.

Do not directly teleport remote actors to each newly received transform.

Concept:

    Snapshot N
    Snapshot N+1
    Snapshot N+2

Render slightly behind server time.

Initial configurable interpolation delay:

    approximately 100 ms

Tune after measurement.

---

# 29. CLIENT MOVEMENT PREDICTION

Do not implement this first.

Once basic movement replication works:

Client simulates its own movement immediately.

Inputs receive sequence numbers.

Conceptually:

    PlayerInput
    {
        sequence
        movement
        look
        buttons
        stance
    }

Host acknowledges processed input.

Client reconciles against authoritative state.

Implement:

- rewind
- replay
- correction smoothing

only after baseline replication works.

---

# 30. AI

AI authority lives on the host/server simulation.

Clients must not independently make authoritative AI decisions.

Replicate enough state for presentation:

- position
- rotation
- movement
- animation state
- combat state
- health
- death
- weapon/fire events
- relevant target/action state

Do not attempt deterministic lockstep AI.

---

# 31. COMBAT

Clients submit combat intentions.

Example:

    FireWeapon

Host resolves:

- ammo
- shot
- hit
- damage
- death
- authoritative result

Replicate resulting events.

Avoid networking every hitscan bullet as an entity.

Actual persistent/projectile objects may require entity replication.

Inspect engine behavior first.

---

# 32. INVENTORY

Inventory changes are transactions/events.

Examples:

    PickupRequest
    PickupConfirmed

    DropRequest
    DropConfirmed

    Equip
    Unequip
    UseItem

Host maintains canonical connected-session inventory state.

Every persistent item needs sufficient identity to prevent ambiguity.

Anti-cheat is irrelevant.

State correctness is not.

---

# 33. CHARACTER PERSISTENCE

Implement versioned portable character state.

Required eventual contents include:

- identity
- health
- radiation
- conditions
- money
- inventory
- equipment
- ammunition
- reputation
- rank
- faction
- progression
- portable personal state

Portable quest state comes later.

---

# 34. CHARACTER IMPORT

Joining:

    Offline Character
          |
          v
    CharacterImporter
          |
          v
      PlayerState
          |
          v
    Host Simulation
          |
          v
        CActor

The original save must not be destructively modified during

[Original message truncated here by the history tool.]
