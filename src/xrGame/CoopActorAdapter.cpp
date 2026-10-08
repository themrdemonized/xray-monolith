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
#include "saved_game_wrapper.h"
#include "game_sv_single.h"
#include "PhysicsShellHolder.h"
#include "PHMovementControl.h"
#include "../xrPhysics/PhysicsShell.h"
#include "../Include/xrRender/KinematicsAnimated.h"
#include <cstring>
extern string_path g_last_saved_game;
namespace engine_coopnet {
namespace {
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
        for (auto id:actors) if (id!=0xffff && changer->coopnet_contains(Level().Objects.net_Find(id))) ++present;
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
    bool restoring=false; std::uint16_t restore_slot=0xffff; unsigned restore_count=0; };
xr_map<u16,GuestSpawn> guests;
std::uint64_t guest_incarnation = 0;
struct SessionItem { std::uint64_t incarnation=0; bool removing=false; };
xr_map<u16,SessionItem> session_items;
std::uint64_t item_incarnation=0;
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
bool capture_actor_condition(std::uint16_t object, ActorConditionState& state) {
    if (!g_pGameLevel || !g_pGameLevel->bReady) return false;
    CActor* actor=smart_cast<CActor*>(Level().Objects.net_Find(object));
    if (!actor || actor->getDestroy()) return false;
    state={actor->GetfHealth(),actor->conditions().GetPower(),actor->conditions().GetRadiation()};
    clamp(state.health,-1.f,1.f); clamp(state.power,-1.f,1.f); clamp(state.radiation,0.f,1.f); return true;
}
bool apply_local_condition(std::uint32_t level, const ActorConditionState& state) {
    LocalActorPose pose;
    if (!capture_local_actor(pose) || pose.level!=level) return false;
    const bool was_alive=g_actor->g_Alive();
    // Authoritative death cannot be undone by a later positive snapshot.
    if (!was_alive && state.health>0) return false;
    g_actor->conditions().SetHealth(state.health);
    g_actor->conditions().SetPower(state.power);
    g_actor->conditions().SetRadiation(state.radiation);
    if (was_alive && state.health<=0) g_actor->Die(nullptr);
    return true;
}
bool apply_guest_condition(std::uint16_t object,const ActorConditionState& state) {
    LocalActorPose pose;
    if (!capture_guest_actor(object,pose) || !std::isfinite(state.health) || state.health>1 || state.health< -1 ||
        !std::isfinite(state.power) || state.power>1 || state.power< -1 ||
        !std::isfinite(state.radiation) || state.radiation>1 || state.radiation<0) return false;
    auto* actor=smart_cast<CActor*>(Level().Objects.net_Find(object));
    actor->conditions().SetHealth(state.health); actor->conditions().SetPower(state.power);
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
    CSE_Abstract* created=Level().Server->Process_spawn(packet,Level().Server->GetServerClient()->ID);
    F_entity_Destroy(abstract);
    if (!created) return 0xffff;
    session_items.emplace(created->ID,SessionItem{++item_incarnation,false}); return created->ID;
}
bool is_session_item(std::uint16_t item) { return session_items.find(item)!=session_items.end(); }
void session_item_destroyed(std::uint16_t item) { session_items.erase(item); }
bool capture_session_item(std::uint16_t item, NativeSessionItem& state) {
    if (!g_pGameLevel || !g_pGameLevel->bReady || !Level().Server) return false;
    const auto record=session_items.find(item);
    CGameObject* object=smart_cast<CGameObject*>(Level().Objects.net_Find(item));
    CSE_Abstract* server=Level().Server->ID_to_entity(item);
    if (record==session_items.end() || record->second.removing || !object || !server || object->getDestroy() ||
        !smart_cast<CInventoryItem*>(object) || object->cNameSect().size()>128) return false;
    state.incarnation=record->second.incarnation; state.object=item; state.owner=server->ID_Parent;
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
        if (actor->Position().distance_to_sqr(object->Position())>4.f) return NativeInventoryStatus::OutOfRange;
        if (!actor->inventory().CanTakeItem(smart_cast<CInventoryItem*>(object))) return NativeInventoryStatus::Capacity;
    }
    NET_Packet packet; CGameObject::u_EventGen(packet,take ? GE_OWNERSHIP_TAKE : GE_OWNERSHIP_REJECT,owner);
    packet.w_u16(item); CGameObject::u_EventSend(packet);
    NativeSessionItem after;
    if (!capture_session_item(item,after) || after.owner!=(take ? owner : 0xffff)) return NativeInventoryStatus::Denied;
    return NativeInventoryStatus::Accepted;
}
void remove_session_item(std::uint16_t item) {
    const auto found=session_items.find(item);
    if (found==session_items.end() || found->second.removing || !g_pGameLevel || !Level().Server) return;
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
    if (guests.find(owner)->second.restoring) return false;
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
        auto* created=Level().Server->Process_spawn(packet,Level().Server->GetServerClient()->ID);
        F_entity_Destroy(abstract);
        if (!created) { for (auto id:created_items) remove_session_item(id); return false; }
        session_items.emplace(created->ID,SessionItem{++item_incarnation,false}); created_items.push_back(created->ID);
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
    actor->o_torso.yaw = local.rotation[1]; actor->o_torso.pitch = 0;
    actor->set_health(1.f);
    NET_Packet packet; abstract->Spawn_Write(packet, TRUE);
    u16 type; packet.r_begin(type);
    const auto* owner = Level().Server->GetServerClient();
    CSE_Abstract* created = Level().Server->Process_spawn(packet, owner->ID);
    F_entity_Destroy(abstract);
    if (!created) return 0xffff;
    guests.emplace(created->ID, GuestSpawn{true,false,++guest_incarnation});
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
    if (state.restoring && actor->inventory().m_all.size()==state.restore_count) {
        actor->inventory().Activate(state.restore_slot,true); state.restoring=false;
        auto* weapon=smart_cast<CWeapon*>(actor->inventory().ItemFromSlot(state.restore_slot));
        Msg("* CoopNet native inventory restoration completed: items %u active slot %u rounds %d",
            state.restore_count,state.restore_slot,weapon ? weapon->GetAmmoElapsed() : -1);
    }
    const auto actions=actor->g_Alive() ? static_cast<std::uint16_t>(buttons & (coopnet::fire_button|coopnet::reload_button)) : 0;
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
void local_actor_spawned() { ++local_incarnation; local_controls = {}; controls_time = 0; local_weapon_buttons=0; }
bool record_coopnet_weapon_input(std::uint16_t object,int command,bool pressed) {
    if (!world_level_is_replica() || !g_actor || g_actor->ID()!=object) return false;
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
    controls = local_controls; controls.incarnation = pose.incarnation; controls.level = pose.level;
    if (Device.Paused() || local_controls.incarnation != pose.incarnation ||
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
