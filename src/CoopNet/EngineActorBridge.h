#pragma once
#include <cstdint>
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
struct NativeSessionItem {
    std::uint64_t incarnation=0;
    std::uint16_t object=0xffff, owner=0xffff, native_owner=0xffff;
    char section[129]{};
};
enum class NativeInventoryStatus : std::uint8_t { Accepted, Unavailable, Conflict, Denied, OutOfRange, Capacity };
bool capture_actor_condition(std::uint16_t object, ActorConditionState& state);
bool apply_local_condition(std::uint32_t level, const ActorConditionState& state);
std::uint16_t spawn_session_item(std::uint16_t actor, const char* section);
bool capture_session_item(std::uint16_t item, NativeSessionItem& state);
bool is_session_item(std::uint16_t item);
void session_item_destroyed(std::uint16_t item);
void remove_session_item(std::uint16_t item);
NativeInventoryStatus transact_session_item(std::uint16_t actor, std::uint16_t item, std::uint64_t incarnation, bool take);
bool damage_guest_probe(std::uint16_t actor);
void local_controls_sampled(std::uint16_t object, std::uint32_t buttons, float yaw, float pitch);
bool capture_local_controls(LocalActorControls& controls);
void local_actor_spawned();
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
