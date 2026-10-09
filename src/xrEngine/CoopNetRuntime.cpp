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
#include "../CoopNet/EngineWorldBridge.h"
#include "../CoopNet/EntityRegistry.h"
#include "../CoopNet/GuestSave.h"
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
coopnet::BaselineDigest baseline_digest(const std::vector<std::uint8_t>& bytes) {
    struct HashHandles {
        BCRYPT_ALG_HANDLE algorithm=nullptr;
        BCRYPT_HASH_HANDLE hash=nullptr;
        ~HashHandles() { if (hash) BCryptDestroyHash(hash); if (algorithm) BCryptCloseAlgorithmProvider(algorithm,0); }
    } handles;
    if (BCryptOpenAlgorithmProvider(&handles.algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)
        throw std::runtime_error("World baseline SHA-256 provider failed");
    DWORD length=0,received=0;
    if (BCryptGetProperty(handles.algorithm,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&length),sizeof(length),&received,0)<0 ||
        !length || length>65536) throw std::runtime_error("World baseline SHA-256 properties failed");
    std::vector<std::uint8_t> object(length);
    // Destroy the hash before its object buffer leaves scope.
    BCRYPT_HASH_HANDLE hash=nullptr;
    if (BCryptCreateHash(handles.algorithm,&hash,object.data(),length,nullptr,0,0)<0)
        throw std::runtime_error("World baseline SHA-256 creation failed");
    handles.hash=hash;
    coopnet::BaselineDigest digest;
    const auto status=BCryptHashData(hash,const_cast<PUCHAR>(bytes.data()),static_cast<ULONG>(bytes.size()),0);
    const auto finished=status<0 ? status : BCryptFinishHash(hash,digest.data(),static_cast<ULONG>(digest.size()),0);
    BCryptDestroyHash(handles.hash); handles.hash=nullptr;
    if (finished<0) throw std::runtime_error("World baseline SHA-256 failed");
    return digest;
}
std::string baseline_name(coopnet::Identity id) {
    char value[48]; snprintf(value,sizeof(value),"coopnet-%016llx",id); return value;
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
    bool weapon_probe=false;
    bool inventory_probe=false;
    bool starter_probe=false;
    bool loot_probe=false;
    unsigned weapon_phase=0;
    double weapon_wait=0;
    bool world_probe=false, world_load_requested=false;
    std::string world_save;
    std::map<coopnet::Identity,std::pair<std::uint32_t,std::uint64_t>> world_sent;
    std::uint32_t world_tick=0;
    unsigned world_updates=0;
    bool party_loading=false, party_authorized=false;
    double party_elapsed=0;
    std::uint32_t party_source=0;
    coopnet::PartyStatus party_status;
    coopnet::PartyBarrier party_barrier;
    std::vector<std::pair<coopnet::Identity,std::uint32_t>> party_members;
    bool party_disarmed=false,party_probe=false;
    unsigned party_probe_phase=0;
    double party_probe_time=0;
    std::uint16_t party_probe_exit=0xffff;
    float party_probe_origin[3]{};
    unsigned condition_corrections=0, inventory_accepts=0, gameplay_phase=0;
    std::map<coopnet::Identity,ActorConditionState> guest_conditions;
    std::map<coopnet::Identity,GuestInventoryState> guest_inventory;
    coopnet::BuildIdentity build;
    coopnet::Identity host_character=0;
    std::string endpoint;
    double reconnect_wait=0,reconnect_delay=2;
    std::uint64_t save_scope=0;
    struct SaveMeta { std::uint64_t sequence=0; coopnet::BaselineDigest digest{}; };
    std::map<coopnet::Identity,SaveMeta> guest_saves;
    std::set<coopnet::Identity> loaded_guest_saves;
    double gameplay_wait=0;
    bool gameplay_pending=false;
    coopnet::InventoryRequest probe_request;
    std::uint32_t inventory_sequence=1000;
    bool native_inventory_pending=false;
    LocalInventoryAction native_inventory_action;
    struct Item {
        std::uint16_t object=0xffff;
        std::uint64_t incarnation=0;
        coopnet::ItemState state;
    };
    std::map<coopnet::Identity,Item> items;
    std::uint64_t world_items_incarnation=0;
    std::set<std::pair<std::uint16_t,std::uint64_t>> unsupported_world_items;
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
        std::uint16_t weapon=0xffff;
        unsigned weapon_phase=0;
        std::uint32_t inventory_revision=0;
        std::vector<std::uint8_t> inventory_signature;
        std::uint16_t world_loot=0xffff;
        unsigned world_loot_phase=0;
    };
    std::map<coopnet::Identity,Guest> guests;
    std::map<coopnet::Identity,std::uint32_t> probe_assignments;
};
std::unique_ptr<Session> session;
std::string guest_save_name(const Session& current,coopnet::Identity character,unsigned slot) {
    char name[64]; snprintf(name,sizeof(name),"coopnet-character-%016llx-%016llx-%u",current.save_scope,character,slot);
    return name;
}
void load_guest_save(Session& current,coopnet::Identity character) {
    if (!current.save_scope || !current.loaded_guest_saves.insert(character).second) return;
    GuestSave selected; coopnet::BaselineDigest selected_digest{};
    for (unsigned slot=0;slot<2;++slot) {
        std::vector<std::uint8_t> file;
        if (!read_guest_save_file(guest_save_name(current,character,slot).c_str(),file)) continue;
        if (file.size()<32) continue;
        const std::vector<std::uint8_t> body(file.begin(),file.end()-32);
        const auto digest=baseline_digest(body);
        GuestSave saved;
        if (!std::equal(digest.begin(),digest.end(),file.end()-32) || !decode_guest_save(body,saved) ||
            saved.scope!=current.save_scope || saved.character!=character || saved.game!=current.build.game || saved.mods!=current.build.mods) {
            Msg("! CoopNet ignored invalid guest save: character %llu slot %u",character,slot); continue;
        }
        if (saved.sequence>selected.sequence) { selected=std::move(saved); selected_digest=digest; }
    }
    if (!selected.sequence) return;
    current.guest_conditions[character]=selected.condition;
    current.guest_inventory[character]=std::move(selected.inventory);
    current.guest_saves[character]={selected.sequence,selected_digest};
    Msg("* CoopNet durable guest save loaded: character %llu sequence %llu items %u",character,selected.sequence,
        static_cast<unsigned>(current.guest_inventory[character].items.size()));
}
void save_guest_state(Session& current,coopnet::Identity character) {
    const auto condition=current.guest_conditions.find(character);
    const auto inventory=current.guest_inventory.find(character);
    if (!current.save_scope || condition==current.guest_conditions.end() || inventory==current.guest_inventory.end()) return;
    auto& meta=current.guest_saves[character];
    GuestSave save;
    save.scope=current.save_scope; save.character=character; save.game=current.build.game; save.mods=current.build.mods;
    save.sequence=meta.sequence ? meta.sequence : 1; save.condition=condition->second; save.inventory=inventory->second;
    auto body=encode_guest_save(save);
    if (meta.sequence && baseline_digest(body)==meta.digest) return;
    if (meta.sequence==UINT64_MAX) throw std::runtime_error("Guest save sequence exhausted");
    save.sequence=meta.sequence+1; body=encode_guest_save(save);
    const auto digest=baseline_digest(body); auto file=body; file.insert(file.end(),digest.begin(),digest.end());
    // Alternating records retain the previous valid version if a write is interrupted.
    if (!write_guest_save_file(guest_save_name(current,character,static_cast<unsigned>(save.sequence&1)).c_str(),file))
        throw std::runtime_error("Durable guest save write failed; previous record retained");
    meta={save.sequence,digest};
    if (save.sequence==1) Msg("* CoopNet durable guest save written: character %llu items %u",character,
        static_cast<unsigned>(save.inventory.items.size()));
}
void party_status(Session& current,coopnet::PartyStage stage,unsigned present,unsigned required,std::uint32_t destination) {
    const auto& previous=current.party_status;
    if (previous.stage==stage && previous.present==present && previous.required==required && previous.destination==destination) return;
    current.party_status={0,destination,stage,static_cast<std::uint8_t>(present),static_cast<std::uint8_t>(required)};
    current.host.publish_party_status(current.party_status);
    display_party_status(static_cast<unsigned>(stage),present,required,destination);
    Msg("* CoopNet party travel: stage %u present %u required %u destination %u",static_cast<unsigned>(stage),present,required,destination);
}
void update_party(Session& current,double elapsed) {
    if (!current.world_probe) return;
    LocalActorPose host; const bool available=capture_local_actor(host);
    unsigned required=0,loaded=0;
    std::vector<std::pair<coopnet::Identity,std::uint32_t>> members;
    std::vector<std::uint16_t> actors;
    for (const auto& player:current.host.session().players()) if (player.connected) {
        ++required;
        members.emplace_back(player.id,player.generation);
        if (player.id==1) {
            actors.push_back(available ? host.object : 0xffff);
            if (available && host.level==current.party_status.destination) ++loaded;
        } else {
            const auto guest=current.guests.find(player.id);
            actors.push_back(guest!=current.guests.end() && guest->second.generation ? guest->second.object : 0xffff);
            if (current.host.level_ready(player.id,current.party_status.destination)) ++loaded;
        }
    }
    if (members!=current.party_members) { current.party_barrier.reset(); current.party_members=std::move(members); }
    if (current.party_loading) {
        current.party_elapsed+=elapsed;
        if (available && host.level==current.party_status.destination && loaded==required) {
            current.party_loading=false; current.party_barrier.reset();
            current.party_disarmed=true;
            party_status(current,coopnet::PartyStage::Arrived,loaded,required,host.level);
        } else if (current.party_elapsed>=180) {
            party_status(current,coopnet::PartyStage::Failed,loaded,required,current.party_status.destination);
            throw std::runtime_error("Party destination loading timed out");
        } else party_status(current,coopnet::PartyStage::Loading,loaded,required,current.party_status.destination);
        return;
    }
    if (!available) { current.party_barrier.reset(); return; }
    NativePartyExit exit;
    if (!capture_party_exit(actors,exit) || exit.destination==host.level) {
        current.party_disarmed=false;
        current.party_barrier.reset();
        if (current.party_status.stage==coopnet::PartyStage::Gathering)
            party_status(current,coopnet::PartyStage::Idle,0,0,0);
        return;
    }
    if (current.party_disarmed) return;
    party_status(current,coopnet::PartyStage::Gathering,exit.present,required,exit.destination);
    if (!current.party_barrier.update(exit.object+1,exit.present,required,elapsed)) return;
    for (const auto& player:current.host.session().players()) {
        const auto guest=current.guests.find(player.id);
        if (guest==current.guests.end()) continue;
        GuestInventoryState inventory;
        if (!capture_guest_inventory(guest->second.object,inventory))
            throw std::runtime_error("Guest inventory capture before travel failed");
        current.guest_inventory[player.character]=std::move(inventory);
        ActorConditionState condition;
        if (capture_actor_condition(guest->second.object,condition)) current.guest_conditions[player.character]=condition;
        save_guest_state(current,player.character);
    }
    current.party_loading=true; current.party_elapsed=0; current.party_source=host.level;
    current.host.suspend_world(); current.probe_assignments.clear();
    party_status(current,coopnet::PartyStage::Loading,0,required,exit.destination);
    current.party_authorized=true;
    const bool started=perform_party_transition(exit.object);
    current.party_authorized=false;
    if (!started) throw std::runtime_error("Native party transition failed");
}
void update_party_probe(Session& current,double elapsed) {
    if (!current.party_probe || current.guests.empty()) return;
    LocalActorPose local; if (!capture_local_actor(local)) return;
    auto& guest=current.guests.begin()->second;
    if (!guest.generation || !current.host.level_ready(current.guests.begin()->first,local.level)) return;
    current.party_probe_time+=elapsed;
    if (current.party_probe_phase==0 && guest.drop_observed && guest.damage_sent &&
        (!current.weapon_probe || guest.weapon_phase==3) && current.party_probe_time>12) {
        if (!prepare_party_probe(current.party_probe_exit,current.party_probe_origin) ||
            !position_party_probe(local.object,current.party_probe_exit,current.party_probe_origin,true))
            throw std::runtime_error("Party probe exit preparation failed");
        current.party_probe_phase=1; current.party_probe_time=0;
    } else if (current.party_probe_phase==1 && current.party_probe_time>2) {
        if (current.party_status.stage!=coopnet::PartyStage::Gathering || current.party_status.present!=1 || current.party_status.required!=2)
            throw std::runtime_error("Party probe lone entrant was not held");
        Msg("* CoopNet party probe lone entrant held");
        position_party_probe(local.object,current.party_probe_exit,current.party_probe_origin,false);
        current.party_probe_phase=2; current.party_probe_time=0;
    } else if (current.party_probe_phase==2 && current.party_probe_time>2) {
        if (current.party_status.stage!=coopnet::PartyStage::Idle) throw std::runtime_error("Party probe exit departure did not reset gathering");
        Msg("* CoopNet party probe departure reset");
        position_party_probe(local.object,current.party_probe_exit,current.party_probe_origin,true);
        position_party_probe(guest.object,current.party_probe_exit,current.party_probe_origin,true);
        current.party_probe_phase=3; current.party_probe_time=0;
    } else if (current.party_probe_phase==3 && current.party_status.stage==coopnet::PartyStage::Arrived) {
        Msg("* CoopNet party probe destination arrived: level %u",local.level); current.party_probe_phase=4;
    }
}
void publish_world(Session& current) {
    if (!current.world_probe || current.world_tick==current.tick) return;
    current.world_tick=current.tick;
    std::uint32_t level=0; std::vector<NativeWorldPose> objects;
    if (!capture_world_objects(level,objects)) return;
    coopnet::WorldState state; state.level=level; state.tick=current.tick;
    for (const auto& native:objects) {
        coopnet::WorldPose pose; pose.anchor=coopnet::world_anchor(current.host.identity(),native.object);
        pose.incarnation=native.incarnation; pose.health=native.health;
        for (unsigned axis=0;axis<3;++axis) { pose.position[axis]=native.position[axis]; pose.rotation[axis]=native.rotation[axis]; }
        state.objects.push_back(pose);
        if (state.objects.size()==128) { current.host.publish_world_state(state); state.objects.clear(); }
    }
    if (!state.objects.empty()) current.host.publish_world_state(state);
    if (!objects.empty() && ++current.world_updates==1)
        Msg("* CoopNet host NPC states: objects %u level %u",static_cast<unsigned>(objects.size()),level);
}
void send_world_baselines(Session& current) {
    if (!current.world_probe) return;
    LocalActorPose local;
    if (!capture_local_actor(local)) return;
    if (current.party_loading && local.level!=current.party_status.destination) return;
    for (const auto& player:current.host.session().players()) {
        const auto binding=std::make_pair(player.generation,local.incarnation);
        if (player.id==1 || !player.connected || current.world_sent[player.id]==binding || !current.host.participant_ready(player.id)) continue;
        const auto id=random_identity(); const auto name=baseline_name(id);
        auto bytes=std::make_shared<std::vector<std::uint8_t>>(); std::uint32_t level=0;
        if (!capture_world_baseline(name.c_str(),level,*bytes)) throw std::runtime_error("Canonical host snapshot failed");
        const coopnet::WorldBaseline manifest{id,level,static_cast<std::uint32_t>(bytes->size()),baseline_digest(*bytes)};
        if (!current.host.send_baseline(player.id,manifest,bytes)) throw std::runtime_error("Canonical host snapshot transfer failed");
        current.world_sent[player.id]=binding;
        Msg("* CoopNet canonical baseline queued: player %llu id %llu bytes %u",player.id,id,manifest.size);
    }
}
coopnet::InventoryResult transact_inventory(Session& current, coopnet::Identity player, const coopnet::InventoryRequest& request) {
    coopnet::InventoryResult result{request.item,0,request.sequence,0,coopnet::InventoryStatus::Unavailable};
    const auto actor=current.guests.find(player); const auto item=current.items.find(request.item);
    if (actor==current.guests.end() || item==current.items.end()) return result;
    auto& record=item->second;
    result.owner=record.state.owner; result.revision=record.state.revision;
    if (!record.state.present) return result;
    if (request.revision!=record.state.revision) { result.status=coopnet::InventoryStatus::Conflict; return result; }
    const auto status=transact_owned_item(actor->second.object,record.object,record.incarnation,request.action,request.slot);
    result.status=static_cast<coopnet::InventoryStatus>(status);
    if (status==NativeInventoryStatus::Accepted) {
        if (request.action==coopnet::InventoryAction::Take) record.state.owner=actor->second.entity;
        else if (request.action==coopnet::InventoryAction::Drop) record.state.owner=0;
        ++record.state.revision;
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
    if (!current.save_scope) {
        current.save_scope=guest_save_scope();
        for (auto value:{current.build.game,current.build.mods,current.host_character})
            for (unsigned byte=0;byte<8;++byte) { current.save_scope^=(value>>(8*byte))&255; current.save_scope*=1099511628211ull; }
        if (!current.save_scope) current.save_scope=1;
    }
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
void capture_world_loot(Session& current) {
    LocalActorPose pose; if (!capture_local_actor(pose)) return;
    if (current.world_items_incarnation!=pose.incarnation) {
        current.host.clear_world_items();
        for (auto it=current.items.begin();it!=current.items.end();) {
            if (it->second.state.world) { current.entities.erase(it->first); it=current.items.erase(it); } else ++it;
        }
        current.world_items_incarnation=pose.incarnation;
        current.unsupported_world_items.clear();
    }
    if (current.tick%10) return;
    std::vector<NativeWorldItem> native_items; if (!capture_world_items(native_items)) return;
    std::set<coopnet::Identity> seen;
    for (const auto& native:native_items) {
        auto validated=native.state; validated.item=1; validated.level=pose.level; validated.revision=1;
        validated.anchor=coopnet::world_anchor(current.host.identity(),native.object); validated.incarnation=native.incarnation;
        if (!coopnet::valid_item_state(validated)) {
            if (current.unsupported_world_items.insert({native.object,native.incarnation}).second)
                Msg("! CoopNet unsupported world item: section %s condition %.3f position %.3f %.3f %.3f",native.state.section.c_str(),native.state.condition,native.state.position[0],native.state.position[1],native.state.position[2]);
            continue;
        }
        coopnet::Identity owner=0;
        if (native.owner!=0xffff) {
            owner=current.host_actor;
            for (const auto& guest:current.guests) if (guest.second.object==native.owner) owner=guest.second.entity;
        }
        auto found=current.items.end();
        for (auto it=current.items.begin();it!=current.items.end();++it)
            if (it->second.object==native.object && it->second.incarnation==native.incarnation) { found=it; break; }
        if (found==current.items.end()) {
            if (owner || current.items.size()>=3000) continue; // reserve identities for three guest inventories
            const auto logical=current.entities.create(); Session::Item record;
            record.object=native.object; record.incarnation=native.incarnation; record.state=native.state;
            record.state.item=logical; record.state.revision=1;
            found=current.items.emplace(logical,std::move(record)).first;
        }
        seen.insert(found->first);
        auto state=native.state; state.item=found->first; state.owner=owner; state.level=pose.level;
        state.anchor=coopnet::world_anchor(current.host.identity(),native.object); state.incarnation=native.incarnation;
        for (auto& value:state.position) value=std::round(value*100.f)/100.f;
        state.revision=found->second.state.revision;
        const auto changed=!coopnet::valid_item_state(found->second.state) || !found->second.state.world || coopnet::encode_item_state(state)!=coopnet::encode_item_state(found->second.state);
        if (changed || !found->second.state.level) {
            if (found->second.state.level) ++state.revision;
            found->second.state=state; current.host.publish_item(state);
        }
    }
    for (auto& record:current.items) if (record.second.state.world && record.second.state.present && !seen.count(record.first)) {
        auto& state=record.second.state; state.present=false; state.owner=0; ++state.revision; current.host.publish_item(state);
    }
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
            if (available) for (const auto& player:current.host.session().players()) if (player.id==it->first) {
                ActorConditionState condition; GuestInventoryState inventory;
                if (capture_actor_condition(guest.object,condition) && capture_guest_inventory(guest.object,inventory)) {
                    current.guest_conditions[player.character]=condition; current.guest_inventory[player.character]=std::move(inventory);
                    save_guest_state(current,player.character);
                }
            }
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
        if (current.world_probe && !current.host.baseline_received(player.id)) continue;
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
            load_guest_save(current,player.character);
            const auto saved=current.guest_conditions.find(player.character);
            if (saved!=current.guest_conditions.end()) {
                if (!apply_guest_condition(guest.object,saved->second)) throw std::runtime_error("Guest condition restoration failed");
                Msg("* CoopNet guest condition restored: character %llu health %.3f power %.3f radiation %.3f",
                    player.character,saved->second.health,saved->second.power,saved->second.radiation);
            }
            const auto inventory=current.guest_inventory.find(player.character);
            if (inventory!=current.guest_inventory.end()) {
                if (!restore_guest_inventory(guest.object,inventory->second)) throw std::runtime_error("Guest inventory restoration failed");
                Msg("* CoopNet guest inventory restored: character %llu items %u",player.character,
                    static_cast<unsigned>(inventory->second.items.size()));
            } else if ((!current.gameplay_probe || current.starter_probe) && !begin_guest_loadout(guest.object))
                throw std::runtime_error("Guest starter loadout creation failed");
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
        if (current.loot_probe && guest.drop_observed && guest.damage_sent) {
            if (!guest.world_loot_phase && prepare_world_loot_probe(guest.object,guest.world_loot)) {
                guest.world_loot_phase=1; Msg("* CoopNet world loot probe: persistent item created");
            }
            NativeSessionItem item;
            if (capture_session_item(guest.world_loot,item)) {
                if ((guest.world_loot_phase==1 || guest.world_loot_phase==3) && item.owner==guest.object && item.native_owner==guest.object && !world_loot_is_registered(guest.world_loot)) {
                    ++guest.world_loot_phase; Msg("* CoopNet world loot probe: guest ownership confirmed stage %u",guest.world_loot_phase);
                } else if (guest.world_loot_phase==2 && item.owner==0xffff && item.native_owner==0xffff && world_loot_is_registered(guest.world_loot)) {
                    guest.world_loot_phase=3; Msg("* CoopNet world loot probe: persistent drop confirmed");
                }
            }
        }
        if (current.weapon_probe && guest.drop_observed && guest.damage_sent) {
            if (guest.weapon==0xffff) guest.weapon=spawn_session_item(guest.object,"wpn_pm");
            NativeSessionItem native;
            if (guest.weapon!=0xffff && capture_session_item(guest.weapon,native)) {
                if (guest.weapon_phase==0 && native.owner==0xffff)
                    transact_session_item(guest.object,guest.weapon,native.incarnation,true);
                if (guest.weapon_phase==0 && native.owner==guest.object && native.native_owner==guest.object && equip_guest_weapon(guest.object,guest.weapon,3))
                    guest.weapon_phase=1;
                unsigned rounds=0; bool ready=false;
                if (guest.weapon_phase==1 && capture_guest_weapon(guest.object,guest.weapon,rounds,ready) && ready) {
                    const auto entity=current.entities.create();
                    if (!current.host.publish_item({entity,guest.entity,pose.level,1,true,"wpn_pm"})) throw std::runtime_error("Weapon probe publication failed");
                    guest.weapon_phase=2;
                    Msg("* CoopNet native weapon ready: guest %llu rounds %u",player.id,rounds);
                }
                if (guest.weapon_phase==2 && capture_guest_weapon(guest.object,guest.weapon,rounds,ready) && rounds<3) {
                    guest.weapon_phase=3;
                    Msg("* CoopNet native guest weapon fired: remaining rounds %u",rounds);
                }
            }
        }
        coopnet::ActorInput input;
        const bool active = current.host.latest_input(player.id,input);
        if (active) ++guest.inputs;
        control_guest_actor(guest.object,active && !current.party_loading && !(current.party_probe && current.party_probe_phase>0) ? input.buttons : 0,active ? input.yaw : pose.rotation[1],
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
        if (current.tick%10==0) {
            std::vector<NativeInventoryViewItem> native_items; std::uint16_t active=0xffff;
            if (capture_guest_inventory_view(guest.object,native_items,active)) {
                coopnet::InventoryView view; view.actor=guest.entity; view.generation=guest.generation; view.level=pose.level;
                view.revision=guest.inventory_revision+1;
                for (const auto& native:native_items) {
                    auto found=current.items.end();
                    for (auto it=current.items.begin();it!=current.items.end();++it)
                        if (it->second.object==native.object && it->second.incarnation==native.incarnation) { found=it; break; }
                    if (found==current.items.end()) {
                        const auto logical=current.entities.create();
                        Session::Item record; record.object=native.object; record.incarnation=native.incarnation;
                        record.state={logical,guest.entity,pose.level,1,true,native.state.section};
                        found=current.items.emplace(logical,std::move(record)).first;
                    }
                    auto state=native.state; state.item=found->first; state.revision=found->second.state.revision;
                    if (native.object==active) view.active=state.item;
                    view.items.push_back(std::move(state));
                }
                if (coopnet::valid_inventory_view(view)) {
                    std::vector<std::uint8_t> signature;
                    for (std::size_t offset=0;offset<view.items.size() || signature.empty();offset+=32) {
                        coopnet::InventoryViewChunk chunk{view,static_cast<std::uint16_t>(offset),static_cast<std::uint16_t>(view.items.size())};
                        chunk.view.revision=1; const auto end=(std::min)(view.items.size(),offset+32);
                        chunk.view.items.assign(view.items.begin()+offset,view.items.begin()+end);
                        const auto bytes=coopnet::encode_view_chunk(chunk); signature.insert(signature.end(),bytes.begin(),bytes.end());
                    }
                    if (signature!=guest.inventory_signature && current.host.publish_inventory_view(player.id,view)) {
                        guest.inventory_revision=view.revision; guest.inventory_signature=std::move(signature);
                    }
                }
            }
        }
        {
            ActorConditionState condition;
            if (capture_actor_condition(guest.object,condition)) {
                current.guest_conditions[player.character]=condition;
                current.host.publish_vitals({guest.entity,guest.generation,pose.level,current.tick,
                    condition.health,condition.power,condition.radiation});
                if (current.tick%25==0) {
                    GuestInventoryState inventory;
                    if (capture_guest_inventory(guest.object,inventory)) {
                        current.guest_inventory[player.character]=std::move(inventory);
                        save_guest_state(current,player.character);
                    }
                }
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
    if (current.gameplay_phase) {
        const auto found=current.client.items().find(current.probe_request.item);
        if (found!=current.client.items().end()) item=&found->second;
    } else for (const auto& entry : current.client.items()) if (!entry.second.world && entry.second.present && entry.second.level==actor.level &&
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
    if (current.weapon_probe && current.gameplay_phase>=3) {
        bool armed=false;
        for (const auto& item:current.client.items()) if (item.second.owner==input.entity && item.second.section=="wpn_pm") armed=true;
        if (armed && current.weapon_phase<2) {
            current.weapon_wait+=elapsed;
            input.buttons=0; input.pitch=-.7f;
            if (current.weapon_wait>.5 && current.weapon_wait<1) {
                input.buttons=coopnet::fire_button;
                if (current.weapon_phase==0) { current.weapon_phase=1; Msg("* CoopNet client weapon fire input: actor %llu",input.entity); }
            }
            if (current.weapon_wait>=1) current.weapon_phase=2;
        }
    }
    // Sending may disconnect and clear the replica registry; send after traversal.
    current.client.send_input(input);
}
}
void stop() {
    if (session) {
        if (session->mode==coopnet::Mode::Host) for (const auto& player:session->host.session().players()) {
            const auto guest=session->guests.find(player.id);
            if (guest==session->guests.end()) continue;
            try {
                ActorConditionState condition; GuestInventoryState inventory;
                if (capture_actor_condition(guest->second.object,condition) && capture_guest_inventory(guest->second.object,inventory)) {
                    session->guest_conditions[player.character]=condition; session->guest_inventory[player.character]=std::move(inventory);
                    save_guest_state(*session,player.character);
                }
            } catch (const std::exception& error) { Msg("! CoopNet guest shutdown save failed: %s",error.what()); }
        }
        if (session->movement_probe && session->mode == coopnet::Mode::Client)
            Msg("* CoopNet owned native snapshots applied: %u",session->corrections);
        if (session->gameplay_probe && session->mode==coopnet::Mode::Client)
            Msg("* CoopNet gameplay results: inventory accepts %u phase %u condition updates %u",
                session->inventory_accepts,session->gameplay_phase,session->condition_corrections);
        for (const auto& entry : session->guests)
            Msg("* CoopNet guest simulation removed: inputs %u distance %.3f",entry.second.inputs,entry.second.distance);
        if (session->world_probe) Msg("* CoopNet NPC state updates: %u",session->world_updates);
        end_world_replication(); clear_guest_actors(); clear_remote_actors(); session.reset(); Msg("* CoopNet session stopped");
    }
}
bool simulation_active() {
    return session && session->movement_probe && (session->mode == coopnet::Mode::Host ||
        session->client.session().state() == coopnet::ClientState::Connected);
}
bool shared_world_active() { return session && session->world_probe; }
bool party_level_change_allowed() {
    return !shared_world_active() || (session->mode==coopnet::Mode::Host && session->party_authorized);
}
bool party_controls_enabled() {
    if (!shared_world_active()) return true;
    return session->mode==coopnet::Mode::Host ? !session->party_loading :
        session->client.party_status().stage!=coopnet::PartyStage::Loading;
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
            capture_world_loot(*session);
            send_world_baselines(*session);
            capture_guests(*session);
            publish_world(*session);
            update_party(*session,elapsed);
            update_party_probe(*session,elapsed);
            if (session->replica_probe && session->host_actor) {
                const auto* actor = session->entities.find(session->host_actor);
                if (actor && actor->active) for (const auto& player : session->host.session().players()) {
                    if (player.id != 1 && player.connected && session->probe_assignments[player.id] != player.generation &&
                        (!session->world_probe || session->host.baseline_received(player.id)) &&
                        (!session->party_loading || actor->engine.level==session->party_status.destination) &&
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
            const auto connection_state=session->client.session().state();
            if (connection_state==coopnet::ClientState::Connected) {
                session->reconnect_wait=0; session->reconnect_delay=2;
            } else if (connection_state==coopnet::ClientState::Disconnected || connection_state==coopnet::ClientState::Offline) {
                session->reconnect_wait+=elapsed;
                if (session->reconnect_wait>=session->reconnect_delay && !session->endpoint.empty()) {
                    session->reconnect_wait=0; session->reconnect_delay=(std::min)(session->reconnect_delay*2,8.0);
                    const auto connection=session->runtime.connect(session->endpoint.c_str());
                    if (connection!=k_HSteamNetConnection_Invalid) {
                        auto transport=std::make_unique<coopnet::GnsTransport>(session->runtime,connection);
                        if (connection_state==coopnet::ClientState::Disconnected) session->client.reconnect(std::move(transport));
                        else session->client.start(std::move(transport),session->host_character,session->build);
                        session->world_load_requested=false;
                        Msg("* CoopNet connection retry: %s",connection_state==coopnet::ClientState::Disconnected ? "resume" : "initial join");
                    }
                }
            }
            if (session->world_probe && session->world_load_requested && !session->client.baseline_acknowledged() &&
                world_baseline_loaded(session->world_save.c_str())) {
                LocalActorPose local;
                if (capture_local_actor(local) && local.level==session->client.baseline().level && session->client.acknowledge_baseline())
                    Msg("* CoopNet canonical baseline loaded and acknowledged: %s level %u",session->world_save.c_str(),local.level);
            }
            if (session->replica_probe && session->client.assignment().ticket) {
                LocalActorPose local;
                if (capture_local_actor(local)) session->client.acknowledge_level(local.level);
            }
            present_client(*session, elapsed);
            update_local_inventory_view();
            update_world_items();
            if (session->loot_probe) exercise_local_world_loot_probe();
            if (session->inventory_probe) exercise_local_inventory_probe();
            if (session->client.session().state()==coopnet::ClientState::Connected) {
                if (!session->native_inventory_pending) session->native_inventory_pending=pop_local_inventory_action(session->native_inventory_action);
                if (session->native_inventory_pending) {
                    coopnet::ActorPresence owned;
                    session->client.actors().visit([&](const coopnet::ActorPresence& actor) {
                        if (actor.player==session->client.session().welcome().player) owned=actor;
                    });
                    const auto& action=session->native_inventory_action;
                    if (owned.entity) {
                        const coopnet::InventoryRequest request{owned.entity,action.item,owned.generation,owned.level,
                            session->inventory_sequence,action.revision,action.action,action.slot};
                        const auto result=session->client.send_inventory(request);
                        if (result!=coopnet::SendResult::Backpressure) {
                            session->native_inventory_pending=false; ++session->inventory_sequence;
                            Msg("* CoopNet guest inventory control sent: action %u result %u",static_cast<unsigned>(action.action),static_cast<unsigned>(result));
                        }
                    }
                }
            }
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
        if (!strcmp(name,"coop_weapon_probe")) {
            if (!session) throw std::runtime_error("Weapon probe requires a session");
            session->weapon_probe=true; Msg("* CoopNet native weapon probe enabled"); return;
        }
        if (!strcmp(name,"coop_loot_probe")) {
            if (!session) throw std::runtime_error("World loot probe requires a session");
            session->loot_probe=true; Msg("* CoopNet native world loot probe enabled"); return;
        }
        if (!strcmp(name,"coop_inventory_probe")) {
            if (!session || session->mode!=coopnet::Mode::Client) throw std::runtime_error("Inventory control probe requires a client");
            session->inventory_probe=true; Msg("* CoopNet native inventory control probe enabled"); return;
        }
        if (!strcmp(name,"coop_starter_probe")) {
            if (!session || session->mode!=coopnet::Mode::Host) throw std::runtime_error("Starter probe requires a host");
            session->starter_probe=true; Msg("* CoopNet native starter loadout probe enabled"); return;
        }
        if (!strcmp(name,"coop_party_probe")) {
            if (!session || session->mode!=coopnet::Mode::Host) throw std::runtime_error("Party probe requires a host");
            session->party_probe=true; Msg("* CoopNet party transition probe enabled"); return;
        }
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
        if (!strcmp(name,"coop_world_probe")) {
            if (!session) throw std::runtime_error("Start a session before the world probe");
            session->world_probe=true; session->replica_probe=true; session->movement_probe=true;
            begin_world_replication();
            begin_guest_simulation();
            Msg("* CoopNet canonical baseline probe enabled; continuous shared world replication pending");
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
        next->build=build; next->host_character=character; next->endpoint=endpoint;
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
            next->client.set_inventory_view_sink([](const coopnet::InventoryView& view) { queue_local_inventory_view(view); });
            next->client.set_item_sink([owner](const coopnet::ItemState& item) {
                if (owner->gameplay_probe && (item.item==owner->probe_request.item || (!item.world && item.section=="bandage")))
                    Msg("* CoopNet fixture item state: item %llu owner %llu revision %u world %u",item.item,item.owner,item.revision,static_cast<unsigned>(item.world));
                queue_world_item_state(owner->client.session().welcome().session,item);
            });
            next->client.set_baseline_progress_sink([](const coopnet::WorldBaseline& manifest,std::uint32_t received) {
                if (!(received%65536) || received==manifest.size)
                    Msg("* CoopNet canonical baseline receiving: id %llu bytes %u/%u",manifest.id,received,manifest.size);
            });
            next->client.set_party_sink([](const coopnet::PartyStatus& status) {
                display_party_status(static_cast<unsigned>(status.stage),status.present,status.required,status.destination);
                Msg("* CoopNet party travel: stage %u present %u required %u destination %u",static_cast<unsigned>(status.stage),status.present,status.required,status.destination);
            });
            next->client.set_baseline_sink([owner](const coopnet::WorldBaseline& manifest,const std::vector<std::uint8_t>& bytes) {
                if (!owner->world_probe) { Msg("! CoopNet canonical baseline requires world mode"); return false; }
                if (baseline_digest(bytes)!=manifest.digest) { Msg("! CoopNet canonical baseline checksum mismatch"); return false; }
                const auto name=baseline_name(manifest.id);
                if (!store_world_baseline(name.c_str(),bytes)) { Msg("! CoopNet canonical baseline storage failed"); return false; }
                if (!load_world_baseline(name.c_str())) { Msg("! CoopNet canonical baseline loading failed"); return false; }
                owner->world_save=name; owner->world_load_requested=true;
                Msg("* CoopNet canonical baseline SHA-256 verified: id %llu bytes %u",manifest.id,manifest.size);
                return true;
            });
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
                if (actor && actor->player==owner->client.session().welcome().player &&
                    apply_local_condition(vitals.level,{vitals.health,vitals.power,vitals.radiation})) {
                    ++owner->condition_corrections;
                    if (vitals.tick%25==0) Msg("* CoopNet authoritative guest health applied: %.3f",vitals.health);
                }
            });
            next->client.set_world_sink([owner](const coopnet::WorldState& state) {
                if (!owner->world_probe) return;
                unsigned applied=0;
                for (const auto& object:state.objects)
                    if (apply_world_object(owner->client.session().welcome().session,object.anchor,object.incarnation,
                        object.position.data(),object.rotation.data(),object.health)) ++applied;
                owner->world_updates+=applied;
                if (applied && owner->world_updates==applied)
                    Msg("* CoopNet authoritative NPC states applied: objects %u level %u",applied,state.level);
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
        session->world_probe=true; session->replica_probe=true; session->movement_probe=true;
        begin_world_replication(); begin_guest_simulation();
        Msg("* CoopNet shared host world started; party travels together");
    } catch (const std::exception& error) { Msg("! %s", error.what()); }
}
}
#else
namespace engine_coopnet {
void update(double) {}
void stop() {}
bool simulation_active() { return false; }
bool shared_world_active() { return false; }
bool party_level_change_allowed() { return true; }
bool party_controls_enabled() { return true; }
void command(const char*, const char*) { Msg("! CoopNet unavailable: build with -CoopNet after setup-coopnet-deps.ps1"); }
}
#endif
