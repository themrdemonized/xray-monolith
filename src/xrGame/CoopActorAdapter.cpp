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
#include "Hit.h"
#include "xrMessages.h"
#include "../xrPhysics/iphworld.h"
#include "../xrPhysics/physicscommon.h"
#include "../CoopNet/EngineActorBridge.h"
#include "../CoopNet/EngineWorldBridge.h"
#include "alife_simulator.h"
#include "saved_game_wrapper.h"
#include "game_sv_single.h"
#include <cstring>
extern string_path g_last_saved_game;
namespace engine_coopnet {
namespace {
bool safe_baseline_name(const char* name) {
    if (!name || strncmp(name,"coopnet-",8) || strlen(name)>64) return false;
    for (const char* c=name;*c;++c) if (!((*c>='a' && *c<='z') || (*c>='0' && *c<='9') || *c=='-')) return false;
    return true;
}
std::uint64_t local_incarnation = 0;
LocalActorControls local_controls;
std::uint32_t controls_time = 0;
struct GuestSpawn { bool pending = true, removing = false; std::uint64_t incarnation = 0; unsigned controls = 0; };
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
std::uint16_t spawn_session_item(std::uint16_t owner, const char* section) {
    LocalActorPose pose;
    if (!capture_guest_actor(owner,pose) || !pSettings->section_exist(section) || session_items.size()>=32) return 0xffff;
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
void guest_level_stopped() { guests.clear(); session_items.clear(); }
void local_actor_spawned() { ++local_incarnation; local_controls = {}; controls_time = 0; }
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
    local_controls.buttons = static_cast<std::uint16_t>(buttons & 0x70bf); // held wishes, never physics-result bits
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
