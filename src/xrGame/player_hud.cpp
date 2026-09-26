#include "stdafx.h"
#include "player_hud.h"
#include "HudItem.h"
#include "ui_base.h"
#include "actor.h"
#include "physic_item.h"
#include "static_cast_checked.hpp"
#include "actoreffector.h"
#include "../xrEngine/IGame_Persistent.h"
#include "inventory_item.h"
#include "weapon.h"
#include "script_attachment_manager.h"
#include "../xrEngine/CameraBase.h"

extern int g_nearwall;

player_hud* g_player_hud = NULL;
Fvector _ancor_pos;
Fvector _wpn_root_pos;

namespace
{
static bool parkour_ik_from_to(Fmatrix& result, const Fvector& from, const Fvector& to)
{
	Fvector a = from;
	Fvector b = to;
	if (a.square_magnitude() < EPS_S)
		return false;
	a.normalize();
	if (b.square_magnitude() < EPS_S)
		return false;
	b.normalize();
	Fvector axis;
	axis.crossproduct(a, b);
	float sine = axis.magnitude();
	float cosine = a.dotproduct(b);
	if (sine < 0.0001f)
	{
		if (cosine > 0.f)
			return false;
		Fvector up;
		up.set(0.f, 1.f, 0.f);
		axis.crossproduct(a, up);
		if (axis.square_magnitude() < EPS_S)
		{
			up.set(1.f, 0.f, 0.f);
			axis.crossproduct(a, up);
		}
		axis.normalize();
		result.rotation(axis, PI);
		return true;
	}
	axis.mul(1.f / sine);
	result.rotation(axis, atan2(sine, cosine));
	return true;
}
}

player_hud_motion* player_hud_motion_container::find_motion(const shared_str& name)
{
	xr_vector<player_hud_motion>::iterator it = m_anims.begin();
	xr_vector<player_hud_motion>::iterator it_e = m_anims.end();
	for (; it != it_e; ++it)
	{
		const shared_str& s = (true) ? (*it).m_alias_name : (*it).m_base_name;
		if (s == name)
			return &(*it);
	}
	return NULL;
}

void player_hud_motion_container::load(IKinematicsAnimated* model, const shared_str& sect)
{
	CInifile::Sect& _sect = pSettings->r_section(sect);
	CInifile::SectCIt _b = _sect.Data.begin();
	CInifile::SectCIt _e = _sect.Data.end();
	player_hud_motion* pm = NULL;

	string512 buff;
	MotionID motion_ID;

	for (; _b != _e; ++_b)
	{
		if (strstr(_b->first.c_str(), "anm_") == _b->first.c_str())
		{
			const shared_str& anm = _b->second;
			m_anims.resize(m_anims.size() + 1);
			pm = &m_anims.back();
			//base and alias name
			pm->m_alias_name = _b->first;

			if (_GetItemCount(anm.c_str()) == 1)
			{
				pm->m_base_name = anm;
				pm->m_additional_name = anm;
			}
			else
			{
				R_ASSERT2(_GetItemCount(anm.c_str()) <= 4, anm.c_str());
				string512 str_item;
				_GetItem(anm.c_str(), 0, str_item);
				pm->m_base_name = str_item;

				_GetItem(anm.c_str(), 1, str_item);
				pm->m_additional_name = str_item;

				_GetItem(anm.c_str(), 2, str_item);
				pm->m_anim_speed = atof(str_item);

				_GetItem(anm.c_str(), 3, str_item);
				pm->m_anim_end = atof(str_item);
			}

			//and load all motions for it

			for (u32 i = 0; i <= 8; ++i)
			{
				if (i == 0)
					xr_strcpy(buff, pm->m_base_name.c_str());
				else
					xr_sprintf(buff, "%s%d", pm->m_base_name.c_str(), i);

				motion_ID = model->ID_Cycle_Safe(buff);
                //code to find hand anim names with speeds of not 1
                //CMotionDef* def = model->LL_GetMotionDef(motion_ID);
                //if (def->Speed() != 1) {
                //    Msg("omf speed is %f, anim_name is %s", def->Speed(), buff);
                //}
				if (!motion_ID.valid() && i == 0)
				{
					motion_ID = model->ID_Cycle_Safe("hand_idle_doun");
				}
				if (motion_ID.valid())
				{
					pm->m_animations.resize(pm->m_animations.size() + 1);
					pm->m_animations.back().mid = motion_ID;
					pm->m_animations.back().name = buff;
#ifdef DEBUG
					//					Msg(" alias=[%s] base=[%s] name=[%s]",pm->m_alias_name.c_str(), pm->m_base_name.c_str(), buff);
#endif // #ifdef DEBUG
				}
			}
			VERIFY2(pm->m_animations.size(), make_string("motion not found [%s]", pm->m_base_name.c_str()).c_str());
		}
	}
}

Fvector& attachable_hud_item::hands_attach_pos()
{
	if (g_player_hud->m_adjust_mode)
		return g_player_hud->m_adjust_offset[0][0];
	return m_measures.m_hands_attach[0];
}

Fvector& attachable_hud_item::hands_attach_rot()
{
	if (g_player_hud->m_adjust_mode)
		return g_player_hud->m_adjust_offset[1][0];
	return m_measures.m_hands_attach[1];
}

Fvector& attachable_hud_item::hands_offset_pos()
{
	u8 idx = m_parent_hud_item->GetCurrentHudOffsetIdx();
	return m_measures.m_hands_offset[0][idx];
}

Fvector& attachable_hud_item::hands_offset_rot()
{
	u8 idx = m_parent_hud_item->GetCurrentHudOffsetIdx();
	return m_measures.m_hands_offset[1][idx];
}

Fvector& attachable_hud_item::aim_offset_pos()
{
	if (g_player_hud->m_adjust_mode) {
		if (m_attach_place_idx == SCOPE_ATTACH_IDX)
			return g_player_hud->m_adjust_offset[0][8];
		return g_player_hud->m_adjust_offset[0][1];
	}
	return m_measures.m_hands_offset[0][1];
}

Fvector& attachable_hud_item::aim_offset_rot()
{
	if (g_player_hud->m_adjust_mode) {
		if (m_attach_place_idx == SCOPE_ATTACH_IDX)
			return g_player_hud->m_adjust_offset[1][8];
		return g_player_hud->m_adjust_offset[1][1];
	}
	return m_measures.m_hands_offset[1][1];
}

Fvector& attachable_hud_item::alt_aim_offset_pos()
{
	if (g_player_hud->m_adjust_mode) {
		if (m_attach_place_idx == SCOPE_ATTACH_IDX)
			return g_player_hud->m_adjust_offset[0][9];
		return g_player_hud->m_adjust_offset[0][3];
	}
	return m_measures.m_hands_offset[0][3];
}

Fvector& attachable_hud_item::alt_aim_offset_rot()
{
	if (g_player_hud->m_adjust_mode) {
		if (m_attach_place_idx == SCOPE_ATTACH_IDX)
			return g_player_hud->m_adjust_offset[1][9];
		return g_player_hud->m_adjust_offset[1][3];
	}
	return m_measures.m_hands_offset[1][3];
}

Fvector& attachable_hud_item::attach_base_offset_pos()
{
	if (g_player_hud->m_adjust_mode)
		return g_player_hud->m_adjust_offset[0][6];
	return m_measures.m_hands_offset[0][6];
}

Fvector& attachable_hud_item::attach_base_offset_rot()
{
	if (g_player_hud->m_adjust_mode)
		return g_player_hud->m_adjust_offset[1][6];
	return m_measures.m_hands_offset[1][6];
}

Fvector& attachable_hud_item::attach_mount_offset_pos()
{
	if (g_player_hud->m_adjust_mode)
		return g_player_hud->m_adjust_offset[0][7];
	return m_measures.m_hands_offset[0][7];
}

Fvector& attachable_hud_item::attach_mount_offset_rot()
{
	if (g_player_hud->m_adjust_mode)
		return g_player_hud->m_adjust_offset[1][7];
	return m_measures.m_hands_offset[1][7];
}

float attachable_hud_item::attach_scale()
{
	if (g_player_hud->m_adjust_mode)
		return g_player_hud->m_adjust_scale;
	return m_measures.m_attach_scale;
}

void attachable_hud_item::set_bone_visible(const shared_str& bone_name, BOOL bVisibility, BOOL bSilent)
{
	u16 bone_id;
	BOOL bVisibleNow;
	bone_id = m_model->LL_BoneID(bone_name);
	if (bone_id == BI_NONE)
	{
		if (bSilent) return;
		R_ASSERT2(
			0, make_string("model [%s] has no bone [%s]", pSettings->r_string(m_sect_name, "item_visual"), bone_name.
				c_str()).c_str());
	}
	bVisibleNow = m_model->LL_GetBoneVisible(bone_id);
	if (bVisibleNow != bVisibility)
		m_model->LL_SetBoneVisible(bone_id, bVisibility, TRUE);
}

void attachable_hud_item::update(bool bForce)
{
	if (!bForce && m_upd_firedeps_frame == Device.dwFrame) return;
	bool is_16x9 = UI().is_widescreen();

	if (!!m_measures.m_prop_flags.test(hud_item_measures::e_16x9_mode_now) != is_16x9)
		m_measures.load(m_sect_name, m_model);

	Fvector ypr = m_parent->m_adjust_mode ? m_parent->m_adjust_obj[1] : m_measures.m_item_attach[1];
	ypr.mul(PI / 180.f);
	m_attach_offset.setHPB(ypr.x, ypr.y, ypr.z);
	m_attach_offset.translate_over(m_parent->m_adjust_mode ? m_parent->m_adjust_obj[0] : m_measures.m_item_attach[0]);

	if (m_attach_place_idx == SCOPE_ATTACH_IDX) {
		m_item_transform.set(m_parent->attached_item(0)->m_item_transform);

		Fmatrix hud_rotation;
		hud_rotation.identity();
		hud_rotation.rotateX(m_parent->attached_item(0)->attach_mount_offset_rot().x);

		Fmatrix hud_rotation_y;
		hud_rotation_y.identity();
		hud_rotation_y.rotateY(m_parent->attached_item(0)->attach_mount_offset_rot().y);
		hud_rotation.mulA_43(hud_rotation_y);

		hud_rotation_y.identity();
		hud_rotation_y.rotateZ(m_parent->attached_item(0)->attach_mount_offset_rot().z);
		hud_rotation.mulA_43(hud_rotation_y);

		hud_rotation.translate_over(m_parent->attached_item(0)->attach_mount_offset_pos());
		m_item_transform.mulB_43(hud_rotation);
	}
	else {
		m_parent->calc_transform(m_attach_place_idx, m_attach_offset, m_item_transform, m_measures.m_bLeadGunLeftHand);
		m_upd_firedeps_frame = Device.dwFrame;
	}

	IKinematicsAnimated* ka = m_model->dcast_PKinematicsAnimated();
	if (ka)
	{
		ka->UpdateTracks();
		ka->dcast_PKinematics()->CalculateBones_Invalidate();
		ka->dcast_PKinematics()->CalculateBones(TRUE);
	}
}

void attachable_hud_item::setup_firedeps(firedeps& fd)
{
	update(false);
	// fire point&direction
	if (m_measures.m_prop_flags.test(hud_item_measures::e_fire_point))
	{
		Fmatrix& fire_mat = m_model->LL_GetTransform(m_measures.m_fire_bone);
		fire_mat.transform_tiny(fd.vLastFP, m_parent->m_adjust_mode ? m_parent->m_adjust_firepoint_shell[0][0] : m_measures.m_fire_point_offset);
		m_item_transform.transform_tiny(fd.vLastFP);

		fd.vLastFD.set(m_parent->m_adjust_mode ? m_parent->m_adjust_firepoint_shell[1][0] : m_measures.m_fire_direction);
		m_item_transform.transform_dir(fd.vLastFD);
		VERIFY(_valid(fd.vLastFD));

		fd.m_FireParticlesXForm.identity();
		fd.m_FireParticlesXForm.k.set(fd.vLastFD);
		Fvector::generate_orthonormal_basis_normalized(fd.m_FireParticlesXForm.k,
			fd.m_FireParticlesXForm.j,
			fd.m_FireParticlesXForm.i);

		VERIFY(_valid(fd.m_FireParticlesXForm));

		// demonized: transforms for fire bone/point silencer, they should be identical to above if they dont exist
		{
			Fmatrix& fire_mat = m_model->LL_GetTransform(m_measures.m_fire_bone_silencer);
			fire_mat.transform_tiny(fd.vLastFPSilencer, m_parent->m_adjust_mode ? m_parent->m_adjust_firepoint_shell[0][0] : m_measures.m_fire_point_silencer);
			m_item_transform.transform_tiny(fd.vLastFPSilencer);
			fd.vLastFD.set(m_parent->m_adjust_mode ? m_parent->m_adjust_firepoint_shell[1][0] : m_measures.m_fire_direction);
			VERIFY(_valid(fd.vLastFPSilencer));
		}
	}

	if (m_measures.m_prop_flags.test(hud_item_measures::e_fire_point2))
	{
		Fmatrix& fire_mat = m_model->LL_GetTransform(m_measures.m_fire_bone2);
		fire_mat.transform_tiny(fd.vLastFP2, m_parent->m_adjust_mode ? m_parent->m_adjust_firepoint_shell[0][1] : m_measures.m_fire_point2_offset);
		m_item_transform.transform_tiny(fd.vLastFP2);
		fd.vLastFD.set(m_parent->m_adjust_mode ? m_parent->m_adjust_firepoint_shell[1][0] : m_measures.m_fire_direction);
		VERIFY(_valid(fd.vLastFP2));
	}

	if (m_measures.m_prop_flags.test(hud_item_measures::e_shell_point))
	{
		Fmatrix& fire_mat = m_model->LL_GetTransform(m_measures.m_shell_bone);
		fire_mat.transform_tiny(fd.vLastSP, m_parent->m_adjust_mode ? m_parent->m_adjust_firepoint_shell[1][1] : m_measures.m_shell_point_offset);
		m_item_transform.transform_tiny(fd.vLastSP);
		VERIFY(_valid(fd.vLastSP));
	}
}

bool attachable_hud_item::need_renderable()
{
	return m_parent_hud_item->need_renderable();
}

void attachable_hud_item::render()
{
	::Render->set_Transform(&m_item_transform);
	::Render->add_Visual(m_model->dcast_RenderVisual());

	m_parent_hud_item->render_hud_mode();

	if (m_parent_hud_item->has_object() && m_parent_hud_item->object().GetAttachments()->size())
	{
		for (auto& pair : *m_parent_hud_item->object().GetAttachments())
		{
			if (pair.second->GetType() == eSA_HUD)
				pair.second->Render(m_model, &m_item_transform);
		}
	}
}

bool attachable_hud_item::render_item_ui_query()
{
	return m_parent_hud_item->render_item_3d_ui_query();
}

void attachable_hud_item::render_item_ui()
{
	m_parent_hud_item->render_item_3d_ui();

	if (m_parent_hud_item->has_object() && m_parent_hud_item->object().GetAttachments()->size())
	{
		for (auto& pair : *m_parent_hud_item->object().GetAttachments())
		{
			if (pair.second->GetType() == eSA_HUD)
				pair.second->RenderUI();
		}
	}
}

void hud_item_measures::load(const shared_str& sect_name, IKinematics* K)
{
	bool is_16x9 = UI().is_widescreen();
	string64 _prefix;
	xr_sprintf(_prefix, "%s", is_16x9 ? "_16x9" : "");
	string128 val_name;

	strconcat(sizeof(val_name), val_name, "hands_position", _prefix);
	m_hands_attach[0] = pSettings->r_fvector3(sect_name, val_name);
	strconcat(sizeof(val_name), val_name, "hands_orientation", _prefix);
	m_hands_attach[1] = pSettings->r_fvector3(sect_name, val_name);

	m_item_attach[0] = pSettings->r_fvector3(sect_name, "item_position");
	m_item_attach[1] = pSettings->r_fvector3(sect_name, "item_orientation");

	shared_str bone_name;
	m_prop_flags.set(e_fire_point, pSettings->line_exist(sect_name, "fire_bone"));
	if (m_prop_flags.test(e_fire_point))
	{
		// demonized: replacing r_string with READ_IF_EXISTS
		bone_name = READ_IF_EXISTS(pSettings, r_string, sect_name, "fire_bone", "wpn_body");
		//bone_name = pSettings->r_string(sect_name, "fire_bone");
		m_fire_bone = K->LL_BoneID(bone_name);

		// Print warning if bone doesn't exist
		if (m_fire_bone == BI_NONE)
		{
			Msg("![%s] Invalid or not found fire_bone %s", sect_name.c_str(), bone_name.c_str());
		}

		m_fire_point_offset = pSettings->r_fvector3(sect_name, "fire_point");
		m_fire_direction = READ_IF_EXISTS(pSettings, r_fvector3, sect_name, "fire_direction",
			Fvector().set(0.f, 0.f, 1.0f));
	}
	else
		m_fire_point_offset.set(0, 0, 0);

	m_prop_flags.set(e_fire_point2, pSettings->line_exist(sect_name, "fire_bone2"));
	if (m_prop_flags.test(e_fire_point2))
	{
		bone_name = READ_IF_EXISTS(pSettings, r_string, sect_name, "fire_bone2", "wpn_body");
		//bone_name = pSettings->r_string(sect_name, "fire_bone2");
		m_fire_bone2 = K->LL_BoneID(bone_name);

		// Print warning if bone doesn't exist
		if (m_fire_bone2 == BI_NONE)
		{
			Msg("![%s] Invalid or not found fire_bone2 %s", sect_name.c_str(), bone_name.c_str());
		}

		m_fire_point2_offset = pSettings->r_fvector3(sect_name, "fire_point2");
	}
	else
		m_fire_point2_offset.set(0, 0, 0);

	// demonized: fire point for silencer
	m_prop_flags.set(e_fire_point_silencer, pSettings->line_exist(sect_name, "fire_bone_silencer"));
	bone_name = READ_IF_EXISTS(pSettings, r_string, sect_name, "fire_bone_silencer", READ_IF_EXISTS(pSettings, r_string, sect_name, "fire_bone", "wpn_body"));
	m_fire_bone_silencer = K->LL_BoneID(bone_name);
	m_fire_point_silencer.set(pSettings->line_exist(sect_name, "fire_point_silencer") ? pSettings->r_fvector3(sect_name, "fire_point_silencer") : m_fire_point_offset);

	m_prop_flags.set(e_shell_point, pSettings->line_exist(sect_name, "shell_bone"));
	if (m_prop_flags.test(e_shell_point))
	{
		bone_name = READ_IF_EXISTS(pSettings, r_string, sect_name, "shell_bone", "wpn_body");
		//bone_name = pSettings->r_string(sect_name, "shell_bone");
		m_shell_bone = K->LL_BoneID(bone_name);

		// Print warning if bone doesn't exist
		if (m_shell_bone == BI_NONE)
		{
			Msg("![%s] Invalid or not found shell_bone %s", sect_name.c_str(), bone_name.c_str());
		}

		m_shell_point_offset = pSettings->r_fvector3(sect_name, "shell_point");
	}
	else
		m_shell_point_offset.set(0, 0, 0);

	m_hands_offset[0][0].set(0, 0, 0);
	m_hands_offset[1][0].set(0, 0, 0);

	//DaimeneX: base_hud_offset_pos and base_hud_offset_rot. Editable variables that allow weapon position adjustements that don't require aim tweaks
	strconcat(sizeof(val_name), val_name, "base_hud_offset_pos", _prefix);
	if (pSettings->line_exist(sect_name, val_name))
		m_hands_offset[0][5] = pSettings->r_fvector3(sect_name, val_name);
	else
		m_hands_offset[0][5].set(0, 0, 0);

	strconcat(sizeof(val_name), val_name, "base_hud_offset_rot", _prefix);
	if (pSettings->line_exist(sect_name, val_name))
		m_hands_offset[1][5] = pSettings->r_fvector3(sect_name, val_name);
	else
		m_hands_offset[1][5].set(0, 0, 0);

	strconcat(sizeof(val_name), val_name, "aim_hud_offset_pos", _prefix);
	m_hands_offset[0][1] = pSettings->r_fvector3(sect_name, val_name);
	strconcat(sizeof(val_name), val_name, "aim_hud_offset_rot", _prefix);
	m_hands_offset[1][1] = pSettings->r_fvector3(sect_name, val_name);

	strconcat(sizeof(val_name), val_name, "gl_hud_offset_pos", _prefix);
	m_hands_offset[0][2] = pSettings->r_fvector3(sect_name, val_name);
	strconcat(sizeof(val_name), val_name, "gl_hud_offset_rot", _prefix);
	m_hands_offset[1][2] = pSettings->r_fvector3(sect_name, val_name);

	// demonized: removing asserts
	/*R_ASSERT2(pSettings->line_exist(sect_name,"fire_point")==pSettings->line_exist(sect_name,"fire_bone"),
			  sect_name.c_str());
	R_ASSERT2(pSettings->line_exist(sect_name,"fire_point2")==pSettings->line_exist(sect_name,"fire_bone2"),
			  sect_name.c_str());
	R_ASSERT2(pSettings->line_exist(sect_name,"shell_point")==pSettings->line_exist(sect_name,"shell_bone"),
			  sect_name.c_str());*/

	m_prop_flags.set(e_16x9_mode_now, is_16x9);

	////////////////////////////////////////////
	//--#SM+# Begin--
	const Fvector vZero = { 0.f, 0.f, 0.f };

	// Альтернативное прицеливание
	strconcat(sizeof(val_name), val_name, "aim_hud_offset_alt_pos", _prefix);
	m_hands_offset[0][3] = READ_IF_EXISTS(pSettings, r_fvector3, sect_name, val_name, vZero);
	strconcat(sizeof(val_name), val_name, "aim_hud_offset_alt_rot", _prefix);
	m_hands_offset[1][3] = READ_IF_EXISTS(pSettings, r_fvector3, sect_name, val_name, vZero);

	// Safemode / lowered weapon position
	strconcat(sizeof(val_name), val_name, "lowered_hud_offset_pos", _prefix);
	m_hands_offset[0][4] = READ_IF_EXISTS(pSettings, r_fvector3, sect_name, val_name, vZero);
	strconcat(sizeof(val_name), val_name, "lowered_hud_offset_rot", _prefix);
	m_hands_offset[1][4] = READ_IF_EXISTS(pSettings, r_fvector3, sect_name, val_name, vZero);

	// Настройки стрейфа (боковая ходьба)
	Fvector vDefStrafeValue;
	vDefStrafeValue.set(vZero);

	//--> Смещение в стрейфе
	strconcat(sizeof(val_name), val_name, "strafe_hud_offset_pos", _prefix);
	m_strafe_offset[0][0] = READ_IF_EXISTS(pSettings, r_fvector3, sect_name, val_name, vDefStrafeValue);
	strconcat(sizeof(val_name), val_name, "strafe_hud_offset_rot", _prefix);
	m_strafe_offset[1][0] = READ_IF_EXISTS(pSettings, r_fvector3, sect_name, val_name, vDefStrafeValue);

	//--> Поворот в стрейфе
	strconcat(sizeof(val_name), val_name, "strafe_aim_hud_offset_pos", _prefix);
	m_strafe_offset[0][1] = READ_IF_EXISTS(pSettings, r_fvector3, sect_name, val_name, vDefStrafeValue);
	strconcat(sizeof(val_name), val_name, "strafe_aim_hud_offset_rot", _prefix);
	m_strafe_offset[1][1] = READ_IF_EXISTS(pSettings, r_fvector3, sect_name, val_name, vDefStrafeValue);

	//--> Параметры стрейфа
	bool bStrafeEnabled = READ_IF_EXISTS(pSettings, r_bool, sect_name, "strafe_enabled", false);
	bool bStrafeEnabled_aim = READ_IF_EXISTS(pSettings, r_bool, sect_name, "strafe_aim_enabled", false);
	float fFullStrafeTime = READ_IF_EXISTS(pSettings, r_float, sect_name, "strafe_transition_time", .5f);
	float fFullStrafeTime_aim = READ_IF_EXISTS(pSettings, r_float, sect_name, "strafe_aim_transition_time", .5f);
	float fStrafeCamLFactor = READ_IF_EXISTS(pSettings, r_float, sect_name, "strafe_cam_limit_factor", 0.5f);
	float fStrafeCamLFactor_aim = READ_IF_EXISTS(pSettings, r_float, sect_name, "strafe_cam_limit_aim_factor", 1.0f);
	float fStrafeMinAngle = READ_IF_EXISTS(pSettings, r_float, sect_name, "strafe_cam_min_angle", 0.0f);
	float fStrafeMinAngle_aim = READ_IF_EXISTS(pSettings, r_float, sect_name, "strafe_cam_aim_min_angle", 7.0f);

	//--> (Data 1)
	m_strafe_offset[2][0].set((bStrafeEnabled ? 1.0f : 0.0f), fFullStrafeTime, NULL); // normal
	m_strafe_offset[2][1].set((bStrafeEnabled_aim ? 1.0f : 0.0f), fFullStrafeTime_aim, NULL); // aim-GL

	//--> (Data 2)
	m_strafe_offset[3][0].set(fStrafeCamLFactor, fStrafeMinAngle, NULL); // normal
	m_strafe_offset[3][1].set(fStrafeCamLFactor_aim, fStrafeMinAngle_aim, NULL); // aim-GL

	// Загрузка параметров смещения / инерции
	m_inertion_params.m_tendto_speed = READ_IF_EXISTS(pSettings, r_float, sect_name, "inertion_tendto_speed",
		TENDTO_SPEED);
	m_inertion_params.m_tendto_speed_aim = READ_IF_EXISTS(pSettings, r_float, sect_name, "inertion_tendto_aim_speed",
		TENDTO_SPEED_AIM);
	m_inertion_params.m_tendto_ret_speed = READ_IF_EXISTS(pSettings, r_float, sect_name, "inertion_tendto_ret_speed",
		TENDTO_SPEED_RET);
	m_inertion_params.m_tendto_ret_speed_aim = READ_IF_EXISTS(pSettings, r_float, sect_name,
		"inertion_tendto_ret_aim_speed", TENDTO_SPEED_RET_AIM);

	m_inertion_params.m_min_angle =
		READ_IF_EXISTS(pSettings, r_float, sect_name, "inertion_min_angle", INERT_MIN_ANGLE);
	m_inertion_params.m_min_angle_aim = READ_IF_EXISTS(pSettings, r_float, sect_name, "inertion_min_angle_aim",
		INERT_MIN_ANGLE_AIM);

	m_inertion_params.m_offset_LRUD = READ_IF_EXISTS(pSettings, r_fvector4, sect_name, "inertion_offset_LRUD",
		Fvector4().set(ORIGIN_OFFSET));
	m_inertion_params.m_offset_LRUD_aim = READ_IF_EXISTS(pSettings, r_fvector4, sect_name, "inertion_offset_LRUD_aim",
		Fvector4().set(ORIGIN_OFFSET_AIM));

	// Загрузка параметров смещения при стрельбе
	m_shooting_params.bShootShake = READ_IF_EXISTS(pSettings, r_bool, sect_name, "shooting_hud_effect", false);
	m_shooting_params.m_shot_max_offset_LRUD = READ_IF_EXISTS(pSettings, r_fvector4, sect_name, "shooting_max_LRUD",
		Fvector4().set(0, 0, 0, 0));
	m_shooting_params.m_shot_max_offset_LRUD_aim = READ_IF_EXISTS(pSettings, r_fvector4, sect_name,
		"shooting_max_LRUD_aim", Fvector4().set(0, 0, 0, 0));
	m_shooting_params.m_shot_offset_BACKW = READ_IF_EXISTS(pSettings, r_fvector2, sect_name, "shooting_backward_offset",
		Fvector2().set(0, 0));
	m_shooting_params.m_ret_speed = READ_IF_EXISTS(pSettings, r_float, sect_name, "shooting_ret_speed", 1.0f);
	m_shooting_params.m_ret_speed_aim = READ_IF_EXISTS(pSettings, r_float, sect_name, "shooting_ret_aim_speed", 1.0f);
	m_shooting_params.m_min_LRUD_power = READ_IF_EXISTS(pSettings, r_float, sect_name, "shooting_min_LRUD_power", 0.0f);

	//--#SM+# End--

	strconcat(sizeof(val_name), val_name, "attach_base_hud_offset_pos", _prefix);
	m_hands_offset[0][6] = READ_IF_EXISTS(pSettings, r_fvector3, sect_name, val_name, vZero);
	strconcat(sizeof(val_name), val_name, "attach_base_hud_offset_rot", _prefix);
	m_hands_offset[1][6] = READ_IF_EXISTS(pSettings, r_fvector3, sect_name, val_name, vZero);

	strconcat(sizeof(val_name), val_name, "attach_mount_hud_offset_pos", _prefix);
	m_hands_offset[0][7] = READ_IF_EXISTS(pSettings, r_fvector3, sect_name, val_name, vZero);
	strconcat(sizeof(val_name), val_name, "attach_mount_hud_offset_rot", _prefix);
	m_hands_offset[1][7] = READ_IF_EXISTS(pSettings, r_fvector3, sect_name, val_name, vZero);

	m_fFreelookZOffset = READ_IF_EXISTS(pSettings, r_float, sect_name, "freelook_z_offset_mul", 0.f);
	m_bLeadGunLeftHand = READ_IF_EXISTS(pSettings, r_bool, sect_name, "lh_lead_gun", false);

	m_attach_scale = READ_IF_EXISTS(pSettings, r_float, sect_name, "attach_scale", 1);
}

attachable_hud_item::~attachable_hud_item()
{
	IRenderVisual* v = m_model->dcast_RenderVisual();
	::Render->model_Delete(v);
	m_model = nullptr;
}

void attachable_hud_item::load(const shared_str& sect_name)
{
	m_sect_name = sect_name;

	// Visual
	LPCSTR visual_name = pSettings->r_string(sect_name, "item_visual");
	::Render->hud_loading = true;
	IKinematicsAnimated* visual = ::Render->model_Create(visual_name)->dcast_PKinematicsAnimated();
	::Render->hud_loading = false;
	R_ASSERT2(visual, make_string("could not create model %s, section %s", visual_name, sect_name.c_str()));
	m_model = smart_cast<IKinematics*>(visual);

	m_attach_place_idx = pSettings->r_u16(sect_name, "attach_place_idx");
	m_measures.load(sect_name, m_model);
	m_hand_motions = m_parent->get_hand_motions(*sect_name);

    // demonized: shell particles for hud
    // An explicitly empty setting disables particles instead of using the weapon default.
    m_shell_particles_override = pSettings->line_exist(sect_name, "shell_particles");
    m_shell_particles = m_shell_particles_override ? pSettings->r_string(sect_name, "shell_particles") : nullptr;
}

player_hud_motion* attachable_hud_item::find_motion(const shared_str& anm_name)
{
	R_ASSERT(strstr(anm_name.c_str(), "anm_") == anm_name.c_str());
	string256 anim_name_r;
	bool is_16x9 = UI().is_widescreen();
	xr_sprintf(anim_name_r, "%s%s", anm_name.c_str(), ((m_attach_place_idx == 1) && is_16x9) ? "_16x9" : "");

	player_hud_motion* anm = m_hand_motions->find_motion(anim_name_r);

	if (!anm)
		anm = m_hand_motions->find_motion(anm_name);

	R_ASSERT2(anm, make_string("model [%s] has no motion alias defined [%s]", m_sect_name.c_str(), anm_name).c_str())
		;
	VERIFY2(anm->m_animations.size(),
		make_string("model [%s] has no motion defined in motion_alias [%s]", pSettings->r_string(m_sect_name,
			"item_visual"), anim_name_r).c_str());

	return anm;
}

u32 attachable_hud_item::anim_play(const shared_str& anm_name_b, BOOL bMixIn, const CMotionDef*& md, u8& rnd_idx,
	float speed, bool bMixIn2)
{
	player_hud_motion* anm = find_motion(anm_name_b);
	rnd_idx = (u8)Random.randI(anm->m_animations.size());
	const motion_descr& M = anm->m_animations[rnd_idx];
	if (speed == 1.f)
		speed = anm->m_anim_speed != 0 ? anm->m_anim_speed : 1.f;
        // Verdatim: store the final anim speed for use in motion mark timing scaling
        final_anim_speed = speed;

	u32 ret = 0;
	if (m_attach_place_idx != SCOPE_ATTACH_IDX) {
		ret = g_player_hud->anim_play(m_attach_place_idx, M.mid, bMixIn, md, speed);
	}

	if (m_model->dcast_PKinematicsAnimated())
	{
		IKinematicsAnimated* ka = m_model->dcast_PKinematicsAnimated();

		shared_str item_anm_name;
		if (anm->m_base_name != anm->m_additional_name)
			item_anm_name = anm->m_additional_name;
		else
			item_anm_name = M.name;

		MotionID M2 = ka->ID_Cycle_Safe(item_anm_name);
		if (!M2.valid())
			M2 = ka->ID_Cycle_Safe("idle");
		else if (bDebug)
			Msg("playing item animation [%s]", item_anm_name.c_str());

		R_ASSERT3(M2.valid(), make_string("model has no motion [idle], section %s", m_sect_name.c_str()).c_str(), pSettings->r_string(m_sect_name, "item_visual"));

		u16 root_id = m_model->LL_GetBoneRoot();
		CBoneInstance& root_binst = m_model->LL_GetBoneInstance(root_id);
		root_binst.set_callback_overwrite(TRUE);
		root_binst.mTransform.identity();
		if (m_attach_place_idx == SCOPE_ATTACH_IDX) {
			float s = m_parent->attached_item(0)->attach_scale();
			root_binst.mTransform.scale(s, s, s);
		}

		u16 pc = ka->partitions().count();
		for (u16 pid = 0; pid < pc; ++pid)
			CBlend* B = ka->PlayCycle(pid, M2, (!!bMixIn && bMixIn2), 0, 0, 0, speed);

		m_model->CalculateBones_Invalidate();
	}

	if (m_attach_place_idx == SCOPE_ATTACH_IDX) {
		return ret;
	}

	R_ASSERT2(m_parent_hud_item, "parent hud item is NULL");
	if (m_parent_hud_item->has_object()) {
		CPhysicItem& parent_object = m_parent_hud_item->object();
		//R_ASSERT2		(parent_object, "object has no parent actor");
		//CObject*		parent_object = static_cast_checked<CObject*>(&m_parent_hud_item->object());

		if (IsGameTypeSingle() && parent_object.H_Parent() == Level().CurrentControlEntity())
		{
			CActor* current_actor = static_cast_checked<CActor*>(Level().CurrentControlEntity());
			VERIFY(current_actor);

			string_path ce_path;
			string_path anm_name;
			strconcat(sizeof(anm_name), anm_name, "camera_effects\\weapon\\", M.name.c_str(), ".anm");
			if (FS.exist(ce_path, "$game_anims$", anm_name))
			{
				int rand = ::Random.randI(5000, 10000);
				CAnimatorCamEffector* e = xr_new<CHudMotionCamEffector>();
				e->SetType(ECamEffectorType(rand));
				e->SetHudAffect(false);
				e->SetCyclic(false);
				e->Start(anm_name);
				current_actor->Cameras().AddCamEffector(e);
			}
		}
	}
	return ret;
}

player_hud::player_hud()
{
	m_model = nullptr;
	m_model_2 = nullptr;

	m_attached_items[0] = nullptr;
	m_attached_items[1] = nullptr;
	m_attached_items[SCOPE_ATTACH_IDX] = nullptr;
	m_attach_offset.identity();
	m_attach_offset_2.identity();
	m_transform.identity();
	m_transform_2.identity();
	m_adjust_mode = false;
	script_anim_part = u8(-1);
	script_anim_offset_factor = 0.f;
	m_item_pos.identity();
	script_override_arms = false;
	for (int side = 0; side != 2; ++side)
	{
		m_parkour_ik[side] = parkour_ik_arm();
		m_parkour_ik_palm_offset[side].set(0.f, 0.f, 0.f);
	}

	//Bone Callback Params
	m_bone_callback_params.insert(mk_pair(r_finger0, xr_new<BoneCallbackParams>()));
	m_bone_callback_params.insert(mk_pair(r_finger01, xr_new<BoneCallbackParams>()));
	m_bone_callback_params.insert(mk_pair(r_finger02, xr_new<BoneCallbackParams>()));
	//m_bone_callback_params.insert(mk_pair(bip01_r_finger1, xr_new<BoneCallbackParams>()));
	//m_bone_callback_params.insert(mk_pair(bip01_r_finger11, xr_new<BoneCallbackParams>()));
	//m_bone_callback_params.insert(mk_pair(bip01_r_finger12, xr_new<BoneCallbackParams>()));

	//Movement Layers
	m_movement_layers.reserve(move_anms_end);

	for (int i = 0; i < move_anms_end; i++)
	{
		movement_layer* anm = xr_new<movement_layer>();

		char temp[20];
		string512 tmp;
		strconcat(sizeof(temp), temp, "movement_layer_", std::to_string(i).c_str());

		if (pSettings->line_exist("hud_movement_layers", temp))
		{
			LPCSTR layer_def = pSettings->r_string("hud_movement_layers", temp);
			R_ASSERT2(_GetItemCount(layer_def) > 0, make_string("Wrong definition for [hud_movement_layers] %s", temp));

			_GetItem(layer_def, 0, tmp);
			anm->Load(tmp);
			_GetItem(layer_def, 1, tmp);
			anm->anm->Speed() = (atof(tmp) ? atof(tmp) : 1.f);
			_GetItem(layer_def, 2, tmp);
			anm->m_power = (atof(tmp) ? atof(tmp) : 1.f);
		}

		m_movement_layers.push_back(anm);
	}
}

player_hud::~player_hud()
{
	IRenderVisual* v = m_model->dcast_RenderVisual();
	::Render->model_Delete(v);
	m_model = nullptr;

	v = m_model_2->dcast_RenderVisual();
	::Render->model_Delete(v);
	m_model_2 = nullptr;

	delete_data(m_hand_motions);
	delete_data(m_script_layers);
	delete_data(m_movement_layers);
	delete_data(m_bone_callback_params);
}

void player_hud::FingerCallback(CBoneInstance* B)
{
	BoneCallbackParams* params = static_cast<BoneCallbackParams*>(B->callback_param());

	Fvector& target = params->m_target;
	Fvector& current = params->m_current;

	if (!target.similar(current))
	{
		Fvector diff[2];
		diff[0] = target;
		diff[0].sub(current);
		diff[0].mul(Device.fTimeDelta / .1f);
		current.add(diff[0]);
	}
	else
		current.set(target);

	Fmatrix rotation;
	rotation.identity();
	rotation.rotateX(current.x);

	Fmatrix rotation_y;
	rotation_y.identity();
	rotation_y.rotateY(current.y);
	rotation.mulA_43(rotation_y);

	rotation_y.identity();
	rotation_y.rotateZ(current.z);
	rotation.mulA_43(rotation_y);

	B->mTransform.mulB_43(rotation);
}

void player_hud::ParkourIKCallback(CBoneInstance* B)
{
	parkour_ik_bone* cb = static_cast<parkour_ik_bone*>(B->callback_param());
	if (!cb || !cb->hud || !cb->hud->m_parkour_ik_applying)
		return;
	parkour_ik_arm& arm = cb->hud->m_parkour_ik[cb->left ? 1 : 0];
	if (!arm.valid || arm.weight <= 0.001f)
		return;

	IKinematics* K = cb->left ? cb->hud->m_model_2->dcast_PKinematics() : cb->hud->m_model->dcast_PKinematics();
	if (!K)
		return;
	if (cb->wrist)
	{
        if (!arm.orient) return;
        Fquaternion current, target, blended;
        current.set(B->mTransform);target.set(arm.hand_rotation);
        blended.slerp(current,target,arm.weight);
        Fmatrix rotation;rotation.rotation(blended);
        B->mTransform.i=rotation.i;B->mTransform.j=rotation.j;B->mTransform.k=rotation.k;
		return;
	}
	// Bone callbacks receive model-space matrices, including the parent.
	// Rotate the actual child offset, not an assumed local +Z bone axis.
	Fvector current, desired;
	B->mTransform.transform_dir(current, cb->upper ? arm.upper_axis : arm.lower_axis);
	desired.sub(cb->upper ? arm.elbow : arm.target, B->mTransform.c);
	Fmatrix delta;
	if (parkour_ik_from_to(delta, current, desired))
	{
		delta.transform_dir(B->mTransform.i);
		delta.transform_dir(B->mTransform.j);
		delta.transform_dir(B->mTransform.k);
	}
}

void player_hud::update_parkour_ik()
{
    CActor* actor = Actor();
    if (!actor || !ParkourMotionPlaying() || !actor->Parkour().Active() || !actor->Parkour().HandsEnabled())
    {
        for (auto& arm : m_parkour_ik) { arm.valid = false; arm.weight = 0.f; arm.palm_calibrated = false; }
        return;
    }
    auto& parkour = actor->Parkour();
    IKinematics* models[2] = {m_model->dcast_PKinematics(), m_model_2->dcast_PKinematics()};
    Fmatrix* transforms[2] = {&m_transform, &m_transform_2};
    Fvector contactTargets[2];
    float catchSlack=flt_max;
    for (int side = 0; side < 2; ++side)
    {
        auto& arm = m_parkour_ik[side];
        Fvector world, normal;
        auto* k = models[side];
        if (!arm.callbacks_owned || arm.wrist_bone == BI_NONE ||
            k->LL_GetBoneInstance(arm.upper).callback() != ParkourIKCallback ||
            k->LL_GetBoneInstance(arm.lower).callback() != ParkourIKCallback ||
            !parkour.HandTarget(side == 1, world, normal))
        { arm.valid = false; arm.weight = 0.f; continue; }

        // HUD and world render at different FOVs. Preserve world contact's
        // screen position at its depth before converting to arm model space.
        Fvector view;
        Device.mView.transform_tiny(view, world);
        // A reachable lip can be beside/behind the eye when hugging a wall or
        // rising above it. Visibility is not a physical hand-contact condition.
        if (!_valid(view) || _abs(Device.mProjectHud._11) < EPS || _abs(Device.mProjectHud._22) < EPS)
        { arm.valid = false; arm.weight = 0.f; continue; }
        view.x *= Device.mProject._11 / Device.mProjectHud._11;
        view.y *= Device.mProject._22 / Device.mProjectHud._22;
        Fmatrix inverseView; inverseView.invert(Device.mView);
        inverseView.transform_tiny(world, view);
        Fmatrix inverseHud; inverseHud.invert(*transforms[side]);
        Fvector target;
        inverseHud.transform_tiny(target, world);
        Fvector facing=parkour.HandDirection(side==1);
        Device.mView.transform_dir(normal);Device.mView.transform_dir(facing);
        const float sx=Device.mProject._11/Device.mProjectHud._11;
        const float sy=Device.mProject._22/Device.mProjectHud._22;
        if (_abs(sx)<EPS || _abs(sy)<EPS) {arm.valid=false;continue;}
        normal.x/=sx;normal.y/=sy;facing.x*=sx;facing.y*=sy;
        inverseView.transform_dir(normal);inverseView.transform_dir(facing);
        inverseHud.transform_dir(normal);inverseHud.transform_dir(facing);
        normal.normalize_safe();facing.mad(normal,-facing.dotproduct(normal));facing.normalize_safe();
        Fvector across;across.crossproduct(normal,facing).normalize_safe();
        const Fvector& offset=m_parkour_ik_palm_offset[side];
        target.mad(across,offset.x);target.mad(normal,offset.y);target.mad(facing,offset.z);

        const Fmatrix& upper = k->LL_GetTransform(arm.upper);
        const Fmatrix& lower = k->LL_GetTransform(arm.lower);
        const Fvector shoulder = upper.c, elbow = lower.c, wrist = k->LL_GetTransform(arm.wrist_bone).c;
        Fvector upperOffset, lowerOffset;
        upperOffset.sub(elbow, shoulder); lowerOffset.sub(wrist, elbow);
        const float a = upperOffset.magnitude(), b = lowerOffset.magnitude();
        if (a < .01f || b < .01f) { arm.valid = false; continue; }
        // HUD shoulders are camera-mounted, not the actor's world shoulders.
        // Allow a small shoulder adjustment instead of clamping both wrists
        // toward the center of the screen when the projected target is farther
        // than the authored arm length. Keep bone lengths and target separation.
        Fvector shoulderToTarget; shoulderToTarget.sub(target, shoulder);
        const float targetDistance = shoulderToTarget.magnitude();
        if (targetDistance > a + b - .02f)
        {
            Fvector localShift = shoulderToTarget;
            const float maxShift = parkour.ShoulderAdjustment();
            localShift.mul(_min(maxShift, targetDistance - (a + b - .02f)) / targetDistance);
            Fvector worldShift; transforms[side]->transform_dir(worldShift, localShift);
            transforms[side]->c.add(worldShift);
            target.sub(localShift);
        }
        contactTargets[side] = target;
        Fvector reach;reach.sub(target,shoulder);
        // Reserve actual chain slack for a falling catch. A straight arm cannot dip.
        catchSlack=_min(catchSlack,_max(0.f,a+b-reach.magnitude()-.02f) /
            _max(1.f,_max(_abs(sx),_abs(sy))));
        Fmatrix inverseBone;
        arm.orient=false;
        if (arm.finger_bone!=BI_NONE && arm.index_bone!=BI_NONE && arm.little_bone!=BI_NONE &&
            k->LL_GetBoneInstance(arm.wrist_bone).callback()==ParkourIKCallback)
        {
            if (!arm.palm_calibrated)
            {
                Fvector fingers, width, palm;
                fingers.sub(k->LL_GetTransform(arm.finger_bone).c,wrist);
                width.sub(k->LL_GetTransform(arm.little_bone).c,k->LL_GetTransform(arm.index_bone).c);
                palm.crossproduct(fingers,width);
                if (side==1) palm.invert();
                if (fingers.square_magnitude()>EPS_S && palm.square_magnitude()>EPS_S*EPS_S)
                {
                    inverseBone.invert(k->LL_GetTransform(arm.wrist_bone));
                    inverseBone.transform_dir(arm.finger_axis,fingers);arm.finger_axis.normalize();
                    inverseBone.transform_dir(arm.palm_axis,palm);arm.palm_axis.normalize();
                    arm.palm_calibrated=true;
                }
            }
            if (arm.palm_calibrated && across.square_magnitude()>EPS_S)
            {
                Fvector localAcross;localAcross.crossproduct(arm.palm_axis,arm.finger_axis).normalize();
                arm.hand_rotation.identity();
                auto axis = [&](Fvector& out,float x,float y,float z)
                {out.set(across);out.mul(x);out.mad(normal,y);out.mad(facing,z);};
                axis(arm.hand_rotation.i,localAcross.x,arm.palm_axis.x,arm.finger_axis.x);
                axis(arm.hand_rotation.j,localAcross.y,arm.palm_axis.y,arm.finger_axis.y);
                axis(arm.hand_rotation.k,localAcross.z,arm.palm_axis.z,arm.finger_axis.z);
                arm.orient=true;
            }
        }
        inverseBone.invert(upper); inverseBone.transform_dir(arm.upper_axis, upperOffset);
        inverseBone.invert(lower); inverseBone.transform_dir(arm.lower_axis, lowerOffset);
        Fvector correction; correction.sub(target, wrist);
        // Bone lengths already constrain reach. Clamping this correction after
        // contact pulls both wrists back toward the clip's centered hand pose.
        const float desiredWeight = parkour.HandWeight();
        if (parkour.Reaching()) arm.weight = desiredWeight;
        else arm.weight += (desiredWeight - arm.weight) * (1.f - expf(-Device.fTimeDelta / .06f));
        target.mad(wrist, correction, arm.weight);
        Fvector direction; direction.sub(target, shoulder);
        float distance = direction.magnitude();
        if (distance < EPS) { arm.valid = false; continue; }
        direction.div(distance);
        distance = clampr(distance, _abs(a - b) + .001f, a + b - .001f);
        target.mad(shoulder, direction, distance);
        // Keep the authored elbow side; normal/side axes only resolve a straight
        // arm's degenerate plane. No right-arm sign inversion or elbow flipping.
        Fvector pole = upperOffset;
        pole.mad(direction, -pole.dotproduct(direction));
        if (pole.square_magnitude() < .00001f)
        {
            pole = normal; pole.mad(direction, -pole.dotproduct(direction));
        }
        if (pole.square_magnitude() < .00001f)
        {
            pole.set(1.f, 0.f, 0.f);
            if (_abs(direction.x) > .9f) pole.set(0.f, 0.f, 1.f);
            pole.mad(direction, -pole.dotproduct(direction));
        }
        pole.normalize();
        const float along = (a*a - b*b + distance*distance) / (2.f*distance);
        const float height = sqrtf(_max(0.f, a*a - along*along));
        arm.elbow.mad(shoulder, direction, along);
        arm.elbow.mad(pole, height);
        arm.target = target;
        arm.valid = true;
    }
    // Temporary reach/FOV/bone failures must not permanently stop both hands.
    // The controller validates real ledge contacts; IK recovers on later frames.
    m_parkour_ik_applying = true;
    for (auto* k : models) { k->CalculateBones_Invalidate(); k->CalculateBones(TRUE); }
    m_parkour_ik_applying = false;
    if(parkour.Reaching())
    {
        bool touching=true;
        for(int side=0;side<2;++side)
        {
            const auto& arm=m_parkour_ik[side];
            if(!arm.valid || arm.weight<.90f ||
                models[side]->LL_GetTransform(arm.wrist_bone).c.distance_to(contactTargets[side])>.10f)
                touching=false;
        }
        parkour.ConfirmHandContact(touching,catchSlack);
    }
}

void player_hud::load(const shared_str& player_hud_sect, bool force)
{
	if (!force && player_hud_sect == m_sect_name) return;

	m_sect_name = player_hud_sect;

	if (script_override_arms) return;

	if (m_model)
	{
		IRenderVisual* v = m_model->dcast_RenderVisual();
		::Render->model_Delete(v);
	}
	if (m_model_2)
	{
		IRenderVisual* v = m_model_2->dcast_RenderVisual();
		::Render->model_Delete(v);
	}

	const shared_str& model_name = pSettings->r_string(player_hud_sect, "visual");
	::Render->hud_loading = true;
	m_model = smart_cast<IKinematicsAnimated*>(::Render->model_Create(model_name.c_str()));
	m_model_2 = smart_cast<IKinematicsAnimated*>(::Render->model_Create(pSettings->line_exist(player_hud_sect, "visual_2") ? pSettings->r_string(player_hud_sect, "visual_2") : model_name.c_str()));
	bool b_reload = (m_attached_items[0] != nullptr || m_attached_items[1] != nullptr);

	::Render->hud_loading = false;
	u16 l_arm = m_model->dcast_PKinematics()->LL_BoneID("l_clavicle");
	u16 r_arm = m_model_2->dcast_PKinematics()->LL_BoneID("r_clavicle");

	u16 bone_r_finger0 = m_model->dcast_PKinematics()->LL_BoneID("r_finger0");
	u16 bone_r_finger01 = m_model->dcast_PKinematics()->LL_BoneID("r_finger01");
	u16 bone_r_finger02 = m_model->dcast_PKinematics()->LL_BoneID("r_finger02");

	//u16 bone_r_triggerfinger0 = m_model->dcast_PKinematics()->LL_BoneID("bip01_r_finger1");
	//u16 bone_r_triggerfinger01 = m_model->dcast_PKinematics()->LL_BoneID("bip01_r_finger11");
	//u16 bone_r_triggerfinger02 = m_model->dcast_PKinematics()->LL_BoneID("bip01_r_finger12");

	m_model->dcast_PKinematics()->LL_GetBoneInstance(bone_r_finger0).set_callback(bctCustom, FingerCallback, m_bone_callback_params[r_finger0]);
	m_model->dcast_PKinematics()->LL_GetBoneInstance(bone_r_finger01).set_callback(bctCustom, FingerCallback, m_bone_callback_params[r_finger01]);
	m_model->dcast_PKinematics()->LL_GetBoneInstance(bone_r_finger02).set_callback(bctCustom, FingerCallback, m_bone_callback_params[r_finger02]);

	const LPCSTR names[2][3] = {
		{ "parkour_ik_right_upper", "parkour_ik_right_lower", "parkour_ik_right_wrist" },
		{ "parkour_ik_left_upper", "parkour_ik_left_lower", "parkour_ik_left_wrist" }
	};
	const bool parkourConfigured=pSettings->section_exist("item_anm_ledge_grabbing");
	const LPCSTR ik_section = parkourConfigured ? "item_anm_ledge_grabbing" : player_hud_sect.c_str();
	m_parkour_ik_palm_offset[0] = READ_IF_EXISTS(pSettings, r_fvector3, ik_section, "parkour_ik_right_palm_offset", Fvector().set(0.f, 0.f, 0.f));
	m_parkour_ik_palm_offset[1] = READ_IF_EXISTS(pSettings, r_fvector3, ik_section, "parkour_ik_left_palm_offset", Fvector().set(0.f, 0.f, 0.f));
	IKinematics* ik_models[2] = { m_model->dcast_PKinematics(), m_model_2->dcast_PKinematics() };
	for (int side = 0; side != 2; ++side)
	{
		m_parkour_ik[side] = parkour_ik_arm();
		if (!parkourConfigured) continue;
		for (int bone = 0; bone != 3; ++bone)
		{
			LPCSTR fallback[2][3] = {
				{ "r_upperarm", "r_forearm", "r_hand" },
				{ "l_upperarm", "l_forearm", "l_hand" }
			};
			shared_str configured = READ_IF_EXISTS(pSettings, r_string, ik_section, names[side][bone], fallback[side][bone]);
			u16 id = ik_models[side]->LL_BoneID(configured);
			if (id == BI_NONE)
				id = ik_models[side]->LL_BoneID(fallback[side][bone]);
			if (bone == 0) m_parkour_ik[side].upper = id;
			if (bone == 1) m_parkour_ik[side].lower = id;
			if (bone == 2) m_parkour_ik[side].wrist_bone = id;
		}
		m_parkour_ik[side].finger_bone=ik_models[side]->LL_BoneID(side==0?"r_finger2":"l_finger2");
		if(m_parkour_ik[side].finger_bone==BI_NONE)
			m_parkour_ik[side].finger_bone=ik_models[side]->LL_BoneID(side==0?"r_finger1":"l_finger1");
        m_parkour_ik[side].index_bone=ik_models[side]->LL_BoneID(side==0?"r_finger1":"l_finger1");
        m_parkour_ik[side].little_bone=ik_models[side]->LL_BoneID(side==0?"r_finger4":"l_finger4");
		m_parkour_ik_bones[side * 3] = { this, side == 1, true, false };
		m_parkour_ik_bones[side * 3 + 1] = { this, side == 1, false, false };
		m_parkour_ik_bones[side * 3 + 2] = { this, side == 1, false, true };
		bool can_own_callbacks = m_parkour_ik[side].upper != BI_NONE && m_parkour_ik[side].lower != BI_NONE &&
			m_parkour_ik[side].wrist_bone != BI_NONE &&
			!ik_models[side]->LL_GetBoneInstance(m_parkour_ik[side].upper).callback() &&
			!ik_models[side]->LL_GetBoneInstance(m_parkour_ik[side].lower).callback();
		if (can_own_callbacks)
		{
			ik_models[side]->LL_GetBoneInstance(m_parkour_ik[side].upper).set_callback(bctCustom, ParkourIKCallback, &m_parkour_ik_bones[side * 3]);
			ik_models[side]->LL_GetBoneInstance(m_parkour_ik[side].lower).set_callback(bctCustom, ParkourIKCallback, &m_parkour_ik_bones[side * 3 + 1]);
            auto& wrist=ik_models[side]->LL_GetBoneInstance(m_parkour_ik[side].wrist_bone);
            if (!wrist.callback())
                wrist.set_callback(bctCustom,ParkourIKCallback,&m_parkour_ik_bones[side*3+2]);
        }
        m_parkour_ik[side].callbacks_owned = can_own_callbacks;
	}

	//m_model->dcast_PKinematics()->LL_GetBoneInstance(bone_r_triggerfinger0).set_callback(bctCustom, FingerCallback, m_bone_callback_params[bip01_r_finger1]);
	//m_model->dcast_PKinematics()->LL_GetBoneInstance(bone_r_triggerfinger01).set_callback(bctCustom, FingerCallback, m_bone_callback_params[bip01_r_finger11]);
	//m_model->dcast_PKinematics()->LL_GetBoneInstance(bone_r_triggerfinger02).set_callback(bctCustom, FingerCallback, m_bone_callback_params[bip01_r_finger12]);

	// hides the unused arm meshes
	m_model->dcast_PKinematics()->LL_SetBoneVisible(l_arm, FALSE, TRUE);
	m_model_2->dcast_PKinematics()->LL_SetBoneVisible(r_arm, FALSE, TRUE);

	CInifile::Sect& _sect = pSettings->r_section(player_hud_sect);
	CInifile::SectCIt _b = _sect.Data.begin();
	CInifile::SectCIt _e = _sect.Data.end();
	for (; _b != _e; ++_b)
	{
		if (strstr(_b->first.c_str(), "ancor_") == _b->first.c_str())
		{
			const shared_str& _bone = _b->second;
			m_ancors.push_back(m_model->dcast_PKinematics()->LL_BoneID(_bone));
		}
	}

	if (!b_reload)
	{
		m_model->PlayCycle("hand_idle_doun");
		m_model_2->PlayCycle("hand_idle_doun");
	}
	else
	{
		if (m_attached_items[1])
			m_attached_items[1]->m_parent_hud_item->on_outfit_changed();

		if (m_attached_items[0])
			m_attached_items[0]->m_parent_hud_item->on_outfit_changed();
	}

	m_model->dcast_PKinematics()->CalculateBones_Invalidate();
	m_model->dcast_PKinematics()->CalculateBones(TRUE);
	m_model_2->dcast_PKinematics()->CalculateBones_Invalidate();
	m_model_2->dcast_PKinematics()->CalculateBones(TRUE);

	//--DSR-- HeatVision_start
	m_model->dcast_RenderVisual()->MarkAsHot(true);
	m_model_2->dcast_RenderVisual()->MarkAsHot(true);
	//--DSR-- HeatVision_end
}

void player_hud::load_script(LPCSTR section)
{
	script_override_arms = false;
	load(section, true);
	script_override_arms = true;
}

bool player_hud::render_item_ui_query()
{
	bool res = false;
	if (m_attached_items[0])
		res |= m_attached_items[0]->render_item_ui_query();

	if (m_attached_items[1])
		res |= m_attached_items[1]->render_item_ui_query();

	if (m_attached_items[SCOPE_ATTACH_IDX])
		res |= m_attached_items[SCOPE_ATTACH_IDX]->render_item_ui_query();

	return res;
}

void player_hud::render_item_ui()
{
	IUIRender::ePointType bk = UI().m_currentPointType;
	UI().m_currentPointType = IUIRender::pttLIT;
	UIRender->CacheSetCullMode(IUIRender::cmNONE);

	if (m_attached_items[0])
		m_attached_items[0]->render_item_ui();

	if (m_attached_items[1])
		m_attached_items[1]->render_item_ui();

	if (m_attached_items[SCOPE_ATTACH_IDX])
		m_attached_items[SCOPE_ATTACH_IDX]->render_item_ui();

	UIRender->CacheSetCullMode(IUIRender::cmCCW);
	UI().m_currentPointType = bk;

	if (g_actor->GetAttachments()->size())
	{
		for (auto& pair : *g_actor->GetAttachments())
			if (pair.second->GetType() == eSA_HUD)
				pair.second->RenderUI();
	}
}

void player_hud::render_hud()
{
	bool b_r0 = ((m_attached_items[0] && m_attached_items[0]->need_renderable()) || script_anim_part == 0 || script_anim_part == 2);
	bool b_r1 = ((m_attached_items[1] && m_attached_items[1]->need_renderable()) || script_anim_part == 1 || script_anim_part == 2);

	if (!b_r0 && !b_r1) return;

	::Render->set_Transform(&m_transform);
	::Render->add_Visual(m_model->dcast_RenderVisual());
	::Render->set_Transform(&m_transform_2);
	::Render->add_Visual(m_model_2->dcast_RenderVisual());

	if (m_attached_items[0])
		m_attached_items[0]->render();

	if (m_attached_items[1])
		m_attached_items[1]->render();

	if (m_attached_items[SCOPE_ATTACH_IDX])
		m_attached_items[SCOPE_ATTACH_IDX]->render();

	if (script_anim_item_model)
	{
		::Render->set_Transform(&m_item_pos);
		::Render->add_Visual(script_anim_item_model->dcast_RenderVisual());
	}

	if (g_actor->GetAttachments()->size())
	{
		for (auto& pair : *g_actor->GetAttachments())
		{
			script_attachment* att = pair.second;

			if (att->GetType() == eSA_HUD)
			{
				// Left arm
				if (att->GetParentBone() < 21)
					att->Render(m_model_2->dcast_PKinematics(), &m_transform_2);

				// Right arm
				else
					att->Render(m_model->dcast_PKinematics(), &m_transform);
			}
		}
	}
}

#include "../xrEngine/motion.h"

u32 player_hud::motion_length_script(LPCSTR section, LPCSTR anm_name, float speed)
{
	if (!pSettings->section_exist(section))
	{
		Msg("!script motion section [%s] does not exist", section);
		return 0;
	}

	player_hud_motion_container* pm = get_hand_motions(section);
	if (!pm)
		return 0;

	player_hud_motion* phm = pm->find_motion(anm_name);
	if (!phm)
	{
		Msg("!script motion [%s] not found in section [%s]", anm_name, section);
		return 0;
	}

	const CMotionDef* temp;
	return motion_length(phm->m_animations[0].mid, temp, speed);
}

u32 player_hud::motion_length(const shared_str& anim_name, const shared_str& hud_name, const CMotionDef*& md)
{
	player_hud_motion_container* pc = get_hand_motions(*hud_name);
	if (!pc) return 0;
	player_hud_motion* pm = pc->find_motion(anim_name);
	if (!pm || !pm->m_animations.size())
		return 100; // ms TEMPORARY
	R_ASSERT2(pm,
		make_string("hudItem model [%s] has no motion with alias [%s]", hud_name.c_str(), anim_name.c_str()).
		c_str()
	);
	return motion_length(pm->m_animations[0].mid, md, 1.f);
}

u32 player_hud::motion_length(const MotionID& M, const CMotionDef*& md, float speed)
{
	md = m_model->LL_GetMotionDef(M);
	VERIFY(md);
	if (md->flags & esmStopAtEnd)
	{
		CMotion* motion = m_model->LL_GetRootMotion(M);
		return iFloor(0.5f + 1000.f * motion->GetLength() / (md->Dequantize(md->speed) * speed));
	}
	return 0;
}

const Fvector player_hud::attach_rot(u8 part) const
{
	if (m_attached_items[part])
		return m_attached_items[part]->hands_attach_rot();
	else if (m_attached_items[!part])
		return m_attached_items[!part]->hands_attach_rot();

	return { 0.f, 0.f, 0.f };
}

const Fvector player_hud::attach_pos(u8 part) const
{
	if (m_attached_items[part])
		return m_attached_items[part]->hands_attach_pos();
	else if (m_attached_items[!part])
		return m_attached_items[!part]->hands_attach_pos();

	return { 0.f, 0.f, 0.f };
}

#include "Inventory.h"
extern float g_freelook_z_offset;
extern float psHUD_FOV;

void player_hud::update(const Fmatrix& cam_trans)
{
	Fmatrix trans = cam_trans;
	Fmatrix trans_b = cam_trans;
	CWeapon* wep = smart_cast<CWeapon*>(Actor()->inventory().ActiveItem());

	float& control_factor = Actor()->freelook_cam_control;
	u8 cam_freelook = Actor()->cam_freelook;
	float sub_z = 0.f;

	if (control_factor > 0)
	{
		Fvector new_k;
		float old_pitch = trans.k.getP();
		float new_pitch = old_pitch > 0.f ? old_pitch * (1.f - psHUD_FOV) : old_pitch * (1.f - (psHUD_FOV / 2.f));
		float final_pitch = angle_lerp(old_pitch, new_pitch, control_factor);

		float body_yaw = -angle_normalize_signed(Actor()->old_torso_yaw);
		float cam_yaw = -angle_normalize_signed(Actor()->cam_FirstEye()->yaw);
		float diff_yaw = angle_difference_signed(body_yaw, cam_yaw);

		if (final_pitch < 0.f)
			sub_z += final_pitch * .35f;

		sub_z -= abs(diff_yaw) * .1f;
		clamp(sub_z, -.2f, 0.f);

		float new_yaw = trans.k.getH() + diff_yaw * psHUD_FOV;
		float final_yaw = angle_lerp(body_yaw, new_yaw, control_factor);

		new_k.setHP(final_yaw, final_pitch);
		trans.k.lerp(trans.k, new_k, control_factor);
		Fvector::generate_orthonormal_basis_normalized(trans.k, trans.j, trans.i);
	}

	if (cam_freelook == eflEnabling || cam_freelook == eflEnabled)
		control_factor += Device.fTimeDelta / .3f;
	else
		control_factor -= Device.fTimeDelta / .3f;

	clamp(control_factor, 0.f, 1.f);

	Fvector m1pos = attach_pos(0);
	Fvector m2pos = attach_pos(1);

	Fvector m1rot = attach_rot(0);
	Fvector m2rot = attach_rot(1);

	// freelook pitch compensation (avoid seeing missing arm parts or weapon parts that are not meant to be visible)
	if ((m_attached_items[0] || m_attached_items[1]) && control_factor > 0 && sub_z < 0)
	{
		float z1, z2, z_factor;
		z_factor = (sub_z * control_factor);
		z1 = (g_freelook_z_offset ? g_freelook_z_offset : m_attached_items[m_attached_items[0] ? 0 : 1]->m_measures.m_fFreelookZOffset);
		z2 = (g_freelook_z_offset ? g_freelook_z_offset : m_attached_items[m_attached_items[1] ? 1 : 0]->m_measures.m_fFreelookZOffset);

		if (Actor()->is_safemode())
		{
			z1 = fmaxf(z1, .5f);
			if (!m_attached_items[1])
				z2 = fmaxf(z2, .5f);
		}

		z1 *= z_factor;
		z2 *= z_factor;

		m1pos.z += z1;
		m2pos.z += z2;
		m1pos.y += z1 / 2.f;
		m2pos.y += z2 / 2.f;
	}

	Fmatrix trans_2 = trans;

	if (m_attached_items[0])
		m_attached_items[0]->m_parent_hud_item->UpdateHudAdditional(trans);

	if (m_attached_items[1])
	{
		m_attached_items[1]->m_parent_hud_item->UpdateHudAdditional(trans_2);
		if (wep && wep->IsZoomed())
			trans_2.mulB_43(wep->m_shoot_shake_mat);
	}

	if (m_attached_items[0] && !m_attached_items[1])
		trans_2 = trans;
	else if (m_attached_items[1] && !m_attached_items[0])
		trans = trans_2;

	// override hand offset for single hand animation
	if (script_anim_part == 2 || (script_anim_part && !m_attached_items[0] && !m_attached_items[1]))
	{
		m1pos = script_anim_offset[0];
		m2pos = script_anim_offset[0];
		m1rot = script_anim_offset[1];
		m2rot = script_anim_offset[1];
		trans = trans_b;
		trans_2 = trans_b;
	}
	else if (script_anim_offset_factor != 0.f)
	{
		Fvector& hand_pos = script_anim_part == 0 ? m1pos : m2pos;
		Fvector& hand_rot = script_anim_part == 0 ? m1rot : m2rot;

		hand_pos.lerp(script_anim_part == 0 ? m1pos : m2pos, script_anim_offset[0], script_anim_offset_factor);
		hand_rot.lerp(script_anim_part == 0 ? m1rot : m2rot, script_anim_offset[1], script_anim_offset_factor);

		if (script_anim_part == 0)
		{
			trans_b.inertion(trans, script_anim_offset_factor);
			trans = trans_b;
		}
		else
		{
			trans_b.inertion(trans_2, script_anim_offset_factor);
			trans_2 = trans_b;
		}
	}

    // Apply after the two-hand script override, which resets BOTH transforms
    // to trans_b. Locking only trans earlier had no effect during this clip.
    if (ParkourMotionPlaying() && Actor() && Actor()->Parkour().Active())
    {
        trans.k = Actor()->Parkour().Result().direction;
        Fvector::generate_orthonormal_basis_normalized(trans.k, trans.j, trans.i);
        trans_2.i = trans.i; trans_2.j = trans.j; trans_2.k = trans.k;
    }
	m1rot.mul(PI / 180.f);
	m_attach_offset.setHPB(m1rot.x, m1rot.y, m1rot.z);
	m_attach_offset.translate_over(m1pos);

	m2rot.mul(PI / 180.f);
	m_attach_offset_2.setHPB(m2rot.x, m2rot.y, m2rot.z);
	m_attach_offset_2.translate_over(m2pos);

	m_transform.mul(trans, m_attach_offset);
	m_transform_2.mul(trans_2, m_attach_offset_2);

	m_parkour_ik_applying = false;
	m_model->UpdateTracks();
	m_model->dcast_PKinematics()->CalculateBones_Invalidate();
	m_model->dcast_PKinematics()->CalculateBones(TRUE);

	m_model_2->UpdateTracks();
	m_model_2->dcast_PKinematics()->CalculateBones_Invalidate();
	m_model_2->dcast_PKinematics()->CalculateBones(TRUE);

	for (script_layer* anm : m_script_layers)
	{
		if (!anm || !anm->anm || (!anm->active && anm->blend_amount == 0.f))
			continue;

		if (anm->active)
			anm->blend_amount += Device.fTimeDelta / .4f;
		else
			anm->blend_amount -= Device.fTimeDelta / .4f;

		clamp(anm->blend_amount, 0.f, 1.f);

		if (anm->blend_amount > 0.f)
		{
			if (anm->anm->bLoop || anm->anm->anim_param().t_current < anm->anm->anim_param().max_t)
				anm->anm->Update(Device.fTimeDelta);
			else
				anm->Stop(false);
		}
		else
		{
			anm->Stop(true);
			continue;
		}

		Fmatrix blend = anm->XFORM();

		if (anm->m_part == 0 || anm->m_part == 2)
        {
			IKinematics* K = m_model->dcast_PKinematics();
			u16 bone_id = K ? K->LL_BoneID(anm->m_pivot_bone) : u16(-1);
			if (bone_id != u16(-1))
			{
				Fmatrix B = K->LL_GetTransform(bone_id);
				Fmatrix invB; invB.invert(B);
				Fmatrix tmp; tmp.mul_43(B, blend);
				tmp.mulB_43(invB);
				m_transform.mulB_43(tmp);
			}
			else
				m_transform.mulB_43(blend);
        }

		if (anm->m_part == 1 || anm->m_part == 2)
        {
			IKinematics* K = m_model_2->dcast_PKinematics();
			u16 bone_id = K ? K->LL_BoneID(anm->m_pivot_bone) : u16(-1);
			if (bone_id != u16(-1))
			{
				Fmatrix B = K->LL_GetTransform(bone_id);
				Fmatrix invB; invB.invert(B);
				Fmatrix tmp; tmp.mul_43(B, blend);
				tmp.mulB_43(invB);
				m_transform_2.mulB_43(tmp);
			}
			else
				m_transform_2.mulB_43(blend);
        }
	}

	bool need_blend[2];
	need_blend[0] = ((script_anim_part == 0 || script_anim_part == 2) || (m_attached_items[0] && m_attached_items[0]->m_parent_hud_item->NeedBlendAnm()));
	need_blend[1] = ((script_anim_part == 1 || script_anim_part == 2) || (m_attached_items[1] && m_attached_items[1]->m_parent_hud_item->NeedBlendAnm()));

	for (movement_layer* anm : m_movement_layers)
	{
		if (!anm || !anm->anm || (!anm->active && anm->blend_amount[0] == 0.f && anm->blend_amount[1] == 0.f))
			continue;

		if (anm->active && (need_blend[0] || need_blend[1]))
		{
			if (need_blend[0])
			{
				anm->blend_amount[0] += Device.fTimeDelta / .4f;

				if (!m_attached_items[1])
					anm->blend_amount[1] += Device.fTimeDelta / .4f;
				else if (!need_blend[1])
					anm->blend_amount[1] -= Device.fTimeDelta / .4f;
			}

			if (need_blend[1])
			{
				anm->blend_amount[1] += Device.fTimeDelta / .4f;

				if (!m_attached_items[0])
					anm->blend_amount[0] += Device.fTimeDelta / .4f;
				else if (!need_blend[0])
					anm->blend_amount[0] -= Device.fTimeDelta / .4f;
			}
		}
		else
		{
			anm->blend_amount[0] -= Device.fTimeDelta / .4f;
			anm->blend_amount[1] -= Device.fTimeDelta / .4f;
		}

		clamp(anm->blend_amount[0], 0.f, 1.f);
		clamp(anm->blend_amount[1], 0.f, 1.f);

		if (anm->blend_amount[0] == 0.f && anm->blend_amount[1] == 0.f)
		{
			anm->Stop(true);
			continue;
		}

		anm->anm->Update(Device.fTimeDelta);

		if (anm->blend_amount[0] == anm->blend_amount[1])
		{
			Fmatrix blend = anm->XFORM(0);
			m_transform.mulB_43(blend);
			m_transform_2.mulB_43(blend);
		}
		else
		{
			if (anm->blend_amount[0] > 0.f)
				m_transform.mulB_43(anm->XFORM(0));

			if (anm->blend_amount[1] > 0.f)
				m_transform_2.mulB_43(anm->XFORM(1));
		}
	}

	if (m_attached_items[0])
		m_attached_items[0]->update(true);

	if (m_attached_items[1])
		m_attached_items[1]->update(true);

	if (m_attached_items[SCOPE_ATTACH_IDX])
		m_attached_items[SCOPE_ATTACH_IDX]->update(true);

	if (script_anim_item_attached && script_anim_item_model)
		update_script_item();

	// single hand offset smoothing + syncing back to other hand animation on end
	if (script_anim_part != u8(-1))
	{
		script_anim_offset_factor += Device.fTimeDelta * 2.5f;

		if (m_bStopAtEndAnimIsRunning && Device.dwTimeGlobal >= script_anim_end &&
            !(ParkourMotionPlaying() && Actor() && Actor()->Parkour().Active()))
			StopScriptAnim();
	}
	else
		script_anim_offset_factor -= Device.fTimeDelta * 5.f;

	clamp(script_anim_offset_factor, 0.f, 1.f);
	update_parkour_ik();
    CActor* actor = Actor();
    if (ParkourMotionPlaying() && actor && actor->Parkour().Active())
    {
        // The shared high-climb clip has a raised-hands tail. Withdraw the
        // rendered arms while contact is still held, then stop that clip
        // before IK releases back to its unrelated authored high pose.
        const float progress = actor->Parkour().Progress();
        const float withdraw = clampr((progress - .78f) / .16f, 0.f, 1.f);
        const float drop = .9f * withdraw * withdraw * (3.f - 2.f * withdraw);
        m_transform.c.mad(Device.vCameraTop, -drop);
        m_transform_2.c.mad(Device.vCameraTop, -drop);
        if (progress >= .94f) StopScriptAnim();
    }
}

void player_hud::updateMovementLayerState()
{
	CActor* pActor = Actor();

	if (!pActor)
		return;

	for (movement_layer* anm : m_movement_layers)
	{
		anm->Stop(false);
	}

	bool need_blend = (script_anim_part != u8(-1) || (m_attached_items[0] && m_attached_items[0]->m_parent_hud_item->NeedBlendAnm()) || (m_attached_items[1] && m_attached_items[1]->m_parent_hud_item->NeedBlendAnm()));

	if (need_blend)
	{
		CWeapon* wep = nullptr;

		if (m_attached_items[0] && m_attached_items[0]->m_parent_hud_item->has_object() && m_attached_items[0]->m_parent_hud_item->object().cast_weapon())
			wep = m_attached_items[0]->m_parent_hud_item->object().cast_weapon();

		if (wep && wep->IsZoomed()) {
			m_movement_layers[eAimIdle]->Play();
		}
		else {
			m_movement_layers[eIdle]->Play();
		}

		if (pActor->AnyMove())
		{
			CEntity::SEntityState state;
			pActor->g_State(state);

			if (wep && wep->IsZoomed()) {
				state.bCrouch ? m_movement_layers[eAimCrouch]->Play() : m_movement_layers[eAimWalk]->Play();
			}
			else if (state.bCrouch) {
				m_movement_layers[eCrouch]->Play();
			}
			else if (state.bSprint) {
				m_movement_layers[eSprint]->Play();
			}
			else if (!isActorAccelerated(pActor->MovingState(), false)) {
				m_movement_layers[eWalk]->Play();
			}
			else {
				m_movement_layers[eRun]->Play();
			}
		}
	}
}

void player_hud::PlayBlendAnm(LPCSTR name, u8 part, float speed, float power, bool bLooped, bool no_restart, LPCSTR pivot_bone)
{
	for (script_layer* anm : m_script_layers)
	{
		if (!xr_strcmp(*anm->m_name, name))
		{
			if (!no_restart)
			{
				anm->anm->Stop();
				anm->blend_amount = 0.f;
				anm->blend.identity();
			}

			if (!anm->anm->IsPlaying())
				anm->anm->Play(bLooped);

			anm->anm->bLoop = bLooped;
			anm->m_part = part;
			anm->anm->Speed() = speed;
			anm->m_power = power;
			anm->active = true;
            anm->m_pivot_bone = pivot_bone;
			return;
		}
	}

	script_layer* anm = xr_new<script_layer>(name, part, speed, power, bLooped, pivot_bone);
	m_script_layers.push_back(anm);
}

void player_hud::StopBlendAnm(LPCSTR name, bool bForce)
{
	for (script_layer* anm : m_script_layers)
	{
		if (!xr_strcmp(*anm->m_name, name))
		{
			anm->Stop(bForce);
			return;
		}
	}
}

void player_hud::StopAllBlendAnms(bool bForce)
{
	for (script_layer* anm : m_script_layers)
	{
		anm->Stop(bForce);
	}
}

float player_hud::SetBlendAnmTime(LPCSTR name, float time)
{
	for (script_layer* anm : m_script_layers)
	{
		if (!xr_strcmp(*anm->m_name, name))
		{
			if (!anm->anm->IsPlaying())
				return 0;

			float speed = (anm->anm->anim_param().max_t - anm->anm->anim_param().t_current) / time;
			anm->anm->Speed() = speed;
			return speed;
		}
	}

	return 0;
}

//0 = both, 1 = left, 2 = right
void play_blend(player_hud* hud, u8 pid, const MotionID& M, BOOL bMixIn, float speed, bool script_anim = false)
{
	switch (pid)
	{
	case 0:
	{
		if (!script_anim && hud->script_anim_part == 2) return;
		play_blend(hud, 1, M, bMixIn, speed);
		play_blend(hud, 2, M, bMixIn, speed);
		break;
	}
	case 1:
		if (!script_anim && hud->script_anim_part == 1) return;
		hud->m_model_2->PlayCycle(0, M, bMixIn, 0, 0, 0, speed);
		hud->m_model_2->PlayCycle(1, M, bMixIn, 0, 0, 0, speed);
		hud->m_model_2->PlayCycle(2, M, bMixIn, 0, 0, 0, speed);
		hud->m_model_2->dcast_PKinematics()->CalculateBones_Invalidate();
		break;
	case 2:
		if (!script_anim && hud->script_anim_part == 0) return;
		hud->m_model->PlayCycle(0, M, bMixIn, 0, 0, 0, speed);
		hud->m_model->PlayCycle(2, M, bMixIn, 0, 0, 0, speed);
		hud->m_model->dcast_PKinematics()->CalculateBones_Invalidate();
		break;
	}
}

extern BOOL print_bone_warnings;
void player_hud::StopScriptAnim()
{
	// Stopping an already stopped animation is valid (e.g. first-update cleanup).
	if (script_anim_part == u8(-1)) return;
	u8 part = script_anim_part;
	script_anim_part = u8(-1);
	m_script_anim_section = nullptr;
	script_anim_item_model = nullptr;
	script_anim_lead_gun = false;

	updateMovementLayerState();

    if (part > 2)
    {
        if (print_bone_warnings)
        {
            Msg("![player_hud::StopScriptAnim()] invalid script_anim_part %d, must be < 3", part);
            ai().script_engine().print_stack();
        }
    }
        
	if (part < 2 && !m_attached_items[part])
		re_sync_anim(part + 1);
	else
		OnMovementChanged((ACTOR_DEFS::EMoveCommand)0);
}

//part: 0 = right arm; 1 = left arm
u32 player_hud::anim_play(u16 part, const MotionID& M, BOOL bMixIn, const CMotionDef*& md, float speed, u16 override_part)
{
	u16 part_id = u16(-1);
	if (attached_item(0) && attached_item(1))
		part_id = ((part == 0) ? 2 : 1);
	else
		part_id = 0;

	if (override_part != u16(-1))
		part_id = override_part;

	play_blend(this, part_id, M, bMixIn, speed);

	return motion_length(M, md, speed);
}

player_hud_motion_container* player_hud::get_hand_motions(LPCSTR section)
{
	for (hand_motions* phm : m_hand_motions)
	{
		if (phm->section == section)
			return &phm->pm;
	}

	hand_motions* res = xr_new<hand_motions>();
	res->section = section;
	res->pm.load(m_model, section);
	m_hand_motions.push_back(res);

	return &res->pm;
}

void player_hud::update_script_item()
{
	Fvector ypr = item_pos[1];
	ypr.mul(PI / 180.f);
	m_attach_offset.setHPB(ypr.x, ypr.y, ypr.z);
	m_attach_offset.translate_over(item_pos[0]);

	calc_transform(m_attach_idx, m_attach_offset, m_item_pos, script_anim_lead_gun);

	if (script_anim_item_model)
	{
		script_anim_item_model->UpdateTracks();
		script_anim_item_model->dcast_PKinematics()->CalculateBones_Invalidate();
		script_anim_item_model->dcast_PKinematics()->CalculateBones(TRUE);
	}
}

// 0 = right arm + hand, 1 = left arm + hand, 2 = both
u32 player_hud::script_anim_play(u8 hand, LPCSTR section, LPCSTR anm_name, bool bMixIn, float speed)
{
	if (!pSettings->section_exist(section))
	{
		Msg("!script motion section [%s] does not exist", section);
		m_bStopAtEndAnimIsRunning = true;
		script_anim_end = Device.dwTimeGlobal;

		return 0;
	}

	xr_string pos = "hands_position";
	xr_string rot = "hands_orientation";

	if (UI().is_widescreen())
	{
		pos.append("_16x9");
		rot.append("_16x9");
	}

	Fvector def = { 0.f, 0.f, 0.f };
	Fvector offs = READ_IF_EXISTS(pSettings, r_fvector3, section, pos.c_str(), def);
	Fvector rrot = READ_IF_EXISTS(pSettings, r_fvector3, section, rot.c_str(), def);

	if (pSettings->line_exist(section, "item_visual"))
	{
		::Render->hud_loading = true;
		script_anim_item_model = ::Render->model_Create(pSettings->r_string(section, "item_visual"))->dcast_PKinematicsAnimated();
		::Render->hud_loading = false;
		item_pos[0] = READ_IF_EXISTS(pSettings, r_fvector3, section, "item_position", def);
		item_pos[1] = READ_IF_EXISTS(pSettings, r_fvector3, section, "item_orientation", def);
		script_anim_item_attached = READ_IF_EXISTS(pSettings, r_bool, section, "item_attached", true);
		m_attach_idx = READ_IF_EXISTS(pSettings, r_u8, section, "attach_place_idx", 0);
		script_anim_lead_gun = READ_IF_EXISTS(pSettings, r_bool, section, "lh_lead_gun", false);

		if (!script_anim_item_attached)
		{
			Fmatrix attach_offs;
			Fvector ypr = item_pos[1];
			ypr.mul(PI / 180.f);
			attach_offs.setHPB(ypr.x, ypr.y, ypr.z);
			attach_offs.translate_over(item_pos[0]);
			m_item_pos = attach_offs;
		}
	}

	script_anim_offset[0] = offs;
	script_anim_offset[1] = rrot;
	script_anim_part = hand;
	m_script_anim_section = section;

	player_hud_motion_container* pm = get_hand_motions(section);
	player_hud_motion* phm = pm->find_motion(anm_name);

	if (!phm)
	{
		Msg("!script motion [%s] not found in section [%s]", anm_name, section);
		m_bStopAtEndAnimIsRunning = true;
		script_anim_end = Device.dwTimeGlobal;

		return 0;
	}

	const motion_descr& M = phm->m_animations[Random.randI(phm->m_animations.size())];

	if (script_anim_item_model)
	{
		shared_str item_anm_name;
		if (phm->m_base_name != phm->m_additional_name)
			item_anm_name = phm->m_additional_name;
		else
			item_anm_name = M.name;

		MotionID M2 = script_anim_item_model->ID_Cycle_Safe(item_anm_name);
		if (!M2.valid())
			M2 = script_anim_item_model->ID_Cycle_Safe("idle");

		R_ASSERT3(M2.valid(), "model %s has no motion [idle] ", pSettings->r_string(m_sect_name, "item_visual"));

		u16 root_id = script_anim_item_model->dcast_PKinematics()->LL_GetBoneRoot();
		CBoneInstance& root_binst = script_anim_item_model->dcast_PKinematics()->LL_GetBoneInstance(root_id);
		root_binst.set_callback_overwrite(TRUE);
		root_binst.mTransform.identity();

		u16 pc = script_anim_item_model->partitions().count();
		for (u16 pid = 0; pid < pc; ++pid)
			CBlend* B = script_anim_item_model->PlayCycle(pid, M2, bMixIn, 0, 0, 0, speed);

		script_anim_item_model->dcast_PKinematics()->CalculateBones_Invalidate();
	}

	play_blend(this, (hand == 2 ? 0 : hand == 0 ? 2 : 1), M.mid, bMixIn, speed, true);

	const CMotionDef* md;
	u32 length = motion_length(M.mid, md, speed);

	if (length > 0)
	{
		m_bStopAtEndAnimIsRunning = true;
		script_anim_end = Device.dwTimeGlobal + length;
	}
	else
		m_bStopAtEndAnimIsRunning = false;

	updateMovementLayerState();

	return length;
}

bool player_hud::allow_activation(CHudItem* item)
{
	if (script_anim_part != u8(-1))
		return false;
	else if (m_attached_items[1])
		return m_attached_items[1]->m_parent_hud_item->CheckCompatibility(item);
	else
		return true;
}

shared_str current_player_hud_sect;
void player_hud::attach_item(CHudItem* item)
{
	attachable_hud_item* pi = item->HudItemData();
	int item_idx = pi->m_attach_place_idx;

	if (m_attached_items[item_idx] != pi)
	{
		if (m_attached_items[item_idx])
			m_attached_items[item_idx]->m_parent_hud_item->on_b_hud_detach();

		m_attached_items[item_idx] = pi;

		if (item_idx == 0 && m_attached_items[1])
			m_attached_items[1]->m_parent_hud_item->CheckCompatibility(item);

		item->on_a_hud_attach();

		updateMovementLayerState();
	}
}

//sync anim of other part to selected part (1 = sync to left hand anim; 2 = sync to right hand anim)
void player_hud::re_sync_anim(u8 part)
{
	u32 bc = part == 1 ? m_model_2->LL_PartBlendsCount(part) : m_model->LL_PartBlendsCount(part);
	for (u32 bidx = 0; bidx < bc; ++bidx)
	{
		CBlend* BR = part == 1 ? m_model_2->LL_PartBlend(part, bidx) : m_model->LL_PartBlend(part, bidx);
		if (!BR)
			continue;

		MotionID M = BR->motionID;

		u16 pc = m_model->partitions().count(); //same on both armatures
		for (u16 pid = 0; pid < pc; ++pid)
		{
			if (pid == 0)
			{
				CBlend* B = m_model->PlayCycle(0, M, TRUE);
				B->timeCurrent = BR->timeCurrent;
				B->speed = BR->speed;
				B = m_model_2->PlayCycle(0, M, TRUE);
				B->timeCurrent = BR->timeCurrent;
				B->speed = BR->speed;
			}
			else if (pid != part)
			{
				CBlend* B = part == 1 ? m_model->PlayCycle(pid, M, TRUE) : m_model_2->PlayCycle(pid, M, TRUE);
				B->timeCurrent = BR->timeCurrent;
				B->speed = BR->speed;
			}
		}
	}
}

//set cycle time (0...1) part: 0 = root; 1 = left hand; 2 = right hand
void player_hud::set_part_cycle_time(u8 part, float time)
{
	if (part == 0)
	{
		set_part_cycle_time(1, time);
		set_part_cycle_time(2, time);
		return;
	}

	u32 bc = part == 1 ? m_model_2->LL_PartBlendsCount(part) : m_model->LL_PartBlendsCount(part);
	for (u32 bidx = 0; bidx < bc; ++bidx)
	{
		CBlend* BR = part == 1 ? m_model_2->LL_PartBlend(part, bidx) : m_model->LL_PartBlend(part, bidx);
		if (!BR)
			continue;

		BR->timeCurrent = BR->timeTotal * time;
	}
}

//part: 0 = root; 1 = left hand; 2 = right hand
void player_hud::set_part_cycle_speed(u8 part, float speed)
{
	if (part == 0)
	{
		set_part_cycle_speed(1, speed);
		set_part_cycle_speed(2, speed);
	}
	u32 bc = part == 1 ? m_model_2->LL_PartBlendsCount(part) : m_model->LL_PartBlendsCount(part);
	for (u32 bidx = 0; bidx < bc; ++bidx)
	{
		CBlend* BR = part == 1 ? m_model_2->LL_PartBlend(part, bidx) : m_model->LL_PartBlend(part, bidx);
		if (!BR)
			continue;

		BR->speed = speed;
	}
}

void player_hud::detach_item_idx(u16 idx)
{
	if (NULL == m_attached_items[idx]) return;

	m_attached_items[idx]->m_parent_hud_item->on_b_hud_detach();
	m_attached_items[idx] = NULL;

	if (idx == 1)
	{
		if (m_attached_items[0])
			re_sync_anim(2);
		else
		{
			m_model_2->PlayCycle("hand_idle_doun");
		}
	}
	else if (idx == 0)
	{
		if (m_attached_items[1])
		{
			//fix for a rare case where the right hand stays visible on screen after detaching the right hand's attached item
			player_hud_motion* pm = m_attached_items[1]->m_hand_motions->find_motion("anm_idle");
			const motion_descr& M = pm->m_animations[0];
			m_model->PlayCycle(0, M.mid, false);
			m_model->PlayCycle(2, M.mid, false);
		}
		else
		{
			m_model->PlayCycle("hand_idle_doun");
		}
	}

	if (!m_attached_items[0] && !m_attached_items[1])
	{
		m_model->PlayCycle("hand_idle_doun");
		m_model_2->PlayCycle("hand_idle_doun");
	}
}

void player_hud::detach_item(CHudItem* item)
{
	if (!item->IsAttachedToHUD()) return;
	u16 item_idx = item->HudItemData()->m_attach_place_idx;

	if (m_attached_items[item_idx] == item->HudItemData())
	{
		detach_item_idx(item_idx);
	}
}

bool player_hud::allow_script_anim()
{
	if (m_attached_items[0] && (m_attached_items[0]->m_parent_hud_item->IsPending() || m_attached_items[0]->m_parent_hud_item->GetState() == CHudItem::EHudStates::eBore))
		return false;
	else if (m_attached_items[1] && (m_attached_items[1]->m_parent_hud_item->IsPending() || m_attached_items[1]->m_parent_hud_item->GetState() == CHudItem::EHudStates::eBore))
		return false;
	else if (script_anim_part != u8(-1))
		return false;

	return true;
}

void player_hud::calc_transform(u16 attach_slot_idx, const Fmatrix& offset, Fmatrix& result, bool leadGun)
{
	IKinematics* kin = (attach_slot_idx == 0) ? m_model->dcast_PKinematics() : m_model_2->dcast_PKinematics();
	Fmatrix ancor_m = kin->LL_GetTransform(m_ancors[(leadGun ? 0 : attach_slot_idx)]);
	result.mul((attach_slot_idx == 0) ? m_transform : m_transform_2, ancor_m);
	result.mulB_43(offset);
}

bool player_hud::inertion_allowed()
{
	attachable_hud_item* hi = m_attached_items[0];
	if (hi)
	{
		bool res = (hi->m_parent_hud_item->HudInertionEnabled() && hi->m_parent_hud_item->HudInertionAllowed());
		return res;
	}
	return true;
}

void player_hud::OnMovementChanged(ACTOR_DEFS::EMoveCommand cmd)
{
	if (cmd == 0)
	{
		if (m_attached_items[0])
		{
			if (m_attached_items[0]->m_parent_hud_item->GetState() == CHUDState::eIdle)
				m_attached_items[0]->m_parent_hud_item->PlayAnimIdle();
		}
		if (m_attached_items[1])
		{
			if (m_attached_items[1]->m_parent_hud_item->GetState() == CHUDState::eIdle)
				m_attached_items[1]->m_parent_hud_item->PlayAnimIdle();
		}
		if (m_attached_items[SCOPE_ATTACH_IDX])
		{
			if (m_attached_items[SCOPE_ATTACH_IDX]->m_parent_hud_item->GetState() == CHUDState::eIdle)
				m_attached_items[SCOPE_ATTACH_IDX]->m_parent_hud_item->PlayAnimIdle();
		}
	}
	else
	{
		if (m_attached_items[0])
			m_attached_items[0]->m_parent_hud_item->OnMovementChanged(cmd);

		if (m_attached_items[1])
			m_attached_items[1]->m_parent_hud_item->OnMovementChanged(cmd);

		if (m_attached_items[SCOPE_ATTACH_IDX])
			m_attached_items[SCOPE_ATTACH_IDX]->m_parent_hud_item->OnMovementChanged(cmd);
	}

	::luabind::functor<void> func;
	if (ai().script_engine().functor("_g.player_hud__OnMovementChanged", func))
	{
		func(cmd);
	}

	updateMovementLayerState();
}

bool nearwall_callback(int target, float ofs, const Fvector& dir, Fmatrix& mat)
{
	::luabind::functor<void> on_nearwall;
	if (!ai().script_engine().functor("_G.CActorHudOnNearWall", on_nearwall))
	{
		return false;
	}

	::luabind::object table = ::luabind::newtable(ai().script_engine().lua());
	table["target"] = target;
	table["offset"] = ofs;
	table["direction"] = dir;
	table["matrix"] = mat;
	table["override"] = false;
	on_nearwall(table);
	mat = ::luabind::object_cast<Fmatrix>(table["matrix"]);
	return ::luabind::object_cast<bool>(table["override"]);
}

void update_nearwall(int target, const attachable_hud_item* item, Fmatrix& nearwall)
{
	CHudItem* parent = item->m_parent_hud_item;
	const SPickParam& pp = parent->GetPick();
	float ofs = parent->GetNearWallOffset();
	nearwall = Fmatrix().identity();
	Fvector dir = Fvector().mul(pp.barrel_matrix.k, -1);
	Device.mView.transform_dir(dir);

	if (!nearwall_callback(target, ofs, dir, nearwall))
	{
		dir.mul(ofs);
		nearwall.translate_add(dir);
	}
}

void apply_nearwall(Fmatrix& transform, Fmatrix& nearwall, attachable_hud_item* item)
{
	transform.mulB_43(nearwall);

	Fmatrix mInv = transform;
	mInv.invert();

	Fmatrix delta = mInv;
	delta.mulB_43(item->m_item_transform);
	Fmatrix deltaInv = delta;
	deltaInv.invert();

	nearwall.mulA_43(deltaInv);
	nearwall.mulB_43(delta);
	item->m_item_transform.mulB_43(nearwall);
	item->m_parent_hud_item->UpdatePick();
}

void player_hud::OnFrame()
{
	// If we have a main-hand item...
	if (m_attached_items[0])
		// Delegate control
		m_attached_items[0]->m_parent_hud_item->OnFrame();

	// If we have an off-hand item...
	if (m_attached_items[1])
		// Delegate control
		m_attached_items[1]->m_parent_hud_item->OnFrame();

	// If near-wall is in position mode...
	if (g_nearwall == NW_POS)
	{
		Fmatrix nearwall_0;

		// If we have a main-hand item...
		if (m_attached_items[0])
		{
			update_nearwall(1, m_attached_items[0], nearwall_0);

			// If we have no off-hand item
			if (!m_attached_items[1])
			{
				// Apply the main hand item's nearwall to the left arm
				m_transform_2.mulB_43(nearwall_0);
			}

			apply_nearwall(m_transform, nearwall_0, m_attached_items[0]);
		}

		// If we have an off-hand item...
		if (m_attached_items[1])
		{
			Fmatrix nearwall_1 = Fmatrix().identity();
			update_nearwall(2, m_attached_items[1], nearwall_1);

			// If we have a main-hand item...
			if (m_attached_items[0])
			{
				// And it is a weapon...
				CWeapon* pWeapon = smart_cast<CWeapon*>(m_attached_items[0]->m_parent_hud_item);
				if (pWeapon)
				{
					// Interpolate between main and off -hand transforms based on aim factor
					float fac = pWeapon->GetZRotatingFactor();
					nearwall_1.i.lerp(nearwall_1.i, nearwall_0.i, fac);
					nearwall_1.i.normalize();
					nearwall_1.j.lerp(nearwall_1.j, nearwall_0.j, fac);
					nearwall_1.j.normalize();
					nearwall_1.k.lerp(nearwall_1.k, nearwall_0.k, fac);
					nearwall_1.k.normalize();
					nearwall_1.c.lerp(nearwall_1.c, nearwall_0.c, fac);
				}
			}

			// Apply nearwall to left arm
			apply_nearwall(m_transform_2, nearwall_1, m_attached_items[1]);
		}

		if (m_attached_items[SCOPE_ATTACH_IDX])
		{
			CHudItem* parent = m_attached_items[SCOPE_ATTACH_IDX]->m_parent_hud_item;
			m_attached_items[SCOPE_ATTACH_IDX]->m_item_transform.mulB_43(nearwall_0);
		}
	}
}

void player_hud::net_Relcase(CObject* obj)
{
	if (m_attached_items[0])
		m_attached_items[0]->m_parent_hud_item->net_Relcase(obj);

	if (m_attached_items[1])
		m_attached_items[1]->m_parent_hud_item->net_Relcase(obj);

	if (m_attached_items[SCOPE_ATTACH_IDX])
		m_attached_items[SCOPE_ATTACH_IDX]->m_parent_hud_item->net_Relcase(obj);
}
