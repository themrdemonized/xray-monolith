#include "stdafx.h"
#include "alife_space.h"
#include "hit.h"
#include "PHDestroyable.h"
#include "car.h"
#include "Actor.h"
#include "xr_level_controller.h"
#include "../xrEngine/xr_input.h"
#include "../Include/xrRender/Kinematics.h"
#include "../xrEngine/bone.h"
#include "../xrphysics/ExtendedGeom.h"
#include "../xrEngine/gamemtllib.h"
#include "CustomZone.h"
#include "CarPush.h"
#include "level.h"
#include "../3rd party/ode/include/ode/mass.h"
#include "ai/stalker/ai_stalker.h"
#include "CharacterPhysicsSupport.h"
#include "PHMovementControl.h"
#include "ai_space.h"
#include "script_engine.h"
#include "script_game_object.h"
#include <ctime>

// Vehicle Handling Overhaul. Replaces the stock velocity-motor drive with an engine -> auto clutch -> gearbox ->
// wheels torque model, plus speed-sensitive steering, tuned suspension, tyre slip and surface grip.
// Everything here is inactive unless m_vho.enabled; the stock code paths stay untouched otherwise.
// Design and parameter reference: docs/vehicles/03-vehicle-handling-overhaul.md

namespace
{
const float RPM_TO_RAD = 2.f * PI / 60.f;
// hinge2 suspension constants from xrPhysics/PHJoint.cpp; joint SD factors are relative to these
const float HINGE2_SPRING = 20000.f;
const float HINGE2_DAMPING = 1000.f;
const float BRAKE_UNIT = 100000.f; // stock brake torque scale (SWheelBreak::Break)

// Handling trace file format. Raise it whenever a column is added, removed, renamed or changes meaning, so a
// parser can tell traces apart.
const u32 VHO_TRACE_SCHEMA = 2; // 2: the test input and its mode column are gone
const u32 TRACE_COOLDOWN_STEPS = 500; // per event type, after an automatic trace was latched
const u32 TRACE_LOCK_CLEAR_STEPS = 50; // a wheel lock must clear this long before it can latch another trace
const u32 TRACE_MAX_AUTO = 40; // automatic traces per car and session

// handling trace: state of one step (SHandlingState::dbg_flags)
enum
{
	TF_LOCKED = 1 << 0, TF_STRICT = 1 << 1, TF_BRAKING = 1 << 2, TF_OPEN = 1 << 3, TF_SHIFTING = 1 << 4,
	TF_REV_FREE = 1 << 5, TF_LUGGING = 1 << 6, TF_DECLUTCHED = 1 << 7, TF_NEUTRAL = 1 << 8, TF_PEDAL = 1 << 9,
	TF_ENGINE_ON = 1 << 10, TF_CLUTCH_MODE = 1 << 11, TF_AUTOMATIC = 1 << 12, TF_HAND = 1 << 13,
	TF_GROUNDED = 1 << 14, TF_LOCK_CENTRE = 1 << 15, TF_LOCK_AXLES = 1 << 16, TF_ENGAGING = 1 << 17,
	TF_HIGH_GEAR = 1 << 18, TF_AGAINST = 1 << 19, TF_CENTRE_HELD = 1 << 20
};
const LPCSTR g_trace_flag_names[] = { "locked", "strict", "braking", "open", "shifting", "rev_free", "lugging",
	"declutched", "neutral", "pedal", "engine_on", "clutch_mode", "automatic", "hand", "grounded", "lock_centre",
	"lock_axles", "engaging", "high_gear", "against", "centre_held" };

struct SHandlingParamInfo
{
	LPCSTR name;
	float CCar::SHandlingParams::* member;
};

const SHandlingParamInfo g_vho_params[] = {
#define VHO_INFO(name, def) {#name, &CCar::SHandlingParams::name},
	VHO_PARAM_LIST(VHO_INFO)
#undef VHO_INFO
};

const SHandlingParamInfo* find_param(LPCSTR name)
{
	if (!name) return NULL;
	for (const SHandlingParamInfo& i : g_vho_params)
		if (!xr_strcmp(i.name, name)) return &i;
	return NULL;
}

IC float approach(float cur, float target, float up_rate, float down_rate, float dt)
{
	if (target > cur) return _min(target, cur + up_rate * dt);
	return _max(target, cur - down_rate * dt);
}

IC float smoothstep01(float x)
{
	clamp(x, 0.f, 1.f);
	return x * x * (3.f - 2.f * x);
}
}

CCar::SHandlingParams::SHandlingParams()
{
#define VHO_DEFAULT(name, def) name = def;
	VHO_PARAM_LIST(VHO_DEFAULT)
#undef VHO_DEFAULT
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////
// setup

void CCar::HandlingResetState()
{
	SHandlingState& S = m_vho.s;
	S.throttle = S.brake = S.steer = S.speed = S.clutch = 0.f;
	S.rpm = b_engine_on ? m_min_rpm : 0.f;
	S.shift_timer = S.auto_timer = S.pending_damage = S.script_steer = 0.f;
	S.brake_hold = S.stall_timer = 0.f;
	S.ground_w = S.shift_throttle = S.hand_slide = S.clutch_engage = 0.f;
	S.crank_timer = S.flip_timer = S.flip_sign = S.water_depth = S.water_check = 0.f;
	S.crank_ok = S.seized = S.start_blocked = false;
	S.wheel_dist = 0.f;
	S.engine_temp = b_engine_on ? 88.f : 15.f;
	S.heat_k = 1.f;
	S.prev_vel.set(0.f, 0.f, 0.f);
	S.crash_dv = S.last_crash_speed = 0.f;
	S.crash_cooldown = 1.f; // no crash from the first velocity sample
	S.script_steer_on = S.locked = S.overrev = S.last_hit_explosive = S.declutched = false;
	// a car nobody drives stands parked: out of gear with the hand brake on
	S.hand_latched = !Owner() && m_vho.p.handbrake_double_tap > 0.f;
	S.hand_tap = 0;
	S.hold_brake = !Owner();
	S.neutral = !Owner();
	S.sprint = S.clutch_held = false;
	S.cog_acc.set(0.f, 0.f, 0.f);
	if (m_pPhysicsShell) m_pPhysicsShell->get_LinearVel(S.cog_prev_v);
	else S.cog_prev_v.set(0.f, 0.f, 0.f);
	S.dbg_air_k = -1.f; // read from the model when the trace first needs it
	S.dbg_prev_step = 0;
}

// Called at the end of CCar::Init, after the stock definitions and physics shell exist.
void CCar::HandlingInit()
{
	SHandlingStock& st = m_vho.stock;
	st.max_power = m_max_power;
	st.max_rpm = m_max_rpm;
	st.min_rpm = m_min_rpm;
	st.power_rpm = m_power_rpm;
	st.torque_rpm = m_torque_rpm;
	st.gears = m_gear_ratious;

	SHandlingState& S = m_vho.s;
	S.step_count = 0;
	S.mass_k = 1.f;
	S.cog_h = 0.f;
	S.chassis_inertia_k = 1.f;
	S.inertia_add.set(0.f, 0.f, 0.f);
	S.hand_k = 0.f;
	IKinematics* K = smart_cast<IKinematics*>(Visual());
	S.chassis = m_pPhysicsShell->get_Element(K->LL_GetBoneRoot());
	{
		xr_vector<Fmatrix> bind;
		K->LL_GetBindTransform(bind);
		Fmatrix inv;
		inv.invert(bind[K->LL_GetBoneRoot()]);
		inv.transform_dir(S.ax_right, Fvector().set(1.f, 0.f, 0.f));
		inv.transform_dir(S.ax_up, Fvector().set(0.f, 1.f, 0.f));
		inv.transform_dir(S.ax_fwd, Fvector().set(0.f, 0.f, 1.f));
		S.ax_right.normalize_safe();
		S.ax_up.normalize_safe();
		S.ax_fwd.normalize_safe();
	}

	// geometry for steering and suspension sizing. "u" is the distance along the car from the middle of the
	// wheels, positive toward the front (the side the steering wheels are on; +z when every wheel steers).
	float wheel_mass = 0.f, steer_z = 0.f, free_z = 0.f, all_z = 0.f, all_x = 0.f;
	int steer_n = 0, free_n = 0;
	auto is_steering = [&](const SWheel* w) {
		for (SWheelSteer& sw : m_steering_wheels)
			if (sw.pwheel == w) return true;
		return false;
	};
	for (auto& it : m_wheels_map)
	{
		SWheel& w = it.second;
		if (w.element) wheel_mass += w.element->getMass();
		all_z += w.pos.z;
		all_x += w.pos.x;
		if (is_steering(&w)) { steer_z += w.pos.z; ++steer_n; }
		else { free_z += w.pos.z; ++free_n; }
	}
	S.axle_mid_z = m_wheels_map.empty() ? 0.f : all_z / m_wheels_map.size();
	S.axle_mid_x = m_wheels_map.empty() ? 0.f : all_x / m_wheels_map.size(); // models are not always centred
	S.front_sign = (steer_n && free_n && steer_z / steer_n < free_z / free_n) ? -1.f : 1.f;
	float rear_sum = 0.f, x_min = 0.f, x_max = 0.f;
	int rear_n = 0;
	S.front_u = 0.f;
	S.rear_steering = false;
	for (auto& it : m_wheels_map)
	{
		SWheel& w = it.second;
		float u = (w.pos.z - S.axle_mid_z) * S.front_sign;
		if (u < 0.f) { rear_sum += u; ++rear_n; }
		if (is_steering(&w))
		{
			S.front_u = _max(S.front_u, u);
			if (u < -0.1f) S.rear_steering = true;
		}
	}
	S.rear_u = rear_n ? rear_sum / rear_n : 0.f;
	S.wheelbase = S.front_u - S.rear_u;
	for (auto& it : m_wheels_map)
	{
		SWheel& w = it.second;
		if ((w.pos.z - S.axle_mid_z) * S.front_sign > S.front_u - 0.3f)
		{
			x_min = _min(x_min, w.pos.x);
			x_max = _max(x_max, w.pos.x);
		}
	}
	S.track = x_max - x_min;

	// Steer joint orientation per wheel. Hinge2 motor speed and joint angle are (chassis - wheel) about the
	// steering axis, so with the axis pointing up a negative angle turns right, the stock convention. Models
	// with the axis pointing down on some wheels (UAZ-452 front left) steer those the other way.
	{
		Fvector up = HandlingDir(S.ax_up);
		for (SWheelSteer& sw : m_steering_wheels)
		{
			SWheel& w = *sw.pwheel;
			if (!w.joint) continue;
			Fvector a;
			w.joint->GetAxisDirDynamic(0, a);
			w.vho_steer_dir = a.dotproduct(up) >= 0.f ? 1.f : -1.f;
		}
	}

	// the model bounding box is often not centred on the car (it made the chase camera orbit the bonnet),
	// so the camera orbits the middle of the wheels, at 80 % of the body height
	S.chase_pivot_ok = false;
	if (!m_wheels_map.empty())
	{
		Fvector c = {0.f, 0.f, 0.f};
		for (auto& it : m_wheels_map)
			c.add(it.second.pos);
		c.div(float(m_wheels_map.size()));
		const Fbox& box = BoundingBox();
		c.y = box.min.y + (box.max.y - box.min.y) * 0.8f;
		S.chase_pivot = c;
		S.chase_pivot_ok = true;
	}

	// wheel rest positions in the chassis (root bone) frame, from the bind pose: a car loaded from a save may
	// spawn with its suspension compressed
	{
		xr_vector<Fmatrix> bind;
		K->LL_GetBindTransform(bind);
		Fmatrix inv;
		inv.invert(bind[K->LL_GetBoneRoot()]);
		for (auto& it : m_wheels_map)
		{
			SWheel& w = it.second;
			inv.transform_tiny(w.rest_local, bind[w.bone_id].c);
			w.vho_comp = 0.f;
		}
	}

	// anti-roll bar partners: the nearest wheel on the other side of the same axle
	for (auto& it : m_wheels_map)
	{
		SWheel& w = it.second;
		w.vho_arb = NULL;
		float best = 0.4f;
		for (auto& jt : m_wheels_map)
		{
			SWheel& o = jt.second;
			if (&o == &w || (o.pos.x - S.axle_mid_x) * (w.pos.x - S.axle_mid_x) >= 0.f) continue;
			float dz = _abs(o.pos.z - w.pos.z);
			if (dz < best) { best = dz; w.vho_arb = &o; }
		}
	}

	u32 n = _max(u32(m_wheels_map.size()), u32(1));
	S.sprung_corner_mass = _max(m_pPhysicsShell->getMass() - wheel_mass, 1.f) / n;

	HandlingResetState();

	m_vho.enabled = false;
	if (m_vho.section.size())
		LoadHandling(*m_vho.section);
}

bool CCar::LoadHandling(LPCSTR section)
{
	if (!section || !pSettings->section_exist(section))
	{
		Msg("! [VHO] %s: handling section [%s] not found", *cName(), section ? section : "");
		return false;
	}

	m_vho.section = section;
	SHandlingParams& P = m_vho.p;
	P = SHandlingParams();
	for (const SHandlingParamInfo& i : g_vho_params)
		if (pSettings->line_exist(section, i.name))
			P.*i.member = pSettings->r_float(section, i.name);

	m_vho.gears.clear();
	if (pSettings->line_exist(section, "gears"))
	{
		LPCSTR str = pSettings->r_string(section, "gears");
		string32 tmp;
		for (int i = 0, cnt = _GetItemCount(str); i < cnt; ++i)
		{
			float g = (float)atof(_GetItem(str, i, tmp));
			if (g > 0.f) m_vho.gears.push_back(g);
		}
	}

	HandlingLoadSurfaces(READ_IF_EXISTS(pSettings, r_string, section, "surfaces", "vho_surfaces"));

	SetHandlingEnabled(!!READ_IF_EXISTS(pSettings, r_bool, section, "enabled", TRUE));
	return true;
}

void CCar::HandlingLoadSurfaces(LPCSTR section)
{
	m_vho.surfaces.clear();
	if (!section || !pSettings->section_exist(section)) return;
	CInifile::Sect& S = pSettings->r_section(section);
	for (const CInifile::Item& item : S.Data)
	{
		string32 tmp;
		LPCSTR v = *item.second;
		int cnt = v ? _GetItemCount(v) : 0;
		float grip = cnt > 0 ? (float)atof(_GetItem(v, 0, tmp)) : 1.f;
		float rolling = cnt > 1 ? (float)atof(_GetItem(v, 1, tmp)) : 1.f;
		float slip = cnt > 2 ? (float)atof(_GetItem(v, 2, tmp)) : 1.f;
		SetHandlingSurface(*item.first, grip, rolling, slip);
	}
}

// material: full name ("materials\\asphalt") or short name ("asphalt")
bool CCar::SetHandlingSurface(LPCSTR material, float grip, float rolling, float slip)
{
	if (!material || !material[0]) return false;
	string_path name;
	if (strchr(material, '\\')) xr_strcpy(name, material);
	else xr_sprintf(name, "materials\\%s", material);
	GameMtlIt it = GMLib.GetMaterialIt(name);
	if (it == GMLib.LastMaterial())
	{
		Msg("! [VHO] unknown surface material '%s'", name);
		return false;
	}
	SHandlingSurface s;
	s.material = *it;
	s.grip = grip;
	s.rolling = rolling;
	s.slip = slip;
	for (SHandlingSurface& e : m_vho.surfaces)
		if (e.material == s.material)
		{
			e = s;
			return true;
		}
	m_vho.surfaces.push_back(s);
	return true;
}

const CCar::SHandlingSurface* CCar::HandlingSurface(SGameMtl* m) const
{
	if (!m) return NULL;
	for (const SHandlingSurface& s : m_vho.surfaces)
		if (s.material == m) return &s;
	return NULL;
}

// Surface factors for this car. offroad > 1 (knobby tyres, locking axles, high clearance) shrinks how much a
// loose surface takes away; < 1 (road tyres, low car) makes it worse. Surfaces at full grip are unchanged.
void CCar::HandlingSurfaceFactors(const SHandlingSurface* s, float& grip, float& rolling, float& slip) const
{
	grip = rolling = slip = 1.f;
	if (!s) return;
	grip = s->grip;
	rolling = s->rolling;
	slip = s->slip;
	float k = _max(m_vho.p.offroad, 0.2f);
	if (k == 1.f || (grip >= 1.f && rolling <= 1.f)) return;
	grip = 1.f - (1.f - grip) / k;
	clamp(grip, 0.1f, 1.f);
	rolling = _max(1.f + (rolling - 1.f) / k, 0.5f);
	slip = _max(1.f + (slip - 1.f) / k, 0.5f);
}

void CCar::SetHandlingEnabled(bool val)
{
	// with the overhaul a car shelters its interior from rain (the rain occlusion map draws it)
	set_rain_occluder(val);
	if (!m_pPhysicsShell || m_gear_ratious.empty())
	{
		m_vho.enabled = val; // not spawned yet: HandlingInit applies it
		return;
	}
	if (val)
	{
		bool was = m_vho.enabled;
		m_vho.enabled = true;
		if (!was)
		{
			HandlingResetState();
			if (m_vho.s.cog_h <= 0.f) HandlingMeasureCog();
			const SHandlingState& S = m_vho.s;
			Msg("[VHO] %s (%s): handling on, physics mass %.0f kg (real_mass %.0f), %d wheels (%d driven), wheelbase %.2f m, track %.2f m, centre of gravity %.2f m (cog_height %.2f)",
			    *cName(), *cNameSect(), m_pPhysicsShell->getMass(), m_vho.p.real_mass, int(m_wheels_map.size()),
			    int(m_driving_wheels.size()), S.wheelbase, S.track, S.cog_h, m_vho.p.cog_height);
		}
		HandlingApply();
	}
	else if (m_vho.enabled)
	{
		m_vho.enabled = false;
		HandlingRestore();
	}
}

void CCar::HandlingApply()
{
	m_vho.s.apply_pending = false;
	const SHandlingParams& P = m_vho.p;
	const SHandlingStock& st = m_vho.stock;

	m_max_power = P.engine_power > 0.f ? P.engine_power * 0.8f * 1000.f : st.max_power;
	m_max_rpm = P.max_rpm > 0.f ? P.max_rpm * RPM_TO_RAD : st.max_rpm;
	m_min_rpm = P.idle_rpm > 0.f ? P.idle_rpm * RPM_TO_RAD : st.min_rpm;
	m_power_rpm = P.max_power_rpm > 0.f ? P.max_power_rpm * RPM_TO_RAD : st.power_rpm;
	m_torque_rpm = P.max_torque_rpm > 0.f ? P.max_torque_rpm * RPM_TO_RAD : st.torque_rpm;
	clamp(m_power_rpm, m_min_rpm + 1.f, m_max_rpm);
	clamp(m_torque_rpm, m_min_rpm, m_power_rpm - 1.f);
	InitParabola();

	// Model physics masses are often several times the real vehicle's; scale the forces instead of the mass
	// so engines, brakes and drag act like on the real car while collisions keep the model's weight.
	float& mass_k = m_vho.s.mass_k;
	mass_k = P.real_mass > 0.f ? m_pPhysicsShell->getMass() / P.real_mass : 1.f;
	clamp(mass_k, 0.2f, 20.f);

	if (P.final_drive > 0.f && !m_vho.gears.empty())
	{
		m_gear_ratious.clear();
		m_gear_ratious.push_back(Fvector().set(-P.reverse_ratio * P.final_drive, 0.f, 0.f));
		for (float g : m_vho.gears)
			m_gear_ratious.push_back(Fvector().set(g * P.final_drive, 0.f, 0.f));
	}
	else
		m_gear_ratious = st.gears;
	if (m_vho.s.low_range_on && P.low_range > 1.f)
		for (Fvector& g : m_gear_ratious)
			g.x *= P.low_range;
	if (m_current_transmission_num >= m_gear_ratious.size())
		m_current_transmission_num = m_gear_ratious.size() - 1;
	if (m_current_transmission_num == 0 && m_gear_ratious.size() > 1 && !bkp)
		m_current_transmission_num = 1;
	m_current_gear_ratio = m_gear_ratious[m_current_transmission_num][0];
	b_transmission_switching = false;

	m_vho.drive.clear();
	for (auto& it : m_wheels_map)
	{
		SWheel& w = it.second;
		if (!w.joint) continue;
		const SWheelDrive* model = NULL;
		for (const SWheelDrive& d : m_driving_wheels)
			if (d.pwheel == &w) model = &d;
		if (model)
		{
			if (P.rear_drive_only <= 0.f || HandlingIsRear(w)) m_vho.drive.push_back(*model);
		}
		else if (P.all_wheel_drive > 0.f)
		{
			SWheelDrive d;
			d.pwheel = &w;
			d.pos_fvd = -1.f; // same convention as SWheelDrive::Init
			d.gear_factor = w.radius / _max(m_ref_radius, 0.01f);
			m_vho.drive.push_back(d);
		}
	}
	if (m_vho.drive.empty()) m_vho.drive = m_driving_wheels;

	// Suspension axis. Hinge2 springs act along the steering axis (the wheel bone's local X); a model with it
	// lying flat on some wheels (buggy rear) springs those fore and aft with no vertical travel, so they get
	// the car's up axis. HandlingRestore puts the model's back.
	SHandlingState& S = m_vho.s;
	if (S.chassis)
	{
		Fvector up = HandlingDir(S.ax_up);
		Fmatrix inv;
		inv.invert(S.chassis->XFORM());
		int fixed = 0;
		for (auto& it : m_wheels_map)
		{
			SWheel& w = it.second;
			if (!w.joint || w.vho_axis_fixed) continue;
			Fvector a;
			w.joint->GetAxisDirDynamic(0, a);
			if (_abs(a.dotproduct(up)) > 0.7f) continue;
			inv.transform_dir(w.vho_axis_stock, a);
			w.joint->SetAxisDir(up, 0);
			w.vho_axis_fixed = true;
			++fixed;
		}
		if (fixed)
			Msg("[VHO] %s (%s): %d wheels have a flat suspension axis in the model; set to the car's up axis",
			    *cName(), *cNameSect(), fixed);
	}

	// Which way each driven wheel turns for "forward". The stock code assumes one axle direction for every
	// model (pos_fvd = -1), so a model built with its axles the other way (or mirrored on one side) drives
	// backwards. Motor speed and joint rate are (chassis - wheel) about the axle, so a positive motor speed
	// rolls the car along -(axle x up): pick the sign that drives toward the front (the steering side).
	// Models that work with the stock code get -1 as before.
	if (S.chassis)
	{
		Fvector up = HandlingDir(S.ax_up), front = HandlingDir(S.ax_fwd);
		front.mul(S.front_sign);
		int flipped = 0;
		for (SWheelDrive& d : m_vho.drive)
		{
			if (!d.pwheel->joint) continue;
			Fvector axle, roll;
			d.pwheel->joint->GetAxisDirDynamic(1, axle);
			roll.crossproduct(axle, up);
			d.pos_fvd = roll.dotproduct(front) >= 0.f ? -1.f : 1.f;
			if (d.pos_fvd > 0.f) ++flipped;
		}
		if (flipped)
			Msg("[VHO] %s (%s): %d of %d driven wheels have reversed axles in the model; driven toward the front",
			    *cName(), *cNameSect(), flipped, int(m_vho.drive.size()));
	}

	// differentials: each driven wheel's partner across its axle, and which side of the centre (or inter-axle)
	// differential its axle is on: the front or the rear half of the driven axles
	{
		float u_min = flt_max, u_max = -flt_max;
		for (SWheelDrive& d : m_vho.drive)
		{
			float u = (d.pwheel->pos.z - S.axle_mid_z) * S.front_sign;
			u_min = _min(u_min, u);
			u_max = _max(u_max, u);
		}
		float u_mid = (u_min + u_max) * 0.5f;
		for (SWheelDrive& d : m_vho.drive)
		{
			SWheel& w = *d.pwheel;
			w.vho_diff_k = 1.f;
			w.vho_diff_mate = NULL;
			for (SWheelDrive& o : m_vho.drive)
				if (o.pwheel == w.vho_arb) w.vho_diff_mate = o.pwheel;
			float u = (w.pos.z - S.axle_mid_z) * S.front_sign;
			w.vho_diff_group = (u_max - u_min > 0.5f && u < u_mid) ? 1 : 0;
		}
	}

	HandlingApplySuspension(false);
	// a fixed preload follows the parameters; a levelling one keeps what it has settled on
	for (auto& it : m_wheels_map)
		if (P.susp_level <= 0.f || it.second.vho_preload <= 0.f)
			it.second.vho_preload = P.susp_preload * S.sprung_corner_mass * EffectiveGravity();
	HandlingSizeBrakes();
	// yaw_inertia: the body turns no easier than a uniform box of the car's size and mass (times yaw_inertia).
	// Models often carry their mass in one small root shape, which lets a car spin round on the spot.
	if (S.chassis)
	{
		HandlingInertiaAdd(Fvector().set(S.inertia_add).invert()); // back to the scaled model tensor
		float k_cur = S.chassis_inertia_k > 0.f ? S.chassis_inertia_k : 1.f;
		float want = 1.f;
		dMass* dm = S.chassis->getMassTensor();
		// size from the wheel layout (cars are about 1.7 wheelbases long and 1.15 tracks wide, and stand about 2.5
		// times their centre of gravity high): the visual's box is unreliable (the Niva's spans 9.4 m) and follows the
		// bones once the car is simulated
		float len = S.wheelbase * 1.7f, wid = _max(S.track, 0.8f) * 1.15f;
		float hgt = clampr(2.5f * (P.cog_height > 0.f ? P.cog_height : S.cog_h), 1.f, 3.5f);
		float mass = m_pPhysicsShell->getMass();
		if (P.yaw_inertia > 0.f && dm && S.wheelbase > 0.5f)
		{
			float box = mass * (len * len + wid * wid) / 12.f;
			float base = HandlingInertiaAbout(S.ax_up) / k_cur;
			if (base > EPS) want = clampr(P.yaw_inertia * box / base, 1.f, 4.f);
		}
		if (dm && _abs(want / k_cur - 1.f) > 0.01f)
		{
			dMass m = *dm;
			for (int i = 0; i < 12; ++i) m.I[i] *= want / k_cur;
			S.chassis->setInertia(m);
			S.chassis_inertia_k = want;
			Msg("[VHO] %s: body inertia x%.2f (yaw_inertia %.2f)", *cName(), want, P.yaw_inertia);
		}
		// Then each axis at least yaw_inertia x that box: models with their mass in one small part (ZIL-130, ZAZ-968)
		// pitched like a short, light car, and the scaling above stops at 4 x. Raising one axis leaves the moments
		// about the other two as they are.
		if (P.yaw_inertia > 0.f && dm && S.wheelbase > 0.5f)
		{
			Fvector need;
			need.set(mass * (len * len + hgt * hgt), mass * (len * len + wid * wid), mass * (wid * wid + hgt * hgt));
			need.mul(P.yaw_inertia / 12.f);
			Fvector add;
			add.set(_max(need.x - HandlingInertiaAbout(S.ax_right), 0.f), _max(need.y - HandlingInertiaAbout(S.ax_up), 0.f),
			        _max(need.z - HandlingInertiaAbout(S.ax_fwd), 0.f));
			HandlingInertiaAdd(add);
			if (add.x + add.y + add.z > 0.f)
				Msg("[VHO] %s: body inertia raised by %.0f (pitch) %.0f (yaw) %.0f (roll) kg m2", *cName(), add.x, add.y, add.z);
		}
	}
	m_pPhysicsShell->remove_ObjectContactCallback(HandlingCreatureContact);
	m_pPhysicsShell->add_ObjectContactCallback(HandlingCreatureContact);
	m_pPhysicsShell->remove_ObjectContactCallback(HandlingBodyContact);
	m_pPhysicsShell->add_ObjectContactCallback(HandlingBodyContact);
	if (S.fresh_spawn)
	{
		S.fresh_spawn = false;
		if (P.spawn_lift > 0.f) HandlingSpawnLift();
	}
	m_pPhysicsShell->Enable();
}

// centre of gravity of the whole shell above the plane the wheels stand on, along the car's up axis
void CCar::HandlingMeasureCog()
{
	SHandlingState& S = m_vho.s;
	if (!S.chassis || m_wheels_map.empty()) return;
	Fvector c = {0.f, 0.f, 0.f};
	float m = 0.f;
	for (u16 i = 0; i < m_pPhysicsShell->get_ElementsNumber(); ++i)
	{
		CPhysicsElement* e = m_pPhysicsShell->get_ElementByStoreOrder(i);
		float em = e->getMass();
		c.mad(e->mass_Center(), em);
		m += em;
	}
	Fvector up = HandlingDir(S.ax_up);
	float ground = 0.f;
	int n = 0;
	for (auto& it : m_wheels_map)
	{
		SWheel& w = it.second;
		if (!w.element) continue;
		ground += w.element->XFORM().c.dotproduct(up) - w.radius;
		++n;
	}
	if (m <= 0.f || !n) return;
	c.mul(1.f / m);
	float h = c.dotproduct(up) - ground / n;
	if (h < 0.05f)
		Msg("! [VHO] %s (%s): the model's centre of mass is %.2f m above the ground its wheels stand on; give the car a "
		    "cog_height, or it hangs under its wheels like a pendulum", *cName(), *cNameSect(), h);
	S.cog_h = _max(h, 0.01f);
}

// Load a wheel carries now, N at physics scale: its joint spring at the present compression plus the preload,
// as the handling trace estimates it (damping left out), kept within a quarter to four times `standing`
float CCar::HandlingWheelLoad(SWheel& w, float standing)
{
	SHandlingState& S = m_vho.s;
	if (!w.joint || !w.element || !S.chassis) return standing;
	Fvector a, rest, d;
	w.joint->GetAxisDirDynamic(0, a);
	if (a.dotproduct(HandlingDir(S.ax_up)) < 0.f) a.invert();
	S.chassis->XFORM().transform_tiny(rest, w.rest_local);
	float comp = d.sub(w.element->XFORM().c, rest).dotproduct(a);
	float cs, cd;
	w.joint->GetJointSDfactors(cs, cd);
	return clampr(cs * HINGE2_SPRING * comp + w.vho_preload, 0.25f * standing, 4.f * standing);
}

// brake_g: brakes sized from the vehicle itself. The models' brake torques differ a hundredfold and often brake
// the rear harder than the front, so most cars locked every wheel at a third of the pedal and the rear first,
// which turns a car round. Full pedal asks brake_g x g of deceleration; the force is shared between the front and
// rear axles as the load is while braking that hard (weight moves forward by brake_g x cog height / wheelbase),
// plus a little more at the front, so on any surface the front axle locks first and the car stays straight, as
// a real brake proportioning valve makes it. The hand brake holds handbrake_lock x what a rear tyre carries
// standing, so a full pull locks the rear wheels on any surface.
void CCar::HandlingSizeBrakes()
{
	const SHandlingParams& P = m_vho.p;
	SHandlingState& S = m_vho.s;
	S.static_front_share = S.brake_front_share = 0.5f;
	if (P.brake_g <= 0.f || m_wheels_map.empty()) return;
	float mass = m_pPhysicsShell->getMass(), g = EffectiveGravity();

	// centre of mass along the car, in model space like the wheel positions
	Fvector c = {0.f, 0.f, 0.f};
	float m = 0.f;
	for (u16 i = 0; i < m_pPhysicsShell->get_ElementsNumber(); ++i)
	{
		CPhysicsElement* e = m_pPhysicsShell->get_ElementByStoreOrder(i);
		c.mad(e->mass_Center(), e->getMass());
		m += e->getMass();
	}
	float u_f = 0.f, u_r = 0.f;
	int n_f = 0, n_r = 0;
	for (auto& it : m_wheels_map)
	{
		float u = (it.second.pos.z - S.axle_mid_z) * S.front_sign;
		if (HandlingIsRear(it.second)) { u_r += u; ++n_r; }
		else { u_f += u; ++n_f; }
	}
	if (n_f && n_r && m > 0.f)
	{
		u_f /= n_f;
		u_r /= n_r;
		Fmatrix inv;
		inv.invert(XFORM());
		Fvector cl;
		inv.transform_tiny(cl, c.mul(1.f / m));
		float L = _max(u_f - u_r, 0.5f);
		float u_cg = (cl.z - S.axle_mid_z) * S.front_sign;
		float h = P.cog_height > 0.f ? P.cog_height : S.cog_h;
		S.static_front_share = clampr((u_cg - u_r) / L, 0.25f, 0.75f);
		S.brake_front_share = P.brake_front > 0.f ? clampr(P.brake_front, 0.f, 1.f) :
			clampr(S.static_front_share + P.brake_g * h / L + 0.03f, 0.4f, 0.85f);
	}
	for (auto& it : m_wheels_map)
	{
		SWheel& w = it.second;
		bool rear = n_f && n_r && HandlingIsRear(w);
		float brake_k = !n_f || !n_r ? 1.f / m_wheels_map.size() : (rear ? (1.f - S.brake_front_share) / n_r : S.brake_front_share / n_f);
		float load_k = !n_f || !n_r ? 1.f / m_wheels_map.size() : (rear ? (1.f - S.static_front_share) / n_r : S.static_front_share / n_f);
		w.vho_brake_full = P.brake_g * g * mass * brake_k * w.radius;
		w.vho_hand_full = P.handbrake_lock * g * mass * load_k * w.radius;
	}
	Msg("[VHO] %s: brakes %.2f g, front share %.2f (weight on the front %.2f)", *cName(), P.brake_g,
	    S.brake_front_share, S.static_front_share);
}

// A new car is placed by its origin, so on uneven ground, or with a model whose wheels hang below that point,
// its wheels can start inside the terrain and the solver throws it into the air. Lift it until every wheel is
// spawn_lift above the static ground under it.
// Adds (or takes back) the rotational inertia of the drivetrain part turning with a wheel. Added alike about every
// axis: only the spin matters, and a wheel whose inertia differs a lot between axes turns gyroscopic when it steers
// at speed, which unsettles the solver.
void CCar::HandlingSetWheelInertia(SWheel& w, float extra)
{
	if (!w.element) return;
	float d = extra - w.vho_inertia;
	if (_abs(d) < 1e-4f) return;
	dMass m = *w.element->getMassTensor();
	for (int i = 0; i < 3; ++i)
		m.I[i * 4 + i] += d;
	w.element->setInertia(m);
	w.vho_inertia = extra;
}

// Some models put their centre of mass far from the vehicle's: the LAZ's 3.8 m under the ground, the GAZ-66's,
// ZIL-130's, KAvZ's and ZAZ-968's below it too. The stock springs are stiff enough to hide that; on real springs such a
// body swings under its wheels like a pendulum, pitches nose up under braking, and its roll inertia is many times a
// real one's (the model's shapes sit metres from that point). When the handling profile gives a cog_height (the
// script hook CCar__CogHeight; none without the overhaul or with it switched off) 0.3 m or more away from the
// model's, the physics is built with the heaviest body part's centre of mass moved straight up or down so the whole
// car's sits there; a model that already has it about right is left alone. Called just before the shell is built.
// Returns the bone changed (BI_NONE: none); HandlingCogRestore puts it back once the shell exists, as every car of
// the model shares the bone data.
u16 CCar::HandlingCogFix(Fvector& saved)
{
	IKinematics* K = smart_cast<IKinematics*>(Visual());
	if (!K || m_wheels_map.empty()) return BI_NONE;
	float target = 0.f;
	::luabind::functor<float> cog_funct;
	if (ai().script_engine().functor("_G.CCar__CogHeight", cog_funct)) target = cog_funct(*cNameSect());
	if (target <= 0.f) return BI_NONE;

	xr_vector<Fmatrix> bind;
	K->LL_GetBindTransform(bind);
	auto physical = [](const CBoneData& b) {
		return b.mass > 0.f && b.shape.type != SBoneShape::stNone && !b.shape.flags.test(SBoneShape::sfNoPhysics);
	};
	Fvector com = {0.f, 0.f, 0.f};
	float mass = 0.f;
	for (u16 i = 0; i < K->LL_BoneCount(); ++i)
	{
		const CBoneData& b = K->LL_GetData(i);
		if (!physical(b)) continue;
		Fvector c;
		bind[i].transform_tiny(c, b.center_of_mass);
		com.mad(c, b.mass);
		mass += b.mass;
	}
	float ground = 0.f;
	int n = 0;
	for (auto& it : m_wheels_map)
	{
		const SBoneShape& s = K->LL_GetData(it.first).shape;
		Fvector c;
		float r;
		if (s.type == SBoneShape::stCylinder) { c = s.cylinder.m_center; r = s.cylinder.m_radius; }
		else if (s.type == SBoneShape::stSphere) { c = s.sphere.P; r = s.sphere.R; }
		else continue;
		bind[it.first].transform_tiny(c);
		ground += c.y - r;
		++n;
	}
	if (mass <= 0.f || !n) return BI_NONE;
	com.mul(1.f / mass);
	ground /= float(n);
	float h = com.y - ground;
	if (_abs(target - h) < 0.3f) return BI_NONE;

	// the heaviest part of the body: the root bone or one fixed rigidly to it (together they make one physics element)
	u16 best = BI_NONE;
	float best_m = 0.f;
	xr_vector<u16> open;
	open.push_back(K->LL_GetBoneRoot());
	while (!open.empty())
	{
		u16 id = open.back();
		open.pop_back();
		const CBoneData& b = K->LL_GetData(id);
		if (physical(b) && b.mass > best_m)
		{
			best_m = b.mass;
			best = id;
		}
		for (CBoneData* ch : b.children)
			if (ch->IK_data.type == jtRigid) open.push_back(ch->GetSelfID());
	}
	if (best == BI_NONE || best_m < 0.25f * mass) return BI_NONE;
	Fmatrix inv;
	inv.invert(bind[best]);
	Fvector d;
	inv.transform_dir(d, Fvector().set(0.f, (ground + target - com.y) * mass / best_m, 0.f));
	CBoneData& b = K->LL_GetData(best);
	saved = b.center_of_mass;
	b.center_of_mass.add(d);
	Msg("[VHO] %s (%s): the model's centre of mass is %.2f m above the ground; its physics is built with it at %.2f m "
	    "(cog_height)", *cName(), *cNameSect(), h, target);
	return best;
}

void CCar::HandlingCogRestore(u16 bone, const Fvector& saved)
{
	IKinematics* K = smart_cast<IKinematics*>(Visual());
	if (K && bone != BI_NONE) K->LL_GetData(bone).center_of_mass = saved;
}

// the chassis' moment of inertia about one of its own axes (chassis frame), kg m2 at physics scale
float CCar::HandlingInertiaAbout(const Fvector& axis) const
{
	const SHandlingState& S = m_vho.s;
	dMass* dm = S.chassis ? S.chassis->getMassTensor() : NULL;
	if (!dm) return 0.f;
	float v[3] = { axis.x, axis.y, axis.z }, I = 0.f;
	for (int i = 0; i < 3; ++i)
		for (int j = 0; j < 3; ++j)
			I += v[i] * dm->I[i * 4 + j] * v[j];
	return I;
}

// k.x, k.y, k.z kg m2 more about the car's right, up and forward axes; each leaves the moments about the two axes
// perpendicular to it as they are. Kept in inertia_add, so it can be taken back.
void CCar::HandlingInertiaAdd(const Fvector& k)
{
	SHandlingState& S = m_vho.s;
	if (!S.chassis || (k.x == 0.f && k.y == 0.f && k.z == 0.f)) return;
	dMass m = *S.chassis->getMassTensor();
	const Fvector* ax[3] = { &S.ax_right, &S.ax_up, &S.ax_fwd };
	float kk[3] = { k.x, k.y, k.z };
	for (int n = 0; n < 3; ++n)
	{
		float v[3] = { ax[n]->x, ax[n]->y, ax[n]->z };
		for (int i = 0; i < 3; ++i)
			for (int j = 0; j < 3; ++j)
				m.I[i * 4 + j] += kk[n] * v[i] * v[j];
	}
	S.chassis->setInertia(m);
	S.inertia_add.add(k);
}

void CCar::HandlingSpawnLift()
{
	float need = -flt_max;
	for (auto& it : m_wheels_map)
	{
		SWheel& w = it.second;
		if (!w.element) continue;
		const Fvector& c = w.element->XFORM().c;
		Fvector start = c;
		start.y += w.radius + 0.5f;
		collide::rq_result R;
		if (Level().ObjectSpace.RayPick(start, Fvector().set(0.f, -1.f, 0.f), 2.f * w.radius + 1.5f, collide::rqtStatic, R, this))
			need = _max(need, (start.y - R.range) - (c.y - w.radius));
	}
	if (need <= -flt_max) return;
	float lift = _min(need + m_vho.p.spawn_lift, 2.f);
	if (lift <= 0.005f) return;
	Fmatrix m;
	m.translate(0.f, lift, 0.f);
	m_pPhysicsShell->TransformPosition(m, mh_clear);
	m_pPhysicsShell->set_LinearVel(Fvector().set(0.f, 0.f, 0.f));
	m_pPhysicsShell->set_AngularVel(Fvector().set(0.f, 0.f, 0.f));
	m_pPhysicsShell->GetGlobalTransformDynamic(&XFORM());
	Msg("[VHO] %s: wheels %.2f m into the ground at spawn; lifted %.2f m", *cName(), _max(need, 0.f), lift);
}

void CCar::HandlingRestore()
{
	m_pPhysicsShell->remove_ObjectContactCallback(HandlingCreatureContact);
	m_pPhysicsShell->remove_ObjectContactCallback(HandlingBodyContact);
	for (auto& it : m_wheels_map)
		HandlingSetWheelInertia(it.second, 0.f);
	HandlingInertiaAdd(Fvector().set(m_vho.s.inertia_add).invert());
	if (m_vho.s.chassis && m_vho.s.chassis_inertia_k > 0.f && m_vho.s.chassis_inertia_k != 1.f)
	{
		dMass m = *m_vho.s.chassis->getMassTensor();
		for (int i = 0; i < 12; ++i) m.I[i] /= m_vho.s.chassis_inertia_k;
		m_vho.s.chassis->setInertia(m);
		m_vho.s.chassis_inertia_k = 1.f;
	}
	m_vho.run_overs.clear();
	m_vho.dump_count = 0; // HandlingUpdateCL, which writes it, no longer runs
	HandlingTraceFree();
	for (auto& it : m_wheels_map)
	{
		SWheel& w = it.second;
		if (!w.joint || (w.vho_bump_s == 1.f && w.vho_bump_d == 1.f)) continue;
		float cs, cd;
		w.joint->GetJointSDfactors(cs, cd);
		w.joint->SetJointSDfactors(cs / w.vho_bump_s, cd / w.vho_bump_d);
		w.vho_bump_s = w.vho_bump_d = 1.f;
	}
	for (auto& it : m_wheels_map)
	{
		SWheel& w = it.second;
		if (!w.vho_axis_fixed) continue;
		w.vho_axis_fixed = false;
		if (!w.joint || !m_vho.s.chassis) continue;
		Fvector a;
		m_vho.s.chassis->XFORM().transform_dir(a, w.vho_axis_stock);
		w.joint->SetAxisDir(a, 0);
	}
	// free the motors of wheels the stock code does not drive (it rebuilds its own)
	for (SWheelDrive& d : m_vho.drive)
	{
		bool stock = false;
		for (SWheelDrive& s : m_driving_wheels)
			if (s.pwheel == d.pwheel) stock = true;
		if (!stock) d.pwheel->ApplyDriveAxisVelTorque(0.f, 0.f);
	}
	m_vho.drive.clear();
	const SHandlingStock& st = m_vho.stock;
	m_max_power = st.max_power;
	m_max_rpm = st.max_rpm;
	m_min_rpm = st.min_rpm;
	m_power_rpm = st.power_rpm;
	m_torque_rpm = st.torque_rpm;
	InitParabola();
	m_gear_ratious = st.gears;
	if (m_current_transmission_num >= m_gear_ratious.size())
		m_current_transmission_num = m_gear_ratious.size() - 1;
	m_current_gear_ratio = m_gear_ratious[m_current_transmission_num][0];

	HandlingApplySuspension(true);
	for (SWheelSteer& sw : m_steering_wheels)
		sw.pwheel->SetSteerLimits(sw.hi_limit, sw.lo_limit);
	SteerIdle();
	Unclutch();
	NeutralDrive();
	if (m_vho.s.hold_brake) HandBreak();
	m_car_sound->snd_engine.set_volume(m_car_sound->volume);
}

void CCar::HandlingApplySuspension(bool stock)
{
	const SHandlingParams& P = m_vho.p;
	float m = m_vho.s.sprung_corner_mass;
	for (auto& it : m_wheels_map)
	{
		SWheel& w = it.second;
		if (!w.joint || fis_zero(w.stock_spring) || fis_zero(w.stock_damping)) continue;
		float sf = 1.f, df = 1.f;
		if (!stock && P.susp_frequency > 0.f)
		{
			float omega = 2.f * PI * P.susp_frequency;
			float k = m * omega * omega;
			float c = 2.f * P.susp_damping * _sqrt(k * m);
			sf = (k / HINGE2_SPRING) / w.stock_spring;
			df = (c / HINGE2_DAMPING) / w.stock_damping;
		}
		// multiplicative, so wheel damage effects already applied to the joint are kept
		float cs, cd;
		w.joint->GetJointSDfactors(cs, cd);
		w.joint->SetJointSDfactors(cs * sf / w.vho_spring_mul, cd * df / w.vho_damping_mul);
		w.vho_spring_mul = sf;
		w.vho_damping_mul = df;
	}
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////
// script parameters

float CCar::GetHandlingParam(LPCSTR name)
{
	const SHandlingParamInfo* i = find_param(name);
	if (!i)
	{
		Msg("! [VHO] unknown handling parameter '%s'", name ? name : "");
		return 0.f;
	}
	return m_vho.p.*i->member;
}

bool CCar::SetHandlingParam(LPCSTR name, float value)
{
	const SHandlingParamInfo* i = find_param(name);
	if (!i || !_valid(value))
	{
		Msg("! [VHO] cannot set handling parameter '%s'", name ? name : "");
		return false;
	}
	m_vho.p.*i->member = value;
	// A script sets dozens of values in a row; apply them together on the next frame. The ones read live every
	// step need no apply at all (weather_grip changes every few seconds).
	static const LPCSTR live[] = { "weather_grip", "throttle_calm", "clutch_pedal", "slide_grip", "slide_speed",
		"handbrake_slip", "flip_speed", "cog_height", "spawn_lift", "bump_ramp", "drivetrain_inertia", "inertia_cap",
		"slip_rpm", "tc_stall_rpm", "tc_multiply", "auto_up_reserve", "hill_shift", "handbrake_rise", "park_release", "calm_hill", "brake_calm", "brake_build", "brake_bite",
		"stall_time", "idle_hold", "clutch_assist", "temp_warmup", "contact_max_vel",
		"body_friction", "wheel_climb", "run_over_mass", "ram_mass", "ram_damage", "ram_player_damage", "shock_recharge",
		"anomaly_pull", "anomaly_lift", "anomaly_lift_height", "anomaly_kick", "anomaly_fire_damage",
		"anomaly_push_damage", "see_through", "dodge", "dodge_see", "dodge_hear", "dodge_time",
		"temp_thermostat", "temp_derate", "temp_damage", "temp_seize", "temp_restart", "debug_trace" };
	for (LPCSTR l : live)
		if (!xr_strcmp(l, name)) return true;
	if (m_vho.enabled && m_pPhysicsShell) m_vho.s.apply_pending = true;
	return true;
}

bool CCar::SetHandlingGears(LPCSTR gears, float reverse, float final_drive)
{
	m_vho.gears.clear();
	if (gears)
	{
		string32 tmp;
		for (int i = 0, cnt = _GetItemCount(gears); i < cnt; ++i)
		{
			float g = (float)atof(_GetItem(gears, i, tmp));
			if (g > 0.f) m_vho.gears.push_back(g);
		}
	}
	if (reverse > 0.f) m_vho.p.reverse_ratio = reverse;
	m_vho.p.final_drive = (m_vho.gears.empty()) ? 0.f : final_drive;
	if (m_vho.enabled && m_pPhysicsShell) m_vho.s.apply_pending = true;
	return !m_vho.gears.empty() || final_drive <= 0.f;
}

float CCar::GetEngineRPM()
{
	return (m_vho.enabled ? m_vho.s.rpm : m_current_rpm) / RPM_TO_RAD;
}

LPCSTR CCar::GetSurface()
{
	u32 now = m_vho.s.step_count;
	for (SWheelDrive& d : m_driving_wheels)
		if (d.pwheel->contact_mtl && now - d.pwheel->contact_step < 4 && d.pwheel->contact_mtl->m_Name.size())
			return *d.pwheel->contact_mtl->m_Name;
	return "";
}

LPCSTR CCar::GetWheelDebug()
{
	static string1024 buf;
	buf[0] = 0;
	IKinematics* K = smart_cast<IKinematics*>(Visual());
	u32 now = m_vho.s.step_count;
	for (SWheelDrive& d : m_vho.drive)
	{
		string128 item;
		xr_sprintf(item, "%s %.0f%s  ", K ? K->LL_BoneName_dbg(d.pwheel->bone_id) : "?",
		           d.ASpeed() * d.pwheel->radius * 3.6f, now - d.pwheel->contact_step < 4 ? "*" : "");
		xr_strcat(buf, item);
	}
	return buf;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////
// input

bool CCar::HandlingInput(int cmd, bool pressed)
{
	switch (cmd)
	{
	case kFWD: fwp = pressed;
		break;
	case kBACK: bkp = pressed;
		break;
	case kL_STRAFE: lsp = pressed;
		if (OwnerActor()) OwnerActor()->steer_Vehicle(pressed ? -1 : (rsp ? 1 : 0));
		break;
	case kR_STRAFE: rsp = pressed;
		if (OwnerActor()) OwnerActor()->steer_Vehicle(pressed ? 1 : (lsp ? -1 : 0));
		break;
	case kJUMP: brp = pressed;
		if (pressed) HandlingHandBrakeTap();
		break;
	case kACCEL: if (pressed && m_vho.p.engine_shift_keys > 0.f) HandlingShiftRequest(1);
		break;
	case kSPRINT_TOGGLE: m_vho.s.sprint = pressed; // held: full throttle (throttle_calm without it)
		break;
	case kCROUCH: if (pressed && m_vho.p.engine_shift_keys > 0.f) HandlingShiftRequest(-1);
		break;
	default: return false;
	}
	if (pressed) m_pPhysicsShell->Enable();
	return true;
}

// a quick second pull while (nearly) stopped leaves the hand brake on; the next pull releases it
void CCar::HandlingHandBrakeTap()
{
	SHandlingState& S = m_vho.s;
	if (m_vho.p.handbrake_double_tap <= 0.f) return;
	u32 now = Device.dwTimeGlobal;
	if (S.hand_latched)
	{
		S.hand_latched = false;
		S.hand_tap = 0;
	}
	else if (S.hand_tap && now - S.hand_tap < u32(m_vho.p.handbrake_double_tap * 1000.f) && _abs(S.speed) < 2.f)
	{
		S.hand_latched = true;
		S.hand_tap = 0;
	}
	else
		S.hand_tap = now;
}

void CCar::HandlingShiftRequest(int dir)
{
	// clutch pedal mode: no shifting without the clutch down; below 1st is reverse
	bool clutch_mode = HandlingClutchMode();
	if (clutch_mode && !m_vho.s.clutch_held) return;
	size_t gear = CurrentTransmission();
	size_t top = m_gear_ratious.size() - 1;
	if (top < 1) return;
	if (m_vho.s.neutral)
		gear = dir > 0 ? 1 : 0; // out of neutral: up into 1st, down into reverse
	else if (gear == 0)
	{
		if (dir <= 0) return; // already in reverse
		gear = 1;
	}
	else if (dir > 0 && gear < top) ++gear;
	else if (dir < 0 && gear > 1) --gear;
	else if (dir < 0 && gear == 1) gear = 0; // below 1st is reverse
	else return;
	HandlingSelectGear(gear);
}

// Into a gear (0 = reverse), also out of neutral. Reverse only while the car is not still rolling forward faster
// than reverse_switch_speed; out of reverse into a forward gear always (rocking a stuck car, pulling away while it
// still rolls back: the clutch takes it). False when refused.
bool CCar::HandlingSelectGear(size_t gear)
{
	SHandlingState& S = m_vho.s;
	if (gear == 0 && (CurrentTransmission() != 0 || S.neutral) && S.speed > _max(m_vho.p.reverse_switch_speed, 0.5f))
		return false;
	S.neutral = false;
	HandlingShift(gear);
	S.auto_timer = 2.5f; // let a manual shift stand before the automatic box reconsiders
	return true;
}

void CCar::SetGear(int gear)
{
	if (!HandlingActive())
	{
		if (gear >= 0) Transmission(size_t(gear));
		return;
	}
	int top = int(m_gear_ratious.size()) - 1;
	if (top < 1) return;
	bool clutch_mode = HandlingClutchMode();
	if (clutch_mode && !m_vho.s.clutch_held) return;
	clamp(gear, 0, top); // 0 = reverse
	HandlingSelectGear(size_t(gear));
}

void CCar::ShiftGear(int dir)
{
	if (!HandlingActive())
	{
		if (dir > 0) TransmissionUp();
		else if (dir < 0) TransmissionDown();
		return;
	}
	HandlingShiftRequest(dir);
}

void CCar::HandlingShift(size_t gear)
{
	if (gear >= m_gear_ratious.size() || gear == m_current_transmission_num) return;
	const SHandlingParams& P = m_vho.p;
	SHandlingState& S = m_vho.s;
	if (P.shift_damage > 0.f && b_engine_on && gear > 0 && gear < m_current_transmission_num)
	{
		// a downshift that forces the engine past its limit hurts it once, more the faster the car goes
		float rpm_new = _abs(S.speed) / _max(m_ref_radius, 0.1f) * m_gear_ratious[gear][0];
		float overrev = P.overrev_rpm > 0.f ? P.overrev_rpm * RPM_TO_RAD : m_max_rpm * 1.05f;
		float excess = (rpm_new / overrev - 1.f) / 0.25f;
		if (excess > 0.f)
			S.pending_damage += P.shift_damage * _min(excess, 2.f) * clampr(_abs(S.speed) / 20.f, 0.25f, 1.5f);
	}
	m_current_transmission_num = gear;
	m_current_gear_ratio = m_gear_ratious[gear][0];
	S.shift_timer = P.shift_time;
	S.auto_timer = _max(S.auto_timer, P.shift_cooldown);
	S.locked = false;
	AscCall(ascSndTransmission);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////
// physics step

float CCar::HandlingEngineTorque(float rpm)
{
	// Parabola() is power; this is the same curve as torque (power / rpm), finite at 0 rpm
	float ex = (rpm - m_b) / m_c;
	// real engines are weak around idle and fade out below it (the curve alone would give most of peak torque
	// there): nothing at 20 % of idle, a quarter at half, nearly all at idle, full from 1.1 x idle. Below idle
	// the engine struggles but still pulls, so a little throttle or the clutch saves it; a too-high gear lugs and
	// stalls only when the load wins.
	float low = smoothstep01((rpm - m_min_rpm * 0.2f) / (m_min_rpm * 0.9f));
	return m_a * expf(-ex * ex) * low;
}

void CCar::HandlingPhTune(float step)
{
	// nothing while the physics sleeps: these forces would wake it every step, and a parked car never slept
	m_vho.s.dbg_ext_force.set(0.f, 0.f, 0.f);
	if (!m_pPhysicsShell->isEnabled())
	{
		m_vho.s.cog_prev_v.set(0.f, 0.f, 0.f); // asleep is at rest
		return;
	}
	const SHandlingParams& P = m_vho.p;
	if (P.drag > 0.f)
	{
		Fvector v;
		m_pPhysicsShell->get_LinearVel(v);
		float sp = v.magnitude();
		if (sp > 0.5f)
		{
			float f = P.drag * m_vho.s.mass_k * sp * sp;
			m_pPhysicsShell->applyForce(Fvector().set(v).mul(-1.f / sp), f);
			m_vho.s.dbg_ext_force.mad(v, -f / sp);
		}
	}

	SHandlingState& S = m_vho.s;
	if ((P.susp_preload > 0.f || P.susp_level > 0.f) && S.chassis)
	{
		// Static preload so the body rests near the model's pose instead of sagging on soft springs. With
		// susp_level it levels itself: each axle's preload follows its load over susp_level seconds until its
		// wheels rest ride_height below the model's pose, so the whole travel to the bump stop stays free
		// whatever the weight distribution. Both wheels of an axle level together, which leaves body roll alone.
		float mg = S.sprung_corner_mass * EffectiveGravity();
		float omega = 2.f * PI * _max(P.susp_frequency, 0.5f);
		float k = S.sprung_corner_mass * omega * omega;
		Fvector up = HandlingDir(S.ax_up);
		for (auto& it : m_wheels_map)
		{
			SWheel& w = it.second;
			if (!w.joint || !w.element) continue;
			if (P.susp_level > 0.f && S.step_count - w.contact_step < 3)
			{
				float comp = w.vho_comp;
				if (w.vho_arb && S.step_count - w.vho_arb->contact_step < 3) comp = (comp + w.vho_arb->vho_comp) * 0.5f;
				w.vho_preload = clampr(w.vho_preload + k * (comp + P.ride_height) * step / P.susp_level, 0.f, 3.f * mg);
			}
			Fvector a;
			w.joint->GetAxisDirDynamic(0, a);
			if (a.dotproduct(up) < 0.f) a.invert();
			S.chassis->applyImpulseVsGF(w.element->XFORM().c, a, w.vho_preload * step);
			w.element->applyForce(Fvector().set(a).invert(), w.vho_preload);
		}
	}
	if (P.cog_height > 0.f && S.cog_h > 0.f && S.chassis && step > 0.f)
	{
		// cog_height: the car's weight acts that high above the ground instead of where the model puts it.
		// Gravity and every acceleration act at the centre of gravity, so moving it by d changes the turning
		// moment by m d x (g - a), a being the body's acceleration (filtered; crash spikes capped): that is what
		// tips a car in a turn or on a slope. Free fall (a = g) gives none. At most 0.5 m from the model's.
		Fvector v, a, d, ga, t;
		m_pPhysicsShell->get_LinearVel(v);
		a.sub(v, S.cog_prev_v).mul(1.f / step);
		S.cog_prev_v = v;
		float g = EffectiveGravity();
		float am = a.magnitude();
		if (am > 3.f * g) a.mul(3.f * g / am);
		S.cog_acc.lerp(S.cog_acc, a, _min(1.f, step * 20.f));
		d = HandlingDir(S.ax_up);
		d.mul(clampr(P.cog_height - S.cog_h, -0.5f, 0.5f));
		ga.set(0.f, -g, 0.f).sub(S.cog_acc);
		t.crossproduct(d, ga).mul(m_pPhysicsShell->getMass());
		S.chassis->applyTorque(t.x, t.y, t.z);
	}
	HandlingBumpStops(step);
	HandlingFlip(step);
}

// hinge2 suspension has no travel limit, so soft springs let wheels sink into the arches over big bumps.
// A progressive rubber stop near the end of travel catches them: slow bumps stay soft, fast hits feel harsh.
void CCar::HandlingBumpStops(float step)
{
	const SHandlingParams& P = m_vho.p;
	SHandlingState& S = m_vho.s;
	bool valving = P.damper_bump != 1.f || P.damper_rebound != 1.f;
	bool progressive = P.damper_soft != 1.f || P.damper_firm != 1.f;
	float rest = P.susp_level > 0.f ? -P.ride_height : 0.f; // compression at rest
	float travel = _max(P.bump_start - rest, 0.02f); // from rest to the bump stop
	if ((P.bump_stiffness <= 0.f && P.anti_roll <= 0.f && !valving && !progressive && P.susp_level <= 0.f) ||
	    !S.chassis) return;
	float m = S.sprung_corner_mass;
	float omega = 2.f * PI * _max(P.susp_frequency, 0.5f);
	float k_spring = m * omega * omega;
	float f_max = 10.f * m * EffectiveGravity();
	const Fmatrix& X = S.chassis->XFORM();
	Fvector up = HandlingDir(S.ax_up);
	for (auto& it : m_wheels_map)
	{
		SWheel& w = it.second;
		if (!w.joint || !w.element) continue;
		Fvector rest, d;
		w.joint->GetAxisDirDynamic(0, w.vho_axis);
		if (w.vho_axis.dotproduct(up) < 0.f) w.vho_axis.invert();
		X.transform_tiny(rest, w.rest_local);
		d.sub(w.element->XFORM().c, rest);
		w.vho_comp_now = d.dotproduct(w.vho_axis);
	}
	for (auto& it : m_wheels_map)
	{
		SWheel& w = it.second;
		if (!w.joint || !w.element) continue;
		float comp = w.vho_comp_now;

		// Bump stop: past bump_start the joint's own spring and damping stiffen progressively. The joint is
		// solved implicitly, so even a very stiff stop stays stable on a heavy body with light wheels (forces
		// applied to the wheel directly made such cars bounce).
		float ns = 1.f, nd = 1.f;
		float x = comp - P.bump_start;
		if (P.bump_stiffness > 0.f && x > 0.f)
		{
			float t = x / _max(P.bump_ramp, 0.005f);
			ns = 1.f + P.bump_stiffness * t * (1.f + t);
			nd = 1.f + P.bump_damping * _min(t, 2.f);
		}
		// Damper valving: softer in bump than in rebound, so a bump is absorbed and the body is then held down
		// instead of bouncing back up. Past the knee speed the bump damping grows much more slowly (blow-off),
		// so sharp hits at speed are not passed straight into the body.
		if (valving && step > 0.f)
		{
			float v = (comp - w.vho_comp) / step;
			float f = P.damper_rebound;
			if (v > 0.f)
			{
				f = P.damper_bump;
				if (P.damper_knee > 0.f && v > P.damper_knee) f *= (P.damper_knee + (v - P.damper_knee) * 0.3f) / v;
			}
			nd *= f;
		}
		// Position-sensitive damping: damper_soft through the first half of the travel from rest to the bump
		// stop, rising smoothly to damper_firm at the stop, so small bumps stay soft and big ones are caught
		if (progressive)
			nd *= P.damper_soft + (P.damper_firm - P.damper_soft) * smoothstep01(((comp - rest) / travel - 0.5f) * 2.f);
		if (valving || progressive)
			nd = _max(0.05f, floorf(nd * 20.f + 0.5f) / 20.f); // in 5% steps, so the joint is not reset every step
		if (ns != w.vho_bump_s || nd != w.vho_bump_d)
		{
			float cs, cd;
			w.joint->GetJointSDfactors(cs, cd);
			w.joint->SetJointSDfactors(cs / w.vho_bump_s * ns, cd / w.vho_bump_d * nd);
			w.vho_bump_s = ns;
			w.vho_bump_d = nd;
		}

		// anti-roll bar: resists one side of an axle compressing more than the other (body roll), not bumps
		// that lift both wheels together. Applied as a force, so its rate is capped by what the wheel's mass
		// can take in one physics step.
		if (P.anti_roll > 0.f && w.vho_arb && w.vho_arb->element)
		{
			float k_arb = _min(k_spring * P.anti_roll, 0.25f * w.element->getMass() / (step * step));
			float f = clampr(k_arb * (comp - w.vho_arb->vho_comp_now), -f_max, f_max);
			S.chassis->applyImpulseVsGF(w.element->XFORM().c, w.vho_axis, f * step);
			w.element->applyForce(Fvector().set(w.vho_axis).invert(), f);
		}
	}
	for (auto& it : m_wheels_map)
		it.second.vho_comp = it.second.vho_comp_now;
}

// rolls a flipped car slowly back toward its wheels while RecoverFlip keeps being called (the use key held):
// sideways only, about its long axis, the way whoever holds the use key pushes it (RecoverFlip picks it)
void CCar::HandlingFlip(float step)
{
	const SHandlingParams& P = m_vho.p;
	SHandlingState& S = m_vho.s;
	if (S.flip_timer <= 0.f || !S.chassis) return;
	S.flip_timer -= step;
	Fvector Y = { 0.f, 1.f, 0.f };
	Fvector axis = HandlingDir(S.ax_fwd);
	Fvector up = HandlingDir(S.ax_up);
	float w_target, rem;
	if (_abs(axis.y) < 0.85f)
	{
		// roll angle to upright about the long axis: the car's up and the world's, seen along that axis
		Fvector yp, up_p, c;
		yp.set(Y).mad(axis, -axis.y).normalize_safe();
		up_p.set(up).mad(axis, -up.dotproduct(axis)).normalize_safe();
		float phi = atan2f(c.crossproduct(up_p, yp).dotproduct(axis), up_p.dotproduct(yp));
		if (_abs(phi) < 0.25f && up.y > 0.f)
		{
			S.flip_timer = 0.f;
			S.flip_sign = 0.f;
			return;
		}
		if (S.flip_sign == 0.f) S.flip_sign = phi >= 0.f ? 1.f : -1.f;
		rem = S.flip_sign > 0.f ? (phi >= 0.f ? phi : phi + PI_MUL_2) : (phi <= 0.f ? -phi : PI_MUL_2 - phi);
		w_target = S.flip_sign * _min(rem * 1.2f, P.flip_speed);
	}
	else
	{
		// standing on its nose or tail: tip it over the shortest way
		axis.crossproduct(up, Y);
		float s = axis.magnitude();
		rem = acosf(clampr(up.y, -1.f, 1.f));
		if (rem < 0.25f || s < 0.05f)
		{
			S.flip_timer = 0.f;
			S.flip_sign = 0.f;
			return;
		}
		axis.mul(1.f / s);
		w_target = _min(rem * 1.2f, P.flip_speed);
	}

	// A torque that beats gravity on the lower edge, servoed to a slow roll speed that eases off near upright. It is
	// a force pair on the body one metre above and below its centre, so the wheels simply follow.
	float mass = m_pPhysicsShell->getMass();
	float g = EffectiveGravity();
	float t_max = 2.f * mass * g * _max(S.track * 0.5f, 0.6f);
	Fvector w;
	m_pPhysicsShell->get_AngularVel(w);
	float torque = clampr((w_target - w.dotproduct(axis)) * t_max, -t_max, t_max);
	Fvector f_dir;
	f_dir.crossproduct(axis, Y); // force at +1 m up gives a torque along axis
	const Fvector& c = S.chassis->XFORM().c;
	float half = 0.5f * torque * step;
	S.chassis->applyImpulseVsGF(Fvector().add(c, Y), f_dir, half);
	S.chassis->applyImpulseVsGF(Fvector().sub(c, Y), Fvector().set(f_dir).invert(), half);
	m_pPhysicsShell->applyForce(0.f, 0.25f * g * mass, 0.f); // ease the edge's friction
	S.dbg_ext_force.y += 0.25f * g * mass;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////
// creature bodies, alive or as ragdolls, cannot shove a standing or slow car: those that may not push it
// (NPCs, ordinary mutants, and the heavy species too, which push with their attacks instead) meet it as a wall
// they cannot move. Drive-through creatures (small animals) also never launch a moving car: it knocks them down
// with a blunt hit and rolls over their bodies. Big creatures (ram_mass) hit by a moving car damage it and jolt
// the driver

void CCar::SetHandlingDriveThrough(LPCSTR species_list)
{
	m_vho.ghost_species.clear();
	m_vho.ghost_cache.clear();
	if (!species_list) return;
	string64 tmp;
	for (int i = 0, cnt = _GetItemCount(species_list); i < cnt; ++i)
	{
		_GetItem(species_list, i, tmp);
		if (tmp[0]) m_vho.ghost_species.push_back(tmp);
	}
}

bool CCar::HandlingIsGhost(CObject* obj)
{
	if (m_vho.ghost_species.empty() || smart_cast<CActor*>(obj)) return false;
	auto it = m_vho.ghost_cache.find(obj->ID());
	if (it != m_vho.ghost_cache.end()) return it->second;
	bool ghost = false;
	LPCSTR species = READ_IF_EXISTS(pSettings, r_string, obj->cNameSect(), "species", "");
	for (const shared_str& s : m_vho.ghost_species)
		if (!xr_strcmp(*s, species)) ghost = true;
	m_vho.ghost_cache[obj->ID()] = ghost;
	return ghost;
}

void CCar::HandlingCreatureContact(bool& do_colide, bool bo1, dContact& c, SGameMtl* material_1,
                                   SGameMtl* material_2)
{
	if (!do_colide) return;
	dxGeomUserData* ud1 = PHRetrieveGeomUserData(c.geom.g1);
	dxGeomUserData* ud2 = PHRetrieveGeomUserData(c.geom.g2);
	if (!ud1 || !ud2) return;
	CCar* car = smart_cast<CCar*>(ud1->ph_ref_object);
	IPhysicsShellHolder* other = ud2->ph_ref_object;
	if (!car)
	{
		car = smart_cast<CCar*>(ud2->ph_ref_object);
		other = ud1->ph_ref_object;
	}
	if (!car || !other || !car->m_vho.enabled) return;
	CEntityAlive* ent = smart_cast<CEntityAlive*>(other); // creatures, alive or as ragdolls (not cars)
	if (!ent || smart_cast<CActor*>(ent)) return;
	bool ghost = car->HandlingIsGhost(ent);
	Fvector v;
	car->m_pPhysicsShell->get_LinearVel(v);
	bool fast = v.square_magnitude() >= _sqr(car->m_vho.p.run_over_speed);
	// a moving car knocks people and other light creatures aside (HandlingRunOver) instead of riding up onto them
	// and being thrown into the air
	if (fast && (ghost || (car->m_vho.p.run_over_mass > 0.f && CreatureMass(ent) <= car->m_vho.p.run_over_mass)))
	{
		car->HandlingRunOver(ent, true);
		if (!ent->g_Alive())
		{
			do_colide = false; // bodies: the car rolls over them
			return;
		}
		// the living, small animals too, are hit, not driven through (without a contact the AI steps them right
		// through the car): a soft, frictionless contact that pushes them along the ground and never up, so the car
		// cannot climb onto them
		float h = _sqrt(c.geom.normal[0] * c.geom.normal[0] + c.geom.normal[2] * c.geom.normal[2]);
		if (h < 0.3f)
		{
			do_colide = false; // under or on top of the car: nothing to push sideways
			return;
		}
		c.geom.normal[0] /= h;
		c.geom.normal[1] = 0.f;
		c.geom.normal[2] /= h;
		MulSprDmp(c.surface.soft_cfm, c.surface.soft_erp, 0.05f, 0.2f);
		c.surface.mu = 0.f;
		car->HandlingLimitCorrection(c);
		return;
	}
	if (fast && ent->g_Alive()) car->HandlingRunOver(ent, false); // too heavy to knock aside: only the blow to the car
	if (!ghost && car->m_vho.p.creature_push >= 1.f) return; // MCM heavy cars off: stock contacts
	if (!fast)
	{
		// Slow: a soft, frictionless contact. It keeps the two apart and eases people out of the way, but cannot
		// shove the heavy car around. (A hand-made one-sided contact here could outlive a body that died.)
		MulSprDmp(c.surface.soft_cfm, c.surface.soft_erp, 0.01f, 0.1f);
		c.surface.mu = 0.f;
	}
}

// Car body (not the tyres) against everything but creatures: a limited push-out and no bounce, and on static
// ground body_friction, so a body that scrapes a rail or a kerb slides over it instead of catching and launching
void CCar::HandlingBodyContact(bool& do_colide, bool bo1, dContact& c, SGameMtl* material_1, SGameMtl* material_2)
{
	if (!do_colide) return;
	dxGeomUserData* ud1 = PHRetrieveGeomUserData(c.geom.g1);
	dxGeomUserData* ud2 = PHRetrieveGeomUserData(c.geom.g2);
	dxGeomUserData* own = bo1 ? ud1 : ud2;
	dxGeomUserData* other = bo1 ? ud2 : ud1;
	if (!own || !own->ph_ref_object) return;
	CCar* car = smart_cast<CCar*>(own->ph_ref_object);
	if (!car || !car->HandlingActive()) return;
	if (own->object_callbacks && own->object_callbacks->HasCallback(SWheel::WheellCollisionCallback)) return; // tyres
	if (other && other->ph_ref_object && smart_cast<CEntityAlive*>(other->ph_ref_object)) return; // creatures
	car->HandlingLimitCorrection(c);
	if ((!other || !other->ph_ref_object) && car->m_vho.p.body_friction > 0.f)
		c.surface.mu = _min(c.surface.mu, car->m_vho.p.body_friction);
}

// How fast a contact may push penetrating bodies apart (contact_max_vel). A fast car that sinks into a rail, a
// kerb or a body between two physics steps otherwise comes out at several m/s, which throws it into the air.
// The contact keeps its stiffness (ERP / CFM): lowering the ERP alone also capped the force a contact can hold at
// contact_max_vel / CFM (about 9 kN for a tyre), whatever the depth, so a heavy wheel, or any wheel over a bump,
// sank steadily into the ground.
void CCar::HandlingLimitCorrection(dContact& c) const
{
	float v = m_vho.p.contact_max_vel;
	if (v <= 0.f || c.geom.depth <= 0.f || !(c.surface.mode & dContactSoftERP)) return;
	float erp = v * fixed_step / c.geom.depth;
	if (c.surface.soft_erp > erp)
	{
		if ((c.surface.mode & dContactSoftCFM) && c.surface.soft_erp > 0.f) c.surface.soft_cfm *= erp / c.surface.soft_erp;
		c.surface.soft_erp = erp;
	}
	c.surface.mode &= ~dContactBounce;
}

// knock: knocked aside with a hit (light creatures and bodies); without, only the blow to the car (heavy ones,
// whose contact stays stock)
void CCar::HandlingRunOver(CObject* obj, bool knock)
{
	const SHandlingParams& P = m_vho.p;
	CEntityAlive* ea = smart_cast<CEntityAlive*>(obj);
	if (!ea) return;
	Fvector v;
	m_pPhysicsShell->get_LinearVel(v);
	float speed = v.magnitude();
	if (speed < P.run_over_speed) return;
	u32 now = Device.dwTimeGlobal;
	auto it = m_vho.run_over_time.find(obj->ID());
	if (it != m_vho.run_over_time.end() && now - it->second < 1000) return;
	m_vho.run_over_time[obj->ID()] = now;
	float m_car = P.real_mass > 0.f ? P.real_mass : m_pPhysicsShell->getMass();
	float m_c = _max(CreatureMass(ea), 1.f);
	SHandling::SRunOver r;
	r.id = obj->ID();
	r.alive = !!ea->g_Alive();
	Fvector fwd, side, rel;
	fwd.set(v).mul(1.f / speed);
	rel.sub(obj->Position(), Position());
	r.car_dir = fwd;
	r.power = r.impulse = r.car_dv = r.shove = 0.f;
	if (knock)
	{
		// knocked ahead and off to the side they are on, a little up; harder by a heavier car, which also needs less
		// speed to kill: a truck at walking pace still crushes a dog
		side.crossproduct(fwd, Fvector().set(0.f, 1.f, 0.f)).normalize_safe();
		if (rel.dotproduct(side) < 0.f) side.invert();
		r.dir.set(fwd).mul(0.75f).mad(side, 0.6f);
		r.dir.y += 0.35f;
		r.dir.normalize_safe();
		r.power = r.alive ? P.run_over_damage * speed * speed * _max(1.f, m_car / 1250.f) : 0.f;
		r.impulse = r.alive ? P.run_over_impulse * speed * clampr(m_car / 1250.f, 0.6f, 3.f) : m_c * speed * 1.2f;
		// the car loses the momentum it hands over, hardly anything for a heavy one (the living push back through
		// their contact)
		r.car_dv = r.alive ? 0.f : speed * m_c / (m_c + m_car);
		r.shove = r.alive ? m_c * speed : 0.f;
	}
	else
		r.dir = fwd;
	// a big creature is a blow to the car: the energy the impact takes from their closing speed (reduced mass), per
	// kg of car, so a heavier car takes less; the driver feels the speed it knocks off the car
	r.car_damage = r.jolt = 0.f;
	if (r.alive && P.ram_mass > 0.f && m_c >= P.ram_mass)
	{
		Fvector cv;
		cv.set(0.f, 0.f, 0.f);
		if (ea->character_physics_support() && ea->character_physics_support()->movement())
			cv.set(ea->character_physics_support()->movement()->GetVelocity());
		rel.y = 0.f;
		rel.normalize_safe();
		float closing = _max(0.f, Fvector().sub(v, cv).dotproduct(rel));
		float mu = m_c * m_car / (m_c + m_car);
		r.car_damage = P.ram_damage * P.collision_damage_scale * 0.5f * mu * closing * closing / m_car;
		r.jolt = mu * closing / m_car;
	}
	if (knock || r.car_damage > 0.f || r.jolt > 0.f) m_vho.run_overs.push_back(r);
}

// creature weight: the ragdoll's for a body, the character controller's for the living (they have no shell)
float CCar::CreatureMass(CEntityAlive* e)
{
	float m = e->GetMass();
	if (m <= 0.f && e->character_physics_support() && e->character_physics_support()->movement())
		m = e->character_physics_support()->movement()->GetMass();
	return m;
}

// People in the car's path who see it coming (or hear it close by) jump aside: a shove through their own
// movement controller, as a hit's knock-back gives, so no AI scheme or planner is touched. Only the player's car.
void CCar::HandlingDodge()
{
	const SHandlingParams& P = m_vho.p;
	SHandlingState& S = m_vho.s;
	if (P.dodge <= 0.f || !smart_cast<CActor*>(Owner())) return;
	u32 now = Device.dwTimeGlobal;
	if (now < S.dodge_next) return;
	S.dodge_next = now + 150;
	Fvector v;
	m_pPhysicsShell->get_LinearVel(v);
	v.y = 0.f;
	float speed = v.magnitude();
	if (speed < 2.5f) return;
	Fvector dir, pos = Position();
	dir.set(v).mul(1.f / speed);
	float nose = S.wheelbase * 0.6f + 1.f; // from the car's origin to its leading end, roughly
	float half_w = S.track * 0.5f + 0.8f;
	m_vho.near_objects.clear();
	Level().ObjectSpace.GetNearest(m_vho.near_objects, pos, speed * P.dodge_time + nose + half_w, this);
	float loud = 0.6f + 0.4f * clampr(S.rpm / _max(m_max_rpm, 1.f), 0.f, 1.f);
	for (CObject* o : m_vho.near_objects)
	{
		CAI_Stalker* st = smart_cast<CAI_Stalker*>(o);
		if (!st || !st->g_Alive() || st->wounded() || o == Owner()) continue;
		Fvector rel;
		rel.sub(st->Position(), pos);
		rel.y = 0.f;
		float along = rel.dotproduct(dir);
		if (along <= 0.f) continue;
		float ttc = _max(along - nose, 0.f) / speed; // until the leading end reaches them
		if (ttc > P.dodge_time) continue;
		Fvector lat;
		lat.set(rel).mad(dir, -along);
		float off = lat.magnitude();
		if (off > half_w) continue;
		auto it = m_vho.dodge_time.find(o->ID());
		if (it != m_vho.dodge_time.end() && now - it->second < 400) continue; // again while still in the way
		// they must notice it: in sight ahead of them, or close enough to hear (louder at high revs)
		float dist = rel.magnitude();
		Fvector look = st->Direction(), to_car;
		look.y = 0.f;
		look.normalize_safe();
		to_car.set(rel).invert().normalize_safe();
		bool sees = look.dotproduct(to_car) > 0.42f && dist < P.dodge_see;
		bool hears = dist < P.dodge_hear * loud;
		if (!sees && !hears) continue;
		CPHMovementControl* mc = st->character_physics_support() ? st->character_physics_support()->movement() : NULL;
		if (!mc) continue;
		m_vho.dodge_time[o->ID()] = now;
		Fvector side;
		side.crossproduct(Fvector().set(0.f, 1.f, 0.f), dir).normalize_safe();
		bool left = off > 0.05f ? lat.dotproduct(side) < 0.f : ::Random.randI(2) == 0;
		if (left) side.invert();
		float jump = clampr((half_w - off + 0.5f) / _max(ttc, 0.25f), 3.5f, 7.f);
		side.y = 0.3f;
		side.normalize_safe();
		mc->ApplyImpulse(side, mc->GetMass() * jump);
		// and the scripts have them run on that way for a few metres (vho_dodge: a generic AI scheme)
		::luabind::functor<void> dodge_funct;
		if (ai().script_engine().functor("_G.CCar__OnDodge", dodge_funct))
			dodge_funct(st->lua_game_object(), side.x, side.z);
		Msg("[VHO] %s jumps out of the way of %s (%s, %.1f s away)", *o->cName(), *cName(), sees ? "saw it" : "heard it", ttc);
	}
	if (m_vho.dodge_time.size() > 64) m_vho.dodge_time.clear();
}

bool CCar::IsFlipped()
{
	const SHandlingState& S = m_vho.s;
	if (!m_vho.enabled || !S.chassis) return false;
	Fvector v;
	m_pPhysicsShell->get_LinearVel(v);
	// on its side (the car's right axis points up or down) or upside down; a nose-down car in a ditch is not
	return (_abs(HandlingDir(S.ax_right).y) > 0.75f || HandlingDir(S.ax_up).y < -0.2f) && v.magnitude() < 2.f;
}

bool CCar::RecoverFlip()
{
	// once rolling it may carry on past the flipped test until it is upright (HandlingFlip clears the timer)
	if (!IsFlipped() && m_vho.s.flip_timer <= 0.f) return false;
	if (m_vho.s.flip_timer <= 0.f)
	{
		// a new push: the car rolls away from the player, its top toward the side he pushes it to (in front of or
		// behind it, or with nobody near, the short way back onto its wheels)
		m_vho.s.flip_sign = 0.f;
		CActor* a = Actor();
		if (a && m_vho.s.chassis)
		{
			Fvector axis = HandlingDir(m_vho.s.ax_fwd), rel, top;
			rel.sub(m_vho.s.chassis->XFORM().c, a->Position());
			rel.y = 0.f;
			top.crossproduct(axis, Fvector().set(0.f, 1.f, 0.f)); // where the top moves rolling about +axis
			float d = top.dotproduct(rel), len = rel.magnitude() * top.magnitude();
			if (len > EPS && _abs(d) > 0.3f * len) m_vho.s.flip_sign = d > 0.f ? 1.f : -1.f;
		}
	}
	m_vho.s.flip_timer = 0.3f;
	m_pPhysicsShell->Enable();
	return true;
}

Fvector CCar::GetDriverEyePos()
{
	IKinematics* K = smart_cast<IKinematics*>(Visual());
	Fmatrix inv;
	inv.invert(K->LL_GetTransform(0));
	Fvector p;
	inv.transform_tiny(p, m_camera_position);
	return p;
}

bool CCar::SetLowRange(bool on)
{
	if (!HandlingActive() || m_vho.p.low_range <= 1.f) return false;
	if (on == m_vho.s.low_range_on) return true;
	if (_abs(m_vho.s.speed) > 2.f) return false; // the transfer box only shifts at a crawl
	m_vho.s.low_range_on = on;
	HandlingApply();
	return true;
}

bool CCar::SetDiffLock(bool on)
{
	if (!HandlingActive() || m_vho.p.diff_lock <= 0.f) return false;
	m_vho.s.diff_locked = on;
	return true;
}

void CCar::HandlingPhUpdate(float step)
{
	const SHandlingParams& P = m_vho.p;
	SHandlingState& S = m_vho.s;
	++S.step_count;
	if (m_gear_ratious.size() < 2 || m_driving_wheels.empty()) return;

	// forward is toward the steering wheels, also on a model built facing -z
	Fvector fwd = HandlingDir(S.ax_fwd);
	fwd.mul(S.front_sign);
	Fvector lin_vel;
	m_pPhysicsShell->get_LinearVel(lin_vel);
	S.speed = lin_vel.dotproduct(fwd);

	// crash: a sudden loss of horizontal speed (walls, trees, vehicles; bodies are too light to cause one).
	// The speed lost over about 0.15 s is collected, then reported once to scripts and may stall the engine.
	{
		Fvector dv;
		dv.sub(lin_vel, S.prev_vel);
		dv.y = 0.f;
		S.prev_vel = lin_vel;
		float lost = dv.magnitude();
		if (lost > 40.f) lost = 0.f; // teleport or level travel, not a crash
		S.dbg_crash_lost = lost;
		S.crash_dv = S.crash_dv * (1.f - _min(1.f, step * 8.f)) + lost;
		if (S.crash_cooldown > 0.f)
		{
			S.crash_cooldown -= step;
			if (S.last_crash_speed < 0.f)
			{
				S.last_crash_speed = -_max(-S.last_crash_speed, S.crash_dv); // negative while collecting
				if (S.crash_cooldown <= 0.f)
				{
					S.last_crash_speed = -S.last_crash_speed;
					++S.crash_count;
					HandlingTraceEvent(SHandling::EV_CRASH, "crash", 100);
					if (P.crash_stall_speed > 0.f && S.last_crash_speed >= P.crash_stall_speed) HandlingStall(3);
					S.crash_cooldown = 1.f;
				}
			}
		}
		else if (S.crash_dv > 5.f)
		{
			S.last_crash_speed = -S.crash_dv;
			S.crash_cooldown = 0.15f;
		}
	}

	// ---- pedals and direction
	// Two pedals, in every gearbox mode: forward is the gas in whatever gear is in (in reverse it backs the car up,
	// in neutral it only revs), whichever way the car rolls; back is the brake, only, and holds the car at a
	// standstill. Both at once: braking with the revs up. Reverse is chosen like any gear (gear down below 1st, or the
	// 1st gear key held; HandlingSelectGear). Without the sprint key the throttle opens only to throttle_calm.
	bool clutch_mode = HandlingClutchMode();
	bool reverse_gear = (CurrentTransmission() == 0);
	float throttle_in = fwp ? 1.f : 0.f, brake_in = bkp ? 1.f : 0.f;
	// Without sprint the pedal goes only to throttle_calm, deeper up a slope the way the car is heading (calm_hill:
	// full from about 3 % + 1 / calm_hill), as a calm driver still presses harder to keep a car climbing
	float calm = P.throttle_calm;
	if (P.calm_hill > 0.f && calm < 1.f)
	{
		float climb = reverse_gear ? -fwd.y : fwd.y;
		calm += (1.f - calm) * clampr((climb - 0.03f) * P.calm_hill, 0.f, 1.f);
	}
	bool sprint = S.sprint;
	throttle_in *= sprint ? 1.f : calm;
	// the same for the brake key: without sprint it presses the pedal only to brake_calm, short of locking the
	// wheels on most ground; with sprint held it stamps on it
	brake_in *= sprint ? 1.f : clampr(P.brake_calm, 0.f, 1.f);
	// and it builds while held, as a foot squeezes a pedal: it bites at brake_bite of that, then presses harder over
	// brake_build seconds (half that with sprint), so a tap slows the car and only a long press brakes hard
	if (brake_in > 0.f && P.brake_build > 0.f)
	{
		S.brake_hold = _min(1.f, S.brake_hold + step / (P.brake_build * (sprint ? 0.5f : 1.f)));
		float bite = clampr(P.brake_bite, 0.f, 1.f);
		brake_in *= bite + (1.f - bite) * S.brake_hold;
	}
	else
		S.brake_hold = 0.f;
	throttle_in *= ThrottleFactor();

	float health = GetfHealth();
	bool engine_ok = b_engine_on && health > P.disable_health;
	if (!engine_ok) throttle_in = 0.f;
	// park_release: a latched parking brake lets go when the driver pulls away (throttle in gear, clutch out);
	// left on, it braked every wheel and the car crawled
	if (S.hand_latched && P.park_release > 0.f && throttle_in > 0.f && !S.neutral && !S.clutch_held)
		S.hand_latched = false;
	S.throttle = approach(S.throttle, throttle_in, P.throttle_rise, P.throttle_fall, step);
	S.brake = approach(S.brake, brake_in, P.brake_rise, 8.f, step);
	S.dbg_thr_in = throttle_in;
	S.dbg_brk_in = brake_in;
	if ((S.throttle > 0.f || S.brake > 0.f) && !m_pPhysicsShell->isEnabled()) m_pPhysicsShell->Enable();

	// hand brake: pulled at a standstill it also opens the clutch (free revs, a launch on release); at speed it
	// takes grip from the rear tyres (HandlingContact), so a short pull starts a slide instead of braking hard
	bool hand = brp || S.hold_brake || S.hand_latched;
	// the lever takes handbrake_rise seconds to pull fully, so a tap only slows the rear wheels
	S.hand_k = P.handbrake_rise > 0.f ? approach(S.hand_k, brp ? 1.f : 0.f, 1.f / P.handbrake_rise, 8.f, step) :
		(brp ? 1.f : 0.f);
	// only from a standstill: at speed the hand brake works on the rear wheels and leaves the clutch alone
	// (never with the clutch pedal: the driver works the clutch)
	// (handbrake_declutch 2: at any speed, so locking the rear wheels never drags the engine down and stalls it)
	S.declutched = !clutch_mode && brp && P.handbrake_declutch > 0.f &&
		(P.handbrake_declutch >= 2.f || S.declutched || _abs(S.speed) < 1.5f);
	S.hand_slide = approach(S.hand_slide, brp ? S.hand_k : 0.f, 10.f, P.handbrake_recover, step);

	// ---- gearbox
	S.shift_timer = _max(0.f, S.shift_timer - step);
	S.auto_timer = _max(0.f, S.auto_timer - step);
	float wheel_w = 0.f;
	for (SWheelDrive& d : m_vho.drive)
		wheel_w += d.ASpeed();
	wheel_w /= _max(float(m_vho.drive.size()), 1.f);
	S.wheel_dist += _abs(wheel_w) * m_ref_radius * step; // like a gearbox-driven odometer: wheelspin counts
	float rpm_idle = m_min_rpm;
	float rpm_max = m_max_rpm;
	// The automatic box decides from ground speed (not wheel spin, which jumps over bumps and in the air)
	// and from a slow throttle average, with hysteresis between the up and down points.
	float ground_target = _abs(S.speed) / _max(m_ref_radius, 0.1f);
	S.ground_w += (ground_target - S.ground_w) * _min(1.f, step * 4.f);
	S.shift_throttle += (S.throttle - S.shift_throttle) * _min(1.f, step * 1.5f);
	bool grounded = false;
	for (SWheelDrive& d : m_vho.drive)
		if (S.step_count - d.pwheel->contact_step < 6) grounded = true;

	if (P.automatic > 0.f && !reverse_gear && !S.neutral && engine_ok && grounded && S.shift_timer <= 0.f &&
	    S.auto_timer <= 0.f)
	{
		size_t gear = CurrentTransmission();
		size_t top = m_gear_ratious.size() - 1;
		float thr = S.shift_throttle;
		// uphill the box shifts as at full throttle, keeping the revs up until the slope eases (hill_shift)
		if (P.hill_shift > 0.f) thr = _max(thr, clampr((fwd.y - 0.02f) * P.hill_shift, 0.f, 1.f));
		float up_full = P.shift_up_rpm > 0.f ? P.shift_up_rpm * RPM_TO_RAD : _min(m_power_rpm * 1.05f, rpm_max * 0.95f);
		float down_full = P.shift_down_rpm > 0.f ? P.shift_down_rpm * RPM_TO_RAD : _max(m_torque_rpm * 0.75f, rpm_idle * 1.5f);
		// calm driving shifts early and keeps the revs low (about 40 % of the full-throttle point at a light
		// throttle, 60 % at the calm one), a floored pedal holds each gear to the power peak
		float up = _max(up_full * (0.38f + 0.62f * thr * thr), rpm_idle * 2.2f);
		float down = rpm_idle * 1.3f + (down_full - rpm_idle * 1.3f) * thr * thr;
		float rpm_here = S.ground_w * _abs(m_gear_ratious[gear][0]);

		// What the car works against (slope, rolling, drag) and what a gear can pull at full throttle, both in
		// real-vehicle newtons: no upshift into a gear that would bog down on a hill, kick down when this one can't.
		float m_real = P.real_mass > 0.f ? P.real_mass : m_pPhysicsShell->getMass();
		float r = _max(m_ref_radius, 0.1f);
		float load = m_real * EffectiveGravity() * (fwd.y + P.rolling_resistance) + P.drag * S.speed * S.speed;
		auto pull = [&](size_t g) {
			float ratio_g = _abs(m_gear_ratious[g][0]);
			float rpm_g = clampr(S.ground_w * ratio_g, rpm_idle, rpm_max);
			return HandlingEngineTorque(rpm_g) * P.power_scale * ratio_g * P.driveline_efficiency / r;
		};
		float reserve = m_real * P.auto_up_reserve; // acceleration left in hand after an upshift, m/s2

		// never up while braking (the slow throttle average still remembered the throttle before it)
		if (gear < top && S.locked && S.brake < 0.05f && rpm_here > up && S.ground_w * m_gear_ratious[gear + 1][0] > down * 1.15f &&
		    (rpm_here > rpm_max * 0.97f || pull(gear + 1) > load + reserve))
			HandlingShift(gear + 1);
		else if (gear > 1 && rpm_here < down && S.ground_w * m_gear_ratious[gear - 1][0] < up * 0.85f)
			HandlingShift(gear - 1);
		else if (gear > 1 && thr > 0.7f && pull(gear) < load && S.ground_w * m_gear_ratious[gear - 1][0] < up * 0.9f)
		{
			HandlingShift(gear - 1); // kick down: this gear is losing the fight against the hill
			S.auto_timer = _max(S.auto_timer, P.shift_cooldown * 2.f); // and stay down for a while
		}
	}

	// ---- engine and automatic clutch
	float ratio = m_current_gear_ratio;
	float abs_ratio = _abs(ratio);
	float shaft = wheel_w * ratio; // engine-side speed, positive when turning the way the gear drives
	// The clutch is open while shifting (the automatic clutch), with the hand brake pulled at a standstill, in
	// neutral, and with the clutch pedal while its key is down. The clutch pedal never opens it by itself.
	bool shifting = !clutch_mode && S.shift_timer > 0.f;
	bool pedal = S.clutch_held; // the clutch key works with the automatic clutch too
	bool rev_free = S.declutched || pedal || S.neutral; // engine revs freely
	float rpm_launch = _max(P.launch_rpm * RPM_TO_RAD, rpm_idle * 1.1f);
	u32 tf = 0; // trace flags of this step
	bool clutch_open = shifting || rev_free; // nothing reaches the wheels: no drive, no engine braking

	if (!b_engine_on)
	{
		S.locked = false;
		S.rpm = approach(S.rpm, 0.f, 0.f, P.rev_down_rate * RPM_TO_RAD, step);
	}
	else
	{
		bool braking = S.brake > 0.05f || hand; // braking opens it, throttle or not: a brake never stalls it
		// Driven wheels skidding far below the road speed (braked into a lock): the automatic clutch lets go, as a
		// driver dips the clutch, so the engine drops to idle instead of being dragged down to a stop with the
		// wheels. It bites again once they roll. (The clutch pedal is the driver's.)
		float road = S.ground_w * abs_ratio;
		bool skid = braking && !clutch_mode && road > rpm_idle && shaft < road * 0.7f;
		bool open = shifting || rev_free || skid;
		clutch_open = open;
		// Manual box in a gear above launch_gears behaves like a real clutch left engaged: it neither slips to
		// save a labouring engine nor holds launch revs while pulling away, and coasting off the throttle keeps the
		// engine connected, so too high a gear for the speed and slope stalls it. Only braking (foot brake or hand
		// brake with the throttle shut) opens the clutch, like a driver dipping it to stop. The automatic (torque
		// converter), the launch gears and reverse keep the forgiving clutch.
		bool high_gear = P.lug_stall > 0.f && P.automatic <= 0.f && !reverse_gear &&
			CurrentTransmission() > size_t(_max(P.launch_gears, 1.f));
		// with the clutch pedal every gear is strict: let the clutch out too early or too slowly and it stalls
		bool strict = clutch_mode || (high_gear && !braking);
		bool can_stall = P.lug_stall > 0.f;
		// the engine dies only after stall_time below stall_rpm (default half the idle): a wheel catching a rock
		// or a dip in the revs while the driver reacts is survived, a gear the car cannot pull is not
		float rpm_stall = P.stall_rpm > 0.f ? P.stall_rpm * RPM_TO_RAD : rpm_idle * 0.5f;
		bool dying = false; // held below rpm_stall this step
		// Under throttle the automatic clutch (or the torque converter) lets the engine run up toward slip_top
		// while the wheels are slow (pulling away, a steep hill, deep mud): it slips instead of lugging, so the
		// revs rise with the throttle and the engine works where it has torque.
		float slip_top = P.automatic > 0.f ?
			(P.tc_stall_rpm > 0.f ? P.tc_stall_rpm * RPM_TO_RAD : rpm_launch * 1.2f) :
			(P.slip_rpm > 0.f ? P.slip_rpm * RPM_TO_RAD : rpm_launch);
		slip_top = _min(slip_top, rpm_max * 0.9f);
		float slip_target = rpm_idle + (slip_top - rpm_idle) * S.throttle;
		bool lugging = !strict && S.throttle > 0.3f && shaft < slip_target * 0.85f;
		if (S.locked && (open || (shaft < rpm_idle * 0.9f && !strict) || lugging)) S.locked = false;
		if (!S.locked && !open && !lugging && shaft >= rpm_idle && shaft >= S.rpm - 50.f * RPM_TO_RAD) S.locked = true;
		S.clutch_engage = strict && !S.locked && !open ? _min(1.f, S.clutch_engage + step / _max(P.clutch_time, 0.1f)) : 0.f;
		tf |= (strict ? TF_STRICT : 0) | (braking ? TF_BRAKING : 0) | (lugging ? TF_LUGGING : 0) | (open ? TF_OPEN : 0) |
			(high_gear ? TF_HIGH_GEAR : 0) | (!open && shaft < -0.1f * rpm_idle ? TF_AGAINST : 0) |
			(strict && !S.locked && !open ? TF_ENGAGING : 0);

		if (S.locked)
		{
			S.rpm += (shaft - S.rpm) * _min(1.f, step * 25.f);
			if (strict && S.rpm < rpm_stall)
			{
				if (!can_stall) S.rpm = rpm_stall;
				else dying = true;
			}
		}
		else if (strict && !open)
		{
			// the clutch drags the engine down toward the gearbox speed; the engine fights back with its torque.
			// Letting the clutch out, the engine helps itself to up to clutch_assist of throttle while its revs sag
			// below 1.5 x idle, a touch of gas a driver would give: without the gas key the car moves off gently and
			// the revs settle back to idle, with it the driver's throttle is the gas.
			float assist = 0.f;
			if (P.clutch_assist > 0.f)
			{
				float top = rpm_idle * 1.5f;
				assist = clampr(P.clutch_assist, 0.f, 1.f) * clampr((top - S.rpm) / _max(top - rpm_stall, 1.f), 0.f, 1.f);
			}
			float t_avail = HandlingEngineTorque(S.rpm) * P.power_scale * _max(S.throttle, _max(0.2f, assist));
			float t_clutch = S.clutch_engage * 1.5f * m_a * P.power_scale;
			float d_rpm = (t_avail - t_clutch) / _max(P.engine_inertia, 0.05f) * step;
			S.rpm = d_rpm > 0.f ? _min(S.rpm + d_rpm, _max(S.rpm, rpm_launch)) : S.rpm + d_rpm;
			if (S.rpm <= shaft + 50.f * RPM_TO_RAD) S.locked = true;
			else if (S.rpm < rpm_stall)
			{
				if (!can_stall) S.rpm = rpm_stall;
				else dying = true;
			}
		}
		else
		{
			float thr = shifting ? 0.f : S.throttle;
			// Pulling away, the auto-clutch loads the engine down to launch revs. Rolling the wrong way for the
			// gear (back down a hill in 1st) nothing holds the revs down, so they follow the throttle.
			bool against = !open && shaft < -0.1f * rpm_idle;
			float top = rev_free ? rpm_max : (against ? rpm_max * 0.9f : slip_top);
			float target = rpm_idle + (top - rpm_idle) * thr;
			if (!open && shaft > target) target = shaft;
			// free revs follow the torque curve: quick through the mid range, slower near the top
			float curve = m_a > EPS ? clampr(HandlingEngineTorque(S.rpm) / m_a, 0.3f, 1.f) : 1.f;
			S.rpm = approach(S.rpm, target, P.rev_up_rate * RPM_TO_RAD * _max(thr, 0.35f) * curve,
			                 P.rev_down_rate * RPM_TO_RAD, step);
			if (rev_free && S.rpm >= rpm_max) S.rpm = rpm_max - 250.f * RPM_TO_RAD; // limiter bounce
		}
		// below stall rpm the engine labours (never backwards); it dies once that has lasted stall_time, unless the
		// clutch, the throttle or the wheels bring it back first
		S.stall_timer = dying ? S.stall_timer + step : 0.f;
		if (dying) S.rpm = _max(S.rpm, 0.f);
		if (dying && S.stall_timer >= P.stall_time)
		{
			S.stall_timer = 0.f;
			HandlingStall(4);
		}
	}
	S.clutch = S.locked ? 1.f : (rev_free || S.rpm <= EPS ? 0.f : _max(0.f, _min(1.f, shaft / S.rpm)));
	tf |= (S.locked ? TF_LOCKED : 0) | (shifting ? TF_SHIFTING : 0) | (rev_free ? TF_REV_FREE : 0) |
		(S.declutched ? TF_DECLUTCHED : 0) | (S.neutral ? TF_NEUTRAL : 0) | (pedal ? TF_PEDAL : 0) |
		(b_engine_on ? TF_ENGINE_ON : 0) | (clutch_mode ? TF_CLUTCH_MODE : 0) | (P.automatic > 0.f ? TF_AUTOMATIC : 0) |
		(hand ? TF_HAND : 0) | (grounded ? TF_GROUNDED : 0);
	S.dbg_shaft = shaft;

	// Drivetrain inertia: with the clutch in, the engine, flywheel and gearbox turn with the driven wheels
	// (engine_inertia x ratio^2, shared out between them). A wheel that loses grip in a spin, a slide or a jump
	// keeps turning, and the revs with it, until the tyres slow it, as on a real car. A slipping clutch or a torque
	// converter couples part of it, an open one none. Changed in steps so the bodies are not reset every step.
	if (P.drivetrain_inertia > 0.f)
	{
		float couple = !b_engine_on || clutch_open ? 0.f : (S.locked ? 1.f : 0.35f);
		float n = _max(float(m_vho.drive.size()), 1.f);
		float extra = couple * P.drivetrain_inertia * P.engine_inertia * abs_ratio * abs_ratio * P.driveline_efficiency *
			S.mass_k / n;
		extra = _min(extra, P.inertia_cap);
		for (SWheelDrive& d : m_vho.drive)
		{
			float cur = d.pwheel->vho_inertia;
			if ((extra <= 0.f) != (cur <= 0.f) || _abs(extra - cur) > 0.1f * _max(extra, cur))
				HandlingSetWheelInertia(*d.pwheel, extra);
		}
	}

	float health_k = 1.f;
	if (health < P.low_health_start && P.low_health_start > P.disable_health)
	{
		float t = (health - P.disable_health) / (P.low_health_start - P.disable_health);
		clamp(t, 0.f, 1.f);
		health_k = P.low_health_power + (1.f - P.low_health_power) * t;
	}

	// idle control: with the clutch in and the revs dragged below idle, the engine opens its own throttle up to
	// idle_hold (fully at stall rpm) to hold the idle, as a real one does, so it creeps instead of dying
	float thr_engine = S.throttle;
	if (P.idle_hold > 0.f && b_engine_on && (S.locked || S.clutch_engage > 0.f) && S.rpm < rpm_idle)
	{
		float stall = P.stall_rpm > 0.f ? P.stall_rpm * RPM_TO_RAD : rpm_idle * 0.5f;
		float k = clampr((rpm_idle - S.rpm) / _max(rpm_idle - stall, 1.f), 0.f, 1.f);
		thr_engine = _max(thr_engine, clampr(P.idle_hold, 0.f, 1.f) * k);
	}
	float t_engine = engine_ok ? HandlingEngineTorque(S.rpm) * P.power_scale * health_k * S.heat_k * thr_engine : 0.f;
	if (S.rpm >= rpm_max) t_engine = 0.f; // rev limiter
	float t_shaft = 0.f;
	// shifting, the clutch key, neutral, the declutched hand brake or a braked skid: the clutch is open and the wheels
	// roll free of the engine (only brakes, tyres and air slow the car)
	if (b_engine_on && !clutch_open)
	{
		if (S.locked)
		{
			// engine braking grows with the square of the revs: off the throttle the car mostly glides and the
			// revs fall slowly with the speed; only high revs (low gear, over-rev) hold it back hard
			float rev = S.rpm / rpm_max;
			float t_brake = P.engine_brake * m_a * rev * rev * (1.f - thr_engine);
			t_shaft = t_engine - t_brake;
		}
		else if (S.clutch_engage > 0.f)
			t_shaft = S.clutch_engage * 1.5f * m_a * P.power_scale; // what the clutch passes while it bites
		else
		{
			t_shaft = t_engine; // slipping clutch passes engine torque, not engine braking
			// a torque converter multiplies it while slipping hard, fading out as the speeds meet
			if (P.automatic > 0.f && P.tc_multiply > 1.f && S.rpm > EPS)
				t_shaft *= 1.f + (P.tc_multiply - 1.f) * clampr(1.f - shaft / (S.rpm * 0.85f), 0.f, 1.f);
		}
	}
	m_current_rpm = S.rpm;
	m_current_engine_power = t_engine * S.rpm;

	float overrev = P.overrev_rpm > 0.f ? P.overrev_rpm * RPM_TO_RAD : rpm_max * 1.05f;
	S.overrev = b_engine_on && S.rpm > overrev;
	if (S.overrev && P.overrev_damage > 0.f)
		S.pending_damage += P.overrev_damage * _min((S.rpm / overrev - 1.f) / 0.25f, 2.f) * step;

	// ---- torques per wheel (positive drive = toward the gear's direction)
	float dir = ratio < 0.f ? -1.f : 1.f;
	float wheel_load = m_pPhysicsShell->getMass() * EffectiveGravity() / _max(u32(m_wheels_map.size()), u32(1));
	for (auto& it : m_wheels_map)
	{
		SWheel& w = it.second;
		w.vho_drive = 0.f;
		const SHandlingSurface* s = (S.step_count - w.contact_step < 4) ? HandlingSurface(w.contact_mtl) : NULL;
		float s_grip, s_rolling, s_slip;
		HandlingSurfaceFactors(s, s_grip, s_rolling, s_slip);
		float rolling = P.rolling_resistance * s_rolling;
		w.vho_resist = m_axle_friction + rolling * wheel_load * w.radius;
	}

	float t_wheels = t_shaft * abs_ratio * P.driveline_efficiency * S.mass_k / _max(float(m_vho.drive.size()), 1.f);
	if (S.locked && t_wheels > 0.f && P.engine_inertia > 0.f && P.drivetrain_inertia <= 0.f) // else the bodies carry it
	{
		// the engine and flywheel spin up with the wheels, ratio^2 times harder in low gears: 1st gear pulls
		// noticeably less than its torque alone suggests, top gear hardly changes
		float m_real = P.real_mass > 0.f ? P.real_mass : m_pPhysicsShell->getMass();
		float r = _max(m_ref_radius, 0.1f);
		t_wheels *= m_real / (m_real + P.engine_inertia * abs_ratio * abs_ratio / (r * r));
	}
	for (SWheelDrive& d : m_vho.drive)
		d.pwheel->vho_drive += t_wheels * d.gear_factor;
	S.dbg_t_engine = t_engine;
	S.dbg_t_shaft = t_shaft;
	S.dbg_t_wheels = t_wheels;
	S.dbg_flags = tf;

	auto brake_wheel = [&](SWheel& w, float foot, float hand_torque) {
		w.vho_resist += BRAKE_UNIT * foot * P.brake_scale * S.mass_k * S.brake;
		if (hand && (S.hold_brake || S.hand_latched || P.handbrake_rear_only <= 0.f || HandlingIsRear(w)))
			w.vho_resist += BRAKE_UNIT * hand_torque * P.handbrake_scale * S.mass_k * ((S.hold_brake || S.hand_latched) ? 1.f : S.hand_k);
	};
	if (P.brake_g > 0.f)
		for (auto& it : m_wheels_map)
		{
			// sized from the vehicle (HandlingSizeBrakes): every wheel brakes, as on every real vehicle
			SWheel& w = it.second;
			w.vho_resist += w.vho_brake_full * P.brake_scale * S.brake;
			if (hand && (S.hold_brake || S.hand_latched || P.handbrake_rear_only <= 0.f || HandlingIsRear(w)))
				w.vho_resist += w.vho_hand_full * P.handbrake_scale * ((S.hold_brake || S.hand_latched) ? 1.f : S.hand_k);
		}
	else if (P.brake_all_wheels > 0.f && !m_breaking_wheels.empty())
	{
		// models that leave wheels without brakes (KamAZ semi) brake every wheel with their mean brake
		float foot = 0.f, hand_t = 0.f;
		for (SWheelBreak& b : m_breaking_wheels)
		{
			foot += b.break_torque;
			hand_t += b.hand_break_torque;
		}
		foot /= float(m_breaking_wheels.size());
		hand_t /= float(m_breaking_wheels.size());
		for (auto& it : m_wheels_map)
			brake_wheel(it.second, foot, hand_t);
	}
	else
		for (SWheelBreak& b : m_breaking_wheels)
			brake_wheel(*b.pwheel, b.break_torque, b.hand_break_torque);

	// one joint motor per wheel: either a torque source toward the drive direction or a brake toward zero.
	// The velocity cap is the engine's inertia: a wheel that loses grip spins up about as fast as the engine
	// revs through the gear, so wheelspin shows in the rpm instead of the wheel flaring instantly.
	float spin_step = _max(2.f * P.rev_up_rate * RPM_TO_RAD * step / _max(abs_ratio, 1.f), 0.2f);
	// Differentials. Drive torque is handed out in equal shares, so an open differential is modelled by what it
	// can pass on: when one side runs away from the other (a wheel in the air, on ice or in mud) by more than the
	// turn and ordinary tyre slip explain, the side that grips gets only what the spinning side can hold, almost
	// nothing, while the spinning side spins up and the engine revs with it instead of pushing the car. A self-locking axle differential keeps
	// diff_axle_bias of it (GAZ-66). The diff lock locks the centre / inter-axle differential, with
	// diff_lock_axles the axle differentials too.
	bool lock_centre = S.diff_locked && P.diff_lock > 0.f;
	bool lock_axles = lock_centre && P.diff_lock_axles > 0.f;
	S.dbg_flags |= (lock_centre ? TF_LOCK_CENTRE : 0) | (lock_axles ? TF_LOCK_AXLES : 0);
	{
		Fvector w_body, fwd = HandlingDir(S.ax_fwd);
		fwd.mul(S.front_sign);
		m_pPhysicsShell->get_AngularVel(w_body);
		float slack = 1.f + 0.05f * _abs(S.speed); // m/s of rim speed difference that is ordinary tyre slip
		auto pass = [&](float excess, float floor) { return excess <= slack ? 1.f : _max(slack / excess, floor); };
		// An open differential passes the gripping side what the spinning one holds. Off the ground that is next to
		// nothing, but a tyre spinning on the road still holds its sliding friction, so the other side keeps
		// pushing with as much (the share of the drive torque, at least the differential's own floor).
		// (with the load the wheel carries now: the inside wheel of a bend, unloaded, holds less and spins first)
		auto hold = [&](SWheel& x) {
			if (S.step_count - x.contact_step >= 4) return 0.f;
			float g, r, sl;
			HandlingSurfaceFactors(HandlingSurface(x.contact_mtl), g, r, sl);
			float mu = (P.tyre_friction > 0.f ? P.tyre_friction : 1.f) * g * P.grip_scale * P.weather_grip *
				_min(P.slide_grip, 1.f);
			return mu * HandlingWheelLoad(x, wheel_load) * x.radius;
		};
		auto hold_k = [&](float held, float drive) { return drive > EPS ? clampr(held / drive, 0.f, 1.f) : 1.f; };
		float v_grp[2] = {0.f, 0.f};
		int n_grp[2] = {0, 0};
		for (SWheelDrive& d : m_vho.drive)
		{
			SWheel& w = *d.pwheel;
			w.vho_rim = d.ASpeed() * w.radius;
			v_grp[w.vho_diff_group] += w.vho_rim;
			++n_grp[w.vho_diff_group];
		}
		float k_centre = 1.f;
		int fast_grp = -1; // the axle group running away from the other (open centre differential)
		if (P.diff_center > 0.f && !lock_centre && n_grp[0] && n_grp[1])
		{
			int fast = v_grp[0] / n_grp[0] > v_grp[1] / n_grp[1] ? 0 : 1;
			float held = 0.f, drive = 0.f;
			for (SWheelDrive& d : m_vho.drive)
				if (d.pwheel->vho_diff_group == fast)
				{
					held += hold(*d.pwheel);
					drive += _max(d.pwheel->vho_drive, 0.f);
				}
			k_centre = pass(_abs(v_grp[0] / n_grp[0] - v_grp[1] / n_grp[1]), _max(0.04f, hold_k(held, drive)));
			fast_grp = fast;
		}
		for (SWheelDrive& d : m_vho.drive)
		{
			SWheel& w = *d.pwheel;
			// The cut goes to the side that grips: it gets only what the spinning side holds. The spinning side keeps
			// its share and runs away (as fast as the engine can rev it), so the revs climb with the wheelspin, as with
			// a real open differential, instead of the engine being held down by wheels that cannot take its torque.
			float k = w.vho_diff_group == fast_grp ? 1.f : k_centre;
			SWheel* m = w.vho_diff_mate;
			if (m && !lock_axles && m->element && w.element)
			{
				// the wheels of an axle turn at different speeds in a bend; only the rest is slip
				Fvector rel, turn;
				rel.sub(w.element->XFORM().c, m->element->XFORM().c);
				turn.crossproduct(w_body, rel);
				SWheel& spinning = _abs(w.vho_rim) > _abs(m->vho_rim) ? w : *m;
				float min_k = _max(_max(0.04f, P.diff_axle_bias), hold_k(hold(spinning), _max(spinning.vho_drive, 0.f)));
				if (&spinning != &w) k *= pass(_abs(w.vho_rim - m->vho_rim - turn.dotproduct(fwd)), min_k);
			}
			// quick to cut, slower to come back, so a wheel that bites again does not make the revs flutter
			w.vho_diff_k = approach(w.vho_diff_k, k, 4.f, 12.f, step);
		}
	}

	// Locked differentials: a driven wheel spinning faster than the slowest one is held back to its speed and
	// its share of the torque goes to the wheels that still grip.
	float lock_boost = 1.f, lock_rate = 0.f;
	if (lock_axles)
	{
		float slowest = flt_max;
		int n = 0, n_spin = 0;
		auto scan_min = [&](SWheelDrive& d) { slowest = _min(slowest, d.ASpeed() * dir); ++n; };
		auto scan_spin = [&](SWheelDrive& d) { if (d.ASpeed() * dir > slowest + 2.f) ++n_spin; };
		for (SWheelDrive& d : m_vho.drive) scan_min(d);
		for (SWheelDrive& d : m_vho.drive) scan_spin(d);
		if (n_spin > 0 && n_spin < n)
		{
			lock_boost = float(n) / float(n - n_spin);
			lock_rate = slowest;
		}
	}
	// Locked or rigid centre (the diff lock without diff_lock_axles, or diff_center 0, a part-time 4x4's transfer box):
	// the front and rear driven axles turn together. An axle running away from the other (in the air, on ice) is held
	// back to its speed, keeping the spread between its own wheels, whose differential stays open, and its share of
	// the torque goes to the axle that grips. (Before, nothing coupled them: the lock only lifted the open centre's
	// cut, so the spinning axle still wasted its half.)
	int centre_fast = -1;
	float centre_shift = 0.f, centre_boost = 1.f;
	if (!lock_axles && (lock_centre || P.diff_center <= 0.f))
	{
		float r[2] = {0.f, 0.f};
		int n[2] = {0, 0};
		for (SWheelDrive& d : m_vho.drive)
		{
			r[d.pwheel->vho_diff_group] += d.ASpeed() * dir;
			++n[d.pwheel->vho_diff_group];
		}
		if (n[0] && n[1])
		{
			r[0] /= n[0];
			r[1] /= n[1];
			// slack for a bend, where the front axle runs a wider arc than the rear (a quarter faster at 35 degrees
			// of lock), so a coupled 4x4 does not bind in tight turns
			int fast = r[0] > r[1] ? 0 : 1;
			float allowed = r[1 - fast] + 2.f + 0.25f * _abs(r[1 - fast]);
			if (r[fast] > allowed)
			{
				centre_fast = fast;
				centre_shift = allowed - r[fast];
				centre_boost = float(n[0] + n[1]) / float(n[1 - fast]);
				S.dbg_flags |= TF_CENTRE_HELD;
			}
		}
	}

	auto motor = [](SWheel& w, float vel, float fmax) {
		w.vho_dbg_mvel = vel;
		w.vho_dbg_mfmax = fmax;
		w.ApplyDriveAxisVelTorque(vel, fmax);
	};
	auto drive_wheel = [&](SWheelDrive& d) {
		SWheel& w = *d.pwheel;
		float wg = d.ASpeed() * dir; // wheel rate in the direction the gear drives
		float drive = w.vho_drive; // positive: pushes the way the gear drives
		w.vho_drive = 0.f; // consumed here; wheels that are not driven are handled below
		if (drive > 0.f) drive *= w.vho_diff_k;
		if (lock_boost > 1.f)
		{
			if (wg > lock_rate + 2.f)
			{
				motor(w, d.pos_fvd * lock_rate * dir, _abs(drive) + w.vho_resist);
				w.vho_resist = -1.f;
				return;
			}
			drive *= lock_boost;
		}
		if (centre_fast >= 0)
		{
			if (w.vho_diff_group == centre_fast)
			{
				motor(w, d.pos_fvd * _max(wg + centre_shift, 0.f) * dir, _abs(drive) + w.vho_resist);
				w.vho_resist = -1.f;
				return;
			}
			drive *= centre_boost;
		}
		if (drive < 0.f)
		{
			// engine braking: acts as resistance, never pushes the car backwards
			w.vho_resist -= drive;
			drive = 0.f;
		}
		if (drive > w.vho_resist)
		{
			// Drives toward the gear's direction even while the wheel still turns the other way (rolling back
			// down a hill in 1st): the torque first stops the wheel, then spins it up if the ground cannot hold it.
			motor(w, d.pos_fvd * dir * (_max(wg, 0.f) + spin_step), drive - w.vho_resist);
		}
		else
			motor(w, 0.f, w.vho_resist - drive);
		w.vho_resist = -1.f; // mark as applied
	};
	for (SWheelDrive& d : m_vho.drive)
		drive_wheel(d);
	for (auto& it : m_wheels_map)
	{
		SWheel& w = it.second;
		if (w.vho_resist >= 0.f) motor(w, 0.f, w.vho_resist);
	}

	HandlingSteer(step);
	if (P.debug_trace > 0.f) HandlingTraceRecord(step);
	else if (!m_vho.trace.empty()) HandlingTraceFree();
}

void CCar::HandlingSteer(float step)
{
	const SHandlingParams& P = m_vho.p;
	SHandlingState& S = m_vho.s;

	float target = S.script_steer_on ? S.script_steer : float((rsp ? 1 : 0) - (lsp ? 1 : 0));
	clamp(target, -1.f, 1.f);
	bool returning = (target * S.steer < 0.f) || (_abs(target) < _abs(S.steer));
	float rate = returning ? P.steer_return_rate : P.steer_rate;
	rate *= 1.f + (P.handbrake_steer_rate - 1.f) * S.hand_slide; // quicker hands during and after a hand-brake turn
	S.steer = approach(S.steer, target, rate, rate, step);

	float k = 1.f;
	if (P.steer_slip <= 0.f && P.steer_high_speed > P.steer_low_speed) // steer_slip replaces it, below
	{
		float t = (_abs(S.speed) - P.steer_low_speed) / (P.steer_high_speed - P.steer_low_speed);
		k = 1.f + (P.steer_high_factor - 1.f) * smoothstep01(t);
		k += (1.f - k) * P.handbrake_steer_lock * S.hand_slide;
	}
	float max_angle = P.steer_max_angle > 0.f ? deg2rad(P.steer_max_angle) : 0.f;

	// Every steering wheel follows one turning circle around a pivot line: the rear axles, or with rear
	// steering wheels (BTR) a point moving toward the middle at low speed so the rear axles counter-steer
	// for tight manoeuvres; above rear_steer_speed they stay straight. Axles nearer the pivot turn less.
	float rear_k = 0.f;
	if (S.rear_steering && P.rear_steer > 0.f)
	{
		float t = P.rear_steer_speed > P.steer_low_speed ?
			(_abs(S.speed) - P.steer_low_speed) / (P.rear_steer_speed - P.steer_low_speed) : 1.f;
		rear_k = P.rear_steer * (1.f - smoothstep01(t));
	}
	float pivot = S.rear_u * (1.f - rear_k);
	float L_ref = S.front_u - pivot;

	// reference: centre-line angle at the frontmost axle, from the tightest joint limit there
	float lim_ref = flt_max;
	for (SWheelSteer& sw : m_steering_wheels)
		if ((sw.pwheel->pos.z - S.axle_mid_z) * S.front_sign > S.front_u - 0.3f)
			lim_ref = _min(lim_ref, max_angle > 0.f ? max_angle : _min(_abs(sw.lo_limit), _abs(sw.hi_limit)));
	if (lim_ref == flt_max) lim_ref = 0.f;

	float s = S.steer * k; // right positive
	// steer_slip: above walking pace the front wheels turn at most steer_slip degrees past the way the front axle
	// actually travels, the slip angle where a tyre grips best. More lock only scrubs the tyres (understeer), and
	// with the rear sliding it steers the car deeper into the spin. The range moves with the front axle's
	// direction, so in a slide the wheels can always point where the car is going (the catch), and full lock on
	// the keys is the most the tyres can turn the car, as a driver's hands would find it.
	if (P.steer_slip > 0.f && lim_ref > 0.f && S.speed > P.steer_low_speed * 0.5f)
	{
		Fvector v = {0.f, 0.f, 0.f}, fwd = HandlingDir(S.ax_fwd), right = HandlingDir(S.ax_right);
		fwd.mul(S.front_sign);
		right.mul(S.front_sign);
		for (SWheelSteer& sw : m_steering_wheels)
			if (sw.pwheel->element && (sw.pwheel->pos.z - S.axle_mid_z) * S.front_sign > S.front_u - 0.3f)
			{
				Fvector wv;
				sw.pwheel->element->get_LinearVel(wv);
				v.add(wv);
			}
		float along = v.dotproduct(fwd);
		if (along > EPS)
		{
			float travel = atan2f(v.dotproduct(right), along); // right positive, like s
			float slip = deg2rad(P.steer_slip);
			float limited = clampr(s, (travel - slip) / lim_ref, (travel + slip) / lim_ref);
			// eased in from half of steer_low_speed to one and a half times it, so pulling away stays smooth
			float fade = clampr((S.speed - P.steer_low_speed * 0.5f) / _max(P.steer_low_speed, 0.5f), 0.f, 1.f);
			s += (limited - s) * fade;
			clamp(s, -1.f, 1.f);
		}
	}
	S.dbg_steer_k = _abs(S.steer) > 0.01f ? s / S.steer : k;
	float side = s > 0.f ? 1.f : -1.f;
	float delta_ref = _abs(s) * lim_ref;
	float R = (L_ref > 0.3f && delta_ref > EPS_L) ? L_ref / tanf(delta_ref) : 0.f;

	for (SWheelSteer& sw : m_steering_wheels)
	{
		SWheel& w = *sw.pwheel;
		if (!w.joint) continue;
		// stock convention (pos_right = -1): a right turn is a negative joint angle
		float lim_right = max_angle > 0.f ? max_angle : _abs(sw.lo_limit);
		float lim_left = max_angle > 0.f ? max_angle : _abs(sw.hi_limit);
		w.SetSteerLimits(lim_left, -lim_right);

		float u = (w.pos.z - S.axle_mid_z) * S.front_sign;
		float L_i = u >= 0.f ? u - pivot : u * rear_k; // rear-half wheels only steer through rear_k
		float yaw = 0.f; // this wheel's turn toward the curve, radians
		if (R > 0.f)
		{
			float par = atanf(L_i * tanf(delta_ref) / L_ref);
			float geo = atanf(L_i / _max(R - side * (w.pos.x - S.axle_mid_x), 0.1f));
			yaw = par + (geo - par) * clampr(P.ackermann, 0.f, 1.f);
		}
		else if (L_ref > 0.3f)
			yaw = delta_ref * L_i / L_ref;
		float target_angle = -side * yaw * w.vho_steer_dir;
		clamp(target_angle, -lim_right, lim_left);
		w.vho_dbg_steer = target_angle;
		float err = target_angle - w.joint->GetAxisAngle(0);
		float vel = err * 12.f;
		clamp(vel, -P.steer_servo_speed, P.steer_servo_speed);
		w.ApplySteerAxisVel(vel);
	}
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////
// tyre contact (called from the wheel collision callback during collision detection)

void CCar::HandlingContact(SWheel& w, dContact& c, SGameMtl* ground)
{
	const SHandlingParams& P = m_vho.p;
	const SHandlingSurface* s = HandlingSurface(ground);
	const SHandlingState& S = m_vho.s;
	dSurfaceParameters& sp = c.surface;
	if (P.wheel_climb > 0.f && w.joint && w.element)
	{
		// A tyre meeting a kerb, a rail or a root climbs it instead of slamming into its edge: a contact low on
		// the wheel with a steep normal (the edge of a step) is tilted toward the car's up axis by wheel_climb,
		// so the suspension takes the step rather than the whole car, as the ray-cast wheels of other games do
		Fvector n, d, axle, up = HandlingDir(m_vho.s.ax_up);
		n.set(c.geom.normal[0], c.geom.normal[1], c.geom.normal[2]);
		d.set(w.element->XFORM().c).sub(Fvector().set(c.geom.pos[0], c.geom.pos[1], c.geom.pos[2]));
		float sign = n.dotproduct(d) >= 0.f ? 1.f : -1.f; // normal pointing into the wheel
		n.mul(sign);
		w.joint->GetAxisDirDynamic(1, axle);
		float nu = n.dotproduct(up);
		if (d.dotproduct(up) > 0.25f * w.radius && nu < 0.8f && nu > -0.2f && _abs(n.dotproduct(axle)) < 0.5f)
		{
			n.lerp(n, up, clampr(P.wheel_climb, 0.f, 1.f));
			n.normalize_safe();
			n.mul(sign);
			c.geom.normal[0] = n.x;
			c.geom.normal[1] = n.y;
			c.geom.normal[2] = n.z;
		}
	}
	float grip, rolling, slip;
	HandlingSurfaceFactors(s, grip, rolling, slip);
	grip *= P.grip_scale * P.weather_grip;
	// tyre_friction: one tyre-on-road coefficient for every model, so the surface table alone decides traction
	// (the stock value multiplies game material frictions and per-model factors that vary a lot)
	if (P.tyre_friction > 0.f) sp.mu = P.tyre_friction * grip;
	else sp.mu *= w.collision_params.mu_factor * grip;
	if (S.hand_slide > 0.f && (P.handbrake_rear_only <= 0.f || HandlingIsRear(w)))
	{
		float k = S.hand_slide * smoothstep01((_abs(S.speed) - 1.5f) / 3.f);
		float loss = _max(1.f - P.handbrake_grip, 0.f);
		sp.mu *= 1.f - loss * k;
		slip *= 1.f + P.handbrake_slip * loss * k; // none at handbrake_grip 1 (heavy vehicles)
	}
	bool tracing = P.debug_trace > 0.f;
	if ((P.slide_grip < 1.f || tracing) && w.element)
	{
		// A sliding tyre holds less than a gripping one: past slide_speed m/s of slip at the contact patch the
		// friction is down to slide_grip of its peak. A car thrown sideways slides instead of tripping over its
		// tyres, as a real one does on a flat road.
		Fvector v, av, r, vc, n;
		w.element->get_LinearVel(v);
		w.element->get_AngularVel(av);
		r.set(c.geom.pos[0], c.geom.pos[1], c.geom.pos[2]).sub(w.element->XFORM().c);
		vc.crossproduct(av, r).add(v);
		n.set(c.geom.normal[0], c.geom.normal[1], c.geom.normal[2]);
		vc.mad(n, -vc.dotproduct(n));
		if (tracing) w.vho_dbg_patch = vc.magnitude();
		if (P.slide_grip < 1.f)
		{
			float k;
			if (P.slide_full > P.slide_peak)
			{
				// as a real tyre: full grip up to its peak slip (slide_peak, about 7 degrees or 12 % wheel slip),
				// then easing down to slide_grip at slide_full. Slip is the patch speed over the wheel's ground
				// speed (at least slide_speed m/s), so ordinary cornering at speed keeps its grip.
				Fvector vg;
				vg.set(v).mad(n, -v.dotproduct(n));
				float slip_k = vc.magnitude() / _max(vg.magnitude(), _max(P.slide_speed, 0.1f));
				k = smoothstep01((slip_k - P.slide_peak) / (P.slide_full - P.slide_peak));
			}
			else
				k = smoothstep01((vc.magnitude() - 0.5f) / _max(P.slide_speed, 0.1f));
			sp.mu *= 1.f - (1.f - P.slide_grip) * k;
		}
	}
	if (P.tyre_spring > 0.f && P.tyre_damping > 0.f)
		MulSprDmp(sp.soft_cfm, sp.soft_erp, P.tyre_spring, P.tyre_damping);
	HandlingLimitCorrection(c);

	if (P.cornering_stiffness > 0.f && w.joint)
	{
		// rolling direction as friction direction 1; force-dependent slip sideways gives progressive grip loss
		Fvector axle, n, roll;
		w.joint->GetAxisDirDynamic(1, axle);
		n.set(c.geom.normal[0], c.geom.normal[1], c.geom.normal[2]);
		roll.crossproduct(axle, n);
		float m = roll.magnitude();
		if (m > 0.1f)
		{
			roll.mul(1.f / m);
			c.fdir1[0] = roll.x;
			c.fdir1[1] = roll.y;
			c.fdir1[2] = roll.z;
			sp.mode |= dContactFDir1 | dContactSlip2;
			// cornering_load: stiffness in proportion to the load a wheel carries (per radian, x its share of the
			// weight), so a truck tyre is as stiff for its load as a car's; else cornering_stiffness N/rad per wheel
			float c_k = P.cornering_load > 0.f ?
				P.cornering_load * m_pPhysicsShell->getMass() * EffectiveGravity() / _max(float(m_wheels_map.size()), 1.f) :
				P.cornering_stiffness * S.mass_k;
			sp.slip2 = _max(_abs(S.speed), 1.f) * slip / _max(c_k, 1.f);
		}
	}
	if (tracing)
	{
		// one collision pass per physics step; the first contact of a new pass starts the count again
		if (w.contact_step != m_vho.s.step_count)
		{
			w.vho_dbg_contacts = 0;
			w.vho_dbg_neg = false;
		}
		++w.vho_dbg_contacts;
		w.vho_dbg_mu = sp.mu;
		w.vho_dbg_slip2 = (sp.mode & dContactSlip2) ? sp.slip2 : 0.f;
	}
	w.contact_mtl = ground;
	w.contact_step = m_vho.s.step_count;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////
// damage

void CCar::HandlingHit(SHit& HDS)
{
	const SHandlingParams& P = m_vho.p;
	bool explosive = HDS.hit_type == ALife::eHitTypeExplosion;
	m_vho.s.last_hit_explosive = explosive;
	bool zone = !!smart_cast<CCustomZone*>(HDS.who);
	bool fire = HDS.hit_type == ALife::eHitTypeBurn || HDS.hit_type == ALife::eHitTypeLightBurn;
	if (zone && fire) HDS.power *= P.anomaly_fire_damage; // burners, campfires
	else if (zone && HDS.hit_type == ALife::eHitTypeStrike) HDS.power *= P.anomaly_push_damage; // gravitational
	else if (explosive) HDS.power *= P.explosion_damage_scale;
	else if (HDS.hit_type == ALife::eHitTypeStrike) HDS.power *= P.collision_damage_scale;
	else HDS.power *= P.damage_scale;

	if (!explosive && P.explosive_only_death > 0.f)
	{
		float room = _max(GetfHealth() - P.min_health, 0.f);
		HDS.power = _min(HDS.power, room);
	}
}

bool CCar::HandlingAllowFuse()
{
	return !m_vho.enabled || m_vho.p.explosive_only_death <= 0.f || m_vho.s.last_hit_explosive;
}

void CCar::HandlingUpdateCL()
{
	const SHandlingParams& P = m_vho.p;
	SHandlingState& S = m_vho.s;
	if (S.apply_pending && m_pPhysicsShell) HandlingApply();
	if (m_vho.dump_count) HandlingTraceWrite();
	// sprint (full throttle) follows the key as it is held: its press can be taken by another handler on the way
	if (OwnerActor() && OwnerActor() == Level().CurrentControlEntity() && pInput)
	{
		int k0 = get_action_dik(kSPRINT_TOGGLE, 0), k1 = get_action_dik(kSPRINT_TOGGLE, 1);
		S.sprint = (k0 && pInput->iGetAsyncKeyState(k0)) || (k1 && pInput->iGetAsyncKeyState(k1));
	}
	if (S.pending_damage > 0.f)
	{
		float dmg = S.pending_damage;
		S.pending_damage = 0.f;
		float h = GetfHealth();
		float nh = _max(P.min_health, h - dmg);
		if (nh < h && g_Alive())
		{
			S.last_hit_explosive = false;
			SetfHealth(nh);
			CDamagableItem::HitEffect();
		}
	}
	if (b_engine_on && GetfHealth() <= P.disable_health)
		StopEngine();

	// hits on run-over creatures, queued during collision detection
	for (const SHandling::SRunOver& r : m_vho.run_overs)
	{
		if (r.car_dv > 0.f)
			m_pPhysicsShell->applyImpulse(Fvector().set(r.car_dir).invert(), r.car_dv * m_pPhysicsShell->getMass());
		if (r.car_damage > 0.f) S.pending_damage += r.car_damage;
		CActor* driver = smart_cast<CActor*>(Owner());
		if (driver && r.jolt > 1.f && P.ram_player_damage > 0.f)
		{
			NET_Packet pkt;
			SHit hit;
			hit.GenHeader(GE_HIT, driver->ID());
			hit.whoID = driver->ID();
			hit.weaponID = ID();
			hit.dir.set(r.car_dir).invert();
			hit.power = P.ram_player_damage * r.jolt;
			hit.p_in_bone_space.set(0.f, 0.f, 0.f);
			hit.impulse = 0.f;
			hit.hit_type = ALife::eHitTypeStrike;
			hit.boneID = 0;
			hit.Write_Packet(pkt);
			u_EventSend(pkt);
		}
		CObject* target = Level().Objects.net_Find(r.id);
		if (!target) continue;
		if (!r.alive)
		{
			CPhysicsShellHolder* sh = smart_cast<CPhysicsShellHolder*>(target);
			if (sh && sh->PPhysicsShell() && sh->PPhysicsShell()->isActive())
				sh->PPhysicsShell()->applyImpulse(r.dir, r.impulse);
			continue;
		}
		// thrown out of the way at the car's speed through their own movement controller (the hit alone hardly
		// moves someone standing)
		CEntityAlive* ea = smart_cast<CEntityAlive*>(target);
		if (r.shove > 0.f && ea && ea->g_Alive() && ea->character_physics_support() &&
		    ea->character_physics_support()->movement())
			ea->character_physics_support()->movement()->ApplyImpulse(r.dir, r.shove);
		if (r.power <= 0.f && r.impulse <= 0.f) continue;
		NET_Packet pkt;
		SHit hit;
		hit.GenHeader(GE_HIT, r.id);
		hit.whoID = Owner() ? Owner()->ID() : ID();
		hit.weaponID = ID();
		hit.dir = r.dir;
		hit.power = r.power;
		hit.p_in_bone_space = target->Position();
		hit.impulse = r.impulse;
		hit.hit_type = ALife::eHitTypeStrike;
		hit.boneID = 0;
		hit.Write_Packet(pkt);
		u_EventSend(pkt);
	}
	m_vho.run_overs.clear();
	if (m_vho.run_over_time.size() > 64) m_vho.run_over_time.clear();
	HandlingDodge();

	// cranking: the engine catches (or sputters out) when the starter time is up
	if (S.crank_timer > 0.f)
	{
		S.crank_timer -= Device.fTimeDelta;
		if (S.crank_timer <= 0.f)
		{
			S.crank_timer = 0.f;
			if (S.crank_ok && m_fuel > EPS && GetfHealth() > P.disable_health)
			{
				PlayExhausts();
				b_engine_on = true;
				m_current_rpm = 0.f;
				b_starting = true;
			}
			else
			{
				m_car_sound->Stall();
				++S.stall_count;
				S.stall_reason = S.start_blocked ? 8 : (S.flood_timer > 0.f ? 5 : (S.seized ? 7 : 6));
			}
		}
	}
	if (S.start_blocked) HandlingStall(8);
	HandlingWater(Device.fTimeDelta);
	HandlingTemp(Device.fTimeDelta);

	// a worn engine may stall: nothing above stall_health, rising to stall_chance per second at disable_health
	float h = GetfHealth();
	if (b_engine_on && Owner() && P.stall_chance > 0.f && h < P.stall_health && P.stall_health > P.disable_health)
	{
		float t = (P.stall_health - h) / (P.stall_health - P.disable_health);
		clamp(t, 0.f, 1.f);
		if (::Random.randF() < P.stall_chance * (0.1f + 0.9f * t) * Device.fTimeDelta)
			HandlingStall(1);
	}
}

// Start attempt with the overhaul: the starter turns for crank_time (longer on a worn engine), then the engine
// catches or not. Worn engines (below stall_health) and flooded ones can fail; try again.
bool CCar::HandlingCrankStart()
{
	const SHandlingParams& P = m_vho.p;
	SHandlingState& S = m_vho.s;
	if (P.crank_time <= 0.f && !S.start_blocked) return false; // instant stock start
	if (S.crank_timer > 0.f) return true; // already cranking
	float h = GetfHealth();
	float worn = P.stall_health > P.disable_health ? (P.stall_health - h) / (P.stall_health - P.disable_health) : 0.f;
	clamp(worn, 0.f, 1.f);
	float fail = (S.flood_timer > 0.f || S.seized || S.start_blocked) ? 1.f : P.start_fail * worn;
	S.crank_ok = ::Random.randF() >= fail;
	S.crank_timer = (P.crank_time > 0.f ? P.crank_time : 0.8f) * (1.f + 1.5f * worn) * ::Random.randF(0.85f, 1.15f);
	m_car_sound->Start();
	return true;
}

// Deep water: a ray from above finds a liquid surface over the car; with the water higher than wade_depth
// above the bottom of the wheels the engine floods, stalls and will not start until it has dried out.
void CCar::HandlingWater(float dt)
{
	const SHandlingParams& P = m_vho.p;
	SHandlingState& S = m_vho.s;
	if (S.flood_timer > 0.f && S.water_depth < P.wade_depth * 0.5f)
		S.flood_timer = _max(0.f, S.flood_timer - dt);
	if (P.wade_depth <= 0.f || !S.chassis || (!b_engine_on && S.flood_timer <= 0.f)) return;
	S.water_check -= dt;
	if (S.water_check > 0.f) return;
	S.water_check = 0.25f;

	float bottom = flt_max;
	for (auto& it : m_wheels_map)
		if (it.second.element)
			bottom = _min(bottom, it.second.element->XFORM().c.y - it.second.radius);
	if (bottom == flt_max) return;
	Fvector from = S.chassis->XFORM().c;
	from.y += 4.f;
	collide::rq_result R;
	S.water_depth = 0.f;
	if (Level().ObjectSpace.RayPick(from, Fvector().set(0.f, -1.f, 0.f), 8.f, collide::rqtStatic, R, this))
	{
		CDB::TRI* T = Level().ObjectSpace.GetStaticTris() + R.element;
		SGameMtl* mtl = GMLib.GetMaterialByIdx(T->material);
		if (mtl && mtl->Flags.test(SGameMtl::flLiquid))
			S.water_depth = _max(0.f, from.y - R.range - bottom);
	}
	if (S.water_depth > P.wade_depth)
	{
		S.flood_timer = P.flood_time;
		if (b_engine_on) HandlingStall(5);
	}
}

// Coolant temperature: friction (growing with the revs) and the power the engine delivers heat it, the radiator
// carries the heat off once the thermostat opens at 88 C, better with speed (airflow) and worse on a damaged
// engine. Until then only the engine's own coolant warms, temp_warmup times faster: idling it reaches the
// thermostat in a few minutes, hard driving in under one. Crawling at full throttle up a long hill, over-revving
// or a worn engine overheat it. With overheat on, it loses power above 108 C, takes damage above 115 C and seizes
// at 128 C (stall reason 7), restarting only below 100 C.
void CCar::HandlingTemp(float dt)
{
	const SHandlingParams& P = m_vho.p;
	SHandlingState& S = m_vho.s;
	const float ambient = 15.f;
	float heat = 0.f;
	float k = 0.004f; // standing still with the engine off
	if (b_engine_on)
	{
		float load = m_max_power > 0.f ? _max(m_current_engine_power, 0.f) / m_max_power : 0.f;
		float revs = m_max_rpm > 0.f ? clampr(S.rpm / m_max_rpm, 0.f, 1.2f) : 0.f;
		heat = (0.15f + 0.6f * load + 0.15f * revs * revs + (S.overrev ? 0.3f : 0.f)) * P.heat_scale;
		k = (0.005f + 0.0006f * _abs(S.speed)) * (0.4f + 0.6f * GetfHealth()) * P.cool_scale;
		if (S.engine_temp < P.temp_thermostat)
		{
			k *= 0.1f; // thermostat closed while it warms up
			heat *= _max(P.temp_warmup, 1.f);
		}
	}
	S.engine_temp += (heat - (S.engine_temp - ambient) * k) * dt;
	clamp(S.engine_temp, ambient, 140.f);

	S.heat_k = 1.f;
	if (S.seized && S.engine_temp < P.temp_restart) S.seized = false;
	if (P.overheat <= 0.f) return;
	if (S.engine_temp > P.temp_derate) S.heat_k = _max(0.5f, 1.f - (S.engine_temp - P.temp_derate) / 40.f);
	if (b_engine_on && S.engine_temp > P.temp_damage) S.pending_damage += 0.0004f * (S.engine_temp - P.temp_damage) * dt;
	if (b_engine_on && S.engine_temp >= P.temp_seize)
	{
		S.seized = true;
		HandlingStall(7);
	}
}

// fuel follows load: idling burns little, engine braking (throttle shut) almost nothing, full throttle the most
void CCar::HandlingFuel(float step)
{
	if (!b_engine_on) return;
	const SHandlingState& S = m_vho.s;
	const SHandlingParams& P = m_vho.p;
	// Fuel follows the work the engine does: an idle flow that grows slowly with the revs (friction and
	// pumping), plus fuel_per_kwh litres for every kWh it delivers. Idle flow defaults to about 1.2 % of the
	// engine's rated power in litres per hour per hp (1 l/h for an 80 hp car, 2.5 l/h for a 210 hp truck).
	float idle_lph = P.fuel_idle > 0.f ? P.fuel_idle : 0.012f * m_max_power / 800.f; // m_max_power = hp * 800
	float revs = _sqrt(_max(S.rpm, m_min_rpm) / _max(m_min_rpm, 1.f));
	float lph = idle_lph * revs + P.fuel_per_kwh * _max(m_current_engine_power, 0.f) / 1000.f;
	m_fuel -= step * lph / 3600.f * P.fuel_scale;
	if (m_fuel < EPS) StopEngine();
}

void CCar::HandlingStall(u32 reason)
{
	if (!b_engine_on) return;
	Stall();
	++m_vho.s.stall_count;
	m_vho.s.stall_reason = reason;
	if (m_vho.p.debug_trace > 0.f)
	{
		string32 tag;
		xr_sprintf(tag, "stall%u", reason);
		Msg("[VHO] %s: engine stalled, reason %u, %.0f rpm, %.1f km/h", *cName(), reason, m_vho.s.rpm / RPM_TO_RAD,
		    m_vho.s.speed * 3.6f);
		HandlingTraceEvent(SHandling::EV_STALL, tag, 100);
	}
}

// a car body touches the ground or a wall at many points every physics step; one impact sound per hit is enough
bool CCar::HandlingCollideSound()
{
	u32 now = Device.dwTimeGlobal;
	if (now - m_vho.s.collide_sound_time < 150) return false;
	m_vho.s.collide_sound_time = now;
	return true;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////
// pushing: anomalies and ordinary creatures cannot shove a car, heavy species (pseudogiant, chimera, burer) can
// with a share each. List items are "species=share" or a bare species, which uses heavy_push.

void CCar::SetHandlingHeavyPushers(LPCSTR species_list)
{
	m_vho.heavy_species.clear();
	m_vho.pusher_cache.clear();
	if (!species_list) return;
	string64 tmp;
	for (int i = 0, cnt = _GetItemCount(species_list); i < cnt; ++i)
	{
		_GetItem(species_list, i, tmp);
		SHandling::SPusher p;
		p.k = -1.f;
		if (char* eq = strchr(tmp, '='))
		{
			*eq = 0;
			p.k = float(atof(eq + 1));
		}
		if (!tmp[0]) continue;
		p.species = tmp;
		m_vho.heavy_species.push_back(p);
	}
}

float CCar::HandlingMassScale()
{
	float m = m_vho.p.real_mass > 0.f ? m_vho.p.real_mass : (m_pPhysicsShell ? m_pPhysicsShell->getMass() : 0.f);
	return m > 0.f ? _min(1250.f / m, 1.5f) : 1.f;
}

// 0 = the push is ignored, 1 = full push. pusher NULL means telekinesis or another source without an owner.
float CCar::HandlingPushScale(CObject* pusher, bool explosion)
{
	if (!m_vho.enabled || explosion) return 1.f;
	const SHandlingParams& P = m_vho.p;
	if (!pusher || smart_cast<CCustomZone*>(pusher)) return P.anomaly_push;
	if (smart_cast<CCar*>(pusher)) return 1.f;
	if (m_vho.heavy_species.empty()) return P.creature_push;
	// also asked from the contact callback every physics step, so the species match is cached per object
	int idx = -1;
	auto it = m_vho.pusher_cache.find(pusher->ID());
	if (it != m_vho.pusher_cache.end())
		idx = it->second;
	else
	{
		if (m_vho.pusher_cache.size() > 256) m_vho.pusher_cache.clear(); // ids are reused
		LPCSTR species = READ_IF_EXISTS(pSettings, r_string, pusher->cNameSect(), "species", "");
		for (u32 i = 0; i < m_vho.heavy_species.size(); ++i)
			if (!xr_strcmp(*m_vho.heavy_species[i].species, species)) idx = int(i);
		m_vho.pusher_cache[pusher->ID()] = idx;
	}
	if (idx < 0) return P.creature_push;
	float k = m_vho.heavy_species[idx].k;
	return k >= 0.f ? k : P.heavy_push;
}

Fvector CCar::HandlingDir(const Fvector& local)
{
	Fvector d;
	(m_vho.s.chassis ? m_vho.s.chassis->XFORM() : XFORM()).transform_dir(d, local);
	return d;
}

float vehicle_push_scale(CPhysicsShellHolder* obj, CObject* pusher)
{
	CCar* car = smart_cast<CCar*>(obj);
	if (!car || !car->IsHandlingEnabled()) return 1.f;
	return car->HandlingPushScale(pusher, false) * car->HandlingMassScale();
}

bool vehicle_blocks_push(CPhysicsShellHolder* obj, CObject* pusher)
{
	// effects that cannot be scaled (lifting, gravity off) need a full push
	CCar* car = smart_cast<CCar*>(obj);
	if (!car) return false;
	return car->HandlingPushScale(pusher, false) < 1.f;
}

// k: anomaly_push times the mass scale, so a light car is moved harder and a heavy one resists
static CCar* vehicle_anomaly_target(CPhysicsShellHolder* obj, CObject* zone, float& k)
{
	CCar* car = smart_cast<CCar*>(obj);
	if (!car || !obj->PPhysicsShell() || !obj->PPhysicsShell()->isActive()) return nullptr;
	k = car->HandlingPushScale(zone, false);
	if (k <= 0.f || k >= 1.f) return nullptr; // immune, or lifted by the telekinesis like any object
	k *= car->HandlingMassScale();
	return car;
}

// Drag toward the centre (anomaly_pull m/s2 at full push) and a lift (anomaly_lift g), both strongest at the
// centre: at the default push a 1250 kg car is pulled off course and just comes off the ground, a light one
// rises up to anomaly_lift_height above the anomaly, a truck is only dragged a little
void vehicle_anomaly_pull(CPhysicsShellHolder* obj, CObject* zone, const Fvector& dir, float rel_power)
{
	float k;
	CCar* car = vehicle_anomaly_target(obj, zone, k);
	if (!car || rel_power <= 0.f) return;
	const CCar::SHandlingParams& P = car->HandlingParams();
	float m = obj->GetMass() * Device.fTimeDelta;
	obj->PPhysicsShell()->applyImpulse(dir, k * P.anomaly_pull * rel_power * m);
	if (P.anomaly_lift > 0.f && obj->Position().y < zone->Position().y + P.anomaly_lift_height)
		obj->PPhysicsShell()->applyImpulse(Fvector().set(0.f, 1.f, 0.f),
		                                   k * P.anomaly_lift * obj->EffectiveGravity() * rel_power * rel_power * m);
}

void vehicle_anomaly_throw(CPhysicsShellHolder* obj, CObject* zone, const Fvector& dir, float power)
{
	float k;
	CCar* car = vehicle_anomaly_target(obj, zone, k);
	if (!car || power <= 0.f) return;
	// out of the anomaly, mostly sideways, like the loose objects it flings when it bursts
	Fvector kick;
	kick.set(-dir.x, 0.f, -dir.z);
	if (kick.square_magnitude() < EPS) kick.set(::Random.randF(-1.f, 1.f), 0.f, ::Random.randF(-1.f, 1.f));
	kick.normalize_safe();
	kick.y = 0.25f;
	kick.normalize();
	obj->PPhysicsShell()->applyImpulse(kick, k * car->HandlingParams().anomaly_kick * _min(power, 1.f) * obj->GetMass());
}

int vehicle_zone_recharge(CObject* obj, ALife::EHitType hit_type)
{
	CCar* car = smart_cast<CCar*>(obj);
	if (!car || !car->IsHandlingEnabled() || hit_type != ALife::eHitTypeShock) return 0;
	return iFloor(_max(car->HandlingParams().shock_recharge, 0.f) * 1000.f);
}

bool vehicle_collide_sound(IPhysicsShellHolder* obj)
{
	CCar* car = smart_cast<CCar*>(obj);
	return !car || !car->IsHandlingEnabled() || car->HandlingCollideSound();
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////
// sound

void CCar::HandlingSound(ref_sound& snd, float base_volume)
{
	const SHandlingParams& P = m_vho.p;
	float k = (m_current_rpm - m_min_rpm) / _max(m_max_rpm - m_min_rpm, 1.f);
	float pitch = P.snd_pitch_idle + (P.snd_pitch_max - P.snd_pitch_idle) * k;
	clamp(pitch, 0.3f, 2.2f);
	float vol = base_volume * (P.snd_volume_idle + (P.snd_volume_load - P.snd_volume_idle) * m_vho.s.throttle);
	if (m_vho.s.overrev) vol *= 1.15f;
	snd.set_frequency(pitch);
	snd.set_volume(vol);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////
// handling trace (debug_trace). Observation only: nothing here changes how the car behaves.

namespace
{
void trace_cat(xr_string& out, LPCSTR fmt, ...)
{
	string1024 tmp;
	va_list args;
	va_start(args, fmt);
	vsnprintf(tmp, sizeof(tmp), fmt, args);
	va_end(args);
	tmp[sizeof(tmp) - 1] = 0;
	out += tmp;
}
}

LPCSTR CCar::GetHandlingFlags()
{
	static string512 buf;
	buf[0] = 0;
	u32 f = m_vho.s.dbg_flags;
	for (u32 i = 0; i < sizeof(g_trace_flag_names) / sizeof(g_trace_flag_names[0]); ++i)
		if (f & (1u << i))
		{
			xr_strcat(buf, g_trace_flag_names[i]);
			xr_strcat(buf, " ");
		}
	return buf;
}

void CCar::HandlingTraceFree()
{
	SHandling& H = m_vho;
	xr_vector<SHandling::STraceStep>().swap(H.trace);
	if (!H.dump_count) xr_vector<SHandling::STraceStep>().swap(H.dump); // else HandlingTraceWrite frees it
	H.trace_head = H.trace_count = 0;
	H.dump_pending = false;
}

// An event latches one trace: the ring keeps the steps before it, post_steps more are recorded, then the trace is
// handed to HandlingUpdateCL to be written. While one is pending, further events are only listed in it. The event
// type is then blocked until it has cleared (HandlingTraceRecord re-arms it) and its cooldown passed.
void CCar::HandlingTraceEvent(u32 type, LPCSTR reason, u32 post_steps)
{
	SHandling& H = m_vho;
	SHandlingState& S = H.s;
	if (H.p.debug_trace <= 0.f || H.trace.empty() || type >= SHandling::EV_COUNT) return;
	bool busy = H.dump_pending; // a latched trace still to be written does not block the next one
	// the latch, whether this event starts a trace or is only listed in the pending one
	if (!S.dbg_armed[type] || S.step_count < S.dbg_block_until[type]) return;
	if (!busy && H.auto_dumps >= TRACE_MAX_AUTO) return;
	S.dbg_armed[type] = false;
	S.dbg_block_until[type] = S.step_count + post_steps + TRACE_COOLDOWN_STEPS;
	string64 ev;
	xr_sprintf(ev, "%s@%u", reason, S.step_count);
	if (busy)
	{
		if (H.dump_events.size() < 1000)
		{
			H.dump_events += " +";
			H.dump_events += ev;
		}
		return;
	}
	if (++H.auto_dumps == TRACE_MAX_AUTO)
		Msg("[VHO] %s: %u handling traces this session, no more", *cName(), TRACE_MAX_AUTO);
	H.dump_pending = true;
	H.dump_at = S.step_count + post_steps; // keep recording a while, so the trace shows what followed
	H.dump_reason = reason;
	H.dump_events = ev;
}

// Latches the ring as the trace to write by swapping buffers: both are sized when tracing starts, so nothing is
// copied or allocated inside the physics step. The ring starts again on the buffer of the trace written last.
void CCar::HandlingTraceSnapshot()
{
	SHandling& H = m_vho;
	H.dump.swap(H.trace);
	H.dump_head = H.trace_head;
	H.dump_count = H.trace_count;
	H.latched_reason = H.dump_reason;
	H.latched_events.swap(H.dump_events);
	H.trace_head = H.trace_count = 0;
	H.dump_pending = false;
}

void CCar::HandlingTraceRecord(float step)
{
	SHandling& H = m_vho;
	SHandlingState& S = H.s;
	const u32 N = SHandling::TRACE_STEPS;
	if (H.trace.size() != N)
	{
		H.trace.resize(N);
		H.trace_head = H.trace_count = 0;
	}
	if (H.dump.size() != N) H.dump.resize(N); // only empty when nothing waits in it
	SHandling::STraceStep& r = H.trace[H.trace_head];
	H.trace_head = (H.trace_head + 1) % N;
	if (H.trace_count < N) ++H.trace_count;

	// The car's own axes, from the root bone's bind rotation (HandlingDir): forward toward the steering wheels,
	// right and up turned with it, so models whose root bone is rotated read the same as the others
	Fvector up = HandlingDir(S.ax_up), fwd = HandlingDir(S.ax_fwd), right = HandlingDir(S.ax_right), v, av;
	fwd.mul(S.front_sign);
	right.mul(S.front_sign);
	m_pPhysicsShell->get_LinearVel(v);
	m_pPhysicsShell->get_AngularVel(av);
	r.step = S.step_count;
	r.time_ms = Device.dwTimeGlobal;
	r.h = step;
	r.flags = S.dbg_flags;
	r.valid = 0;
	r.v_long = v.dotproduct(fwd);
	r.v_lat = v.dotproduct(right);
	r.v_vert = v.dotproduct(up);
	r.yaw_rate = av.dotproduct(up);
	r.roll_rate = av.dotproduct(fwd);
	r.pitch_rate = av.dotproduct(right);
	r.roll = rad2deg(asinf(clampr(right.y, -1.f, 1.f)));
	r.pitch = rad2deg(asinf(clampr(fwd.y, -1.f, 1.f)));
	r.heading = rad2deg(atan2f(fwd.x, fwd.z));
	float vh = _sqrt(v.x * v.x + v.z * v.z);
	r.vel_heading = vh > 0.5f ? rad2deg(atan2f(v.x, v.z)) : r.heading;
	r.sideslip = 0.f;
	if (vh > 1.f)
	{
		r.sideslip = rad2deg(atan2f(r.v_lat, _abs(r.v_long))); // + = the car slides toward its right side
		r.valid |= 1;
	}

	// Momentum balance of the whole vehicle. The solver steps every body as v' = v + h/m (F_external + constraint
	// forces); joint and motor forces are internal and cancel, so the momentum change less gravity and the known
	// non-contact forces is the net contact force on the vehicle (tyres, body scrapes, anything it touches).
	{
		Fvector p = {0.f, 0.f, 0.f}, air = {0.f, 0.f, 0.f};
		float mass = 0.f;
		if (S.dbg_air_k < 0.f)
		{
			IKinematics* K = smart_cast<IKinematics*>(Visual());
			CInifile* ini = K ? K->LL_UserData() : NULL;
			S.dbg_air_k = ini && ini->section_exist("air_resistance") ?
				default_k_l * READ_IF_EXISTS(ini, r_float, "air_resistance", "linear_factor", 0.f) : 0.f;
		}
		for (u16 i = 0; i < m_pPhysicsShell->get_ElementsNumber(); ++i)
		{
			CPhysicsElement* e = m_pPhysicsShell->get_ElementByStoreOrder(i);
			Fvector ve;
			e->get_LinearVel(ve);
			float me = e->getMass();
			p.mad(ve, me);
			mass += me;
			bool wheel = false;
			for (auto& it : m_wheels_map)
				if (it.second.element == e) wheel = true;
			if (!wheel && S.dbg_air_k > 0.f) air.mad(ve, -S.dbg_air_k * ve.magnitude()); // applied in the next step
		}
		Fvector a = {0.f, 0.f, 0.f}, fc = {0.f, 0.f, 0.f};
		// (not while repairing: Revert lifts the car with a force this balance does not know)
		if (S.dbg_prev_step + 1 == S.step_count && step > 0.f && mass > 0.f && !m_repairing)
		{
			Fvector dp;
			dp.sub(p, S.dbg_prev_p).mul(1.f / step);
			a.set(dp).mul(1.f / mass);
			fc.set(dp).sub(S.dbg_ext_force).sub(S.dbg_air_force);
			fc.y += mass * EffectiveGravity();
			r.valid |= 2;
		}
		r.a_long = a.dotproduct(fwd);
		r.a_lat = a.dotproduct(right);
		r.a_vert = a.dotproduct(up);
		r.fc_long = fc.dotproduct(fwd);
		r.fc_lat = fc.dotproduct(right);
		r.fc_norm = fc.dotproduct(up);
		S.dbg_prev_p = p;
		S.dbg_air_force = air;
		S.dbg_prev_step = S.step_count;
	}
	r.thr_in = S.dbg_thr_in;
	r.thr = S.throttle;
	r.brk_in = S.dbg_brk_in;
	r.brk = S.brake;
	r.hand_k = S.hand_k;
	r.hand_slide = S.hand_slide;
	r.steer = S.steer;
	r.steer_k = S.dbg_steer_k;
	r.rpm = S.rpm / RPM_TO_RAD;
	r.shaft = S.dbg_shaft / RPM_TO_RAD;
	r.ground = S.ground_w * _abs(m_current_gear_ratio) / RPM_TO_RAD;
	r.ratio = m_current_gear_ratio;
	r.clutch = S.clutch;
	r.engage = S.clutch_engage;
	r.t_engine = S.dbg_t_engine;
	r.t_shaft = S.dbg_t_shaft;
	r.t_wheels = S.dbg_t_wheels;
	r.crash_lost = S.dbg_crash_lost;
	r.crash_dv = S.crash_dv;
	r.stall_count = u16(S.stall_count);
	r.gear = u8(CurrentTransmission());
	r.stall_reason = u8(S.stall_reason);

	u32 i = 0;
	bool locking = false;
	for (auto& it : m_wheels_map)
	{
		if (i >= SHandling::TRACE_WHEELS) break;
		SWheel& w = it.second;
		SHandling::STraceWheel& tw = r.wheel[i++];
		// forward rotation for every wheel, driven or not (same rule as the driven wheels' pos_fvd)
		float rate = 0.f, cs = 1.f, cd = 1.f;
		if (w.joint)
		{
			Fvector axle, roll;
			w.joint->GetAxisDirDynamic(1, axle);
			roll.crossproduct(axle, up);
			rate = w.joint->GetAxisAngleRate(1) * (roll.dotproduct(fwd) >= 0.f ? -1.f : 1.f);
			w.joint->GetJointSDfactors(cs, cd);
		}
		// compression after this step, as HandlingBumpStops measures it (which skips it when no suspension option uses it)
		float comp = 0.f;
		if (w.joint && w.element && S.chassis)
		{
			Fvector a, rest, d;
			w.joint->GetAxisDirDynamic(0, a);
			if (a.dotproduct(up) < 0.f) a.invert();
			S.chassis->XFORM().transform_tiny(rest, w.rest_local);
			comp = d.sub(w.element->XFORM().c, rest).dotproduct(a);
		}
		u32 age = S.step_count - w.contact_step; // 1 = touched the ground in this step's collision pass
		bool contact = age <= 1;
		tw.w = rate;
		tw.rim = rate * w.radius;
		tw.comp = comp;
		// an estimate, not the solved contact force: the joint spring at this compression plus the preload
		// (damping, the anti-roll bar and the wheel's own inertia are left out)
		tw.load_est = contact ? _max(cs * HINGE2_SPRING * comp + w.vho_preload, 0.f) : 0.f;
		tw.mvel = w.vho_dbg_mvel;
		tw.mfmax = w.vho_dbg_mfmax;
		tw.diff_k = w.vho_diff_k;
		tw.mu = w.vho_dbg_mu;
		tw.slip2 = w.vho_dbg_slip2;
		tw.patch = w.vho_dbg_patch;
		tw.steer_tgt = w.vho_dbg_steer;
		tw.steer_act = w.joint ? w.joint->GetAxisAngle(0) : 0.f;
		tw.mtl = contact && w.contact_mtl ? s16(w.contact_mtl->GetID()) : s16(-1);
		tw.contact = u8(_min(age, 255u));
		tw.contacts = u8(_min(w.vho_dbg_contacts, 255u));
		tw.neg = w.vho_dbg_neg ? 1 : 0;
		if (contact && _abs(S.speed) > 3.f && S.brake > 0.05f && _abs(tw.rim) < 0.2f * _abs(S.speed)) locking = true;
	}
	r.wheels = u8(i);

	// Re-arm each automatic event type once it has cleared: the engine runs again after a stall, the crash detector
	// is quiet again, no wheel has been locked for a while. A wheel hovering at the lock threshold, a crash that
	// keeps registering or a stall that persists therefore latch one trace, not one per step.
	S.dbg_lock_clear = locking ? 0 : _min(S.dbg_lock_clear + 1, 100000u);
	if (b_engine_on) S.dbg_armed[SHandling::EV_STALL] = true;
	if (S.crash_cooldown <= 0.f && S.crash_dv < 2.f) S.dbg_armed[SHandling::EV_CRASH] = true;
	if (S.dbg_lock_clear >= TRACE_LOCK_CLEAR_STEPS) S.dbg_armed[SHandling::EV_LOCK] = true;
	if (locking) HandlingTraceEvent(SHandling::EV_LOCK, "wheel_lock", 150);
	// (a trace latched earlier and not yet written holds the other buffer: wait for it, the ring keeps recording)
	if (H.dump_pending && S.step_count >= H.dump_at && !H.dump_count) HandlingTraceSnapshot();
}

// Writes the latched trace to logs\vho_trace\<time>_<section>_<n>_<reason>.csv: a header that pins down the
// whole configuration (schema, build, the script's description, every parameter, gears, surfaces, wheels), then
// one row per physics step. Called from HandlingUpdateCL, never from the physics step or a contact callback.
void CCar::HandlingTraceWrite()
{
	SHandling& H = m_vho;
	const SHandlingParams& P = H.p;
	const SHandlingState& S = H.s;
	const u32 N = SHandling::TRACE_STEPS;
	auto row = [&](u32 k) -> const SHandling::STraceStep& { return H.dump[(H.dump_head + N - H.dump_count + k) % N]; };
	string64 stamp;
	time_t now = time(NULL);
	tm lt;
	localtime_s(&lt, &now);
	strftime(stamp, sizeof(stamp), "%Y%m%d_%H%M%S", &lt);
	static u32 seq = 0; // shared by every car, so two traces in the same second never share a name
	string_path fname;
	xr_sprintf(fname, "vho_trace\\%s_%s_%u_%s.csv", stamp, *cNameSect(), ++seq, *H.latched_reason);
	IWriter* W = FS.w_open("$logs$", fname);
	if (!W || !W->valid())
	{
		Msg("! [VHO] cannot write the handling trace %s", fname);
		if (W) FS.w_close(W);
		H.dump_count = 0;
		if (P.debug_trace <= 0.f) HandlingTraceFree();
		return;
	}
	xr_string l;
	auto line = [&]() { W->w_string(l.c_str()); l.clear(); };

	// identity: format, build, parameter list
	trace_cat(l, "# vho_trace_schema=%u", VHO_TRACE_SCHEMA); line();
	{
		string_path exe = "?";
		GetModuleFileNameA(NULL, exe, sizeof(exe));
		WIN32_FILE_ATTRIBUTE_DATA fa;
		SYSTEMTIME st = {};
		u64 size = 0;
		if (GetFileAttributesExA(exe, GetFileExInfoStandard, &fa))
		{
			FileTimeToSystemTime(&fa.ftLastWriteTime, &st);
			size = (u64(fa.nFileSizeHigh) << 32) | fa.nFileSizeLow;
		}
		trace_cat(l, "# build: exe %s, %llu bytes, written %04u-%02u-%02u %02u:%02u:%02u UTC; CarHandling.cpp compiled %s %s",
		          exe, size, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, __DATE__, __TIME__);
		line();
		u32 h = 2166136261u, n = 0;
		for (const SHandlingParamInfo& p : g_vho_params)
		{
			for (LPCSTR c = p.name; *c; ++c) h = (h ^ u8(*c)) * 16777619u;
			h = (h ^ u8(',')) * 16777619u;
			++n;
		}
		trace_cat(l, "# param_list: %u parameters, fnv1a %08x", n, h); line();
	}
	trace_cat(l, "# car %s section %s reason %s; steps %u..%u; fixed_step %.4f s", *cName(), *cNameSect(),
	          *H.latched_reason, row(0).step, row(H.dump_count - 1).step, fixed_step); line();
	l = "# events: ";
	l += H.latched_events;
	line();
	// the script's description of the configuration, one header line per line of it
	{
		xr_string info = H.trace_info.size() ? *H.trace_info : "(none: the script did not describe it)";
		size_t pos = 0;
		while (pos <= info.size())
		{
			size_t e = info.find('\n', pos);
			if (e == xr_string::npos) e = info.size();
			l = "# config: ";
			l += info.substr(pos, e - pos);
			line();
			pos = e + 1;
		}
	}
	trace_cat(l, "# mass: physics %.1f kg, real_mass %.1f, mass_k %.4f, sprung corner %.1f kg, wheelbase %.3f m, "
	          "track %.3f m, model cog %.3f m, chassis inertia x%.3f + %.0f/%.0f/%.0f kg m2 (pitch/yaw/roll), now "
	          "%.0f/%.0f/%.0f", m_pPhysicsShell->getMass(), P.real_mass, S.mass_k,
	          S.sprung_corner_mass, S.wheelbase, S.track, S.cog_h, S.chassis_inertia_k, S.inertia_add.x, S.inertia_add.y,
	          S.inertia_add.z, HandlingInertiaAbout(S.ax_right), HandlingInertiaAbout(S.ax_up),
	          HandlingInertiaAbout(S.ax_fwd)); line();
	trace_cat(l, "# engine: peak torque %.2f N m at %.0f rpm, peak power %.0f W at %.0f rpm, idle %.0f rpm, max %.0f rpm; "
	          "reference radius %.4f m; axle_friction %.5f; gravity %.3f", m_a, m_torque_rpm / RPM_TO_RAD, m_max_power,
	          m_power_rpm / RPM_TO_RAD, m_min_rpm / RPM_TO_RAD, m_max_rpm / RPM_TO_RAD, m_ref_radius, m_axle_friction,
	          EffectiveGravity()); line();
	l = "# gears (total ratios, final drive included";
	if (S.low_range_on) l += ", low range on";
	l += "):";
	for (u32 g = 0; g < m_gear_ratious.size(); ++g)
		trace_cat(l, " %s%u=%.4f", g ? "" : "R", g, m_gear_ratious[g][0]);
	line();
	{
		u32 n = 0;
		l = "# params:";
		for (const SHandlingParamInfo& p : g_vho_params)
		{
			trace_cat(l, " %s=%g", p.name, P.*p.member);
			if (++n % 12 == 0) { line(); l = "# params:"; }
		}
		if (n % 12) line();
		l.clear();
	}
	for (const SHandlingSurface& s : H.surfaces)
	{
		trace_cat(l, "# surface %s id=%d grip=%.3f rolling=%.3f slip=%.3f", *s.material->m_Name, s.material->GetID(),
		          s.grip, s.rolling, s.slip);
		line();
	}
	{
		// the model's own air drag stays on with the overhaul (k_l * |v| * v on every non-wheel element, k_w * w)
		IKinematics* K = smart_cast<IKinematics*>(Visual());
		CInifile* ini = K ? K->LL_UserData() : NULL;
		if (ini && ini->section_exist("air_resistance"))
			trace_cat(l, "# model air_resistance: linear_factor %g (k_l %g), angular_factor %g (x 0.05)",
			          READ_IF_EXISTS(ini, r_float, "air_resistance", "linear_factor", 0.f), S.dbg_air_k,
			          READ_IF_EXISTS(ini, r_float, "air_resistance", "angular_factor", 0.f));
		else
			l = "# model air_resistance: none";
		line();
	}
	{
		IKinematics* K = smart_cast<IKinematics*>(Visual());
		float mu = (P.tyre_friction > 0.f ? P.tyre_friction : 1.f) * P.grip_scale * P.weather_grip;
		float static_load = m_pPhysicsShell->getMass() * EffectiveGravity() / _max(float(m_wheels_map.size()), 1.f);
		u32 i = 0;
		for (auto& it : m_wheels_map)
		{
			SWheel& w = it.second;
			trace_cat(l, "# wheel %u %s: radius %.3f m, mass %.1f kg, pos %.2f %.2f %.2f, rear %d, stock spring/damping %.3f/%.3f",
			          i, K ? K->LL_BoneName_dbg(w.bone_id) : "?", w.radius, w.element ? w.element->getMass() : 0.f, w.pos.x,
			          w.pos.y, w.pos.z, HandlingIsRear(w) ? 1 : 0, w.stock_spring, w.stock_damping);
			for (const SWheelDrive& d : H.drive)
				if (d.pwheel == &w)
					trace_cat(l, ", driven (pos_fvd %.0f, gear_factor %.3f, diff group %d, mate %s)", d.pos_fvd, d.gear_factor,
					          w.vho_diff_group, w.vho_diff_mate && K ? K->LL_BoneName_dbg(w.vho_diff_mate->bone_id) : "-");
			for (const SWheelSteer& sw : m_steering_wheels)
				if (sw.pwheel == &w)
					trace_cat(l, ", steering (limits %.1f/%.1f deg, axis %+.0f)", rad2deg(sw.lo_limit), rad2deg(sw.hi_limit),
					          w.vho_steer_dir);
			if (P.brake_g > 0.f)
				trace_cat(l, ", brakes sized (full pedal %.0f N m, hand %.0f N m; front share %.2f, weight on the front %.2f)",
				          w.vho_brake_full * P.brake_scale, w.vho_hand_full * P.handbrake_scale, S.brake_front_share,
				          S.static_front_share);
			else
				for (const SWheelBreak& b : m_breaking_wheels)
					if (b.pwheel == &w)
						trace_cat(l, ", brakes (model %.4f / hand %.4f; full pedal %.0f N m)", b.break_torque,
						          b.hand_break_torque, BRAKE_UNIT * b.break_torque * P.brake_scale * S.mass_k);
			trace_cat(l, ", static tyre capacity mu*N*r %.0f N m (mu %.3f on grip 1)", mu * static_load * w.radius, mu);
			line();
			++i;
		}
		if (P.brake_all_wheels > 0.f)
		{
			l = "# brake_all_wheels: every wheel brakes with the mean of the model's brake torques";
			line();
		}
	}
	{
		xr_set<int> ids;
		for (u32 k = 0; k < H.dump_count; ++k)
			for (u32 i = 0; i < row(k).wheels; ++i)
				if (row(k).wheel[i].mtl >= 0) ids.insert(row(k).wheel[i].mtl);
		l = "# materials under the wheels:";
		for (int id : ids)
		{
			SGameMtl* m = GMLib.GetMaterialByID(id);
			trace_cat(l, " %d=%s", id, m ? *m->m_Name : "?");
		}
		line();
	}
	l = "# flags bits:";
	for (u32 i = 0; i < sizeof(g_trace_flag_names) / sizeof(g_trace_flag_names[0]); ++i)
		trace_cat(l, " %u=%s", i, g_trace_flag_names[i]);
	line();
	l = "# frame: the car's own axes from the root bone's bind rotation; forward points toward the steering wheels, "
	    "right and up turn with it. Signs are as the engine reports them (X-Ray world: x, y up, z), so check them "
	    "against a known manoeuvre: heading, vel_heading deg from +z toward +x; sideslip deg, + = sliding toward the "
	    "car's right; roll deg, + = right side up; pitch deg, + = nose up; yaw/roll/pitch rates rad/s about up/forward/right";
	line();
	l = "# units: h s (physics step), time_ms game time; v_* m/s of the chassis body; a_* m/s2 of the whole vehicle's "
	    "centre of mass (gravity not included); rpm; ratio signed (reverse < 0); torques N m at physics scale "
	    "(t_engine, t_shaft engine side, t_wheels per driven wheel before the differentials); crash_lost/crash_dv m/s; "
	    "valid bits 1 = sideslip (over 1 m/s), "
	    "2 = contact force (momentum of the previous step known)";
	line();
	l = "# solved vs estimated: fc_long/fc_lat/fc_norm N are SOLVED, for the whole vehicle: its momentum change over "
	    "the step less gravity, overhaul drag, flip force and the model's air drag; that is the net contact force of "
	    "everything it touches (all tyres, plus any body scrape, creature or other object; anomaly impulses would also "
	    "show here). Per-wheel contact forces are NOT available: the wheel contact joints are not reachable from "
	    "xrGame without changing xrPhysics. w*_load_est_N is an ESTIMATE (joint spring at this compression plus "
	    "preload; no damping, anti-roll bar or wheel inertia). w*_mu and w*_slip2 are what the contact was given "
	    "(the solver may override mu where w*_neg = 1), not what it used.";
	line();
	l = "# wheels: w rad/s forward, rim m/s, comp m (+ = into the body), mvel/mfmax joint motor target rad/s (joint "
	    "sign) and force cap N m, diff_k, mu and slip2 ((m/s)/N) of the last contact, patch m/s contact-patch slip, "
	    "steer target/actual rad, mtl id, contact = steps since the last contact (1 = this step), n = contacts in "
	    "that pass, neg = pushed out of a triangle's back side (the solver gives that contact unlimited friction)";
	line();

	u32 nw = row(0).wheels;
	l = "step,time_ms,h,flags,valid,gear,ratio,v_long,v_lat,v_vert,a_long,a_lat,a_vert,yaw_rate,roll_rate,"
	    "pitch_rate,roll,pitch,heading,vel_heading,sideslip,fc_long,fc_lat,fc_norm,thr_in,thr,brk_in,brk,hand_k,"
	    "hand_slide,steer,steer_k,rpm,shaft,ground,clutch,engage,t_engine,t_shaft,t_wheels,crash_lost,crash_dv,stalls,"
	    "stall_reason";
	for (u32 i = 0; i < nw; ++i)
		trace_cat(l, ",w%u_w,w%u_rim,w%u_comp,w%u_load_est_N,w%u_mvel,w%u_mfmax,w%u_diff_k,w%u_mu,w%u_slip2,w%u_patch,"
		          "w%u_steer_tgt,w%u_steer_act,w%u_mtl,w%u_contact,w%u_n,w%u_neg",
		          i, i, i, i, i, i, i, i, i, i, i, i, i, i, i, i);
	line();
	for (u32 k = 0; k < H.dump_count; ++k)
	{
		const SHandling::STraceStep& r = row(k);
		trace_cat(l, "%u,%u,%.4f,0x%05x,%u,%u,%.4f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.4f,%.4f,%.4f,%.2f,%.2f,%.2f,%.2f,%.2f,",
		          r.step, r.time_ms, r.h, r.flags, r.valid, r.gear, r.ratio, r.v_long, r.v_lat, r.v_vert, r.a_long,
		          r.a_lat, r.a_vert, r.yaw_rate, r.roll_rate, r.pitch_rate, r.roll, r.pitch, r.heading, r.vel_heading,
		          r.sideslip);
		trace_cat(l, "%.0f,%.0f,%.0f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.0f,%.0f,%.0f,%.3f,%.3f,%.2f,%.2f,%.1f,%.4f,%.3f,%u,%u",
		          r.fc_long, r.fc_lat, r.fc_norm, r.thr_in, r.thr, r.brk_in, r.brk, r.hand_k, r.hand_slide, r.steer, r.steer_k,
		          r.rpm, r.shaft, r.ground, r.clutch, r.engage, r.t_engine, r.t_shaft, r.t_wheels, r.crash_lost, r.crash_dv,
		          r.stall_count, r.stall_reason);
		for (u32 i = 0; i < nw && i < r.wheels; ++i)
		{
			const SHandling::STraceWheel& w = r.wheel[i];
			trace_cat(l, ",%.3f,%.3f,%.4f,%.0f,%.3f,%.1f,%.3f,%.3f,%.3g,%.3f,%.4f,%.4f,%d,%u,%u,%u", w.w, w.rim, w.comp,
			          w.load_est, w.mvel, w.mfmax, w.diff_k, w.mu, w.slip2, w.patch, w.steer_tgt, w.steer_act, w.mtl,
			          w.contact, w.contacts, w.neg);
		}
		line();
	}
	FS.w_close(W);
	Msg("[VHO] handling trace written: logs\\%s (%u steps; %s)", fname, H.dump_count, H.latched_events.c_str());
	H.dump_count = 0; // the buffer stays sized for the next swap
	if (P.debug_trace <= 0.f) HandlingTraceFree();
}
