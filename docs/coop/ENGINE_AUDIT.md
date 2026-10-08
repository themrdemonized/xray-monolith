# Engine reconnaissance

Paths below are repository-relative. Findings describe inspected source at `a91b22ce`; proposed seams are recommendations, not existing CoopNet APIs.

## Level and process ownership

`src/xrEngine/IGame_Level.cpp:20` defines `g_pGameLevel`; the base constructor assigns `this` to it. `src/xrGame/Level.h:387` implements `Level()` by casting that global. `Game()`, `OnServer()`, and `OnClient()` also resolve through it. `CApplication::OnEvent` in `src/xrEngine/x_ray.cpp` asserts no existing level on start, creates a game level, and stops/deletes it on disconnect. These are process lifecycle assumptions, not merely a convenience accessor.

`IGame_Level` owns its runtime object list and collision object space. `CLevel` adds server/game state, bullets, spawn/game-event processing, and gameplay managers. Its constructor/destructor and `net_Stop` are lifecycle boundaries. `CLevel::remove_objects` in `Level_network.cpp` clears server/client objects, bullets, physics commanders, scripts/garbage, renderer resources, and particles. A remote player's departure must not run this process-wide cleanup.

## Actor ownership and presentation

Follow-up against the current worktree: `CActor::shedule_Update` in `Actor.cpp` runs the wish-state control/physics pipeline only when this actor is `Level().CurrentControlEntity()`. The other branch interpolates existing native network state. Merely setting a second actor's `mstate_wishful` will therefore not provide host-controlled guest simulation. Extract a host-authoritative physical step without the local input polling, toggle clearing, HUD/contact/drop/UI side effects; do not switch the process control entity to simulate a guest.

Movement isolation follow-up: pending jump impulse was a global `NET_Jump` in `Actor.cpp`; it is now per actor. `g_cl_CheckControls` transforms acceleration using that actor's model yaw, but its single-player movement effect block previously chose `CurrentControlEntity()` as the effect target without checking the moving actor. That block now requires `this` to be the control actor. The legacy landing functor receives only contact speed, so it is also local-control-only until a player-scoped script event exists. `g_Physics` still passes the actor's active camera direction to the movement controller; host guest simulation must supply the guest's aim direction explicitly. Collision damage emission currently requires the control actor and also needs an authoritative guest path.

`CActor::net_Spawn` forces the LOCAL flag on the server and sets HUD render-target flags. ASPLAYER controls `g_actor` assignment and `CLevel::g_sv_Spawn` control/view selection. A host guest needs explicit role guards around these singleton side effects and distinct lifecycle/persistence ownership before native spawning is enabled. The existing remote renderer avoids them, but has no physics or gameplay ownership.

Correction after following declarations: `game_news_registry` is a `CActor` member allocated in its constructor. `CALifeRegistryWrapper::init(ID())` stores that actor's holder ID, and the backing ALife registry selects data by this ID. Its initialization does not overwrite a process-global news owner. Preserve this actor-keyed storage when mapping persistent players. `CInventoryOwner::IsTalking()` also reads per-owner state. `CActor::CanMove()` uses per-actor conditions, but emitted global UI warning statics; those warnings now require the current control actor, without changing the movement restriction for another actor.

Additional native guest seams found in `CActor::UpdateCL`: Discord updates and `g_pGamePersistent->actor_data` shader health/stamina/bleeding writes are not guarded by view ownership. The scheduled interaction path reads the global HUD ray query before choosing use/talk/loot targets. Guest simulation must separate these local presentation/interaction paths from physical movement and per-actor inventory updates. `xrServer::Process_spawn` assigns a connection's owner when ASPLAYER is present, and `CALifeGraphRegistry::update` chooses its sole actor on the same flag; a host-side guest spawn must keep this flag clear so neither owner is replaced.

Script-spawn follow-up: `CGameObject::net_Spawn` independently invokes configured binder reload/reinit, binder net-spawn, and the generic `_G.CGameObject_NetSpawn` hook. A secondary single-player actor now skips those paths while retaining native initialization. `CGameObject::net_Destroy` also has a generic Lua hook independent of `CScriptBinder::net_Destroy`; it now restricts that hook to the primary single-player actor (or non-actors/non-single-player objects). `CActor::net_Destroy` calls inherited destruction before clearing `g_actor`, so the primary actor still qualifies. The current secondary-actor classification must be narrowed to an explicit CoopNet guest role before supporting custom secondary-actor binders. Player-scoped quest/script behavior is still unimplemented.

`src/xrGame/Actor_Network.cpp:58` defines one `g_actor`; `Actor()` asserts single-player game type. `CActor::net_Spawn` assigns the global for single-player, and destruction clears it when appropriate. `Actor.cpp` and `Actor_Network.cpp` gate some camera/HUD work on `Level().CurrentEntity()` or `CurrentViewEntity()`. Existing remote-actor paths are useful evidence, but spawning another actor in single-player is not sufficient: it can replace the actor global and expose scripts, UI, and control paths to the wrong actor.

Persistent cooperative players must live outside CActor. Initially keep stable PlayerId/CharacterId/session identity and actor bindings in a session owner that survives level teardown. Do not change `Actor()` to return an arbitrary remote participant. Keep one local control/view actor per client and review remote presentation/input gates before connecting actor replication.

## Entity IDs and lifecycle

`src/xrServerEntities/xrServer_Object_Base.h:75` stores `u16 ID`, `ID_Parent`, and `ID_Phantom`; `0xffff` represents no parent/phantom. `src/xrEngine/xr_object_list.cpp` keeps an ID-to-runtime-object map. These IDs are scoped to engine state and cannot independently identify a persistent character or a world entity across workers/reloads.

`xrServer::Process_spawn` in `src/xrGame/xrServer_process_spawn.cpp` handles server entity registration, parent ownership, and spawn packets. `src/xrGame/Level_network_spawn.cpp` materializes client runtime objects. `src/xrGame/xrServer_process_event_destroy.cpp` handles destroy events; `CActor::net_Destroy` demonstrates dependent inventory/presentation teardown.

Reuse these lifecycle mechanisms through an adapter. For external replication, map a session-scoped identity to the current engine ID with level/worker and generation context. Remove bindings on destroy/unload; reject delayed snapshots for an old generation. Never persist pointers or assume recycled u16 IDs remain globally unique.

## Existing transport and serialization

`src/xrNetServer/NET_Client.cpp` and `NET_Server.cpp` implement DirectPlay interfaces, receive/send paths, connection statistics, and connection setup. The direct-connect path bypasses portions of remote connection handling. `CLevel` receives/processes legacy game messages; `xrServer` dispatches gameplay messages and sends broadcasts. These paths supply lifecycle knowledge, not a ready Anomaly co-op session model.

`src/xrCore/net_utils.h:9` sets `NET_PacketSizeLimit` to 16 KiB. Packet writes use native scalar/vector layout, u16 message types, and size assertions. `NET_Packet::construct` directly copies the supplied size. A new protocol must validate length before any copy, define byte order/version/message limits, reject truncated payloads, and bound strings/arrays. Do not expose native packet parsing directly to an unvalidated transport payload or serialize arbitrary C++ structures.

`Actor_Network.cpp` contains `net_Export`, `net_Import_Base`, and physics interpolation/correction paths. Audit their fields and mode assumptions before borrowing them. Durable spawn/destroy/transactions need reliable ordering; rapidly obsolete movement needs explicit sequencing. Transport delivery alone does not supply gameplay authority or persistence.

## ALife and AI

`src/xrGame/game_sv_single.cpp` owns a `CALifeSimulator` when the `/alife` option is used. `src/xrGame/ai_space.h` contains one level graph, ALife simulator pointer, and script engine; `ai_space.cpp` creates/loads/unloads these process-wide objects.

`src/xrGame/alife_graph_registry.h:54` stores one actor pointer and one active level registry. `alife_graph_registry.cpp` chooses the active registry from the actor's game-graph vertex. `alife_switch_manager.cpp::add_online` requires an object's level to match the active registry. `alife_update_manager.cpp` updates switching for that registry and runs scheduled ALife simulation. Objects can exist on other game-graph levels without having detailed online physics/AI there.

Host authority must cover ALife and online AI together. Extending relevance/online switching around all players on one map is a separate task; transmitting snapshots alone does not keep distant NPCs online around a remote player.

## Save/load and portable characters

`src/xrGame/alife_storage_manager.cpp` saves header, time, spawns, objects, and registries into compressed, versioned world storage. Loading unloads/reloads state, rebuilds registrations/IDs, invokes object registration, and requires an actor. `prepare_objects_for_save` calls the active level's `ClientSend` and `ClientSave`. This is a world save path, not a portable-character serializer.

`CActor::save/load` and inventory-owner/item serialization must be inventoried before defining a character schema. Joining must extract only allowed character data, remap item IDs/parent references, and preserve the original offline save. Export must checkpoint authoritative connected-session state and reconstruct into an offline world through an adapter. Never merge ALife databases or replace a host world with a guest save.

## Inventory and interactions

`src/xrGame/Inventory.cpp` provides `Take`, `DropItem`, slot/belt/ruck placement, and owner notifications. `game_sv_single.cpp::OnTouch/OnDetach` changes ALife attachment relationships. Runtime placement and authoritative parent ownership must agree; replicating only UI inventory lists would miss world object ownership and scripts.

Use host-resolved transactions with item identity, idempotent request IDs, ordered outcomes, and explicit duplicate pickup handling. Persistence and reconnect must retain the canonical inventory result. Combat damage/death, doors/containers, quests, and arbitrary script side effects require additional individual audits before replication.

## Level transition path

`src/xrGame/level_changer.cpp:131` builds `M_CHANGE_LEVEL` with destination graph/node, position, and angles. `UIGameSP.cpp` also sends this message. `game_sv_Single::change_level` delegates to ALife. `CALifeUpdateManager::change_level` modifies the single graph actor, saves the world, and restores its previous state while preparing the switch.

`src/xrGame/xrServer.cpp:578` broadcasts an accepted change-level message. `Level_network_messages.cpp` handles it with reconnect logic; `CLevel::MakeReconnect` defers disconnect/start events. This path changes the active game world, not a selected cooperative player's residency. CoopNet needs its own per-player request/checkpoint/destination-ready/binding state machine and must not forward this broadcast unchanged.

## Lua, scheduling, and threading

`ai_space.cpp` owns one `CScriptEngine`; `src/xrServerEntities/script_engine.*` contains script loading/execution. Single-player scripts and callbacks can resolve process-global actor/level state. Script synchronization cannot safely be inferred from existing multiplayer packets. Keep gameplay Lua on its established engine execution paths; transport callbacks should enqueue bounded immutable messages.

`src/xrGame/Level.cpp:976` receives packets then processes game events during `OnFrame`. Spawn prefetching also uses locks/queues. `src/xrEngine/xrSheduler.cpp` dispatches scheduled updates using engine time; `CALifeUpdateManager::shedule_Update` can place ALife updates in `Device.seqParallel` when configured. Existing multithreading is not permission to mutate actor/object/Lua state from a network thread.

Introduce a monotonic fixed networking cadence (configurable 20–30 Hz) with bounded catch-up. Apply session commands at a documented safe engine point; capture snapshots only with understood synchronization relative to physics/AI jobs. Rendering FPS must not determine protocol ticks.

## Physics and global presentation

`src/xrPhysics/PHWorld.cpp:40` defines one `ph_world`; creation replaces this global and destruction clears it. Physics world creation accepts one object space/object list/device. `src/xrEngine/Environment.cpp`, `Environment_misc.cpp`, and renderer paths use process-global device/render/persistent/level state. Independent active maps require more than an array of CLevel instances.

## Limits of this audit

This is static source reconnaissance with a proven compile/startup baseline. It does not prove a headless level worker, multi-actor single-player, safe concurrent Lua, portable quest state, or multiplayer gameplay. Those are explicit prototype/field-level audit gates in the implementation plan.
