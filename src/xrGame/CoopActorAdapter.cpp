#include "stdafx.h"
#include "Actor.h"
#include "ActorCondition.h"
#include "Level.h"
#include "ai_space.h"
#include "level_graph.h"
#include "ai_object_location.h"
#include "CharacterPhysicsSupport.h"
#include "xrServer.h"
#include "xrServer_Objects_ALife_Monsters.h"
#include "xrServer_Objects_ALife_Items.h"
#include "inventory_item.h"
#include "Inventory.h"
#include "xr_level_controller.h"
#include "Weapon.h"
#include "WeaponAmmo.h"
#include "../CoopNet/ActorInput.h"
#include "Hit.h"
#include "xrMessages.h"
#include "../xrPhysics/iphworld.h"
#include "../xrPhysics/physicscommon.h"
#include "../CoopNet/EngineActorBridge.h"
#include "../CoopNet/EngineWorldBridge.h"
#include "../CoopNet/WorldState.h"
#include "../CoopNet/GuestSave.h"
#include "entity_alive.h"
#include "level_changer.h"
#include "UIGameCustom.h"
#include "ui/UIMessagesWindow.h"
#include "alife_simulator.h"
#include "alife_object_registry.h"
#include "saved_game_wrapper.h"
#include "game_sv_single.h"
#include "PhysicsShellHolder.h"
#include "PHMovementControl.h"
#include "../xrPhysics/PhysicsShell.h"
#include "../Include/xrRender/KinematicsAnimated.h"
#include <cstring>
#include <deque>
#include "../xrEngine/CoopNetRuntime.h"
#include "../xrEngine/Environment.h"
#include "../xrEngine/xr_IOConsole.h"
#include "script_engine.h"
#include "lua.hpp"
#include "alife_time_manager.h"
#include "game_cl_single.h"
extern string_path g_last_saved_game;
namespace engine_coopnet {
namespace {
const char* options_hook=
#include "CoopNetOptions.inc"
;
std::uint32_t host_rules_revision=0;
std::vector<coopnet::WorldRule> host_rules;
bool settings_probe_done=false;
int saved_world_difficulty=-1;
int lua_settings_locked(lua_State* state) { lua_pushboolean(state,guest_settings_locked()); return 1; }
int lua_world_path(lua_State* state) { const char* path=lua_tostring(state,1); lua_pushboolean(state,path && coopnet::world_rule_path(path)); return 1; }
int lua_world_command(lua_State* state) { const char* command=lua_tostring(state,1); if (command) register_world_setting_command(command); return 0; }
bool options_call(lua_State* state,const char* function,int arguments,int results) {
    if (lua_pcall(state,arguments,results,0)==0) return true;
    Msg("! CoopNet options %s failed: %s",function,lua_tostring(state,-1)); lua_pop(state,1); return false;
}
bool ensure_options_hook(lua_State*& state) {
    if (!g_ai_space) return false; state=ai().script_engine().lua(); if (!state) return false;
    lua_getglobal(state,"coopnet_options"); const bool installed=lua_istable(state,-1); lua_pop(state,1);
    if (installed) return true;
    lua_pushcfunction(state,lua_settings_locked); lua_setglobal(state,"coopnet_world_settings_locked");
    lua_pushcfunction(state,lua_world_path); lua_setglobal(state,"coopnet_world_rule_path");
    lua_pushcfunction(state,lua_world_command); lua_setglobal(state,"coopnet_register_world_command");
    if (luaL_loadbuffer(state,options_hook,strlen(options_hook),"@coopnet_options")!=0) {
        Msg("! CoopNet options hook compile failed: %s",lua_tostring(state,-1)); lua_pop(state,1); return false;
    }
    if (!options_call(state,"install",0,0)) { lua_pushnil(state); lua_setglobal(state,"coopnet_options"); return false; }
    return true;
}
void options_function(lua_State* state,const char* name) {
    lua_getglobal(state,"coopnet_options"); lua_getfield(state,-1,name); lua_remove(state,-2);
}
struct WorldObject { std::uint64_t incarnation=0; bool replica=false, animated=false; std::uint64_t authority=0; };
xr_map<const CGameObject*,WorldObject> world_objects;
std::uint64_t world_incarnation=0, replica_frames=0, replica_schedules=0;
unsigned world_replica_count=0;
bool collect_world_objects=false;
u16 replica_local_root=0xffff;
std::string replica_world_save;
bool safe_baseline_name(const char* name) {
    if (!name || strncmp(name,"coopnet-",8) || strlen(name)>64) return false;
    for (const char* c=name;*c;++c) if (!((*c>='a' && *c<='z') || (*c>='0' && *c<='9') || *c=='-')) return false;
    return true;
}
}
bool read_join_profile_file(std::vector<std::uint8_t>& bytes) {
    string_path path; FS.update_path(path,"$app_data_root$","coopnet-connections.dat");
    auto* reader=FS.r_open(path); if (!reader) return false;
    const auto size=reader->length();
    if (!size || size>8192) { FS.r_close(reader); return false; }
    const auto* begin=static_cast<const std::uint8_t*>(reader->pointer()); bytes.assign(begin,begin+size);
    FS.r_close(reader); return true;
}
bool write_join_profile_file(const std::vector<std::uint8_t>& bytes) {
    if (bytes.empty() || bytes.size()>8192) return false;
    string_path path,partial; FS.update_path(path,"$app_data_root$","coopnet-connections.dat");
    FS.update_path(partial,"$app_data_root$","coopnet-connections.tmp");
    auto* writer=FS.w_open(partial); if (!writer) return false;
    writer->w(bytes.data(),static_cast<u32>(bytes.size())); FS.w_close(writer);
    return MoveFileExA(partial,path,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE;
}
void export_settings_audit() {
    for (const char* name:{"ui_main_menu.script","ui_options.script","axr_main.script"}) {
        string_path source,target; FS.update_path(source,"$game_scripts$",name); FS.update_path(target,"$app_data_root$",name);
        FS.file_copy(source,target);
    }
    Msg("* CoopNet settings source audit exported to isolated appdata");
}
bool capture_world_rules(std::vector<coopnet::WorldRule>& rules) {
    if (!g_pGameLevel || !g_pGameLevel->bReady || world_level_is_replica()) return false;
    lua_State* state=nullptr; if (!ensure_options_hook(state)) return false;
    const int top=lua_gettop(state); options_function(state,"capture");
    if (!options_call(state,"capture",0,1)) { lua_settop(state,top); return false; }
    if (!lua_istable(state,-1) || lua_objlen(state,-1)>4096) { Msg("! CoopNet options capture invalid result: type %d count %u",lua_type(state,-1),static_cast<unsigned>(lua_objlen(state,-1))); lua_settop(state,top); return false; }
    std::vector<coopnet::WorldRule> captured;
    const auto count=lua_objlen(state,-1);
    for (std::size_t n=1;n<=count;++n) {
        lua_rawgeti(state,-1,static_cast<int>(n)); coopnet::WorldRule rule;
        lua_getfield(state,-1,"name"); const char* name=lua_tostring(state,-1); if (name) rule.name=name; lua_pop(state,1);
        lua_getfield(state,-1,"value");
        if (lua_isboolean(state,-1)) { rule.type=1; rule.value=lua_toboolean(state,-1) ? "true" : "false"; }
        else if (lua_type(state,-1)==LUA_TNUMBER) { rule.type=2; char text[64]; snprintf(text,sizeof(text),"%.17g",lua_tonumber(state,-1)); rule.value=text; }
        else { const char* value=lua_tostring(state,-1); if (value) rule.value=value; }
        lua_pop(state,2);
        if (!coopnet::valid_world_rule(rule)) { Msg("! CoopNet unsupported world rule: %s",rule.name.c_str()); lua_settop(state,top); return false; }
        captured.push_back(std::move(rule));
    }
    lua_settop(state,top); rules=std::move(captured); return true;
}
void queue_host_world_rules(std::uint32_t revision,const std::vector<coopnet::WorldRule>& rules) {
    if (saved_world_difficulty<0) saved_world_difficulty=static_cast<int>(g_SingleGameDifficulty);
    host_rules_revision=revision; host_rules=rules;
    Msg("* CoopNet host world rules received: revision %u count %u",revision,static_cast<unsigned>(rules.size()));
}
void update_host_world_rules() {
    if (!guest_settings_locked() || !host_rules_revision) return;
    lua_State* state=nullptr; if (!ensure_options_hook(state)) return;
    const int top=lua_gettop(state); lua_getglobal(state,"coopnet_options"); lua_getfield(state,-1,"revision");
    const auto applied=static_cast<std::uint32_t>(lua_tonumber(state,-1)); lua_settop(state,top);
    if (applied==host_rules_revision) return;
    options_function(state,"apply"); lua_pushnumber(state,host_rules_revision); lua_createtable(state,static_cast<int>(host_rules.size()),0);
    for (std::size_t i=0;i<host_rules.size();++i) {
        const auto& rule=host_rules[i]; lua_createtable(state,0,2);
        lua_pushstring(state,rule.name.c_str()); lua_setfield(state,-2,"name");
        if (rule.type==1) lua_pushboolean(state,rule.value=="true");
        else if (rule.type==2) lua_pushnumber(state,std::strtod(rule.value.c_str(),nullptr));
        else lua_pushstring(state,rule.value.c_str());
        lua_setfield(state,-2,"value"); lua_rawseti(state,-2,static_cast<int>(i+1));
    }
    applying_host_settings(true); const bool called=options_call(state,"apply",2,1); applying_host_settings(false);
    if (called && lua_toboolean(state,-1)) Msg("* CoopNet host world rules applied: revision %u count %u",host_rules_revision,static_cast<unsigned>(host_rules.size()));
    else Msg("! CoopNet host world rules could not be applied");
    lua_settop(state,top);
}
void clear_host_world_rules() {
    if (host_rules_revision && g_ai_space) {
        auto* state=ai().script_engine().lua(); const int top=lua_gettop(state);
        lua_getglobal(state,"coopnet_options"); const bool installed=lua_istable(state,-1); lua_settop(state,top);
        if (installed) { options_function(state,"clear"); applying_host_settings(true); options_call(state,"clear",0,0); applying_host_settings(false); lua_settop(state,top); }
    }
    host_rules.clear(); host_rules_revision=0; settings_probe_done=false;
    if (saved_world_difficulty>=0) g_SingleGameDifficulty=static_cast<ESingleGameDifficulty>(saved_world_difficulty);
    saved_world_difficulty=-1;
}
bool capture_world_clock(coopnet::WorldClock& clock) {
    LocalActorPose pose; if (!capture_local_actor(pose) || world_level_is_replica() || !g_pGamePersistent) return false;
    auto& environment=g_pGamePersistent->Environment(); if (!environment.CurrentCycleName.size()) return false;
    clock.level=pose.level; clock.game_time=Level().GetGameTime(); clock.time_factor=Level().GetGameTimeFactor();
    clock.difficulty=static_cast<std::uint8_t>(g_SingleGameDifficulty); clock.cycle=*environment.CurrentCycleName;
    if (environment.bWFX && environment.CurrentWeatherName.size()) { clock.fx=*environment.CurrentWeatherName; clock.fx_remaining=(std::max)(0.f,environment.wfx_time); }
    return coopnet::valid_world_clock(clock);
}
bool apply_host_world_clock(const coopnet::WorldClock& clock) {
    LocalActorPose pose; if (!world_level_is_replica() || !capture_local_actor(pose) || pose.level!=clock.level || !g_pGamePersistent || !ai().get_alife()) return false;
    auto& environment=g_pGamePersistent->Environment();
    if (environment.WeatherCycles.find(clock.cycle.c_str())==environment.WeatherCycles.end() ||
        (!clock.fx.empty() && environment.WeatherFXs.find(clock.fx.c_str())==environment.WeatherFXs.end())) return false;
    const_cast<CALifeTimeManager&>(ai().alife().time_manager()).set_replica_time(clock.game_time,clock.time_factor);
    if (static_cast<unsigned>(g_SingleGameDifficulty)!=clock.difficulty) {
        g_SingleGameDifficulty=static_cast<ESingleGameDifficulty>(clock.difficulty);
        if (auto* game=smart_cast<game_cl_Single*>(Level().game)) game->OnDifficultyChanged();
    }
    environment.m_paused=false;
    environment.SetGameTime(static_cast<float>(clock.game_time%86400000)/1000.f,clock.time_factor);
    if (xr_strcmp(*environment.CurrentCycleName,clock.cycle.c_str())) { if (environment.bWFX) environment.StopWFX(); environment.SetWeather(clock.cycle.c_str(),true); }
    if (clock.fx.empty()) { if (environment.bWFX) environment.StopWFX(); }
    else if (!environment.bWFX || xr_strcmp(*environment.CurrentWeatherName,clock.fx.c_str())) environment.StartWeatherFXFromTime(clock.fx.c_str(),clock.fx_remaining);
    else environment.wfx_time=clock.fx_remaining;
    return true;
}
void exercise_world_settings_probe() {
    if (!g_pGameLevel || !g_pGameLevel->bReady) return;
    if (!guest_settings_locked()) {
        if (!fsimilar(Level().GetGameTimeFactor(),7.f)) Level().Server->game->SetGameTimeFactor(7.f);
        if (!settings_probe_done) Msg("* CoopNet settings probe: host time factor 7");
        settings_probe_done=true; return;
    }
    if (settings_probe_done) return;
    if (!host_rules_revision || !world_level_is_replica() || !fsimilar(Level().GetGameTimeFactor(),7.f)) return;
    const auto difficulty=g_SingleGameDifficulty;
    Console->Execute("al_time_factor 99"); Console->Execute("g_game_difficulty novice");
    if (!fsimilar(Level().GetGameTimeFactor(),7.f) || difficulty!=g_SingleGameDifficulty) return;
    lua_State* state=nullptr; if (!ensure_options_hook(state)) return;
    const int top=lua_gettop(state); options_function(state,"probe"); const bool called=options_call(state,"probe",0,1);
    const bool valid=called && lua_toboolean(state,-1); lua_settop(state,top);
    if (!valid) return;
    settings_probe_done=true; Msg("* CoopNet settings probe: guest world commands and scripted writes denied; host factor 7 retained");
}
void begin_world_replication() {
    collect_world_objects=true;
    if (!g_pGameLevel || !g_pGameLevel->bReady) return;
    for (u32 index=0;index<Level().Objects.o_count();++index) {
        auto* object=smart_cast<CGameObject*>(Level().Objects.o_get_by_iterator(index));
        if (object && !world_objects.count(object)) world_objects.emplace(object,WorldObject{++world_incarnation,false,false});
    }
}
void end_world_replication() { collect_world_objects=false; }
bool capture_party_exit(const std::vector<std::uint16_t>& actors,NativePartyExit& exit) {
    if (!g_pGameLevel || !g_pGameLevel->bReady || world_level_is_replica()) return false;
    exit={};
    for (auto* changer:g_lchangers) {
        const auto destination=changer->coopnet_destination();
        if (!destination || changer->getDestroy()) continue;
        unsigned present=0;
        for (auto id:actors) if (id!=0xffff) {
            auto* actor=smart_cast<CActor*>(Level().Objects.net_Find(id));
            if (actor && !actor->is_coopnet_downed() && actor->g_Alive() && changer->coopnet_contains(actor)) ++present;
        }
        if (present>exit.present) exit={changer->ID(),destination,present};
    }
    return exit.present!=0;
}
bool perform_party_transition(std::uint16_t exit) {
    if (!g_pGameLevel || !g_pGameLevel->bReady || world_level_is_replica()) return false;
    auto* changer=smart_cast<CLevelChanger*>(Level().Objects.net_Find(exit));
    return changer && !changer->getDestroy() && changer->coopnet_transition();
}
bool prepare_party_probe(std::uint16_t& exit,float* origin) {
    LocalActorPose local; if (!capture_local_actor(local) || world_level_is_replica()) return false;
    for (unsigned axis=0;axis<3;++axis) origin[axis]=local.position[axis];
    float best=FLT_MAX; exit=0xffff;
    for (auto* changer:g_lchangers) if (changer->coopnet_destination() && changer->coopnet_destination()!=local.level) {
        Fvector center; changer->Center(center); const auto distance=center.distance_to_sqr(g_actor->Position());
        if (distance<best) { best=distance; exit=changer->ID(); }
    }
    return exit!=0xffff;
}
bool position_party_probe(std::uint16_t actor_id,std::uint16_t exit,const float* origin,bool at_exit) {
    if (!g_pGameLevel || !g_pGameLevel->bReady || world_level_is_replica()) return false;
    auto* actor=smart_cast<CActor*>(Level().Objects.net_Find(actor_id));
    auto* changer=smart_cast<CLevelChanger*>(Level().Objects.net_Find(exit));
    if (!actor || !changer) return false;
    Fmatrix transform=actor->XFORM();
    if (at_exit) changer->Center(transform.c); else transform.c.set(origin[0],origin[1],origin[2]);
    actor->ForceTransform(transform); return true;
}
void display_party_status(unsigned stage,unsigned present,unsigned required,std::uint32_t) {
    if (!g_pGameLevel || !g_pGameLevel->bReady || !CurrentGameUI() || !CurrentGameUI()->m_pMessagesWnd) return;
    string256 text;
    if (stage==1) xr_sprintf(text,"Co-op travel: %u/%u players at the exit. Gather here to travel together.",present,required);
    else if (stage==2) xr_strcpy(text,"Co-op travel: loading the next location. Waiting for the party.");
    else if (stage==3) xr_strcpy(text,"Co-op travel: everyone has arrived.");
    else if (stage==4) xr_strcpy(text,"Co-op travel failed. Check the host connection.");
    else return;
    CurrentGameUI()->m_pMessagesWnd->AddLogMessage(shared_str(text));
}
bool capture_world_objects(std::uint32_t& level,std::vector<NativeWorldPose>& objects) {
    LocalActorPose local;
    if (!capture_local_actor(local) || world_level_is_replica()) return false;
    level=local.level; objects.clear();
    for (const auto& record:world_objects) {
        auto* object=const_cast<CGameObject*>(record.first);
        auto* entity=smart_cast<CEntityAlive*>(object);
        if (!entity || object->cast_actor() || object->getDestroy()) continue;
        NativeWorldPose pose; pose.object=object->ID(); pose.incarnation=record.second.incarnation;
        for (unsigned axis=0;axis<3;++axis) pose.position[axis]=object->Position()[axis];
        object->XFORM().getHPB(pose.rotation[0],pose.rotation[1],pose.rotation[2]);
        pose.health=entity->GetfHealth(); clamp(pose.health,-1.f,1.f); objects.push_back(pose);
        if (objects.size()>=4096) break;
    }
    return true;
}
bool apply_world_object(std::uint64_t session_id,std::uint64_t anchor,std::uint64_t incarnation,
    const float* position,const float* rotation,float health) {
    if (!world_level_is_replica() || !g_pGameLevel->bReady) return false;
    for (auto& record:world_objects) {
        auto* object=const_cast<CGameObject*>(record.first);
        if (!record.second.replica || object->getDestroy() || coopnet::world_anchor(session_id,object->ID())!=anchor) continue;
        auto* entity=smart_cast<CEntityAlive*>(object);
        if (!entity || object->cast_actor() || (record.second.authority && record.second.authority!=incarnation)) return false;
        record.second.authority=incarnation;
        object->XFORM().setHPB(rotation[0],rotation[1],rotation[2]); object->Position().set(position[0],position[1],position[2]);
        if (auto* support=entity->character_physics_support()) if (support->movement()) {
            support->movement()->SetPosition(object->Position()); support->movement()->DisableCharacter();
        }
        entity->SetfHealth(health); return true;
    }
    return false;
}
bool world_level_is_replica() {
    if (replica_world_save.empty() || !g_pGameLevel || !Level().Server) return false;
    const auto& options=Level().Server->GetConnectOptions();
    const auto length=replica_world_save.size();
    return options.size()>length && !strncmp(options.c_str(),replica_world_save.c_str(),length) && options.c_str()[length]=='/';
}
void world_object_spawned(CGameObject* object,const CSE_Abstract* source) {
    if (!collect_world_objects && replica_world_save.empty()) return;
    bool replica=world_level_is_replica();
    if (replica) {
        const auto* actor=smart_cast<const CSE_ALifeCreatureActor*>(source);
        if (actor && source->s_flags.is(M_SPAWN_OBJECT_ASPLAYER)) { replica_local_root=source->ID; replica=false; }
        else if (source->ID_Parent==replica_local_root && replica_local_root!=0xffff) replica=false;
    }
    const auto old=world_objects.find(object);
    if (old!=world_objects.end() && old->second.replica) --world_replica_count;
    world_objects[object]={++world_incarnation,replica,false};
    if (replica) ++world_replica_count;
}
void world_object_destroyed(CGameObject* object) {
    const auto found=world_objects.find(object);
    if (found==world_objects.end()) return;
    if (found->second.replica) --world_replica_count;
    world_objects.erase(found);
}
bool world_replica_object(const CGameObject* object) {
    if (!world_replica_count) return false;
    const auto found=world_objects.find(object);
    return found!=world_objects.end() && found->second.replica;
}
bool update_world_replica(CObject* base) {
    if (!world_replica_count) return false;
    auto* object=smart_cast<CGameObject*>(base);
    if (!object) return false;
    const auto found=world_objects.find(object);
    if (found==world_objects.end() || !found->second.replica) return false;
    if (auto* physical=object->cast_physics_shell_holder()) {
        if (auto* support=physical->character_physics_support()) {
            if (support->movement()) support->movement()->DisableCharacter();
        }
        if (physical->PPhysicsShell()) physical->PPhysicsShell()->Disable();
    }
    base->CObject::UpdateCL();
    if (object->Visual()) {
        if (auto* animated=object->Visual()->dcast_PKinematicsAnimated()) {
            if (!found->second.animated) {
                for (const auto* name:{"norm_idle_0","norm_torso_0_aim_0","head_idle_0","stand_idle_0"}) {
                    const auto motion=animated->ID_Cycle_Safe(name);
                    if (motion.valid()) animated->PlayCycle(motion,TRUE);
                }
                found->second.animated=true;
            }
            animated->UpdateTracks();
        }
        if (auto* skeleton=object->Visual()->dcast_PKinematics()) skeleton->CalculateBones(TRUE);
    }
    ++replica_frames; return true;
}
bool schedule_world_replica(ISheduled* scheduled,std::uint32_t elapsed) {
    if (!world_replica_count) return false;
    auto* object=smart_cast<CGameObject*>(scheduled);
    if (!object || !world_replica_object(object)) return false;
    object->CObject::shedule_Update(elapsed);
    ++replica_schedules; return true;
}
void world_level_stopped() {
    if (replica_frames || replica_schedules)
        Msg("* CoopNet passive world stopped: frame updates %llu scheduled updates %llu",replica_frames,replica_schedules);
    if (world_level_is_replica()) replica_world_save.clear();
    world_objects.clear(); world_replica_count=0; replica_local_root=0xffff; replica_frames=0; replica_schedules=0;
}
namespace {
std::uint64_t local_incarnation = 0;
LocalActorControls local_controls;
std::uint16_t local_weapon_buttons=0;
std::uint32_t controls_time = 0;
struct GuestSpawn { bool pending = true, removing = false; std::uint64_t incarnation = 0; unsigned controls = 0; std::uint16_t weapon_buttons=0;
    bool restoring=false; std::uint16_t restore_slot=0xffff; unsigned restore_count=0;
    bool starter_pending=false; xr_vector<u16> starter_items; };
xr_map<u16,GuestSpawn> guests;
std::uint64_t guest_incarnation = 0;
struct SessionItem { std::uint64_t incarnation=0; bool removing=false, enters_world=false; };
xr_map<u16,SessionItem> session_items;
std::uint64_t item_incarnation=0;
coopnet::InventoryView local_inventory_view;
xr_map<std::uint64_t,u16> local_inventory_items;
std::uint64_t inventory_local_incarnation=0;
bool inventory_cleared=false;
std::uint32_t inventory_reported=0;
std::deque<LocalInventoryAction> local_inventory_actions;
unsigned inventory_probe_phase=0;
unsigned loot_probe_phase=0;
coopnet::Identity loot_probe_item=0;
xr_map<coopnet::Identity,coopnet::ItemState> local_world_items;
xr_map<coopnet::Identity,u16> local_world_objects;
std::uint64_t local_world_session=0;
std::set<coopnet::Identity> retired_world_items;
}
bool capture_world_baseline(const char* name,std::uint32_t& level,std::vector<std::uint8_t>& bytes) {
    LocalActorPose pose;
    if (!safe_baseline_name(name) || !capture_local_actor(pose) || !Level().Server || !ai().get_alife() ||
        CSavedGameWrapper::saved_game_exist(name)) return false;
    auto* game=smart_cast<game_sv_Single*>(Level().Server->game);
    if (!game) return false;
    // Use the native save preparation path to synchronize current object state first.
    // Preserve the user's selected save name and last-save UI state.
    string_path previous; xr_strcpy(previous,g_last_saved_game);
    NET_Packet packet; packet.B.count=0; packet.r_pos=0; packet.w_stringZ(name); packet.w_u8(0);
    game->alife().save(packet);
    xr_strcpy(g_last_saved_game,previous);
    string_path path; CSavedGameWrapper::saved_game_full_name(name,path);
    IReader* reader=FS.r_open(path);
    if (!reader) return false;
    const auto size=reader->length();
    if (size<12 || size>64*1024*1024) { FS.r_close(reader); return false; }
    const auto* begin=static_cast<const std::uint8_t*>(reader->pointer());
    bytes.assign(begin,begin+size); FS.r_close(reader);
    level=pose.level;
    Msg("* CoopNet canonical baseline captured: %s bytes %u level %u",name,size,level);
    return true;
}
bool store_world_baseline(const char* name,const std::vector<std::uint8_t>& bytes) {
    if (!safe_baseline_name(name) || bytes.size()<12 || bytes.size()>64*1024*1024 ||
        CSavedGameWrapper::saved_game_exist(name)) return false;
    std::uint32_t header[3]; std::memcpy(header,bytes.data(),sizeof(header));
    if (header[0]!=UINT32_MAX || header[1]!=ALIFE_VERSION || header[2]<12 || header[2]>512*1024*1024) return false;
    string_path path,partial;
    CSavedGameWrapper::saved_game_full_name(name,path); strconcat(sizeof(partial),partial,path,".partial");
    if (FS.exist(partial)) return false;
    IWriter* writer=FS.w_open(partial);
    if (!writer) return false;
    writer->w(bytes.data(),static_cast<u32>(bytes.size())); FS.w_close(writer);
    FS.file_rename(partial,path,false);
    if (!FS.exist(path)) { FS.file_delete(partial); return false; }
    Msg("* CoopNet canonical baseline stored: %s bytes %u",name,static_cast<unsigned>(bytes.size()));
    return true;
}
bool load_world_baseline(const char* name) {
    if (!safe_baseline_name(name) || !CSavedGameWrapper::valid_saved_game(name)) return false;
    string128 server; strconcat(sizeof(server),server,name,"/single/alife/load");
    replica_world_save=name;
    if (g_pGameLevel) Engine.Event.Defer("KERNEL:disconnect");
    Engine.Event.Defer("KERNEL:start",u64(xr_strdup(server)),u64(xr_strdup("localhost")));
    Msg("* CoopNet canonical baseline load queued: %s",name);
    return true;
}
bool world_baseline_loaded(const char* name) {
    LocalActorPose pose;
    if (!safe_baseline_name(name) || !capture_local_actor(pose) || !Level().Server) return false;
    const auto& options=Level().Server->GetConnectOptions();
    const auto length=strlen(name);
    return options.size()>length && !strncmp(options.c_str(),name,length) && options.c_str()[length]=='/';
}
bool local_actor_downed() { return g_actor && g_actor->is_coopnet_downed(); }
bool down_actor(std::uint16_t object) {
    if (!g_pGameLevel || !g_pGameLevel->bReady) return false;
    auto* actor=smart_cast<CActor*>(Level().Objects.net_Find(object));
    if (!actor || actor->getDestroy() || (actor!=g_actor && !actor->is_coopnet_guest())) return false;
    actor->conditions().SetHealth(0);
    actor->KillEntity(actor->ID(),TRUE);
    return actor->is_coopnet_downed();
}
bool respawn_actor(std::uint16_t object,std::uint32_t level,const float* position) {
    LocalActorPose local;
    if (!position || !capture_local_actor(local) || local.level!=level) return false;
    for (unsigned axis=0;axis<3;++axis) if (!std::isfinite(position[axis]) || std::abs(position[axis])>1000000) return false;
    auto* actor=smart_cast<CActor*>(Level().Objects.net_Find(object));
    if (!actor || actor->getDestroy() || !actor->is_coopnet_downed() || (actor!=g_actor && !actor->is_coopnet_guest())) return false;
    Fvector target; target.set(position[0],position[1],position[2]);
    const auto node=ai().level_graph().vertex(actor->ai_location().level_vertex_id(),target);
    if (!ai().level_graph().valid_vertex_id(node)) return false;
    actor->coopnet_revive(target); return true;
}
bool capture_actor_condition(std::uint16_t object, ActorConditionState& state) {
    if (!g_pGameLevel || !g_pGameLevel->bReady) return false;
    CActor* actor=smart_cast<CActor*>(Level().Objects.net_Find(object));
    if (!actor || actor->getDestroy()) return false;
    state={actor->is_coopnet_downed() ? 0.f : actor->GetfHealth(),actor->conditions().GetPower(),actor->conditions().GetRadiation()};
    clamp(state.health,-1.f,1.f); clamp(state.power,-1.f,1.f); clamp(state.radiation,0.f,1.f); return true;
}
bool apply_local_condition(std::uint32_t level, const ActorConditionState& state) {
    LocalActorPose pose;
    if (!capture_local_actor(pose) || pose.level!=level) return false;
    if (g_actor->is_coopnet_downed() && state.health>0) return false;
    if (state.health<=0) g_actor->coopnet_down();
    else g_actor->conditions().SetHealth(state.health);
    g_actor->conditions().SetPower(state.power);
    g_actor->conditions().SetRadiation(state.radiation);
    return true;
}
bool apply_guest_condition(std::uint16_t object,const ActorConditionState& state) {
    LocalActorPose pose;
    if (!capture_guest_actor(object,pose) || !std::isfinite(state.health) || state.health>1 || state.health< -1 ||
        !std::isfinite(state.power) || state.power>1 || state.power< -1 ||
        !std::isfinite(state.radiation) || state.radiation>1 || state.radiation<0) return false;
    auto* actor=smart_cast<CActor*>(Level().Objects.net_Find(object));
    if (actor->is_coopnet_downed() && state.health>0) return false;
    if (state.health<=0) actor->coopnet_down(); else actor->conditions().SetHealth(state.health);
    actor->conditions().SetPower(state.power);
    actor->conditions().SetRadiation(state.radiation); return true;
}
std::uint64_t guest_save_scope() {
    if (!g_pGameLevel || !g_pGameLevel->bReady || !Level().Server || world_level_is_replica()) return 0;
    const auto& options=Level().Server->GetConnectOptions();
    if (!options.size()) return 0;
    std::uint64_t hash=14695981039346656037ull;
    for (const char* text=options.c_str();*text && *text!='/';++text) {
        hash^=static_cast<unsigned char>(*text); hash*=1099511628211ull;
    }
    hash^=ALIFE_VERSION; hash*=1099511628211ull;
    return hash ? hash : 1;
}
bool read_guest_save_file(const char* name,std::vector<std::uint8_t>& bytes) {
    if (!safe_baseline_name(name) || strncmp(name,"coopnet-character-",18) || world_level_is_replica()) return false;
    string_path path; FS.update_path(path,"$game_saves$",name);
    IReader* reader=FS.r_open(path); if (!reader) return false;
    const auto size=reader->length();
    if (size<92 || size>max_guest_save+32) { FS.r_close(reader); return false; }
    const auto* begin=static_cast<const std::uint8_t*>(reader->pointer());
    bytes.assign(begin,begin+size); FS.r_close(reader); return true;
}
bool write_guest_save_file(const char* name,const std::vector<std::uint8_t>& bytes) {
    if (!safe_baseline_name(name) || strncmp(name,"coopnet-character-",18) || world_level_is_replica() ||
        bytes.size()<92 || bytes.size()>max_guest_save+32) return false;
    string_path path; FS.update_path(path,"$game_saves$",name);
    IWriter* writer=FS.w_open(path); if (!writer) return false;
    writer->w(bytes.data(),static_cast<u32>(bytes.size())); FS.w_close(writer);
    std::vector<std::uint8_t> verify;
    return read_guest_save_file(name,verify) && verify==bytes;
}
std::uint16_t spawn_session_item(std::uint16_t owner, const char* section) {
    LocalActorPose pose;
    if (!capture_guest_actor(owner,pose) || !pSettings->section_exist(section) || session_items.size()>=768) return 0xffff;
    CActor* actor=smart_cast<CActor*>(Level().Objects.net_Find(owner));
    Fvector position=actor->Position(); position.y+=.15f;
    CSE_Abstract* abstract=Level().spawn_item(section,position,actor->ai_location().level_vertex_id(),0xffff,true);
    if (!smart_cast<CSE_ALifeInventoryItem*>(abstract)) { F_entity_Destroy(abstract); return 0xffff; }
    abstract->m_bALifeControl=false;
    NET_Packet packet; abstract->Spawn_Write(packet,TRUE); u16 type; packet.r_begin(type);
    CSE_Abstract* created=Level().Server->Process_spawn(packet,Level().Server->GetServerClient()->ID,FALSE,nullptr,true);
    F_entity_Destroy(abstract);
    if (!created) return 0xffff;
    session_items.emplace(created->ID,SessionItem{++item_incarnation,false}); return created->ID;
}
bool is_session_item(std::uint16_t item) { return session_items.find(item)!=session_items.end(); }
bool session_item_enters_world(std::uint16_t item) {
    const auto found=session_items.find(item); return found!=session_items.end() && found->second.enters_world;
}
bool capture_world_items(std::vector<NativeWorldItem>& items) {
    LocalActorPose pose; if (!capture_local_actor(pose) || world_level_is_replica()) return false;
    items.clear();
    for (const auto& record:world_objects) {
        auto* object=const_cast<CGameObject*>(record.first);
        auto* item=smart_cast<CInventoryItem*>(object);
        auto* server=Level().Server->ID_to_entity(object->ID());
        auto* persistent=smart_cast<CSE_ALifeDynamicObject*>(server);
        if (!item || object->getDestroy() || item->IsQuestItem() || !persistent ||
            persistent->m_story_id!=ALife::_STORY_ID(-1) || !persistent->children.empty()) continue;
        if (is_session_item(object->ID()) && !session_item_enters_world(object->ID())) continue;
        NativeSessionItem native; if (!capture_session_item(object->ID(),native) || native.owner!=native.native_owner) continue;
        NativeWorldItem value; value.object=native.object; value.owner=native.owner; value.incarnation=native.incarnation;
        value.state.world=true; value.state.section=native.section; value.state.condition=item->GetCondition();
        for (unsigned axis=0;axis<3;++axis) value.state.position[axis]=object->Position()[axis];
        if (auto* weapon=smart_cast<CWeapon*>(object)) { value.state.kind=1; value.state.ammo=static_cast<u16>(weapon->GetAmmoElapsed()); value.state.ammo_type=weapon->GetAmmoType(); }
        else if (auto* ammo=smart_cast<CWeaponAmmo*>(object)) { value.state.kind=2; value.state.ammo=ammo->m_boxCurr; }
        items.push_back(std::move(value)); if (items.size()==4096) break;
    }
    return true;
}
void queue_world_item_state(std::uint64_t session,const coopnet::ItemState& item) {
    if (!item.world) return;
    local_world_session=session; local_world_items[item.item]=item; retired_world_items.erase(item.item);
}
void update_world_items() {
    LocalActorPose pose; if (!world_level_is_replica() || !capture_local_actor(pose) || !local_world_session) return;
    for (const auto& record:local_world_items) {
        const auto& state=record.second; if (state.level!=pose.level) continue;
        if (retired_world_items.count(record.first)) continue;
        auto found=local_world_objects.find(record.first);
        CGameObject* object=found==local_world_objects.end() ? nullptr : smart_cast<CGameObject*>(Level().Objects.net_Find(found->second));
        if (!object && found!=local_world_objects.end() && Level().Server->ID_to_entity(found->second)) continue;
        if (object && object->getDestroy()) object=nullptr;
        if (!object) for (const auto& world:world_objects) {
            auto* candidate=const_cast<CGameObject*>(world.first);
            if (world.second.replica && !candidate->getDestroy() && coopnet::world_anchor(local_world_session,candidate->ID())==state.anchor &&
                xr_strcmp(*candidate->cNameSect(),state.section.c_str())==0) { object=candidate; break; }
        }
        if (!state.present || state.owner) {
            if (object) { NET_Packet packet; CGameObject::u_EventGen(packet,GE_DESTROY,object->ID()); CGameObject::u_EventSend(packet); }
            local_world_objects.erase(record.first); retired_world_items.insert(record.first); continue;
        }
        if (!object) {
            if (!pSettings->section_exist(state.section.c_str())) continue;
            Fvector position; position.set(state.position[0],state.position[1],state.position[2]);
            auto* abstract=Level().spawn_item(state.section.c_str(),position,g_actor->ai_location().level_vertex_id(),0xffff,true);
            auto* inventory=smart_cast<CSE_ALifeInventoryItem*>(abstract);
            if (!inventory) { F_entity_Destroy(abstract); continue; }
            abstract->m_bALifeControl=false; inventory->m_fCondition=state.condition;
            if (auto* weapon=smart_cast<CSE_ALifeItemWeapon*>(abstract)) { weapon->a_elapsed=state.ammo; weapon->ammo_type=state.ammo_type; }
            if (auto* ammo=smart_cast<CSE_ALifeItemAmmo*>(abstract)) ammo->a_elapsed=state.ammo;
            NET_Packet packet; abstract->Spawn_Write(packet,TRUE); u16 type; packet.r_begin(type);
            auto* created=Level().Server->Process_spawn(packet,Level().Server->GetServerClient()->ID,FALSE,nullptr,true); F_entity_Destroy(abstract);
            if (!created) continue;
            session_items[created->ID]={++item_incarnation,false,false}; local_world_objects[record.first]=created->ID;
            continue;
        }
        local_world_objects[record.first]=object->ID();
        if (object->H_Parent()) continue;
        object->Position().set(state.position[0],state.position[1],state.position[2]);
        if (auto* item=smart_cast<CInventoryItem*>(object)) item->SetCondition(state.condition);
        if (auto* weapon=smart_cast<CWeapon*>(object)) {
            if (state.ammo<=weapon->GetAmmoMagSize()) weapon->SetAmmoElapsed(state.ammo);
            if (state.ammo_type<weapon->m_ammoTypes.size()) weapon->SetAmmoType(state.ammo_type);
        } else if (auto* ammo=smart_cast<CWeaponAmmo*>(object)) if (state.ammo<=ammo->m_boxSize) ammo->m_boxCurr=state.ammo;
    }
}
void session_item_destroyed(std::uint16_t item) { session_items.erase(item); }
bool capture_session_item(std::uint16_t item, NativeSessionItem& state) {
    if (!g_pGameLevel || !g_pGameLevel->bReady || !Level().Server) return false;
    const auto record=session_items.find(item);
    CGameObject* object=smart_cast<CGameObject*>(Level().Objects.net_Find(item));
    CSE_Abstract* server=Level().Server->ID_to_entity(item);
    if ((record!=session_items.end() && record->second.removing) || !object || !server || object->getDestroy() ||
        !smart_cast<CInventoryItem*>(object) || object->cNameSect().size()>128) return false;
    if (record!=session_items.end()) state.incarnation=record->second.incarnation;
    else {
        const auto world=world_objects.find(object);
        if (world==world_objects.end() || world->second.replica || !ai().get_alife() || !ai().alife().objects().object(item,true)) return false;
        state.incarnation=(std::uint64_t(1)<<63)|world->second.incarnation;
    }
    state.object=item; state.owner=server->ID_Parent;
    state.native_owner=object->H_Parent() ? object->H_Parent()->ID() : 0xffff;
    xr_strcpy(state.section,*object->cNameSect()); return true;
}
NativeInventoryStatus transact_session_item(std::uint16_t owner, std::uint16_t item, std::uint64_t incarnation, bool take) {
    NativeSessionItem state; LocalActorPose pose;
    if (!capture_session_item(item,state) || state.incarnation!=incarnation) return NativeInventoryStatus::Unavailable;
    if (!capture_guest_actor(owner,pose)) return NativeInventoryStatus::Denied;
    CActor* actor=smart_cast<CActor*>(Level().Objects.net_Find(owner));
    CGameObject* object=smart_cast<CGameObject*>(Level().Objects.net_Find(item));
    if (!actor->g_Alive()) return NativeInventoryStatus::Denied;
    if (state.native_owner!=state.owner) return NativeInventoryStatus::Conflict; // native event still queued
    if ((take && state.owner!=0xffff) || (!take && state.owner!=owner)) return NativeInventoryStatus::Conflict;
    if (take) {
        if (actor->inventory().m_all.size()>=256) return NativeInventoryStatus::Capacity;
        if (actor->Position().distance_to_sqr(object->Position())>4.f) return NativeInventoryStatus::OutOfRange;
        if (!actor->inventory().CanTakeItem(smart_cast<CInventoryItem*>(object))) return NativeInventoryStatus::Capacity;
    }
    CSE_ALifeDynamicObject* withdrawn=nullptr;
    if (take && ai().get_alife() && ai().alife().objects().object(item,true)) {
        auto* inventory=smart_cast<CInventoryItem*>(object);
        auto* persistent=smart_cast<CSE_ALifeDynamicObject*>(Level().Server->ID_to_entity(item));
        if (!persistent || inventory->IsQuestItem() || persistent->m_story_id!=ALife::_STORY_ID(-1) || !persistent->children.empty()) return NativeInventoryStatus::Denied;
        // Keep the native item and its save data; remove only persistent-world ownership.
        const_cast<CALifeSimulator&>(ai().alife()).unregister_object(persistent,false); persistent->m_bALifeControl=false;
        session_items[item]={incarnation,false,true}; withdrawn=persistent;
    }
    NET_Packet packet; CGameObject::u_EventGen(packet,take ? GE_OWNERSHIP_TAKE : GE_OWNERSHIP_REJECT,owner);
    packet.w_u16(item); CGameObject::u_EventSend(packet);
    NativeSessionItem after;
    if (!capture_session_item(item,after) || after.owner!=(take ? owner : 0xffff)) {
        if (withdrawn && withdrawn->ID_Parent==0xffff) {
            withdrawn->m_bALifeControl=true; const_cast<CALifeSimulator&>(ai().alife()).register_object(withdrawn,true); session_items.erase(item);
        }
        return NativeInventoryStatus::Denied;
    }
    return NativeInventoryStatus::Accepted;
}
void remove_session_item(std::uint16_t item) {
    const auto found=session_items.find(item);
    if (found==session_items.end() || found->second.removing || !g_pGameLevel || !Level().Server) return;
    if (found->second.enters_world && !world_level_is_replica() && ai().get_alife() && ai().alife().objects().object(item,true)) return;
    found->second.removing=true;
    NET_Packet packet; CGameObject::u_EventGen(packet,GE_DESTROY,item); CGameObject::u_EventSend(packet);
}
bool damage_guest_probe(std::uint16_t object) {
    LocalActorPose pose; if (!capture_guest_actor(object,pose) || !g_actor) return false;
    CActor* actor=smart_cast<CActor*>(Level().Objects.net_Find(object));
    Fvector direction; direction.set(0,0,1);
    SHit hit(.2f,direction,g_actor,BI_NONE,Fvector().set(0,0,0),0,ALife::eHitTypeStrike,0,false);
    hit.GenHeader(GE_HIT,object); hit.whoID=g_actor->ID(); hit.weaponID=g_actor->ID();
    NET_Packet packet; hit.Write_Packet(packet); CGameObject::u_EventSend(packet); return true;
}
bool equip_guest_weapon(std::uint16_t owner,std::uint16_t item,unsigned rounds) {
    LocalActorPose pose; NativeSessionItem state;
    if (!capture_guest_actor(owner,pose) || !capture_session_item(item,state) || state.owner!=owner || state.native_owner!=owner) return false;
    auto* actor=smart_cast<CActor*>(Level().Objects.net_Find(owner));
    auto* weapon=smart_cast<CWeapon*>(Level().Objects.net_Find(item));
    if (!weapon || rounds>static_cast<unsigned>(weapon->GetAmmoMagSize())) return false;
    weapon->SetAmmoElapsed(static_cast<int>(rounds));
    const auto slot=weapon->BaseSlot();
    if (actor->inventory().ItemFromSlot(slot)!=weapon && !actor->inventory().Slot(slot,weapon,true)) return false;
    actor->inventory().Activate(slot,true); return true;
}
bool capture_guest_weapon(std::uint16_t owner,std::uint16_t item,unsigned& rounds,bool& ready) {
    LocalActorPose pose; if (!capture_guest_actor(owner,pose)) return false;
    auto* actor=smart_cast<CActor*>(Level().Objects.net_Find(owner));
    auto* weapon=smart_cast<CWeapon*>(Level().Objects.net_Find(item));
    if (!weapon || weapon->H_Parent()!=actor || actor->inventory().ActiveItem()!=weapon) return false;
    rounds=static_cast<unsigned>(weapon->GetAmmoElapsed());
    ready=!weapon->IsPending() && weapon->GetState()==CWeapon::eIdle && weapon->GetNextState()==CWeapon::eIdle;
    return true;
}
bool capture_guest_inventory(std::uint16_t owner,GuestInventoryState& output) {
    LocalActorPose pose; if (!capture_guest_actor(owner,pose)) return false;
    if (guests.find(owner)->second.restoring || guests.find(owner)->second.starter_pending) return false;
    auto* actor=smart_cast<CActor*>(Level().Objects.net_Find(owner));
    if (actor->inventory().m_all.size()>256) return false;
    GuestInventoryState state; state.active_slot=actor->inventory().GetActiveSlot();
    for (auto* item:actor->inventory().m_all) {
        auto& object=item->object();
        auto* server=Level().Server->ID_to_entity(object.ID());
        if (object.getDestroy() || object.H_Parent()!=actor || !server || server->ID_Parent!=owner ||
            !smart_cast<CSE_ALifeInventoryItem*>(server)) return false;
        NET_Packet update; update.B.count=0; update.r_pos=0;
        object.net_Export(update); server->UPDATE_Read(update);
        if (!update.r_eof()) return false;
        NET_Packet saved; saved.B.count=0; saved.r_pos=0;
        object.net_Save(saved); server->load(saved);
        if (!saved.r_eof()) return false;
        NET_Packet spawn; server->Spawn_Write(spawn,TRUE);
        GuestInventoryItem record;
        record.section=*object.cNameSect();
        record.spawn.assign(spawn.B.data,spawn.B.data+spawn.B.count);
        state.items.push_back(std::move(record));
    }
    output=std::move(state); return true;
}
bool capture_guest_inventory_view(std::uint16_t owner,std::vector<NativeInventoryViewItem>& output,std::uint16_t& active) {
    LocalActorPose pose; if (!capture_guest_actor(owner,pose) || guests.find(owner)->second.restoring || guests.find(owner)->second.starter_pending) return false;
    auto* actor=smart_cast<CActor*>(Level().Objects.net_Find(owner));
    if (actor->inventory().m_all.size()>256) return false;
    std::vector<NativeInventoryViewItem> items;
    for (auto* item:actor->inventory().m_all) {
        auto& object=item->object();
        auto* server=Level().Server->ID_to_entity(object.ID());
        if (object.getDestroy() || object.H_Parent()!=actor || !server || server->ID_Parent!=owner) return false;
        auto record=session_items.find(object.ID());
        if (record==session_items.end()) return false;
        NativeInventoryViewItem value; value.object=object.ID(); value.incarnation=record->second.incarnation;
        value.state.section=*object.cNameSect(); value.state.condition=item->GetCondition();
        value.state.slot=item->CurrSlot();
        value.state.place=item->CurrPlace()==eItemPlaceSlot ? 2 : item->CurrPlace()==eItemPlaceBelt ? 1 : 0;
        if (auto* weapon=smart_cast<CWeapon*>(&object)) {
            value.state.kind=1; value.state.ammo=static_cast<u16>(weapon->GetAmmoElapsed()); value.state.ammo_type=weapon->GetAmmoType();
        } else if (auto* ammo=smart_cast<CWeaponAmmo*>(&object)) { value.state.kind=2; value.state.ammo=ammo->m_boxCurr; }
        items.push_back(std::move(value));
    }
    active=actor->inventory().ActiveItem() ? actor->inventory().ActiveItem()->object().ID() : 0xffff;
    output=std::move(items); return true;
}
bool begin_guest_loadout(std::uint16_t owner) {
    LocalActorPose pose; if (!capture_guest_actor(owner,pose)) return false;
    auto* actor=smart_cast<CActor*>(Level().Objects.net_Find(owner));
    if (!actor->inventory().m_all.empty()) return false;
    auto& guest=guests.find(owner)->second;
    for (const char* section:{"wpn_pm","ammo_9x18_fmj","bandage","device_pda"}) {
        const auto item=spawn_session_item(owner,section);
        if (item==0xffff) { for (auto id:guest.starter_items) remove_session_item(id); guest.starter_items.clear(); return false; }
        guest.starter_items.push_back(item);
        session_items[item].enters_world=true;
    }
    guest.starter_pending=true; return true;
}
void queue_local_inventory_view(const coopnet::InventoryView& view) { local_inventory_view=view; }
bool queue_local_inventory_action(std::uint16_t object,coopnet::InventoryAction action,std::uint16_t slot) {
    if (!world_level_is_replica()) return false;
    if (action==coopnet::InventoryAction::Take) for (const auto& record:local_world_objects) if (record.second==object) {
        const auto state=local_world_items.find(record.first);
        if (state!=local_world_items.end() && state->second.present && !state->second.owner && local_inventory_actions.size()<32) {
            for (const auto& pending:local_inventory_actions) if (pending.item==record.first && pending.action==action) return true;
            local_inventory_actions.push_back({record.first,state->second.revision,action,slot});
        }
        return true;
    }
    for (const auto& record:local_inventory_items) if (record.second==object) {
        for (const auto& state:local_inventory_view.items) if (state.item==record.first) {
            for (const auto& pending:local_inventory_actions) if (pending.item==state.item && pending.action==action && pending.slot==slot) return true;
            if (local_inventory_actions.size()<32) local_inventory_actions.push_back({state.item,state.revision,action,slot});
            return true;
        }
    }
    return true; // Unknown shadow items cannot mutate the host world.
}
bool pop_local_inventory_action(LocalInventoryAction& action) {
    if (local_inventory_actions.empty()) return false;
    action=local_inventory_actions.front(); local_inventory_actions.pop_front(); return true;
}
bool prepare_world_loot_probe(std::uint16_t owner,std::uint16_t& object) {
    LocalActorPose pose; if (!capture_guest_actor(owner,pose) || !ai().get_alife()) return false;
    if (object==0xffff) { object=spawn_session_item(owner,"bandage"); return false; }
    auto* item=smart_cast<CInventoryItem*>(Level().Objects.net_Find(object));
    auto* server=smart_cast<CSE_ALifeDynamicObject*>(Level().Server->ID_to_entity(object));
    auto* actor=smart_cast<CActor*>(Level().Objects.net_Find(owner));
    auto* parent=smart_cast<CSE_ALifeObject*>(Level().Server->ID_to_entity(owner));
    if (!item || !server || !parent || item->object().H_Parent()) return false;
    item->SetCondition(.4321f); session_items[object].enters_world=true;
    server->m_tGraphID=parent->m_tGraphID; server->m_tNodeID=actor->ai_location().level_vertex_id();
    server->m_bOnline=true; server->m_bALifeControl=true;
    if (!ai().alife().objects().object(object,true)) const_cast<CALifeSimulator&>(ai().alife()).create(server);
    return ai().alife().objects().object(object,true)!=nullptr;
}
bool world_loot_is_registered(std::uint16_t object) { return ai().get_alife() && ai().alife().objects().object(object,true); }
void exercise_local_world_loot_probe() {
    if (!world_level_is_replica() || !g_actor || loot_probe_phase>=3) return;
    if (loot_probe_phase==0) for (const auto& record:local_world_items) {
        const auto& state=record.second; const auto native=local_world_objects.find(record.first);
        if (state.present && !state.owner && state.section=="bandage" && std::abs(state.condition-.4321f)<.0001f &&
            native!=local_world_objects.end() && Level().Objects.net_Find(native->second)) {
            loot_probe_item=record.first; queue_local_inventory_action(native->second,coopnet::InventoryAction::Take);
            loot_probe_phase=1; Msg("* CoopNet world loot probe: pickup requested"); return;
        }
    }
    if (loot_probe_phase==1) for (const auto& item:local_inventory_view.items) if (item.item==loot_probe_item) {
        const auto native=local_inventory_items.find(item.item);
        if (native!=local_inventory_items.end() && Level().Objects.net_Find(native->second)) {
            queue_local_inventory_action(native->second,coopnet::InventoryAction::Drop); loot_probe_phase=2;
            Msg("* CoopNet world loot probe: drop requested"); return;
        }
    }
    if (loot_probe_phase==2) {
        const auto state=local_world_items.find(loot_probe_item); const auto native=local_world_objects.find(loot_probe_item);
        if (state!=local_world_items.end() && state->second.present && !state->second.owner &&
            native!=local_world_objects.end() && Level().Objects.net_Find(native->second)) {
            queue_local_inventory_action(native->second,coopnet::InventoryAction::Take); loot_probe_phase=3;
            Msg("* CoopNet world loot probe: second pickup requested");
        }
    }
}
void exercise_local_inventory_probe() {
    if (!g_actor || !world_level_is_replica() || inventory_probe_phase>=3) return;
    for (const auto& state:local_inventory_view.items) if (state.section=="wpn_pm" && state.ammo==2) {
        const auto found=local_inventory_items.find(state.item); if (found==local_inventory_items.end()) return;
        auto* weapon=smart_cast<CWeapon*>(Level().Objects.net_Find(found->second)); if (!weapon) return;
        if (inventory_probe_phase==0 && g_actor->inventory().ActiveItem()==weapon && !weapon->IsPending()) {
            queue_local_inventory_action(weapon->ID(),coopnet::InventoryAction::Ruck); inventory_probe_phase=1;
            Msg("* CoopNet inventory control probe: ruck requested");
        } else if (inventory_probe_phase==1 && state.place==0 && !local_inventory_view.active && weapon->CurrPlace()==eItemPlaceRuck) {
            queue_local_inventory_action(weapon->ID(),coopnet::InventoryAction::Equip,weapon->BaseSlot()); inventory_probe_phase=2;
            Msg("* CoopNet inventory control probe: equip requested");
        } else if (inventory_probe_phase==2 && state.place==2 && local_inventory_view.active==state.item &&
            g_actor->inventory().ActiveItem()==weapon && !weapon->IsPending()) {
            inventory_probe_phase=3; Msg("* CoopNet inventory control probe completed: rounds %d",weapon->GetAmmoElapsed());
        }
        return;
    }
}
NativeInventoryStatus transact_owned_item(std::uint16_t owner,std::uint16_t item,std::uint64_t incarnation,coopnet::InventoryAction action,std::uint16_t slot) {
    if (action==coopnet::InventoryAction::Take || action==coopnet::InventoryAction::Drop)
        return transact_session_item(owner,item,incarnation,action==coopnet::InventoryAction::Take);
    NativeSessionItem state; LocalActorPose pose;
    if (!capture_session_item(item,state) || state.incarnation!=incarnation) return NativeInventoryStatus::Unavailable;
    if (!capture_guest_actor(owner,pose)) return NativeInventoryStatus::Denied;
    if (state.owner!=owner || state.native_owner!=owner) return NativeInventoryStatus::Conflict;
    auto* actor=smart_cast<CActor*>(Level().Objects.net_Find(owner));
    auto* inventory_item=smart_cast<CInventoryItem*>(Level().Objects.net_Find(item));
    if (!actor->g_Alive() || !inventory_item || (inventory_item->IsQuestItem() && action==coopnet::InventoryAction::Use)) return NativeInventoryStatus::Denied;
    bool accepted=false;
    switch (action) {
    case coopnet::InventoryAction::Equip:
        if (slot<actor->inventory().FirstSlot() || slot>actor->inventory().LastSlot()) return NativeInventoryStatus::Denied;
        accepted=actor->inventory().Slot(slot,inventory_item,true);
        if (inventory_item->CurrPlace()==eItemPlaceSlot && inventory_item->CurrSlot()==slot) { actor->inventory().Activate(slot,true); accepted=true; }
        break;
    case coopnet::InventoryAction::Ruck: accepted=actor->inventory().Ruck(inventory_item); break;
    case coopnet::InventoryAction::Belt: accepted=actor->inventory().Belt(inventory_item); break;
    case coopnet::InventoryAction::Use: accepted=actor->inventory().Eat(inventory_item); break;
    case coopnet::InventoryAction::Activate:
        if (slot>=actor->inventory().FirstSlot() && slot<=actor->inventory().LastSlot() && actor->inventory().ItemFromSlot(slot)==inventory_item) {
            actor->inventory().Activate(slot,true); accepted=true;
        }
        break;
    case coopnet::InventoryAction::Holster:
        if (actor->inventory().ActiveItem()==inventory_item) { actor->inventory().Activate(NO_ACTIVE_SLOT,true); accepted=true; }
        break;
    default: break;
    }
    return accepted ? NativeInventoryStatus::Accepted : NativeInventoryStatus::Denied;
}
void update_local_inventory_view() {
    LocalActorPose pose;
    if (!world_level_is_replica() || !local_inventory_view.actor || !capture_local_actor(pose) || pose.level!=local_inventory_view.level || !Level().Server) return;
    if (inventory_local_incarnation!=pose.incarnation) {
        local_inventory_items.clear(); inventory_cleared=false; inventory_local_incarnation=pose.incarnation;
    }
    if (!inventory_cleared) {
        xr_vector<u16> cloned;
        for (auto* item:g_actor->inventory().m_all) cloned.push_back(item->object().ID());
        for (auto id:cloned) { NET_Packet packet; CGameObject::u_EventGen(packet,GE_DESTROY,id); CGameObject::u_EventSend(packet); }
        inventory_cleared=true;
        Msg("* CoopNet guest cloned inventory retired: items %u",static_cast<unsigned>(cloned.size()));
        return;
    }
    for (auto* item:g_actor->inventory().m_all) {
        bool ours=false; for (const auto& record:local_inventory_items) if (record.second==item->object().ID()) ours=true;
        if (!ours) return; // wait for the ordered native clone destruction
    }
    std::set<coopnet::Identity> wanted;
    for (const auto& state:local_inventory_view.items) wanted.insert(state.item);
    for (auto it=local_inventory_items.begin();it!=local_inventory_items.end();) {
        if (!wanted.count(it->first)) { remove_session_item(it->second); it=local_inventory_items.erase(it); } else ++it;
    }
    bool ready=true;
    for (const auto& state:local_inventory_view.items) {
        if (state.place==2 && (state.slot<g_actor->inventory().FirstSlot() || state.slot>g_actor->inventory().LastSlot())) return;
        auto found=local_inventory_items.find(state.item);
        if (found==local_inventory_items.end()) {
            if (!pSettings->section_exist(state.section.c_str())) { Msg("! CoopNet guest inventory section unavailable: %s",state.section.c_str()); return; }
            auto* abstract=Level().spawn_item(state.section.c_str(),g_actor->Position(),g_actor->ai_location().level_vertex_id(),g_actor->ID(),true);
            auto* inventory=smart_cast<CSE_ALifeInventoryItem*>(abstract);
            if (!inventory) { F_entity_Destroy(abstract); return; }
            abstract->m_bALifeControl=false; inventory->m_fCondition=state.condition;
            if (auto* weapon=smart_cast<CSE_ALifeItemWeapon*>(abstract)) { weapon->a_elapsed=state.ammo; weapon->ammo_type=state.ammo_type; }
            if (auto* ammo=smart_cast<CSE_ALifeItemAmmo*>(abstract)) ammo->a_elapsed=state.ammo;
            NET_Packet packet; abstract->Spawn_Write(packet,TRUE); u16 type; packet.r_begin(type);
            auto* created=Level().Server->Process_spawn(packet,Level().Server->GetServerClient()->ID,FALSE,nullptr,true); F_entity_Destroy(abstract);
            if (!created) return;
            session_items.emplace(created->ID,SessionItem{++item_incarnation,false});
            found=local_inventory_items.emplace(state.item,created->ID).first;
        }
        auto* item=smart_cast<CInventoryItem*>(Level().Objects.net_Find(found->second));
        if (!item || item->object().H_Parent()!=g_actor) { ready=false; continue; }
        item->SetCondition(state.condition);
        if (auto* weapon=smart_cast<CWeapon*>(&item->object())) {
            if (state.kind!=1 || (!weapon->m_ammoTypes.empty() && state.ammo_type>=weapon->m_ammoTypes.size()) ||
                (weapon->m_ammoTypes.empty() && state.ammo) || state.ammo>weapon->GetAmmoMagSize()) return;
            if (!weapon->m_ammoTypes.empty()) weapon->SetAmmoType(state.ammo_type);
            weapon->SetAmmoElapsed(state.ammo);
        } else if (auto* ammo=smart_cast<CWeaponAmmo*>(&item->object())) {
            if (state.kind!=2 || state.ammo>ammo->m_boxSize) return; ammo->m_boxCurr=state.ammo;
        }
        if (state.place==2 && (item->CurrPlace()!=eItemPlaceSlot || item->CurrSlot()!=state.slot)) g_actor->inventory().Slot(state.slot,item,true);
        else if (state.place==1 && item->CurrPlace()!=eItemPlaceBelt) g_actor->inventory().Belt(item);
        else if (state.place==0 && item->CurrPlace()!=eItemPlaceRuck) g_actor->inventory().Ruck(item);
    }
    if (!ready) return;
    auto* active=g_actor->inventory().ActiveItem();
    const auto desired=local_inventory_items.find(local_inventory_view.active);
    if (!local_inventory_view.active) { if (active) g_actor->inventory().Activate(NO_ACTIVE_SLOT,true); }
    else if (desired!=local_inventory_items.end() && (!active || active->object().ID()!=desired->second)) {
        auto* item=smart_cast<CInventoryItem*>(Level().Objects.net_Find(desired->second));
        if (item) g_actor->inventory().Activate(item->CurrSlot(),true);
    }
    if (inventory_reported!=local_inventory_view.revision) {
        inventory_reported=local_inventory_view.revision;
        auto* weapon=smart_cast<CWeapon*>(g_actor->inventory().ActiveItem());
        Msg("* CoopNet guest inventory view applied: items %u active rounds %d",static_cast<unsigned>(local_inventory_view.items.size()),weapon ? weapon->GetAmmoElapsed() : -1);
    }
}
bool restore_guest_inventory(std::uint16_t owner,const GuestInventoryState& state) {
    LocalActorPose pose; if (!capture_guest_actor(owner,pose) || state.items.size()>256 ||
        session_items.size()+state.items.size()>768) return false;
    auto* actor=smart_cast<CActor*>(Level().Objects.net_Find(owner));
    if (!actor->inventory().m_all.empty()) return false;
    xr_vector<u16> created_items;
    for (const auto& record:state.items) {
        if (record.section.empty() || record.section.size()>128 || !pSettings->section_exist(record.section.c_str()) ||
            record.spawn.empty() || record.spawn.size()>NET_PacketSizeLimit) return false;
        CSE_Abstract* abstract=F_entity_Create(record.section.c_str());
        if (!abstract || !smart_cast<CSE_ALifeInventoryItem*>(abstract)) {
            if (abstract) F_entity_Destroy(abstract); return false;
        }
        NET_Packet packet; packet.B.count=static_cast<u32>(record.spawn.size()); packet.r_pos=0;
        memcpy(packet.B.data,record.spawn.data(),record.spawn.size());
        const bool read=!!abstract->Spawn_Read(packet);
        if (!read || xr_strcmp(abstract->s_name.c_str(),record.section.c_str())) { F_entity_Destroy(abstract); return false; }
        abstract->ID=0xffff; abstract->ID_Parent=owner; abstract->ID_Phantom=0xffff;
        abstract->m_bALifeControl=false; abstract->o_Position=actor->Position();
        if (auto* life=smart_cast<CSE_ALifeObject*>(abstract)) {
            life->m_tNodeID=actor->ai_location().level_vertex_id();
            if (auto* parent=smart_cast<CSE_ALifeObject*>(Level().Server->ID_to_entity(owner))) life->m_tGraphID=parent->m_tGraphID;
        }
        abstract->Spawn_Write(packet,TRUE); u16 type; packet.r_begin(type);
        auto* created=Level().Server->Process_spawn(packet,Level().Server->GetServerClient()->ID,FALSE,nullptr,true);
        F_entity_Destroy(abstract);
        if (!created) { for (auto id:created_items) remove_session_item(id); return false; }
        session_items.emplace(created->ID,SessionItem{++item_incarnation,false,true}); created_items.push_back(created->ID);
    }
    auto& guest=guests.find(owner)->second;
    guest.restoring=true; guest.restore_slot=state.active_slot; guest.restore_count=static_cast<unsigned>(state.items.size());
    return true;
}
void begin_guest_simulation() { Device.Pause(FALSE, TRUE, FALSE, "CoopNet native movement"); }
std::uint16_t spawn_guest_actor() {
    LocalActorPose local;
    if (!capture_local_actor(local) || !Level().Server || !Level().Server->GetServerClient() || guests.size() >= 3)
        return 0xffff;
    auto& graph = ai().level_graph();
    const auto start = g_actor->ai_location().level_vertex_id();
    if (!graph.valid_vertex_id(start)) return 0xffff;
    // Search connected navigation cells rather than offsetting into a bunker wall.
    xr_vector<u32> candidates; candidates.push_back(start);
    u32 node = u32(-1); Fvector position;
    for (size_t next = 0; next < candidates.size() && next < 128; ++next) {
        const auto current = candidates[next];
        Fvector candidate = graph.vertex_position(current);
        const auto distance = candidate.distance_to(g_actor->Position());
        if (distance >= 1.5f + guests.size() * 1.5f && distance <= 8.f &&
            _abs(candidate.y - g_actor->Position().y) < 1.f) {
            Fvector from = g_actor->Position(), target = candidate, direction;
            from.y += 1.f; target.y += 1.f; direction.sub(target,from);
            const float length = direction.magnitude(); direction.div(length);
            bool clear = !Level().ObjectSpace.RayTest(from,direction,length,collide::rqtStatic,NULL,NULL);
            for (unsigned side = 0; clear && side < 4; ++side) {
                direction.set(side == 0 ? 1.f : side == 1 ? -1.f : 0.f,0.f,
                    side == 2 ? 1.f : side == 3 ? -1.f : 0.f);
                clear = !Level().ObjectSpace.RayTest(target,direction,.5f,collide::rqtStatic,NULL,NULL);
            }
            if (clear) { node = current; position = candidate; position.y += .05f; break; }
        }
        const auto* vertex = graph.vertex(current);
        for (unsigned side = 0; side < 4 && candidates.size() < 128; ++side) {
            const auto adjacent = vertex->link(side);
            if (graph.valid_vertex_id(adjacent) && std::find(candidates.begin(),candidates.end(),adjacent) == candidates.end())
                candidates.push_back(adjacent);
        }
    }
    if (!graph.valid_vertex_id(node)) return 0xffff;
    CSE_Abstract* abstract = Level().spawn_item(*g_actor->cNameSect(), position, node, 0xffff, true);
    CSE_ALifeCreatureActor* actor = smart_cast<CSE_ALifeCreatureActor*>(abstract);
    if (!actor) { F_entity_Destroy(abstract); return 0xffff; }
    actor->m_bALifeControl = false; // Session actor is not persisted as a second ALife primary.
    actor->s_flags.set(M_SPAWN_OBJECT_ASPLAYER,FALSE);
    actor->o_torso.yaw = local.rotation[1]; actor->o_torso.pitch = 0;
    actor->set_health(1.f);
    NET_Packet packet; abstract->Spawn_Write(packet, TRUE);
    u16 type; packet.r_begin(type);
    const auto* owner = Level().Server->GetServerClient();
    CSE_Abstract* created = Level().Server->Process_spawn(packet, owner->ID,FALSE,nullptr,true);
    F_entity_Destroy(abstract);
    if (!created) return 0xffff;
    guests.emplace(created->ID, GuestSpawn{true,false,++guest_incarnation});
    Msg("* CoopNet guest ALife registration: %u",ai().get_alife() && ai().alife().objects().object(created->ID,true) ? 1u : 0u);
    Msg("* CoopNet native guest requested: object %u position %.3f %.3f %.3f", created->ID,position.x,position.y,position.z);
    return created->ID;
}
bool claim_guest_spawn(std::uint16_t object) {
    auto found = guests.find(object);
    if (found == guests.end() || !found->second.pending) return false;
    found->second.pending = false;
    Msg("* CoopNet native guest spawned: object %u", object);
    return true;
}
void guest_actor_destroyed(std::uint16_t object) { guests.erase(object); }
bool capture_guest_actor(std::uint16_t object, LocalActorPose& pose) {
    if (!g_pGameLevel || !g_pGameLevel->bReady || !ai().get_level_graph()) return false;
    auto found = guests.find(object);
    if (found == guests.end() || found->second.pending || found->second.removing) return false;
    CActor* actor = smart_cast<CActor*>(Level().Objects.net_Find(object));
    if (!actor || !actor->is_coopnet_guest() || actor->getDestroy() || !actor->character_physics_support() ||
        !actor->character_physics_support()->movement()) return false;
    LocalActorPose value;
    value.incarnation = found->second.incarnation;
    value.level = static_cast<std::uint32_t>(ai().level_graph().level_id()) + 1;
    value.object = object; value.movement = static_cast<u16>(actor->MovingState());
    value.stance = (value.movement & mcCrouch) ? 1 : 0;
    const auto visual = actor->cNameVisual();
    if (!visual.size() || visual.size() >= sizeof(value.visual)) return false;
    xr_strcpy(value.visual, *visual);
    const auto& position = actor->Position();
    const auto& velocity = actor->character_physics_support()->movement()->GetVelocity();
    float heading, pitch, bank; actor->XFORM().getHPB(heading,pitch,bank);
    value.position[0] = position.x; value.position[1] = position.y; value.position[2] = position.z;
    value.velocity[0] = velocity.x; value.velocity[1] = velocity.y; value.velocity[2] = velocity.z;
    value.rotation[0] = pitch; value.rotation[1] = -heading; value.rotation[2] = bank;
    pose = value; return true;
}
void control_guest_actor(std::uint16_t object, std::uint16_t buttons, float yaw, float pitch) {
    LocalActorPose pose;
    if (!capture_guest_actor(object,pose)) return;
    CActor* actor = smart_cast<CActor*>(Level().Objects.net_Find(object));
    actor->coopnet_controls(buttons,yaw,pitch);
    auto& state = guests.find(object)->second;
    if (state.starter_pending) {
        bool ready=true;
        for (auto id:state.starter_items) {
            NativeSessionItem item;
            if (!capture_session_item(id,item)) { ready=false; continue; }
            if (item.owner==0xffff) transact_session_item(object,id,item.incarnation,true);
            if (item.owner!=object || item.native_owner!=object) ready=false;
        }
        if (ready) {
            auto* weapon=smart_cast<CWeapon*>(Level().Objects.net_Find(state.starter_items.front()));
            if (!weapon) return;
            if (actor->inventory().ItemFromSlot(weapon->BaseSlot())!=weapon && !actor->inventory().Slot(weapon->BaseSlot(),weapon,true)) return;
            actor->inventory().Activate(weapon->BaseSlot(),true); state.starter_pending=false;
            Msg("* CoopNet guest starter loadout ready: items %u rounds %d",static_cast<unsigned>(state.starter_items.size()),weapon->GetAmmoElapsed());
        }
    }
    if (state.restoring && actor->inventory().m_all.size()==state.restore_count) {
        actor->inventory().Activate(state.restore_slot,true); state.restoring=false;
        auto* weapon=smart_cast<CWeapon*>(actor->inventory().ItemFromSlot(state.restore_slot));
        Msg("* CoopNet native inventory restoration completed: items %u active slot %u rounds %d",
            state.restore_count,state.restore_slot,weapon ? weapon->GetAmmoElapsed() : -1);
    }
    const auto actions=actor->g_Alive() && !actor->is_coopnet_downed() ? static_cast<std::uint16_t>(buttons & (coopnet::fire_button|coopnet::reload_button)) : 0;
    if ((actions^state.weapon_buttons)&coopnet::fire_button) {
        const bool accepted=actor->inventory().Action(kWPN_FIRE,(actions&coopnet::fire_button) ? CMD_START : CMD_STOP);
        auto* weapon=smart_cast<CWeapon*>(actor->inventory().ActiveItem());
        Msg("* CoopNet guest fire edge: pressed %u accepted %u weapon %u state %u pending %u rounds %d",
            !!(actions&coopnet::fire_button),accepted,weapon ? weapon->ID() : 0xffff,
            weapon ? weapon->GetState() : 0,weapon ? weapon->IsPending() : false,weapon ? weapon->GetAmmoElapsed() : 0);
    }
    if ((actions&coopnet::reload_button) && !(state.weapon_buttons&coopnet::reload_button))
        actor->inventory().Action(kWPN_RELOAD,CMD_START);
    state.weapon_buttons=actions;
    if (++state.controls == 1 || state.controls % 300 == 0)
        Msg("* CoopNet guest physics: buttons %u movement %u enabled %u ready %u paused %u dt %u power %.3f character %u environment %u steps %llu",
            buttons,actor->MovingState(),actor->getEnabled(),actor->Ready(),Device.Paused(),Device.dwTimeDelta,
            actor->conditions().GetPower(),actor->character_physics_support()->movement()->CharacterExist(),
            actor->character_physics_support()->movement()->Environment(),physics_world()->StepsNum());
}
void remove_guest_actor(std::uint16_t object) {
    if (!g_pGameLevel || !Level().Server) return;
    auto found = guests.find(object);
    if (found == guests.end() || found->second.removing) return;
    found->second.removing = true;
    NET_Packet packet;
    CGameObject::u_EventGen(packet,GE_DESTROY,object);
    CGameObject::u_EventSend(packet);
    // Keep pending spawn roles until the ordered native destroy is processed.
    Msg("* CoopNet native guest removed: object %u", object);
}
void clear_guest_actors() {
    xr_vector<u16> items;
    for (const auto& item : session_items) items.push_back(item.first);
    for (const auto item : items) remove_session_item(item);
    if (!g_pGameLevel || !Level().Server) { guests.clear(); session_items.clear(); return; }
    xr_vector<u16> objects;
    for (const auto& entry : guests) objects.push_back(entry.first);
    for (const auto object : objects) remove_guest_actor(object);
}
void guest_level_stopped() { guests.clear(); session_items.clear(); world_level_stopped(); }
void local_actor_spawned() { ++local_incarnation; local_controls = {}; controls_time = 0; local_weapon_buttons=0;
    local_world_items.clear(); local_world_objects.clear(); retired_world_items.clear(); local_world_session=0;
    loot_probe_phase=0; loot_probe_item=0;
    local_inventory_view={}; local_inventory_items.clear(); local_inventory_actions.clear(); inventory_cleared=false; inventory_reported=0; inventory_probe_phase=0; }
bool record_coopnet_weapon_input(std::uint16_t object,int command,bool pressed) {
    if (!world_level_is_replica() || !g_actor || g_actor->ID()!=object) return false;
    if (command>=kWPN_1 && command<=kWPN_6) {
        if (pressed) { const auto slot=static_cast<u16>(command-kWPN_1+1); auto* item=g_actor->inventory().ItemFromSlot(slot);
            if (item) queue_local_inventory_action(item->object().ID(),coopnet::InventoryAction::Activate,slot); }
        return true;
    }
    if (command==kDROP) { if (!pressed && g_actor->inventory().ActiveItem())
        queue_local_inventory_action(g_actor->inventory().ActiveItem()->object().ID(),coopnet::InventoryAction::Drop); return true; }
    const auto bit=command==kWPN_FIRE ? coopnet::fire_button : command==kWPN_RELOAD ? coopnet::reload_button : 0;
    if (!bit) return false;
    if (pressed) local_weapon_buttons|=bit; else local_weapon_buttons&=~bit;
    return true;
}
bool reconcile_local_actor(std::uint32_t level, const float* position, const float* velocity) {
    LocalActorPose local;
    if (!capture_local_actor(local) || local.level != level || !g_actor->g_Alive()) return false;
    Fmatrix transform = g_actor->XFORM(); transform.c.set(position[0],position[1],position[2]);
    g_actor->ForceTransform(transform);
    g_actor->character_physics_support()->movement()->SetVelocity(velocity[0],velocity[1],velocity[2]);
    return true;
}
void local_controls_sampled(std::uint16_t object, std::uint32_t buttons, float yaw, float pitch) {
    if (!g_pGameLevel || !g_actor || g_actor->ID() != object || Level().CurrentControlEntity() != g_actor) return;
    local_controls.incarnation = local_incarnation;
    local_controls.buttons = static_cast<std::uint16_t>((buttons & 0x70bf)|local_weapon_buttons);
    local_controls.yaw = angle_normalize_signed(yaw);
    local_controls.pitch = angle_normalize_signed(pitch);
    clamp(local_controls.pitch,-PI_DIV_2,PI_DIV_2);
    controls_time = Device.dwTimeGlobal;
}
bool capture_local_controls(LocalActorControls& controls) {
    LocalActorPose pose;
    if (!capture_local_actor(pose) || Level().CurrentControlEntity() != g_actor) return false;
    if (g_actor->is_coopnet_downed()) { local_weapon_buttons=0; local_controls.buttons=0; }
    controls = local_controls; controls.incarnation = pose.incarnation; controls.level = pose.level;
    if (g_actor->is_coopnet_downed() || Device.Paused() || local_controls.incarnation != pose.incarnation ||
        static_cast<std::uint32_t>(Device.dwTimeGlobal - controls_time) >= 250) controls.buttons = 0;
    return true;
}
bool capture_local_actor(LocalActorPose& pose) {
    if (!g_pGameLevel || !g_pGameLevel->bReady || !g_actor || !local_incarnation ||
        !ai().get_level_graph() || g_actor->ID() == 0xffff ||
        Level().Objects.net_Find(g_actor->ID()) != g_actor ||
        !g_actor->character_physics_support() || !g_actor->character_physics_support()->movement()) return false;
    LocalActorPose value;
    value.incarnation = local_incarnation;
    // The native graph can use level zero; wire level IDs reserve zero for absent.
    value.level = static_cast<std::uint32_t>(ai().level_graph().level_id()) + 1;
    value.object = g_actor->ID();
    value.movement = static_cast<std::uint16_t>(g_actor->MovingState() & 0xffff);
    value.stance = (value.movement & mcCrouch) ? 1 : 0;
    const auto& position = g_actor->Position();
    const auto& velocity = g_actor->character_physics_support()->movement()->GetVelocity();
    // Replicate the rendered body orientation, rather than the independently aimed torso.
    float heading, pitch, bank;
    g_actor->XFORM().getHPB(heading, pitch, bank);
    const auto visual = g_actor->cNameVisual();
    if (!visual.size() || visual.size() >= sizeof(value.visual)) return false;
    xr_strcpy(value.visual, *visual);
    value.position[0] = position.x; value.position[1] = position.y; value.position[2] = position.z;
    value.velocity[0] = velocity.x; value.velocity[1] = velocity.y; value.velocity[2] = velocity.z;
    value.rotation[0] = pitch; value.rotation[1] = -heading; value.rotation[2] = bank;
    pose = value; return true;
}
}
