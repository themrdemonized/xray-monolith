// player_hud_legs.h
#pragma once

#include "stdafx.h"

class CActor;
class IKinematicsAnimated;
struct IDSGraphManager;

struct legs_config
{
    float fwd_offset = -0.65f;
    float y_offset = 0.0f;
    float side_offset = 0.25f;

    float yaw_speed_moving = 20.f;
    float yaw_speed_idle = 12.f;
    float accel_factor = 6.f;
    float accel_angle = 15.f;

    float strafe_roll = 2.5f;

    float bob_speed_walk = 10.f;
    float bob_speed_sprint = 14.f;
    float bob_speed_crouch = 7.f;
    float bob_amount_walk = 0.007f;
    float bob_amount_sprint = 0.012f;
    float bob_amount_crouch = 0.004f;

    float idle_sway_speed = 1.8f;
    float idle_sway_amount = 0.002f;

    float land_duration = 0.35f;
    float land_squat = 0.05f;

    float pos_smooth_time = 0.06f;
    float y_smooth_speed = 6.f;

    float fwd_collision_margin = 0.1f;
    float fwd_offset_min_ratio = 0.6f;

    shared_str anim_sprint;
    shared_str anim_walk_fwd;
    shared_str anim_walk_back;
    shared_str anim_strafe_left;
    shared_str anim_strafe_right;
    shared_str anim_crouch_fwd;
    shared_str anim_crouch_back;
    shared_str anim_crouch_left;
    shared_str anim_crouch_right;
    shared_str anim_idle;
    shared_str anim_crouch_idle;
    shared_str anim_climb;
    shared_str anim_jump;
    float anim_blend_time = 0.2f;

    void load(const shared_str& section);
};

struct legs_motion_state
{
    float current_yaw = 0.f;
    float target_yaw = 0.f;
    bool  yaw_initialized = false;

    float current_roll = 0.f;

    float bob_timer = 0.f;
    float bob_amount = 0.f;

    float idle_timer = 0.f;

    float land_timer = 0.f;
    bool  was_airborne = false;

    Fvector smooth_pos = { 0, 0, 0 };
    Fvector velocity = { 0, 0, 0 };
    bool    pos_initialized = false;

    float current_y_offset = 0.f;
    float target_y_offset = 0.f;

    void reset();
};

struct actor_movement_info
{
    bool is_airborne = false;
    bool is_moving = false;
    bool is_crouch = false;
    bool is_sprint = false;
    bool is_climb = false;
    bool is_back = false;
    bool is_left = false;
    bool is_right = false;
    bool is_lean_left = false;
    bool is_lean_right = false;

    u32  raw_state = 0;

    static actor_movement_info from_actor(const CActor* actor);
};

class player_legs_controller
{
public:
    void  update(const Fmatrix& cam_trans, CActor* actor, float dt);
    void  render(IDSGraphManager* DM);
    void  destroy();

    bool  is_active() const { return m_model != nullptr; }
    IKinematicsAnimated* model() const { return m_model; }
    const Fmatrix& transform() const { return m_transform; }

private:
    IKinematicsAnimated* m_model = nullptr;
    shared_str           m_visual_name;
    shared_str           m_last_outfit_sect;
    bool                 m_config_warned = false;

    legs_config        m_cfg;
    legs_motion_state  m_motion;
    shared_str         m_current_anim;
    Fmatrix            m_transform;

    bool  resolve_config(CActor* actor, shared_str& out_section);
    bool  ensure_model(const shared_str& legs_section);
    void  select_and_play_animation(const actor_movement_info& move);
    void  update_yaw(const actor_movement_info& move, float target_yaw, float dt);
    void  update_roll(const actor_movement_info& move, float dt);
    void  update_bobbing(const actor_movement_info& move, float dt);
    void  update_landing(const actor_movement_info& move, float dt);
    float compute_total_y_offset(const actor_movement_info& move, float dt);
    void  compute_transform(const Fmatrix& cam_trans, CActor* actor,
        const actor_movement_info& move, float dt);
    void  update_skeleton();
    bool resolve_outfit_config(const shared_str& outfit_sect, shared_str& out_section);
    bool resolve_default_config(shared_str& out_section);
    void warn_once(const char* fmt, ...);

    shared_str choose_anim_name(const actor_movement_info& move) const;
    MotionID   find_motion_with_fallback(const shared_str& name) const;
    float      cast_floor_ray(const Fvector& actor_pos) const;
    float      clamp_fwd_offset(const Fvector& origin,
        const Fvector& fwd, float offset) const;
};