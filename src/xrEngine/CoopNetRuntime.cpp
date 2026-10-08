#include "CoopNetRuntime.h"
#include <cstdarg>
#include <cstdio>
#include <cstring>
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
    std::map<coopnet::Identity,std::uint32_t> probe_assignments;
};
std::unique_ptr<Session> session;
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
        if (!current.entities.bind(current.host_actor, {1,pose.level,pose.object}))
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
    // Sending may disconnect and clear the replica registry; send after traversal.
    current.client.send_input(input);
}
}
void stop() {
    if (session) { clear_remote_actors(); session.reset(); Msg("* CoopNet session stopped"); }
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
        if (!strcmp(name, "coop_replica_probe")) {
            if (!session) throw std::runtime_error("Start a transport session before the replica probe");
            session->replica_probe = true;
            Msg("* CoopNet replica probe: display test with independent copied worlds; shared gameplay is not enabled");
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
            next->mode = coopnet::Mode::Host;
        } else if (!strcmp(name, "coop_join")) {
            const auto connection = next->runtime.connect(endpoint.c_str());
            if (connection == k_HSteamNetConnection_Invalid) throw std::runtime_error("Invalid endpoint or connect failed");
            next->client.start(std::make_unique<coopnet::GnsTransport>(next->runtime, connection), character, build);
            auto* owner = next.get();
            next->client.set_snapshot_sink([owner](const coopnet::ActorSnapshot& snapshot) {
                if (!owner->server_clock_known || snapshot.time_us > owner->server_us) owner->server_us = snapshot.time_us;
                owner->server_clock_known = true;
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
void command(const char*, const char*) { Msg("! CoopNet unavailable: build with -CoopNet after setup-coopnet-deps.ps1"); }
}
#endif
