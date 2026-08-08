#pragma once
#include "firedeps.h"
#include "../xrEngine/ObjectAnimator.h"
#include "../Include/xrRender/Kinematics.h"
#include "../Include/xrRender/KinematicsAnimated.h"
#include "actor_defs.h"
#include "player_hud_legs.h"

#define SCOPE_ATTACH_IDX 2

class player_hud;
class CHudItem;
class CMotionDef;

struct motion_descr
{
	MotionID mid;
	shared_str name;
};

struct player_hud_motion
{
	shared_str m_alias_name;
	shared_str m_base_name;
	shared_str m_additional_name;
	float m_anim_speed, m_anim_end = 0.f;
	xr_vector<motion_descr> m_animations;
};

struct player_hud_motion_container
{
	xr_vector<player_hud_motion> m_anims;
	player_hud_motion* find_motion(const shared_str& name);
	void load(IKinematicsAnimated* model, const shared_str& sect);
};

struct hand_motions
{
	shared_str section;
	player_hud_motion_container pm;
};

enum eMovementLayers
{
	eAimWalk = 0,
	eAimCrouch,
	eCrouch,
	eWalk,
	eRun,
	eSprint,
	eAimIdle,
	eIdle,
	move_anms_end
};

struct movement_layer
{
	CObjectAnimator* anm;
	float blend_amount[2];
	bool active;
	float m_power;
	Fmatrix blend;
	u8 m_part;

	movement_layer()
	{
		blend.identity();
		anm = xr_new<CObjectAnimator>();
		blend_amount[0] = 0.f;
		blend_amount[1] = 0.f;
		active = false;
		m_power = 1.f;
	}

	void Load(LPCSTR name)
	{
		if (xr_strcmp(name, anm->Name()))
			anm->Load(name);
	}

	void Play(bool bLoop = true)
	{
		if (!anm->Name() || !xr_strcmp("", anm->Name()))
			return;

		if (IsPlaying())
		{
			active = true;
			return;
		}

		anm->Play(bLoop);
		active = true;
	}

	bool IsPlaying()
	{
		return anm->IsPlaying();
	}

	void Stop(bool bForce)
	{
		if (bForce)
		{
			anm->Stop();
			blend_amount[0] = 0.f;
			blend_amount[1] = 0.f;
			blend.identity();
		}

		active = false;
	}

	const Fmatrix& XFORM(u8 part)
	{
		auto min_jerk_interp = [](float t) {
			return 10*t*t*t - 15*t*t*t*t + 6*t*t*t*t*t;
		};

		float eased = min_jerk_interp(blend_amount[part]);

		// Rotation interpolation
		Fquaternion qA; qA.identity();
		Fquaternion qB; qB.set(anm->XFORM());
		{
			// Take m_power into account
			Fvector axis;
			float angle;
			if (qB.get_axis_angle(axis, angle)) {
				angle *= m_power;
				qB.rotation(axis, angle);
			}
		}
		Fquaternion q; q.slerp(qA, qB, eased);

		// Translation interpolation
		Fvector t = anm->XFORM().c;
		t.mul(m_power * eased);

		blend.mk_xform(q, t);
		return blend;
	}
};

struct script_layer
{
	shared_str m_name;
	CObjectAnimator* anm;
	float blend_amount;
	float m_power;
	bool active;
	Fmatrix blend;
	u8 m_part;
    shared_str m_pivot_bone;

	script_layer(LPCSTR name, u8 part, float speed = 1.f, float power = 1.f, bool looped = true, LPCSTR pivot_bone = nullptr)
	{
		m_name = name;
		m_part = part;
		m_power = power;
		m_pivot_bone = pivot_bone;
		blend.identity();
		anm = xr_new<CObjectAnimator>();
		anm->Load(name);
		anm->Play(looped);
		anm->Speed() = speed;
		blend_amount = 0.f;
		active = true;
	}

	bool IsPlaying()
	{
		return anm->IsPlaying();
	}

	void Stop(bool bForce)
	{
		if (bForce)
		{
			anm->Stop();
			blend_amount = 0.f;
			blend.identity();
		}

		active = false;
	}

	const Fmatrix& XFORM()
	{
		auto min_jerk_interp = [](float t) {
			return 10*t*t*t - 15*t*t*t*t + 6*t*t*t*t*t;
		};

		float eased = min_jerk_interp(blend_amount);

		// Rotation interpolation
		Fquaternion qA; qA.identity();
		Fquaternion qB; qB.set(anm->XFORM());
		{
			// Take m_power into account
			Fvector axis;
			float angle;
			if (qB.get_axis_angle(axis, angle)) {
				angle *= m_power;
				qB.rotation(axis, angle);
			}
		}
		Fquaternion q; q.slerp(qA, qB, eased);

		// Translation interpolation
		Fvector t = anm->XFORM().c;
		t.mul(m_power * eased);

		blend.mk_xform(q, t);
		return blend;
	}
};

struct BoneCallbackParams
{
	Fvector m_current;
	Fvector m_target;

	BoneCallbackParams()
	{
		m_current = { 0,0,0 };
		m_target = { 0,0,0 };
	}
};

enum EBoneCallbackParam
{
	r_finger0 = 0,
	r_finger01,
	r_finger02,
	//bip01_r_finger1,
	//bip01_r_finger11,
	//bip01_r_finger12,
};

struct hud_item_measures
{
	enum { e_fire_point = (1 << 0), e_fire_point2 = (1 << 1), e_shell_point = (1 << 2), e_16x9_mode_now = (1 << 3), e_fire_point_silencer = (1 << 4) };

	Flags8 m_prop_flags;

	Fvector m_item_attach[2]; // pos,rot
	Fvector m_hands_offset[2][8]; // pos,rot/ normal,aim,GL,aim_alt,safemode, normal2, attach_base, attach_mount --#SM+#--
	Fvector m_strafe_offset[4][2]; // pos,rot,data1,data2/ normal,aim-GL	 --#SM+#--

	u16 m_fire_bone;
	Fvector m_fire_point_offset;
	u16 m_fire_bone2;
	Fvector m_fire_point2_offset;
	u16 m_fire_bone_silencer;
	Fvector m_fire_point_silencer;
	Fvector m_fire_direction;
	u16 m_shell_bone;
	Fvector m_shell_point_offset;

	Fvector m_hands_attach[2]; //pos,rot

	void load(const shared_str& sect_name, IKinematics* K);

	struct inertion_params
	{
		float m_tendto_speed;
		float m_tendto_speed_aim;
		float m_tendto_ret_speed;
		float m_tendto_ret_speed_aim;

		float m_min_angle;
		float m_min_angle_aim;

		Fvector4 m_offset_LRUD;
		Fvector4 m_offset_LRUD_aim;
	};

	inertion_params m_inertion_params;

	struct shooting_params
	{
		bool bShootShake;
		Fvector4 m_shot_max_offset_LRUD;
		Fvector4 m_shot_max_offset_LRUD_aim;
		Fvector2 m_shot_offset_BACKW;
		float m_ret_speed;
		float m_ret_speed_aim;
		float m_min_LRUD_power;
	};

	shooting_params m_shooting_params;

	float m_fFreelookZOffset;
	bool m_bLeadGunLeftHand;
	float m_attach_scale;
};

struct attachable_hud_item
{
	player_hud* m_parent;
	CHudItem* m_parent_hud_item;
	shared_str m_sect_name;
	IKinematics* m_model;
	u16 m_attach_place_idx;
	hud_item_measures m_measures;

	//runtime positioning
	Fmatrix m_attach_offset;
	Fmatrix m_item_transform;

	player_hud_motion_container* m_hand_motions;

	attachable_hud_item(player_hud* pparent) : m_parent(pparent), m_upd_firedeps_frame(u32(-1)), m_parent_hud_item(nullptr),
		m_model(nullptr), m_attach_place_idx(0) {
	}
	~attachable_hud_item();
	void load(const shared_str& sect_name);
	void update(bool bForce);
	void setup_firedeps(firedeps& fd);
	void render();
	void render_item_ui();
	bool render_item_ui_query();
	bool need_renderable();
	void set_bone_visible(const shared_str& bone_name, BOOL bVisibility, BOOL bSilent = FALSE);
	void debug_draw_firedeps();
	player_hud_motion* find_motion(const shared_str& anm_name);
	//hands bind position
	Fvector& hands_attach_pos();
	Fvector& hands_attach_rot();

	//hands runtime offset
	Fvector& hands_offset_pos();
	Fvector& hands_offset_rot();

	Fvector& aim_offset_pos();
	Fvector& aim_offset_rot();

	Fvector& alt_aim_offset_pos();
	Fvector& alt_aim_offset_rot();

	Fvector& attach_base_offset_pos();
	Fvector& attach_base_offset_rot();
	Fvector& attach_mount_offset_pos();
	Fvector& attach_mount_offset_rot();
	float attach_scale();

	//props
	u32 m_upd_firedeps_frame;
	void tune(Ivector values);
	u32 anim_play(const shared_str& anim_name, BOOL bMixIn, const CMotionDef*& md, u8& rnd, float speed = 0, bool bMixIn2 = true);
};

class player_hud
{
public:
	player_hud();
	~player_hud();
	void load(const shared_str& model_name, bool force = false);
	void load_script(LPCSTR section);
	void reset_model_script() { script_override_arms = false; load(m_sect_name, true); };
	void load_default() { load("actor_hud_05", true); };
	void update(const Fmatrix& trans);
	void updateMovementLayerState();
	void StopScriptAnim();
	void PlayBlendAnm(LPCSTR name, u8 part = 0, float speed = 1.f, float power = 1.f, bool bLooped = true, bool no_restart = false, LPCSTR pivot_bone = nullptr);
	void StopBlendAnm(LPCSTR name, bool bForce = false);
	void StopAllBlendAnms(bool bForce);
	float SetBlendAnmTime(LPCSTR name, float time);
	void render_hud();
	void render_item_ui();
	bool render_item_ui_query();
	u32 anim_play(u16 part, const MotionID& M, BOOL bMixIn, const CMotionDef*& md, float speed, u16 override_part = u16(-1));
	u32 script_anim_play(u8 hand, LPCSTR itm_name, LPCSTR anm_name, bool bMixIn = true, float speed = 1.f);
	const shared_str& section_name() const { return m_sect_name; }
	void OnFrame();
	void net_Relcase(CObject* obj);

	u8 script_anim_part;
	Fvector script_anim_offset[2];
	u32 script_anim_end;
	float script_anim_offset_factor;
	bool m_bStopAtEndAnimIsRunning;
	bool script_anim_item_attached;
	bool script_override_arms;
	bool script_anim_lead_gun;
	IKinematicsAnimated* script_anim_item_model;
	Fvector item_pos[2];
	Fmatrix m_item_pos;
	u8 m_attach_idx;

	//Movement animation layers: 0 = aim_walk, 1 = aim_crouch, 2 = crouch, 3 = walk, 4 = run, 5 = sprint
	xr_vector<movement_layer*> m_movement_layers;
	xr_vector<script_layer*> m_script_layers;

	xr_vector<hand_motions*> m_hand_motions;
	player_hud_motion_container* get_hand_motions(LPCSTR section);

	void update_script_item();

	void attach_item(CHudItem* item);
	void re_sync_anim(u8 part);
	void set_part_cycle_time(u8 part, float time);
	void set_part_cycle_speed(u8 part, float speed);
	bool allow_activation(CHudItem* item);
	attachable_hud_item* attached_item(u16 item_idx) { return m_attached_items[item_idx]; };
	void detach_item_idx(u16 idx);
	void detach_item(CHudItem* item);

	bool allow_script_anim();

	void detach_all_items()
	{
		m_attached_items[0] = NULL;
		m_attached_items[1] = NULL;
		m_attached_items[SCOPE_ATTACH_IDX] = NULL;
	};

	Fmatrix m_transform;
	Fmatrix m_transform_2;

	Fmatrix m_attach_offset;
	Fmatrix m_attach_offset_2;

	void calc_transform(u16 attach_slot_idx, const Fmatrix& offset, Fmatrix& result, bool leadGun = false);
	void tune(Ivector values);
	u32 motion_length(const MotionID& M, const CMotionDef*& md, float speed);
	u32 motion_length_script(LPCSTR section, LPCSTR anm_name, float speed);
	u32 motion_length(const shared_str& anim_name, const shared_str& hud_name, const CMotionDef*& md);
	void OnMovementChanged(ACTOR_DEFS::EMoveCommand cmd);
	bool inertion_allowed();

private:
	const Fvector attach_rot(u8 part) const;
	const Fvector attach_pos(u8 part) const;
	shared_str m_sect_name;
	xr_vector<u16> m_ancors;
	attachable_hud_item* m_attached_items[3];
	static void _BCL FingerCallback(CBoneInstance* B);

public:
	IKinematicsAnimated* m_model;
	IKinematicsAnimated* m_model_2;
	Fvector m_adjust_offset[2][10]; // pos,rot/ normal,aim,GL,aim_alt,safemode, normal2, attach_base, attach_mount, aim for attach, alt aim for attach
	Fvector m_adjust_obj[2]; // pos,rot; used for the item/weapon itself
	Fvector m_adjust_ui_offset[2]; // pos,rot; used for custom device ui

	Fvector m_adjust_firepoint_shell[2][2];
	xr_map<EBoneCallbackParam, BoneCallbackParams*> m_bone_callback_params; // bonename,params
	int m_edit_attachment;
	float m_adjust_zoom_factor[3];
	float m_adjust_scale;
	bool m_adjust_mode;
	u16 m_edit_bone;

	void reset_thumb(bool bForce)
	{
		if (bForce)
		{
			m_bone_callback_params[r_finger0]->m_current.set(0.f, 0.f, 0.f);
			m_bone_callback_params[r_finger01]->m_current.set(0.f, 0.f, 0.f);
			m_bone_callback_params[r_finger02]->m_current.set(0.f, 0.f, 0.f);
		}
		
		m_bone_callback_params[r_finger0]->m_target.set(0.f, 0.f, 0.f);
		m_bone_callback_params[r_finger01]->m_target.set(0.f, 0.f, 0.f);
		m_bone_callback_params[r_finger02]->m_target.set(0.f, 0.f, 0.f);
	}

	/*void reset_triggerfinger(bool bForce)
	{
		if (bForce)
		{
			m_bone_callback_params[bip01_r_finger1]->m_current.set(0.f, 0.f, 0.f);
			m_bone_callback_params[bip01_r_finger11]->m_current.set(0.f, 0.f, 0.f);
			m_bone_callback_params[bip01_r_finger12]->m_current.set(0.f, 0.f, 0.f);
		}

		m_bone_callback_params[bip01_r_finger1]->m_target.set(0.f, 0.f, 0.f);
		m_bone_callback_params[bip01_r_finger11]->m_target.set(0.f, 0.f, 0.f);
		m_bone_callback_params[bip01_r_finger12]->m_target.set(0.f, 0.f, 0.f);
	}*/

	DECLARE_SCRIPT_REGISTER_FUNCTION
};

extern player_hud* g_player_hud;
