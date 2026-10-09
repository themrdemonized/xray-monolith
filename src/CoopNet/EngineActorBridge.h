#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "InventoryView.h"
namespace engine_coopnet {
// Copied owner-thread state only. No engine pointer crosses the transport boundary.
struct LocalActorPose {
    std::uint64_t incarnation = 0;
    std::uint32_t level = 0;
    std::uint16_t object = 0xffff, movement = 0;
    std::uint8_t stance = 0;
    float position[3]{}, velocity[3]{}, rotation[3]{};
    char visual[192]{};
};
struct RemoteActorPose {
    std::uint64_t entity = 0;
    std::uint32_t generation = 0, level = 0;
    float position[3]{}, rotation[3]{};
    std::uint16_t movement = 0;
    char visual[192]{};
};
struct LocalActorControls {
    std::uint64_t incarnation = 0;
    std::uint32_t level = 0;
    std::uint16_t buttons = 0;
    float yaw = 0, pitch = 0;
};
struct ActorConditionState { float health=1, power=1, radiation=0; };
bool local_actor_downed();
bool respawn_actor(std::uint16_t object,std::uint32_t level,const float* position);
bool down_actor(std::uint16_t object);
// Host-created native records stay inside the host process. They are never accepted from peers.
struct GuestInventoryItem {
    std::string section;
    std::vector<std::uint8_t> spawn;
};
struct GuestInventoryState {
    std::vector<GuestInventoryItem> items;
    std::uint16_t active_slot=0xffff;
};
bool capture_guest_inventory(std::uint16_t actor,GuestInventoryState& state);
bool restore_guest_inventory(std::uint16_t actor,const GuestInventoryState& state);
std::uint64_t guest_save_scope();
bool read_guest_save_file(const char* name,std::vector<std::uint8_t>& bytes);
bool write_guest_save_file(const char* name,const std::vector<std::uint8_t>& bytes);
struct NativeSessionItem {
    std::uint64_t incarnation=0;
    std::uint16_t object=0xffff, owner=0xffff, native_owner=0xffff;
    char section[129]{};
};
struct NativeInventoryViewItem { std::uint16_t object=0xffff; std::uint64_t incarnation=0; coopnet::InventoryViewItem state; };
bool capture_guest_inventory_view(std::uint16_t actor,std::vector<NativeInventoryViewItem>& items,std::uint16_t& active);
bool begin_guest_loadout(std::uint16_t actor);
void queue_local_inventory_view(const coopnet::InventoryView& view);
void update_local_inventory_view();
struct LocalInventoryAction { coopnet::Identity item=0; std::uint32_t revision=0; coopnet::InventoryAction action=coopnet::InventoryAction::Drop; std::uint16_t slot=0xffff; };
enum class NativeInventoryStatus : std::uint8_t;
bool queue_local_inventory_action(std::uint16_t object,coopnet::InventoryAction action,std::uint16_t slot=0xffff);
bool pop_local_inventory_action(LocalInventoryAction& action);
void exercise_local_inventory_probe();
NativeInventoryStatus transact_owned_item(std::uint16_t actor,std::uint16_t item,std::uint64_t incarnation,coopnet::InventoryAction action,std::uint16_t slot);
enum class NativeInventoryStatus : std::uint8_t { Accepted, Unavailable, Conflict, Denied, OutOfRange, Capacity };
bool capture_actor_condition(std::uint16_t object, ActorConditionState& state);
bool apply_guest_condition(std::uint16_t object,const ActorConditionState& state);
bool apply_local_condition(std::uint32_t level, const ActorConditionState& state);
std::uint16_t spawn_session_item(std::uint16_t actor, const char* section);
bool capture_session_item(std::uint16_t item, NativeSessionItem& state);
bool is_session_item(std::uint16_t item);
bool session_item_enters_world(std::uint16_t item);
struct NativeWorldItem { std::uint16_t object=0xffff,owner=0xffff; std::uint64_t incarnation=0; coopnet::ItemState state; };
bool capture_world_items(std::vector<NativeWorldItem>& items);
void queue_world_item_state(std::uint64_t session,const coopnet::ItemState& item);
void update_world_items();
bool prepare_world_loot_probe(std::uint16_t owner,std::uint16_t& object);
bool world_loot_is_registered(std::uint16_t object);
void exercise_local_world_loot_probe();
void exercise_container_probe(std::uint16_t owner,unsigned& phase,std::uint16_t& source,std::uint16_t& item);
void exercise_local_container_probe();
void session_item_destroyed(std::uint16_t item);
void remove_session_item(std::uint16_t item);
NativeInventoryStatus transact_session_item(std::uint16_t actor, std::uint16_t item, std::uint64_t incarnation, bool take);
bool damage_guest_probe(std::uint16_t actor);
bool equip_guest_weapon(std::uint16_t actor,std::uint16_t item,unsigned rounds);
bool capture_guest_weapon(std::uint16_t actor,std::uint16_t item,unsigned& rounds,bool& ready);
void local_controls_sampled(std::uint16_t object, std::uint32_t buttons, float yaw, float pitch);
bool capture_local_controls(LocalActorControls& controls);
void local_actor_spawned();
bool record_coopnet_weapon_input(std::uint16_t object,int command,bool pressed);
bool capture_local_actor(LocalActorPose& pose);
bool reconcile_local_actor(std::uint32_t level, const float* position, const float* velocity);
// Native host actor lifecycle. IDs never leave this owner-thread adapter.
std::uint16_t spawn_guest_actor();
void begin_guest_simulation();
bool claim_guest_spawn(std::uint16_t object);
void guest_actor_destroyed(std::uint16_t object);
bool capture_guest_actor(std::uint16_t object, LocalActorPose& pose);
void control_guest_actor(std::uint16_t object, std::uint16_t buttons, float yaw, float pitch);
void remove_guest_actor(std::uint16_t object);
void clear_guest_actors();
void guest_level_stopped();
bool present_remote_actor(const RemoteActorPose& pose);
void remove_remote_actor(std::uint64_t entity);
void clear_remote_actors();
}
