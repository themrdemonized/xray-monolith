// player_hud_legs.cpp
#include "stdafx.h"
#include "player_hud_legs.h"
#include "player_hud.h"
#include "actor.h"
#include "inventory_item.h"
#include "Inventory.h"

extern BOOL g_legs_enabled;

namespace legs_math
{
    static float lerp(float a, float b, float t)
    {
        return a + (b - a) * clampr(t, 0.f, 1.f);
    }

    static float angle_lerp(float from, float to, float t)
    {
        float diff = angle_difference_signed(to, from);
        return from + diff * clampr(t, 0.f, 1.f);
    }

    static float smooth_damp(float current, float target,
        float& velocity, float smooth_time, float dt)
    {
        const float omega = 2.f / fmax(smooth_time, 0.0001f);
        const float x = omega * dt;
        const float exp_f = 1.f / (1.f + x + 0.48f * x * x + 0.235f * x * x * x);
        const float delta = current - target;
        const float temp = (velocity + omega * delta) * dt;

        velocity = (velocity - omega * temp) * exp_f;
        float result = target + (delta + temp) * exp_f;

        if ((target - current > 0.f) == (result > target))
        {
            result = target;
            velocity = 0.f;
        }
        return result;
    }
} 

namespace legs_const
{
    constexpr float FLOOR_RAY_START_HEIGHT = 0.5f;
    constexpr float FLOOR_RAY_LENGTH = 1.0f;
    constexpr float FLOOR_DIFF_MIN = 0.01f;
    constexpr float FLOOR_DIFF_MAX = 0.5f;

    constexpr float FWD_RAY_HEIGHT_OFFSET = 0.5f;

    const float YAW_SNAP_THRESHOLD = deg2rad(90.f);

    constexpr float ROLL_LERP_RATE = 6.f;
    constexpr float BOB_LERP_RATE = 8.f;

    static const char* ANIM_FALLBACKS[] = {
        "lancew_legs_moving",
        "lancew_legs_idle",
        "leg_stand_idle",
        nullptr
    };
} 

void legs_config::load(const shared_str& sect)
{
#define LOAD_F(field, key, def) \
        field = READ_IF_EXISTS(pSettings, r_float, sect, key, def)
#define LOAD_S(field, key, def) \
        field = READ_IF_EXISTS(pSettings, r_string, sect, key, def)

    LOAD_F(fwd_offset, "fwd_offset", -0.65f);
    LOAD_F(y_offset, "y_offset", 0.0f);
    LOAD_F(side_offset, "side_offset", 0.25f);

    LOAD_F(yaw_speed_moving, "yaw_speed_moving", 20.f);
    LOAD_F(yaw_speed_idle, "yaw_speed_idle", 12.f);
    LOAD_F(accel_factor, "yaw_accel_factor", 6.f);
    LOAD_F(accel_angle, "yaw_accel_angle", 15.f);

    LOAD_F(strafe_roll, "strafe_roll", 2.5f);

    LOAD_F(bob_speed_walk, "bob_speed_walk", 10.f);
    LOAD_F(bob_speed_sprint, "bob_speed_sprint", 14.f);
    LOAD_F(bob_speed_crouch, "bob_speed_crouch", 7.f);
    LOAD_F(bob_amount_walk, "bob_amount_walk", 0.007f);
    LOAD_F(bob_amount_sprint, "bob_amount_sprint", 0.012f);
    LOAD_F(bob_amount_crouch, "bob_amount_crouch", 0.004f);

    LOAD_F(idle_sway_speed, "idle_sway_speed", 1.8f);
    LOAD_F(idle_sway_amount, "idle_sway_amount", 0.002f);

    LOAD_F(land_duration, "land_duration", 0.35f);
    LOAD_F(land_squat, "land_squat", 0.05f);

    LOAD_F(pos_smooth_time, "pos_smooth_time", 0.06f);
    LOAD_F(y_smooth_speed, "y_smooth_speed", 6.f);

    LOAD_S(anim_sprint, "anim_sprint", "lancew_legs_sprint");
    LOAD_S(anim_walk_fwd, "anim_walk_fwd", "lancew_legs_moving");
    LOAD_S(anim_walk_back, "anim_walk_back", "lancew_legs_moving");
    LOAD_S(anim_strafe_left, "anim_strafe_left", "lancew_legs_moving");
    LOAD_S(anim_strafe_right, "anim_strafe_right", "lancew_legs_moving");
    LOAD_S(anim_crouch_fwd, "anim_crouch_fwd", "lancew_legs_moving_c");
    LOAD_S(anim_crouch_back, "anim_crouch_back", "lancew_legs_moving_c");
    LOAD_S(anim_crouch_left, "anim_crouch_left", "lancew_legs_moving_c");
    LOAD_S(anim_crouch_right, "anim_crouch_right", "lancew_legs_moving_c");
    LOAD_S(anim_idle, "anim_idle", "leg_stand_idle");
    LOAD_S(anim_crouch_idle, "anim_crouch_idle", "leg_crouch_idle");
    LOAD_S(anim_climb, "anim_climb", "lancew_legs_idle");
    LOAD_S(anim_jump, "anim_jump", "lancew_legs_jump_idle");
    LOAD_F(anim_blend_time, "anim_blend_time", 0.2f);

    LOAD_F(fwd_collision_margin, "fwd_collision_margin", 0.1f);
    LOAD_F(fwd_offset_min_ratio, "fwd_offset_min_ratio", 0.6f);

#undef LOAD_F
#undef LOAD_S
}

void legs_motion_state::reset()
{
    *this = legs_motion_state{};
}

actor_movement_info actor_movement_info::from_actor(const CActor* actor)
{
    actor_movement_info info;
    info.raw_state = actor->MovingState();

    const u32 s = info.raw_state;
    info.is_airborne = !!(s & (mcJump | mcFall));
    info.is_moving = !!(s & mcAnyMove);
    info.is_crouch = !!(s & mcCrouch);
    info.is_sprint = !!(s & mcSprint);
    info.is_climb = !!(s & mcClimb);
    info.is_back = !!(s & mcBack);
    info.is_left = !!(s & mcLStrafe);
    info.is_right = !!(s & mcRStrafe);
    info.is_lean_left = !!(s & mcLLookout);
    info.is_lean_right = !!(s & mcRLookout);

    return info;
}

void player_legs_controller::destroy()
{
    if (!m_model)
        return;

    IRenderVisual* v = m_model->dcast_RenderVisual();
    if (v)
        ::Render->model_Delete(v);

    m_model = nullptr;
    m_visual_name = "";
    m_current_anim = "";
    m_motion.reset();
}

void player_legs_controller::update(const Fmatrix& cam_trans,
    CActor* actor, float dt)
{
    if (!g_legs_enabled || !actor)
    {
        destroy();
        return;
    }

    if (actor->Holder() != nullptr)
    {
        destroy();
        return;
    }

    shared_str legs_section;
    if (!resolve_config(actor, legs_section))
    {
        destroy();
        return;
    }

    if (!ensure_model(legs_section))
        return;

    const auto move = actor_movement_info::from_actor(actor);

    select_and_play_animation(move);

    compute_transform(cam_trans, actor, move, dt);

    update_skeleton();
}

void player_legs_controller::render(IDSGraphManager* DM)
{
    if (!g_legs_enabled || !m_model)
        return;

    DM->add_Dynamic(m_model->dcast_RenderVisual(), &m_transform);
}

bool player_legs_controller::resolve_config(CActor* actor,
    shared_str& out_section)
{
    PIItem outfit = actor->inventory().ItemFromSlot(OUTFIT_SLOT);
    shared_str current_outfit = outfit
        ? outfit->object().cNameSect()
        : shared_str("");

    if (m_last_outfit_sect != current_outfit)
    {
        m_config_warned = false;
        m_last_outfit_sect = current_outfit;
    }

    if (outfit)
        return resolve_outfit_config(current_outfit, out_section);
    else
        return resolve_default_config(out_section);
}

bool player_legs_controller::resolve_outfit_config(const shared_str& outfit_sect,
    shared_str& out_section)
{
    if (pSettings->line_exist(outfit_sect, "legs_visual_sect"))
    {
        shared_str candidate = pSettings->r_string(outfit_sect, "legs_visual_sect");
        if (pSettings->section_exist(candidate))
        {
            out_section = candidate;
            return true;
        }

        warn_once("legs_visual_sect [%s] referenced by outfit [%s] does not exist",
            candidate.c_str(), outfit_sect.c_str());
        return false;
    }

    string256 auto_sect;
    xr_sprintf(auto_sect, "%s_legs", outfit_sect.c_str());
    if (pSettings->section_exist(auto_sect))
    {
        out_section = auto_sect;
        return true;
    }

    if (pSettings->line_exist(outfit_sect, "legs_visual"))
    {
        out_section = outfit_sect;
        return true;
    }

    warn_once("no legs config for outfit [%s] (tried [%s], legs_visual_sect, legs_visual)",
        outfit_sect.c_str(), auto_sect);
    return false;
}

bool player_legs_controller::resolve_default_config(shared_str& out_section)
{
    if (pSettings->section_exist("actor_legs_default"))
    {
        out_section = "actor_legs_default";
        return true;
    }

    warn_once("section [actor_legs_default] not found, legs disabled");
    return false;
}

void player_legs_controller::warn_once(const char* fmt, ...)
{
    if (m_config_warned) return;
    m_config_warned = true;

    string512 buf;
    va_list args;
    va_start(args, fmt);
    vsprintf_s(buf, sizeof(buf), fmt, args);
    va_end(args);

    Msg("! [player_legs] %s", buf);
}

bool player_legs_controller::ensure_model(const shared_str& legs_section)
{
    if (!pSettings->line_exist(legs_section, "visual"))
    {
        warn_once("section [%s] has no 'visual' field", legs_section.c_str());
        destroy();
        return false;
    }

    shared_str new_visual = pSettings->r_string(legs_section, "visual");

    if (m_model && m_visual_name == new_visual)
        return true;  

    destroy();

    IRenderVisual* raw = ::Render->model_Create(new_visual.c_str());
    if (!raw)
    {
        warn_once("failed to create model [%s] from [%s]",
            new_visual.c_str(), legs_section.c_str());
        return false;
    }

    IKinematicsAnimated* animated = smart_cast<IKinematicsAnimated*>(raw);
    if (!animated)
    {
        ::Render->model_Delete(raw);
        warn_once("model [%s] from [%s] is not animated",
            new_visual.c_str(), legs_section.c_str());
        return false;
    }

    m_model = animated;
    m_visual_name = new_visual;
    m_cfg.load(legs_section);

    return true;
}

shared_str player_legs_controller::choose_anim_name(
    const actor_movement_info& m) const
{
    if (m.is_climb)    return m_cfg.anim_climb;
    if (m.is_airborne) return m_cfg.anim_jump;
    if (m.is_sprint)   return m_cfg.anim_sprint;

    if (m.is_moving)
    {
        if (m.is_crouch)
        {
            if (m.is_back)  return m_cfg.anim_crouch_back;
            if (m.is_left)  return m_cfg.anim_crouch_left;
            if (m.is_right) return m_cfg.anim_crouch_right;
            return m_cfg.anim_crouch_fwd;
        }
        else
        {
            if (m.is_back)  return m_cfg.anim_walk_back;
            if (m.is_left)  return m_cfg.anim_strafe_left;
            if (m.is_right) return m_cfg.anim_strafe_right;
            return m_cfg.anim_walk_fwd;
        }
    }

    return m.is_crouch ? m_cfg.anim_crouch_idle : m_cfg.anim_idle;
}

MotionID player_legs_controller::find_motion_with_fallback(
    const shared_str& name) const
{
    MotionID motion = m_model->ID_Cycle_Safe(name.c_str());
    if (motion.valid())
        return motion;

    for (int i = 0; legs_const::ANIM_FALLBACKS[i]; ++i)
    {
        motion = m_model->ID_Cycle_Safe(legs_const::ANIM_FALLBACKS[i]);
        if (motion.valid())
            return motion;
    }

    return MotionID();
}

void player_legs_controller::select_and_play_animation(
    const actor_movement_info& move)
{
    shared_str anim_name = choose_anim_name(move);

    if (m_current_anim == anim_name)
        return;

    MotionID motion = find_motion_with_fallback(anim_name);
    if (!motion.valid())
        return;

    CBlend* B = m_model->PlayCycle(motion, TRUE);
    if (B)
    {
        B->blendAmount = 0.f;
        B->blendAccrue = 1.f / fmax(m_cfg.anim_blend_time, 0.01f);
    }

    m_current_anim = anim_name;
}

void player_legs_controller::update_yaw(
    const actor_movement_info& move, float target_yaw, float dt)
{
    if (!m_motion.yaw_initialized)
    {
        m_motion.current_yaw = target_yaw;
        m_motion.target_yaw = target_yaw;
        m_motion.yaw_initialized = true;
        return;
    }

    m_motion.target_yaw = target_yaw;

    if (move.is_sprint || move.is_climb || move.is_airborne)
    {
        m_motion.current_yaw = target_yaw;
        return;
    }

    float diff = angle_difference(m_motion.current_yaw, m_motion.target_yaw);

    if (diff > legs_const::YAW_SNAP_THRESHOLD)
    {
        m_motion.current_yaw = target_yaw;
        return;
    }

    float base_speed = move.is_moving ? m_cfg.yaw_speed_moving : m_cfg.yaw_speed_idle;
    float diff_factor = 1.f + (diff / deg2rad(m_cfg.accel_angle)) * m_cfg.accel_factor;
    float yaw_speed = base_speed * diff_factor;

    m_motion.current_yaw = legs_math::angle_lerp(
        m_motion.current_yaw, m_motion.target_yaw, dt * yaw_speed);
}

void player_legs_controller::update_roll(
    const actor_movement_info& move, float dt)
{
    float target_roll = 0.f;

    if (move.is_moving && !move.is_climb && !move.is_airborne)
    {
        if (move.is_left)  target_roll = deg2rad(m_cfg.strafe_roll);
        if (move.is_right) target_roll = -deg2rad(m_cfg.strafe_roll);
    }

    m_motion.current_roll = legs_math::lerp(
        m_motion.current_roll, target_roll, dt * legs_const::ROLL_LERP_RATE);
}

void player_legs_controller::update_bobbing(
    const actor_movement_info& move, float dt)
{
    float target_intensity = 0.f;
    float speed = 0.f;

    if (move.is_moving && !move.is_airborne && !move.is_climb)
    {
        if (move.is_sprint)
        {
            speed = m_cfg.bob_speed_sprint;
            target_intensity = m_cfg.bob_amount_sprint;
        }
        else if (move.is_crouch)
        {
            speed = m_cfg.bob_speed_crouch;
            target_intensity = m_cfg.bob_amount_crouch;
        }
        else
        {
            speed = m_cfg.bob_speed_walk;
            target_intensity = m_cfg.bob_amount_walk;
        }
    }

    m_motion.bob_amount = legs_math::lerp(
        m_motion.bob_amount, target_intensity, dt * legs_const::BOB_LERP_RATE);

    if (m_motion.bob_amount > 0.001f)
        m_motion.bob_timer += dt * speed;
    else
        m_motion.bob_timer = 0.f;
}

void player_legs_controller::update_landing(
    const actor_movement_info& move, float dt)
{
    if (m_motion.was_airborne && !move.is_airborne)
        m_motion.land_timer = m_cfg.land_duration;

    m_motion.was_airborne = move.is_airborne;

    if (m_motion.land_timer > 0.f)
        m_motion.land_timer -= dt;
}

float player_legs_controller::compute_total_y_offset(
    const actor_movement_info& move, float dt)
{
    float bob_y = _sin(m_motion.bob_timer) * m_motion.bob_amount;

    float idle_y = 0.f;
    if (!move.is_moving && !move.is_airborne)
    {
        m_motion.idle_timer += dt;
        idle_y = _sin(m_motion.idle_timer * m_cfg.idle_sway_speed)
            * m_cfg.idle_sway_amount;
    }
    else
    {
        m_motion.idle_timer = 0.f;
    }

    float land_squat = 0.f;
    if (m_motion.land_timer > 0.f && m_cfg.land_duration > EPS)
    {
        float t = clampr(m_motion.land_timer / m_cfg.land_duration, 0.f, 1.f);
        land_squat = -_sin(t * PI) * m_cfg.land_squat;
    }

    m_motion.target_y_offset = m_cfg.y_offset;
    m_motion.current_y_offset = legs_math::lerp(
        m_motion.current_y_offset, m_motion.target_y_offset,
        dt * m_cfg.y_smooth_speed);

    return m_motion.current_y_offset + bob_y + idle_y + land_squat;
}

float player_legs_controller::cast_floor_ray(const Fvector& actor_pos) const
{
    collide::rq_result RQ;
    Fvector ray_start = {
        actor_pos.x,
        actor_pos.y + legs_const::FLOOR_RAY_START_HEIGHT,
        actor_pos.z
    };
    Fvector ray_dir = { 0.f, -1.f, 0.f };

    if (Level().ObjectSpace.RayPick(ray_start, ray_dir,
        legs_const::FLOOR_RAY_LENGTH, collide::rqtStatic, RQ, nullptr))
    {
        float detected_y = ray_start.y - RQ.range;
        float diff = detected_y - actor_pos.y;

        if (diff > legs_const::FLOOR_DIFF_MIN &&
            diff < legs_const::FLOOR_DIFF_MAX)
            return detected_y;
    }

    return actor_pos.y;
}

float player_legs_controller::clamp_fwd_offset(
    const Fvector& origin, const Fvector& fwd, float offset) const
{
    if (offset >= 0.f)
        return offset;

    collide::rq_result RQ;
    Fvector ray_start = origin;
    ray_start.y += legs_const::FWD_RAY_HEIGHT_OFFSET;

    Fvector ray_dir;
    ray_dir.set(fwd).invert();

    float check_dist = _abs(offset) + m_cfg.fwd_collision_margin;

    if (Level().ObjectSpace.RayPick(ray_start, ray_dir, check_dist,
        collide::rqtStatic, RQ, nullptr))
    {
        float max_back = RQ.range - m_cfg.fwd_collision_margin;
        if (max_back < _abs(offset))
        {
            float min_back = _abs(offset) * m_cfg.fwd_offset_min_ratio;
            return -clampr(max_back, min_back, _abs(offset));
        }
    }

    return offset;
}

void player_legs_controller::compute_transform(
    const Fmatrix& cam_trans, CActor* actor,
    const actor_movement_info& move, float dt)
{
    float target_yaw, cam_pitch;
    cam_trans.k.getHP(target_yaw, cam_pitch);

    update_yaw(move, target_yaw, dt);
    update_roll(move, dt);
    update_bobbing(move, dt);
    update_landing(move, dt);

    float total_y = compute_total_y_offset(move, dt);
    float base_y = cast_floor_ray(actor->Position());

    Fvector target_pos = { cam_trans.c.x, base_y, cam_trans.c.z };

    if (!m_motion.pos_initialized)
    {
        m_motion.smooth_pos = target_pos;
        m_motion.velocity.set(0, 0, 0);
        m_motion.pos_initialized = true;
    }

    if (move.is_sprint || move.is_airborne)
    {
        m_motion.smooth_pos = target_pos;
        m_motion.velocity.set(0, 0, 0);
    }
    else
    {
        m_motion.smooth_pos.x = legs_math::smooth_damp(
            m_motion.smooth_pos.x, target_pos.x,
            m_motion.velocity.x, m_cfg.pos_smooth_time, dt);
        m_motion.smooth_pos.z = legs_math::smooth_damp(
            m_motion.smooth_pos.z, target_pos.z,
            m_motion.velocity.z, m_cfg.pos_smooth_time, dt);
    }
    m_motion.smooth_pos.y = target_pos.y;

    m_transform.identity();
    m_transform.setHPB(m_motion.current_yaw, 0, m_motion.current_roll);
    m_transform.c.set(
        m_motion.smooth_pos.x,
        base_y + total_y,
        m_motion.smooth_pos.z);

    Fvector v_fwd;
    v_fwd.set(m_transform.k);

    float fwd_offset = clamp_fwd_offset(
        m_transform.c, v_fwd, m_cfg.fwd_offset);
    m_transform.c.mad(v_fwd, fwd_offset);

    Fvector v_right;
    v_right.set(m_transform.i);

    float side = 0.f;
    if (move.is_lean_left)  side = -m_cfg.side_offset;
    if (move.is_lean_right) side = m_cfg.side_offset;
    m_transform.c.mad(v_right, side);
}

void player_legs_controller::update_skeleton()
{
    m_model->UpdateTracks();

    IKinematics* K = m_model->dcast_PKinematics();
    K->CalculateBones_Invalidate();
    K->CalculateBones(TRUE);
}

void player_hud::update_legs(const Fmatrix& cam_trans)
{
    m_legs_controller.update(cam_trans, g_actor, Device.fTimeDelta);
}

void player_hud::render_legs(IDSGraphManager* DM)
{
    m_legs_controller.render(DM);
}

void player_hud::delete_legs_model()
{
    m_legs_controller.destroy();
}