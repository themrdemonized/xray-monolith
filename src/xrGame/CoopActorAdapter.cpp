#include "stdafx.h"
#include "Actor.h"
#include "Level.h"
#include "ai_space.h"
#include "level_graph.h"
#include "CharacterPhysicsSupport.h"
#include "../CoopNet/EngineActorBridge.h"
namespace engine_coopnet {
namespace {
std::uint64_t local_incarnation = 0;
LocalActorControls local_controls;
std::uint32_t controls_time = 0;
}
void local_actor_spawned() { ++local_incarnation; local_controls = {}; controls_time = 0; }
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
