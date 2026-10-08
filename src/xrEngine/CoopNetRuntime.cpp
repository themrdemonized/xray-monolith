#include "CoopNetRuntime.h"
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cmath>
namespace {
void coopnet_log(const char* format, ...) {
    char text[1024];
    va_list arguments; va_start(arguments, format);
    vsnprintf(text, sizeof(text), format, arguments);
    va_end(arguments);
    engine_coopnet::report(text);
}
}
#define Msg coopnet_log
#ifdef XR_COOPNET
#include "../CoopNet/GnsTransport.h"
#include "../CoopNet/HostPump.h"
#include "../CoopNet/ClientPump.h"
#include "../CoopNet/EngineActorBridge.h"
#include "../CoopNet/EntityRegistry.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#include <sstream>
#include <chrono>
namespace engine_coopnet {
namespace {
coopnet::Identity random_identity() {
    coopnet::Identity value = 0;
    do {
        if (BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(&value), sizeof(value),
            BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) throw std::runtime_error("CoopNet random source failed");
    } while (!value);
    return value;
}
struct Session {
    // Connections are destroyed before the networking runtime.
    coopnet::GnsRuntime runtime;
    coopnet::HostPump host;
    coopnet::ClientPump client;
    coopnet::Mode mode = coopnet::Mode::Offline;
    coopnet::ClientState last_client_state = coopnet::ClientState::Offline;
    unsigned last_ready = 0;
    std::uint32_t last_roster_revision = 0;
    std::chrono::steady_clock::time_point last_update{};
    bool clock_started = false;
    coopnet::EntityRegistry entities;
    coopnet::Identity host_actor = 0;
    std::uint64_t host_incarnation = 0, server_us = 0;
    bool server_clock_known = false;
    std::uint32_t tick = 0;
    std::uint32_t input_sequence = 0;
    coopnet::TickClock ticks;
    std::string host_visual;
    std::set<coopnet::Identity> presented;
    bool replica_probe = false;
    bool movement_probe = false;
    bool automated_controls = false;
    unsigned corrections = 0;
    bool gameplay_probe=false;
    unsigned condition_corrections=0, inventory_accepts=0, gameplay_phase=0;
    double gameplay_wait=0;
    bool gameplay_pending=false;
    coopnet::InventoryRequest probe_request;
    struct Item {
        std::uint16_t object=0xffff;
        std::uint64_t incarnation=0;
        coopnet::ItemState state;
    };
    std::map<coopnet::Identity,Item> items;
    struct Guest {
        coopnet::Identity entity = 0;
        std::uint16_t object = 0xffff;
        std::uint32_t generation = 0, last_tick = 0;
        std::uint64_t host_incarnation = 0;
        float origin[3]{};
        double distance = 0;
        unsigned inputs = 0;
        std::uint16_t fixture=0xffff;
        coopnet::Identity fixture_entity=0;
        bool take_observed=false, drop_observed=false, damage_sent=false;
    };
    std::map<coopnet::Identity,Guest> guests;
    std::map<coopnet::Identity,std::uint32_t> probe_assignments;
};
std::unique_ptr<Session> session;
coopnet::InventoryResult transact_inventory(Session& current, coopnet::Identity player, const coopnet::InventoryRequest& request) {
    coopnet::InventoryResult result{request.item,0,request.sequence,0,coopnet::InventoryStatus::Unavailable};
    const auto actor=current.guests.find(player); const auto item=current.items.find(request.item);
    if (!current.gameplay_probe || actor==current.guests.end() || item==current.items.end()) return result;
    auto& record=item->second;
    result.owner=record.state.owner; result.revision=record.state.revision;
    if (!record.state.present) return result;
    if (request.revision!=record.state.revision) { result.status=coopnet::InventoryStatus::Conflict; return result; }
    const bool take=request.action==coopnet::InventoryAction::Take;
    const auto status=transact_session_item(actor->second.object,record.object,record.incarnation,take);
    result.status=static_cast<coopnet::InventoryStatus>(status);
    if (status==NativeInventoryStatus::Accepted) {
        record.state.owner=take ? actor->second.entity : 0; ++record.state.revision;
        if (!current.host.publish_item(record.state)) throw std::runtime_error("Item ownership publication failed");
        result.owner=record.state.owner; result.revision=record.state.revision;
        Msg("* CoopNet inventory native transaction: sequence %u action %u owner %llu revision %u",
            request.sequence,static_cast<unsigned>(request.action),result.owner,result.revision);
    }
    return result;
}
const char* state_name(coopnet::ClientState state) {
    switch (state) {
    case coopnet::ClientState::Offline: return "offline";
    case coopnet::ClientState::Connecting: return "connecting";
    case coopnet::ClientState::Connected: return "connected";
    case coopnet::ClientState::Disconnected: return "disconnected";
    case coopnet::ClientState::Rejected: return "rejected";
    }
    return "unknown";
}
void capture_host(Session& current, double elapsed) {
    current.server_us += static_cast<std::uint64_t>(elapsed * 1000000);
    const auto due = current.ticks.advance(elapsed);
    LocalActorPose pose;
    const bool available = capture_local_actor(pose);
    auto* previous = current.host_actor ? current.entities.find(current.host_actor) : nullptr;
    const bool changed = available && previous && previous->active &&
        (pose.incarnation != current.host_incarnation || previous->engine.level != pose.level ||
            previous->engine.object != pose.object || current.host_visual != pose.visual);
    if (previous && previous->active && (!available || changed)) {
        current.host.remove_actor(previous->entity, previous->generation);
        current.entities.unbind(previous->entity, previous->generation);
        Msg("* CoopNet host actor unbound");
    }
    if (!available) return;
    if (!current.host_actor) current.host_actor = current.entities.create();
    previous = current.entities.find(current.host_actor);
    if (!previous->active) {
        if (!current.entities.bind(current.host_actor, {pose.level,pose.object}))
            throw std::runtime_error("CoopNet host actor binding failed");
        previous = current.entities.find(current.host_actor);
        const auto& player = current.host.session().players()[0];
        if (!current.host.create_actor({previous->entity,player.id,player.character,previous->generation,pose.level,pose.visual}))
            throw std::runtime_error("CoopNet host actor publication failed");
        current.host_incarnation = pose.incarnation;
        current.host_visual = pose.visual;
        Msg("* CoopNet host actor bound: generation %u level %u", previous->generation, pose.level);
    }
    if (!due) return;
    current.tick += due;
    coopnet::ActorSnapshot snapshot;
    snapshot.entity = previous->entity; snapshot.generation = previous->generation; snapshot.level = pose.level;
    snapshot.tick = current.tick; snapshot.time_us = current.server_us;
    snapshot.movement = pose.movement; snapshot.stance = pose.stance;
    for (unsigned axis = 0; axis < 3; ++axis) {
        snapshot.position[axis] = pose.position[axis]; snapshot.velocity[axis] = pose.velocity[axis];
        snapshot.rotation[axis] = pose.rotation[axis];
    }
    if (!current.host.publish_snapshot(snapshot)) throw std::runtime_error("Invalid engine actor snapshot");
}
void present_client(Session& current, double elapsed) {
    if (current.server_clock_known) current.server_us += static_cast<std::uint64_t>(elapsed * 1000000);
    LocalActorPose local;
    if (current.client.session().state() != coopnet::ClientState::Connected || !capture_local_actor(local)) {
        clear_remote_actors(); current.presented.clear(); return;
    }
    std::set<coopnet::Identity> visible;
    current.client.actors().visit([&](const coopnet::ActorPresence& actor) {
        if (actor.level != local.level || actor.player == current.client.session().welcome().player) return;
        coopnet::ActorSnapshot sample;
        if (!current.client.actors().sample(actor.entity,current.server_us,sample)) return;
        RemoteActorPose pose;
        pose.entity = actor.entity; pose.generation = actor.generation; pose.level = actor.level;
        pose.movement = sample.movement;
        std::memcpy(pose.visual,actor.visual.c_str(),actor.visual.size() + 1);
        for (unsigned axis = 0; axis < 3; ++axis) {
            pose.position[axis] = sample.position[axis]; pose.rotation[axis] = sample.rotation[axis];
        }
        if (present_remote_actor(pose)) visible.insert(actor.entity);
    });
    for (const auto entity : current.presented) if (!visible.count(entity)) remove_remote_actor(entity);
    current.presented = std::move(visible);
}
void capture_guests(Session& current) {
    if (!current.movement_probe) return;
    LocalActorPose host;
    const bool available = capture_local_actor(host);
    for (auto it = current.guests.begin(); it != current.guests.end();) {
        bool connected = false;
        for (const auto& player : current.host.session().players())
            if (player.id == it->first && player.connected) connected = true;
        if (!available || !connected || it->second.host_incarnation != host.incarnation) {
            auto& guest = it->second;
            if (guest.fixture_entity) {
                auto& item=current.items.at(guest.fixture_entity);
                item.state.present=false; item.state.owner=0; ++item.state.revision;
                current.host.publish_item(item.state); remove_session_item(item.object);
            }
            if (guest.generation) {
                current.host.remove_actor(guest.entity,guest.generation);
                current.entities.unbind(guest.entity,guest.generation);
            }
            remove_guest_actor(guest.object);
            Msg("* CoopNet guest simulation removed: inputs %u distance %.3f",guest.inputs,guest.distance);
            current.entities.erase(guest.entity);
            it = current.guests.erase(it);
        } else ++it;
    }
    if (!available) return;
    for (const auto& player : current.host.session().players()) {
        if (player.id == 1 || !player.connected) continue;
        auto found = current.guests.find(player.id);
        if (found == current.guests.end()) {
            const auto object = spawn_guest_actor();
            if (object == 0xffff) continue;
            Session::Guest value;
            value.object = object; value.entity = current.entities.create(); value.host_incarnation = host.incarnation;
            found = current.guests.emplace(player.id,value).first;
        }
        auto& guest = found->second;
        LocalActorPose pose;
        if (!capture_guest_actor(guest.object,pose)) continue;
        if (!guest.generation) {
            if (!current.entities.bind(guest.entity,{pose.level,pose.object}))
                throw std::runtime_error("Guest native binding failed");
            guest.generation = current.entities.find(guest.entity)->generation;
            if (!current.host.create_actor({guest.entity,player.id,player.character,guest.generation,pose.level,pose.visual}))
                throw std::runtime_error("Guest actor publication failed");
            for (unsigned axis = 0; axis < 3; ++axis) guest.origin[axis] = pose.position[axis];
            Msg("* CoopNet native guest bound: object %u generation %u",guest.object,guest.generation);
        }
        if (current.gameplay_probe) {
            if (guest.fixture==0xffff) guest.fixture=spawn_session_item(guest.object,"bandage");
            NativeSessionItem native;
            if (guest.fixture!=0xffff && capture_session_item(guest.fixture,native)) {
                if (!guest.fixture_entity) {
                    guest.fixture_entity=current.entities.create();
                    Session::Item item;
                    item.object=guest.fixture; item.incarnation=native.incarnation;
                    item.state={guest.fixture_entity,0,pose.level,1,true,native.section};
                    if (!current.host.publish_item(item.state)) throw std::runtime_error("Item baseline publication failed");
                    current.items.emplace(guest.fixture_entity,std::move(item));
                    Msg("* CoopNet session loot published: item %llu object %u",guest.fixture_entity,guest.fixture);
                }
                auto& record=current.items.at(guest.fixture_entity);
                const auto native_owner=native.owner==guest.object ? guest.entity :
                    native.owner==host.object ? current.host_actor : coopnet::Identity{0};
                const bool available=native.owner==0xffff || native_owner!=0;
                if (record.state.owner!=native_owner || record.state.present!=available) {
                    record.state.owner=native_owner; record.state.present=available; ++record.state.revision;
                    if (!current.host.publish_item(record.state)) throw std::runtime_error("Native item reconciliation failed");
                }
                if (native.native_owner==guest.object && !guest.take_observed) {
                    guest.take_observed=true;
                    Msg("* CoopNet native inventory take confirmed: object %u actor %u",guest.fixture,guest.object);
                }
                if (guest.take_observed && native.native_owner==0xffff && !guest.drop_observed) {
                    guest.drop_observed=true;
                    Msg("* CoopNet native inventory drop confirmed: object %u",guest.fixture);
                }
                if (guest.take_observed && !guest.damage_sent) {
                    guest.damage_sent=damage_guest_probe(guest.object);
                    if (guest.damage_sent) Msg("* CoopNet native damage event sent: actor %u",guest.object);
                }
            }
        }
        coopnet::ActorInput input;
        const bool active = current.host.latest_input(player.id,input);
        if (active) ++guest.inputs;
        control_guest_actor(guest.object,active ? input.buttons : 0,active ? input.yaw : pose.rotation[1],
            active ? input.pitch : pose.rotation[0]);
        const double dx = pose.position[0] - guest.origin[0], dz = pose.position[2] - guest.origin[2];
        guest.distance = (std::max)(guest.distance,std::sqrt(dx * dx + dz * dz));
        if (guest.last_tick == current.tick) continue;
        guest.last_tick = current.tick;
        coopnet::ActorSnapshot snapshot;
        snapshot.entity = guest.entity; snapshot.generation = guest.generation; snapshot.level = pose.level;
        snapshot.tick = current.tick; snapshot.time_us = current.server_us;
        snapshot.movement = pose.movement; snapshot.stance = pose.stance;
        for (unsigned axis = 0; axis < 3; ++axis) {
            snapshot.position[axis] = pose.position[axis]; snapshot.velocity[axis] = pose.velocity[axis];
            snapshot.rotation[axis] = pose.rotation[axis];
        }
        if (!current.host.publish_snapshot(snapshot)) throw std::runtime_error("Invalid native guest snapshot");
        if (current.gameplay_probe) {
            ActorConditionState condition;
            if (capture_actor_condition(guest.object,condition)) {
                current.host.publish_vitals({guest.entity,guest.generation,pose.level,current.tick,
                    condition.health,condition.power,condition.radiation});
                if (guest.damage_sent && current.tick%25==0)
                    Msg("* CoopNet host guest health: %.3f",condition.health);
            }
        }
    }
}
void send_gameplay_probe(Session& current, double elapsed) {
    if (!current.gameplay_probe || current.client.session().state()!=coopnet::ClientState::Connected || current.gameplay_phase>=3) return;
    current.gameplay_wait+=elapsed;
    coopnet::ActorPresence actor;
    current.client.actors().visit([&](const coopnet::ActorPresence& value) {
        if (value.player==current.client.session().welcome().player) actor=value;
    });
    if (!actor.entity || current.gameplay_wait<.5 || current.gameplay_pending) return;
    const coopnet::ItemState* item=nullptr;
    for (const auto& entry : current.client.items()) if (entry.second.present && entry.second.level==actor.level &&
        entry.second.section=="bandage" && (entry.second.owner==0 || entry.second.owner==actor.entity)) { item=&entry.second; break; }
    if (!item) return;
    if (current.gameplay_phase==0) {
        current.probe_request={actor.entity,item->item,actor.generation,actor.level,1,item->revision,coopnet::InventoryAction::Take};
    } else if (current.gameplay_phase==1) {
        if (item->owner!=actor.entity) return;
        // Deliberately replay the exact take request: the server must not apply it twice.
    } else {
        if (item->owner!=actor.entity) return;
        current.probe_request={actor.entity,item->item,actor.generation,actor.level,2,item->revision,coopnet::InventoryAction::Drop};
    }
    if (current.client.send_inventory(current.probe_request)==coopnet::SendResult::Sent) {
        current.gameplay_wait=0; current.gameplay_pending=true;
    }
}
void send_client_controls(Session& current, double elapsed) {
    const auto due = current.ticks.advance(elapsed);
    if (!due || current.client.session().state() != coopnet::ClientState::Connected) return;
    LocalActorControls controls;
    if (!capture_local_controls(controls)) return;
    current.input_sequence += due;
    coopnet::ActorPresence owned;
    current.client.actors().visit([&](const coopnet::ActorPresence& actor) {
        if (actor.player == current.client.session().welcome().player && actor.level == controls.level) owned = actor;
    });
    if (!owned.entity) return;
    coopnet::ActorInput input{owned.entity,owned.generation,owned.level,current.input_sequence,
        controls.buttons,controls.yaw,controls.pitch};
    if (current.automated_controls) {
        // Explicit automated test stimulus through the real client input channel.
        static constexpr std::uint16_t directions[] = {1,2,4,8};
        input.buttons = directions[(current.input_sequence % 200) / 50];
        input.yaw = 0; input.pitch = 0;
    }
    if (current.gameplay_probe && current.gameplay_phase<3) input.buttons=0;
    // Sending may disconnect and clear the replica registry; send after traversal.
    current.client.send_input(input);
}
}
void stop() {
    if (session) {
        if (session->movement_probe && session->mode == coopnet::Mode::Client)
            Msg("* CoopNet owned native snapshots applied: %u",session->corrections);
        if (session->gameplay_probe && session->mode==coopnet::Mode::Client)
            Msg("* CoopNet gameplay results: inventory accepts %u phase %u condition updates %u",
                session->inventory_accepts,session->gameplay_phase,session->condition_corrections);
        for (const auto& entry : session->guests)
            Msg("* CoopNet guest simulation removed: inputs %u distance %.3f",entry.second.inputs,entry.second.distance);
        clear_guest_actors(); clear_remote_actors(); session.reset(); Msg("* CoopNet session stopped");
    }
}
bool simulation_active() {
    return session && session->movement_probe && (session->mode == coopnet::Mode::Host ||
        session->client.session().state() == coopnet::ClientState::Connected);
}
void update(double) {
    if (!session) return;
    try {
        // Game time is zero while paused and clamped during stalls. Network deadlines
        // begin with owner-thread dispatch and continue independently of game time.
        const auto now = std::chrono::steady_clock::now();
        const double elapsed = session->clock_started ?
            std::chrono::duration<double>(now - session->last_update).count() : 0;
        session->last_update = now; session->clock_started = true;
        session->runtime.poll();
        if (session->mode == coopnet::Mode::Host) {
            for (unsigned i = 0; i < 3; ++i) {
                const auto pending = session->runtime.take_pending();
                if (pending == k_HSteamNetConnection_Invalid) break;
                session->host.attach(pending, std::make_unique<coopnet::GnsTransport>(session->runtime, pending));
            }
            session->host.update(elapsed);
            capture_host(*session, elapsed);
            capture_guests(*session);
            if (session->replica_probe && session->host_actor) {
                const auto* actor = session->entities.find(session->host_actor);
                if (actor && actor->active) for (const auto& player : session->host.session().players()) {
                    if (player.id != 1 && player.connected && session->probe_assignments[player.id] != player.generation &&
                        session->host.assign_level(player.id,actor->engine.level,random_identity())) {
                        session->probe_assignments[player.id] = player.generation;
                    }
                }
            }
            const auto ready = session->host.ready_participants();
            if (ready != session->last_ready) {
                session->last_ready = ready;
                Msg("* CoopNet host ready participants: %u (transport only)", ready);
            }
        } else {
            session->client.update(elapsed);
            if (session->replica_probe && session->client.assignment().ticket) {
                LocalActorPose local;
                if (capture_local_actor(local)) session->client.acknowledge_level(local.level);
            }
            present_client(*session, elapsed);
            send_gameplay_probe(*session,elapsed);
            send_client_controls(*session, elapsed);
            const auto state = session->client.session().state();
            if (state != session->last_client_state) {
                session->last_client_state = state;
                Msg("* CoopNet client state: %s (transport only)", state_name(state));
            }
            if (const auto* roster = session->client.roster()) {
                const auto& value = roster->current();
                if (value.revision != session->last_roster_revision) {
                    session->last_roster_revision = value.revision;
                    Msg("* CoopNet roster revision %u: %u participants (transport only)",
                        value.revision, static_cast<unsigned>(value.participants.size()));
                }
            }
        }
    } catch (const std::exception& error) {
        Msg("! CoopNet update failed: %s", error.what()); stop();
    }
}
void command(const char* name, const char* arguments) {
    try {
        if (!strcmp(name, "coop_disconnect")) { stop(); Msg("* CoopNet offline"); return; }
        if (!strcmp(name,"coop_gameplay_probe")) {
            if (!session || !session->movement_probe) throw std::runtime_error("Gameplay probe requires an active native movement probe");
            session->gameplay_probe=true;
            Msg("* CoopNet gameplay fixture enabled: transient native loot and damage only; shared world pending"); return;
        }
        if (!strcmp(name, "coop_replica_probe")) {
            if (!session) throw std::runtime_error("Start a transport session before the replica probe");
            session->replica_probe = true;
            Msg("* CoopNet replica probe: display test with independent copied worlds; shared gameplay is not enabled");
            return;
        }
        if (!strcmp(name,"coop_movement_probe")) {
            if (!session) throw std::runtime_error("Start a session before the movement probe");
            session->replica_probe = true; session->movement_probe = true;
            session->automated_controls = !strcmp(arguments,"auto");
            begin_guest_simulation();
            Msg("* CoopNet native movement probe enabled: automatic controls %u; independent client world; gameplay authority pending",
                static_cast<unsigned>(session->automated_controls));
            return;
        }
        if (!strcmp(name, "coop_status")) {
            if (!session) { Msg("* CoopNet offline"); return; }
            if (session->mode == coopnet::Mode::Host) {
                unsigned connected = 0;
                for (const auto& player : session->host.session().players()) if (player.connected) ++connected;
                Msg("* CoopNet host: %u participants (transport only; gameplay adapter pending)", connected);
            } else Msg("* CoopNet client: %s (transport only; gameplay adapter pending)",
                state_name(session->client.session().state()));
            return;
        }
        if (session) throw std::runtime_error("Disconnect the current CoopNet session first");
        std::istringstream input(arguments);
        std::string endpoint, extra;
        coopnet::Identity character = 0;
        coopnet::BuildIdentity build{};
        if (!(input >> endpoint >> character >> build.game >> build.mods) || (input >> extra) ||
            !character || !build.game || !build.mods)
            throw std::invalid_argument("Usage: coop_host <port> <character-id> <game-fingerprint> <mod-fingerprint>; coop_join <IP:port> <character-id> <game-fingerprint> <mod-fingerprint> (decimal IDs)");
        // Publish the session only after all initialization succeeds.
        auto next = std::make_unique<Session>();
        if (!strcmp(name, "coop_host")) {
            std::size_t consumed = 0;
            const auto port = std::stoul(endpoint, &consumed);
            if (consumed != endpoint.size() || !port || port > 65535 ||
                !next->runtime.listen(static_cast<std::uint16_t>(port))) throw std::runtime_error("Invalid port or listen failed");
            next->host.start(random_identity(), character, build, random_identity);
            auto* owner=next.get();
            next->host.set_inventory_handler([owner](coopnet::Identity player,const coopnet::InventoryRequest& request) {
                return transact_inventory(*owner,player,request);
            });
            next->mode = coopnet::Mode::Host;
        } else if (!strcmp(name, "coop_join")) {
            const auto connection = next->runtime.connect(endpoint.c_str());
            if (connection == k_HSteamNetConnection_Invalid) throw std::runtime_error("Invalid endpoint or connect failed");
            next->client.start(std::make_unique<coopnet::GnsTransport>(next->runtime, connection), character, build);
            auto* owner = next.get();
            next->client.set_inventory_sink([owner](const coopnet::InventoryResult& result) {
                if (!owner->gameplay_probe || !owner->gameplay_pending || result.sequence!=owner->probe_request.sequence || result.item!=owner->probe_request.item) return;
                owner->gameplay_pending=false;
                Msg("* CoopNet inventory result: sequence %u status %u revision %u",result.sequence,static_cast<unsigned>(result.status),result.revision);
                if (result.status==coopnet::InventoryStatus::Accepted) {
                    ++owner->inventory_accepts; ++owner->gameplay_phase; owner->gameplay_wait=0;
                }
            });
            next->client.set_vitals_sink([owner](const coopnet::ActorVitals& vitals) {
                const auto* actor=owner->client.actors().find(vitals.actor);
                if (owner->gameplay_probe && actor && actor->player==owner->client.session().welcome().player &&
                    apply_local_condition(vitals.level,{vitals.health,vitals.power,vitals.radiation})) {
                    ++owner->condition_corrections;
                    if (vitals.tick%25==0) Msg("* CoopNet authoritative guest health applied: %.3f",vitals.health);
                }
            });
            next->client.set_snapshot_sink([owner](const coopnet::ActorSnapshot& snapshot) {
                if (!owner->server_clock_known || snapshot.time_us > owner->server_us) owner->server_us = snapshot.time_us;
                owner->server_clock_known = true;
                const auto* actor = owner->client.actors().find(snapshot.entity);
                if (owner->movement_probe && actor && actor->player == owner->client.session().welcome().player &&
                    reconcile_local_actor(snapshot.level,snapshot.position.data(),snapshot.velocity.data())) ++owner->corrections;
            });
            next->mode = coopnet::Mode::Client;
        } else throw std::invalid_argument("Unknown CoopNet command");
        session = std::move(next);
        Msg("* CoopNet session started (transport only; use coop_status to inspect admission)");
    } catch (const std::exception& error) { Msg("! %s", error.what()); }
}
}
#else
namespace engine_coopnet {
void update(double) {}
void stop() {}
bool simulation_active() { return false; }
void command(const char*, const char*) { Msg("! CoopNet unavailable: build with -CoopNet after setup-coopnet-deps.ps1"); }
}
#endif
