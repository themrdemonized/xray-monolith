#include "stdafx.h"
#include "player_hud.h"
#include "actor.h"
#include "inventory_item.h"
#include "Inventory.h"

extern BOOL g_legs_enabled;

static float lerp(float a, float b, float t)
{
	return a + (b - a) * clampr(t, 0.f, 1.f);
}

static float legs_angle_lerp(float from, float to, float t)
{
	float diff = angle_difference_signed(to, from);
	return from + diff * clampr(t, 0.f, 1.f);
}

static float smooth_damp(float current, float target, float& velocity, float smooth_time, float dt)
{
	float omega = 2.f / fmax(smooth_time, 0.0001f);
	float x = omega * dt;
	float exp_factor = 1.f / (1.f + x + 0.48f * x * x + 0.235f * x * x * x);
	float delta = current - target;
	float temp = (velocity + omega * delta) * dt;
	velocity = (velocity - omega * temp) * exp_factor;
	float result = target + (delta + temp) * exp_factor;
	if ((target - current > 0.f) == (result > target)) {
		result = target;
		velocity = 0.f;
	}
	return result;
}

void player_hud::load_legs_config(const shared_str& sect)
{
	// смещение
	m_legs_cfg.fwd_offset = READ_IF_EXISTS(pSettings, r_float, sect, "fwd_offset", -0.65f);
	m_legs_cfg.y_offset = READ_IF_EXISTS(pSettings, r_float, sect, "y_offset", 0.0f);
	m_legs_cfg.side_offset = READ_IF_EXISTS(pSettings, r_float, sect, "side_offset", 0.25f);

	// поворот туловищп
	m_legs_cfg.yaw_speed_moving = READ_IF_EXISTS(pSettings, r_float, sect, "yaw_speed_moving", 20.f);
	m_legs_cfg.yaw_speed_idle = READ_IF_EXISTS(pSettings, r_float, sect, "yaw_speed_idle", 12.f);
	m_legs_cfg.accel_factor = READ_IF_EXISTS(pSettings, r_float, sect, "yaw_accel_factor", 6.f);
	m_legs_cfg.accel_angle = READ_IF_EXISTS(pSettings, r_float, sect, "yaw_accel_angle", 15.f);

	// наклон при стрейах 
	m_legs_cfg.strafe_roll = READ_IF_EXISTS(pSettings, r_float, sect, "strafe_roll", 2.5f);

	// bobbing
	m_legs_cfg.bob_speed_walk = READ_IF_EXISTS(pSettings, r_float, sect, "bob_speed_walk", 10.f);
	m_legs_cfg.bob_speed_sprint = READ_IF_EXISTS(pSettings, r_float, sect, "bob_speed_sprint", 14.f);
	m_legs_cfg.bob_speed_crouch = READ_IF_EXISTS(pSettings, r_float, sect, "bob_speed_crouch", 7.f);
	m_legs_cfg.bob_amount_walk = READ_IF_EXISTS(pSettings, r_float, sect, "bob_amount_walk", 0.007f);
	m_legs_cfg.bob_amount_sprint = READ_IF_EXISTS(pSettings, r_float, sect, "bob_amount_sprint", 0.012f);
	m_legs_cfg.bob_amount_crouch = READ_IF_EXISTS(pSettings, r_float, sect, "bob_amount_crouch", 0.004f);

	// idle дыхание
	m_legs_cfg.idle_sway_speed = READ_IF_EXISTS(pSettings, r_float, sect, "idle_sway_speed", 1.8f);
	m_legs_cfg.idle_sway_amount = READ_IF_EXISTS(pSettings, r_float, sect, "idle_sway_amount", 0.002f);

	// приземление
	m_legs_cfg.land_duration = READ_IF_EXISTS(pSettings, r_float, sect, "land_duration", 0.35f);
	m_legs_cfg.land_squat = READ_IF_EXISTS(pSettings, r_float, sect, "land_squat", 0.05f);

	// инерция позиции
	m_legs_cfg.pos_smooth_time = READ_IF_EXISTS(pSettings, r_float, sect, "pos_smooth_time", 0.06f);

	// плавная смена высоты
	m_legs_cfg.y_smooth_speed = READ_IF_EXISTS(pSettings, r_float, sect, "y_smooth_speed", 6.f);

	// анимации 
	m_legs_cfg.anim_sprint = READ_IF_EXISTS(pSettings, r_string, sect, "anim_sprint", "lancew_legs_sprint");
	m_legs_cfg.anim_walk_fwd = READ_IF_EXISTS(pSettings, r_string, sect, "anim_walk_fwd", "lancew_legs_moving");
	m_legs_cfg.anim_walk_back = READ_IF_EXISTS(pSettings, r_string, sect, "anim_walk_back", "lancew_legs_moving");
	m_legs_cfg.anim_strafe_left = READ_IF_EXISTS(pSettings, r_string, sect, "anim_strafe_left", "lancew_legs_moving");
	m_legs_cfg.anim_strafe_right = READ_IF_EXISTS(pSettings, r_string, sect, "anim_strafe_right", "lancew_legs_moving");
	m_legs_cfg.anim_crouch_fwd = READ_IF_EXISTS(pSettings, r_string, sect, "anim_crouch_fwd", "lancew_legs_moving_c");
	m_legs_cfg.anim_crouch_back = READ_IF_EXISTS(pSettings, r_string, sect, "anim_crouch_back", "lancew_legs_moving_c");
	m_legs_cfg.anim_crouch_left = READ_IF_EXISTS(pSettings, r_string, sect, "anim_crouch_left", "lancew_legs_moving_c");
	m_legs_cfg.anim_crouch_right = READ_IF_EXISTS(pSettings, r_string, sect, "anim_crouch_right", "lancew_legs_moving_c");
	m_legs_cfg.anim_idle = READ_IF_EXISTS(pSettings, r_string, sect, "anim_idle", "leg_stand_idle");
	m_legs_cfg.anim_crouch_idle = READ_IF_EXISTS(pSettings, r_string, sect, "anim_crouch_idle", "leg_crouch_idle");
	m_legs_cfg.anim_climb = READ_IF_EXISTS(pSettings, r_string, sect, "anim_climb", "lancew_legs_idle");
	m_legs_cfg.anim_jump = READ_IF_EXISTS(pSettings, r_string, sect, "anim_jump", "lancew_legs_jump_idle");

	m_legs_cfg.anim_blend_time = READ_IF_EXISTS(pSettings, r_float, sect, "anim_blend_time", 0.2f);
}

void player_hud::delete_legs_model()
{
	if (!m_legs_model)
		return;

	IRenderVisual* v = m_legs_model->dcast_RenderVisual();
	if (v)
		::Render->model_Delete(v);

	m_legs_model = nullptr;
	m_legs_visual_name = "";
	m_current_legs_anim = "";
	m_legs_yaw_initialized = false;
	m_legs_pos_initialized = false;
	m_legs_was_airborne = false;
	m_legs_land_timer = 0.f;
	m_legs_idle_timer = 0.f;
	m_legs_current_roll = 0.f;
	m_legs_bob_timer = 0.f;
	m_legs_bob_amount = 0.f;
	m_legs_current_y_offset = 0.f;
	m_legs_velocity.set(0, 0, 0);
	m_legs_ray_timer = 0.f;
	m_legs_hide_by_wall = false;
}

void player_hud::update_legs(const Fmatrix& cam_trans)
{
	if (!g_legs_enabled)
	{
		delete_legs_model();
		return;
	}

	CActor* pActor = g_actor;
	if (!pActor) return;

	float dt = Device.fTimeDelta;

	if (pActor->Holder() != nullptr)
	{
		delete_legs_model();
		return;
	}

	PIItem outfit = pActor->inventory().ItemFromSlot(OUTFIT_SLOT);
	shared_str legs_sect = "actor_legs_default";

	if (outfit)
	{
		shared_str outfit_sect = outfit->object().cNameSect();

		if (pSettings->line_exist(outfit_sect, "legs_visual_sect"))
		{
			legs_sect = pSettings->r_string(outfit_sect, "legs_visual_sect");
		}
		else
		{
			string256 auto_sect;
			xr_sprintf(auto_sect, "%s_legs", outfit_sect.c_str());

			if (pSettings->section_exist(auto_sect))
				legs_sect = auto_sect;
			else if (pSettings->line_exist(outfit_sect, "legs_visual"))
				legs_sect = outfit_sect;
		}
	}

	shared_str new_visual = READ_IF_EXISTS(pSettings, r_string, legs_sect, "visual", "sm\\actor_legs\\no_outfit");

	if (!m_legs_model || (m_legs_visual_name != new_visual))
	{
		delete_legs_model();

		m_legs_model = smart_cast<IKinematicsAnimated*>(::Render->model_Create(new_visual.c_str()));
		m_legs_visual_name = new_visual;

		load_legs_config(legs_sect);
	}

	if (!m_legs_model) return;

	u32 state = pActor->MovingState();

	bool is_airborne = !!(state & (mcJump | mcFall));
	bool is_moving = !!(state & mcAnyMove);
	bool is_crouch = !!(state & mcCrouch);
	bool is_sprint = !!(state & mcSprint);
	bool is_climb = !!(state & mcClimb);

	if (m_legs_was_airborne && !is_airborne)
	{
		m_legs_land_timer = m_legs_cfg.land_duration;
	}
	m_legs_was_airborne = is_airborne;

	bool is_landing = (m_legs_land_timer > 0.f);
	if (is_landing)
		m_legs_land_timer -= dt;

	shared_str anim_name;

	if (is_climb)
	{
		anim_name = m_legs_cfg.anim_climb;
	}
	else if (is_airborne)
	{
		anim_name = m_legs_cfg.anim_jump;
	}
	else if (is_sprint)
	{
		anim_name = m_legs_cfg.anim_sprint;
	}
	else if (is_moving)
	{
		bool back = !!(state & mcBack);
		bool left = !!(state & mcLStrafe);
		bool right = !!(state & mcRStrafe);

		if (is_crouch)
		{
			if (back)       anim_name = m_legs_cfg.anim_crouch_back;
			else if (left)  anim_name = m_legs_cfg.anim_crouch_left;
			else if (right) anim_name = m_legs_cfg.anim_crouch_right;
			else            anim_name = m_legs_cfg.anim_crouch_fwd;
		}
		else
		{
			if (back)       anim_name = m_legs_cfg.anim_walk_back;
			else if (left)  anim_name = m_legs_cfg.anim_strafe_left;
			else if (right) anim_name = m_legs_cfg.anim_strafe_right;
			else            anim_name = m_legs_cfg.anim_walk_fwd;
		}
	}
	else
	{
		anim_name = is_crouch ? m_legs_cfg.anim_crouch_idle : m_legs_cfg.anim_idle;
	}

	if (m_current_legs_anim != anim_name)
	{
		MotionID motion = m_legs_model->ID_Cycle_Safe(anim_name.c_str());

		if (!motion.valid())
		{
			static const char* fallbacks[] = {
				"lancew_legs_moving",
				"lancew_legs_idle",
				"leg_stand_idle",
				nullptr
			};

			for (int i = 0; fallbacks[i]; ++i)
			{
				motion = m_legs_model->ID_Cycle_Safe(fallbacks[i]);
				if (motion.valid())
				{
					anim_name = fallbacks[i];
					break;
				}
			}
		}

		if (motion.valid())
		{
			CBlend* B = m_legs_model->PlayCycle(motion, TRUE);
			if (B)
			{
				B->blendAmount = 0.f;
				B->blendAccrue = 1.f / fmax(m_legs_cfg.anim_blend_time, 0.01f);
			}
			m_current_legs_anim = anim_name;
		}
	}

	float target_yaw, cam_pitch;
	cam_trans.k.getHP(target_yaw, cam_pitch);

	if (!m_legs_yaw_initialized)
	{
		m_legs_current_yaw = target_yaw;
		m_legs_target_yaw = target_yaw;
		m_legs_yaw_initialized = true;
	}

	m_legs_target_yaw = target_yaw;

	float yaw_diff = angle_difference(m_legs_current_yaw, m_legs_target_yaw);

	if (is_sprint || is_climb || is_airborne)
	{
		m_legs_current_yaw = m_legs_target_yaw;
	}
	else
	{
		float base_speed = is_moving ? m_legs_cfg.yaw_speed_moving : m_legs_cfg.yaw_speed_idle;
		float diff_factor = 1.f + (yaw_diff / deg2rad(m_legs_cfg.accel_angle)) * m_legs_cfg.accel_factor;
		float yaw_speed = base_speed * diff_factor;

		if (yaw_diff > deg2rad(90.f))
			m_legs_current_yaw = m_legs_target_yaw;
		else
			m_legs_current_yaw = legs_angle_lerp(m_legs_current_yaw, m_legs_target_yaw, dt * yaw_speed);
	}

	float target_roll = 0.f;

	if (is_moving && !is_climb && !is_airborne)
	{
		if (state & mcLStrafe) target_roll = deg2rad(m_legs_cfg.strafe_roll);
		if (state & mcRStrafe) target_roll = deg2rad(-m_legs_cfg.strafe_roll);
	}

	m_legs_current_roll = lerp(m_legs_current_roll, target_roll, dt * 6.f);

	float bob_speed = 0.f;
	float bob_intensity = 0.f;

	if (is_moving && !is_airborne && !is_climb)
	{
		if (is_sprint)
		{
			bob_speed = m_legs_cfg.bob_speed_sprint;
			bob_intensity = m_legs_cfg.bob_amount_sprint;
		}
		else if (is_crouch)
		{
			bob_speed = m_legs_cfg.bob_speed_crouch;
			bob_intensity = m_legs_cfg.bob_amount_crouch;
		}
		else
		{
			bob_speed = m_legs_cfg.bob_speed_walk;
			bob_intensity = m_legs_cfg.bob_amount_walk;
		}
	}

	m_legs_bob_amount = lerp(m_legs_bob_amount, bob_intensity, dt * 8.f);

	if (m_legs_bob_amount > 0.001f)
		m_legs_bob_timer += dt * bob_speed;
	else
		m_legs_bob_timer = 0.f;

	// float bob_y = _sin(m_legs_bob_timer) * m_legs_bob_amount;

	m_legs_target_y_offset = m_legs_cfg.y_offset;

	m_legs_current_y_offset = lerp(m_legs_current_y_offset, m_legs_target_y_offset, dt * m_legs_cfg.y_smooth_speed);

	Fvector target_pos;
	target_pos.set(cam_trans.c.x, pActor->Position().y, cam_trans.c.z);

	if (!m_legs_pos_initialized)
	{
		m_legs_smooth_pos = target_pos;
		m_legs_velocity.set(0, 0, 0);
		m_legs_pos_initialized = true;
	}

	if (is_sprint || is_airborne)
	{
		m_legs_smooth_pos = target_pos;
		m_legs_velocity.set(0, 0, 0);
	}
	else
	{
		m_legs_smooth_pos.x = smooth_damp(m_legs_smooth_pos.x, target_pos.x, m_legs_velocity.x, m_legs_cfg.pos_smooth_time, dt);
		m_legs_smooth_pos.z = smooth_damp(m_legs_smooth_pos.z, target_pos.z, m_legs_velocity.z, m_legs_cfg.pos_smooth_time, dt);
	}

	m_legs_smooth_pos.y = target_pos.y;

	m_legs_transform.identity();
	m_legs_transform.setHPB(m_legs_current_yaw, 0, m_legs_current_roll);

	m_legs_transform.c.set(m_legs_smooth_pos.x, pActor->Position().y + m_legs_cfg.y_offset, m_legs_smooth_pos.z);

	Fvector v_fwd, v_right;
	v_fwd.set(m_legs_transform.k);
	v_right.set(m_legs_transform.i);

	m_legs_transform.c.mad(v_fwd, m_legs_cfg.fwd_offset);

	float right_offset = 0.f;
	if (state & mcLLookout) right_offset = -m_legs_cfg.side_offset;
	if (state & mcRLookout) right_offset = m_legs_cfg.side_offset;
	m_legs_transform.c.mad(v_right, right_offset);

	m_legs_model->UpdateTracks();
	IKinematics* K = m_legs_model->dcast_PKinematics();
	K->CalculateBones_Invalidate();
	K->CalculateBones(TRUE);
}

void player_hud::render_legs(IDSGraphManager* DM)
{
	if (!g_legs_enabled) return;
	if (!m_legs_model) return;

	DM->add_Dynamic(m_legs_model->dcast_RenderVisual(), &m_legs_transform);
}