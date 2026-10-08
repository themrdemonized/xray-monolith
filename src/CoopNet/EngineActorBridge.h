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
void local_controls_sampled(std::uint16_t object, std::uint32_t buttons, float yaw, float pitch);
bool capture_local_controls(LocalActorControls& controls);
void local_actor_spawned();
bool capture_local_actor(LocalActorPose& pose);
bool present_remote_actor(const RemoteActorPose& pose);
void remove_remote_actor(std::uint64_t entity);
void clear_remote_actors();
}
