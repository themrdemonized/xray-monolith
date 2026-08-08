#include "stdafx.h"
#ifdef STATIONARYMGUN_NEW
#include "WeaponStatMgun.h"
#include "../Include/xrRender/Kinematics.h"
#include "../xrphysics/PhysicsShell.h"
#include "CustomRocket.h"

void CWeaponStatMgun::OnEvent(NET_Packet &P, u16 type)
{
	inherited::OnEvent(P, type);
	u16 id;
	switch (type)
	{
	case GE_OWNERSHIP_TAKE:
	{
		P.r_u16(id);
		CObject *O = Level().Objects.net_Find(id);
		if (O)
		{
#if 0
			if (m_anim_weapon.IsMagazine(O))
			{
				m_anim_weapon.AttachMagazine(id, this);
				m_anim_weapon.LaunchMagazine(O);
				break;
			}
#endif
		}
	}
	break;
	case GE_OWNERSHIP_REJECT:
	case GE_LAUNCH_ROCKET:
	{
		P.r_u16(id);
		CObject *O = Level().Objects.net_Find(id);
		if (O)
		{
#if 0
			if (m_anim_weapon.IsMagazine(O))
			{
				m_anim_weapon.DetachMagazine(id);
				break;
			}
#endif
		}
	}
	break;
	}
}

void CWeaponStatMgun::OverrideRangeFOV(const CGameObject* npc, float& range)
{
    /*
        The default visibility range isn't enough for stationary machine guns.
        Max max_view_distance of danger mental state is 114m after all calculation in object_visible_distance() - update_range_fov().
        It also affects other place that use update_range_fov().
        It makes gunner:see(target) return false in LUA.
        This overrider allows changing start_range in update_range_fov() effectively changing visibility range.
        For now it's used for machine gunners only. It allow gunners to engage actor from further away.
    */
    if (m_on_range_fov_callback.size() == 0)
        return;
    ::luabind::functor<::luabind::object> lua_function;
    if (ai().script_engine().functor(m_on_range_fov_callback.c_str(), lua_function) == false)
        return;
    ::luabind::object lua_var = ::luabind::newtable(ai().script_engine().lua());
    lua_var["range"] = range;
    ::luabind::object lua_res = lua_function(lua_game_object(), npc->lua_game_object(), lua_var);
    if (lua_res && lua_res.type() == LUA_TTABLE)
    {
        range = ::luabind::object_cast<float>(lua_res["range"]);
    }
}

/*----------------------------------------------------------------------------------------------------
	SStmAnimWeapon
----------------------------------------------------------------------------------------------------*/
CWeaponStatMgun::SStmAnimWeapon::SStmAnimWeapon()
{
	m_current_anm = 0;
	m_current_idx = 0;
	m_current_mid.invalidate();

	m_hand_bid = BI_NONE;
	m_hand_pos.set(0, 0, 0);
	m_hand_vis._set("");

	m_magazine_hide_bid = BI_NONE;
	m_magazine_hide_anm.clear();
#if 0
	m_reload0_mag_idx = u8(-1);
	m_reload1_mag_idx = u8(-1);
	m_magazine_sec = "";
	m_magazine_dir.set(0, 0, 1);
	m_magazine_vel = 0.0F;
#endif
}

CWeaponStatMgun::SStmAnimWeapon::~SStmAnimWeapon()
{
	HandRemove();
}

void CWeaponStatMgun::SStmAnimWeapon::Init(CWeaponStatMgun *stm)
{
	R_ASSERT(stm);
	m_stm = stm;
}

BOOL CWeaponStatMgun::SStmAnimWeapon::net_Spawn(CSE_Abstract *DC)
{
	IKinematics *K = m_stm->Visual()->dcast_PKinematics();
	CInifile *ini = K->LL_UserData();
	const LPCSTR anim_weapon = "animation_weapon";
	IKinematicsAnimated *A = m_stm->Visual()->dcast_PKinematicsAnimated();
	if (A)
	{
		for (int i = 0; i < eStmAnimWeapon_size; i++)
		{
			m_anims[i].clear();
		}
		if (ini->section_exist(anim_weapon))
		{
			char *anim_name[eStmAnimWeapon_size] = {0};
			anim_name[eStmAnimWeapon_idle] = "anm_idle";
			anim_name[eStmAnimWeapon_shot] = "anm_shot";
			anim_name[eStmAnimWeapon_reload0] = "anm_reload0";
			anim_name[eStmAnimWeapon_reload1] = "anm_reload1";
			anim_name[eStmAnimWeapon_chamber] = "anm_chamber";
			for (int i = 0; i < eStmAnimWeapon_size; i++)
			{
				LPCSTR str = READ_IF_EXISTS(ini, r_string, anim_weapon, anim_name[i], nullptr);
				string128 tmp;
				if (str && strlen(str))
				{
					for (int k = 0, n = _GetItemCount(str); k < n; ++k)
					{
						memset(tmp, 0, sizeof(tmp));
						m_anims[i].push_back(A->ID_Cycle(_GetItem(str, k, tmp)));
					}
				}
			}
			{
				m_magazine_hide_bid = ini->line_exist(anim_weapon, "magazine_hide_bid") ? K->LL_BoneID(ini->r_string(anim_weapon, "magazine_hide_bid")) : BI_NONE;
				m_magazine_hide_anm.clear();
				LPCSTR str = READ_IF_EXISTS(ini, r_string, anim_weapon, "magazine_hide_anm", nullptr);
				string128 tmp;
				if (str && strlen(str))
				{
					for (int k = 0, n = _GetItemCount(str); k < n; ++k)
					{
						memset(tmp, 0, sizeof(tmp));
						m_magazine_hide_anm.push_back(A->ID_Cycle(_GetItem(str, k, tmp)));
					}
				}
			}
#if 0
			m_reload0_mag_idx = READ_IF_EXISTS(ini, r_u8, anim_weapon, "reload0_mag_idx", u8(-1));
			m_reload1_mag_idx = READ_IF_EXISTS(ini, r_u8, anim_weapon, "reload1_mag_idx", u8(-1));
			m_magazine_sec = READ_IF_EXISTS(ini, r_string, anim_weapon, "magazine_sec", "");
			m_magazine_dir = READ_IF_EXISTS(ini, r_fvector3, anim_weapon, "magazine_dir", Fvector().set(0, 0, 1)).normalize_safe();
			m_magazine_vel = READ_IF_EXISTS(ini, r_float, anim_weapon, "magazine_vel", 0.0F);
#endif
			Play(eStmAnimWeapon_idle);
		}
	}

	if (ini->section_exist("animation_hand"))
	{
		m_hand_bid = ini->line_exist("animation_hand", "hand_bid") ? K->LL_BoneID(ini->r_string("animation_hand", "hand_bid")) : BI_NONE;
		m_hand_pos = READ_IF_EXISTS(ini, r_fvector3, "animation_hand", "hand_pos", Fvector().set(0, 0, 0));
		m_hand_anims[eStmAnimWeapon_idle]._set(READ_IF_EXISTS(ini, r_string, "animation_hand", "hand_idle", ""));
	}
	return TRUE;
}

void CWeaponStatMgun::SStmAnimWeapon::Play(u8 anim)
{
	if (m_stm->Visual()->dcast_PKinematicsAnimated() == nullptr)
		return;
	if (anim >= eStmAnimWeapon_size)
		return;
	if (m_anims[anim].size() == 0)
		return;
	m_current_anm = anim;
	m_current_idx = 0;
	m_current_mid = m_anims[m_current_anm].at(m_current_idx);
	m_stm->Visual()->dcast_PKinematicsAnimated()->PlayCycle(m_current_mid, TRUE, AnimationCallback, m_stm);
	UpdateMagazineVisibility();
}

void CWeaponStatMgun::SStmAnimWeapon::OnAnimationEnd()
{
	if (m_current_idx + 1 >= m_anims[m_current_anm].size())
		return;

#if 0
	switch (m_current_anm)
	{
	case eStmAnimWeapon_reload0:
		if (m_current_idx == m_reload0_mag_idx)
			CreateMagazine();
		break;
	case eStmAnimWeapon_reload1:
		if (m_current_idx == m_reload1_mag_idx)
			CreateMagazine();
		break;
	default:
		break;
	}
#endif

	m_current_idx++;
	m_current_mid = m_anims[m_current_anm].at(m_current_idx);
	m_stm->Visual()->dcast_PKinematicsAnimated()->PlayCycle(m_current_mid, TRUE, AnimationCallback, m_stm);
	UpdateMagazineVisibility();
}

void CWeaponStatMgun::SStmAnimWeapon::AnimationCallback(CBlend *B)
{
	CWeaponStatMgun *stm = (CWeaponStatMgun *)B->CallbackParam;
	stm->m_anim_weapon.OnAnimationEnd();
}

bool CWeaponStatMgun::SStmAnimWeapon::HandGetVisualName()
{
	m_hand_vis._set("");
	if (g_player_hud == nullptr || g_player_hud->section_name().size() == 0)
		return false;
	LPCSTR str = READ_IF_EXISTS(pSettings, r_string, g_player_hud->section_name().c_str(), "visual", nullptr);
	if (str == nullptr || strlen(str) == 0)
		return false;
	string128 visual_name;
	xr_sprintf(visual_name, sizeof(visual_name), "%s.ogf", str);
	m_hand_vis._set(visual_name);
	return true;
}

void CWeaponStatMgun::SStmAnimWeapon::HandCreate()
{
	if (m_hand_bid == BI_NONE)
		return;
	if (HandGetVisualName() == false)
		return;
	script_attachment *att = m_stm->get_attachment(m_hand_atm);
	if (att && xr_strcmp(m_hand_vis, att->GetModelScript()) == 0)
		return;
	att = xr_new<script_attachment>(m_hand_atm, m_hand_vis.c_str());
	R_ASSERT(att);
	att->SetType(script_attachment_type::eSA_World);
	att->SetParent(m_stm->cast_game_object());
	att->SetParentBone(m_hand_bid);
	att->SetPosition(m_hand_pos);
	HandPlay(eStmAnimWeapon_idle);
}

void CWeaponStatMgun::SStmAnimWeapon::HandRemove()
{
	m_stm->remove_attachment(m_hand_atm);
}

void CWeaponStatMgun::SStmAnimWeapon::HandPlay(u8 anim)
{
	script_attachment *att = m_stm->get_attachment(m_hand_atm);
	if (att == nullptr)
		return;
	if (m_hand_anims[anim].size())
	{
		att->PlayMotion(m_hand_anims[anim].c_str(), false, 1.0);
	}
}

void CWeaponStatMgun::SStmAnimWeapon::UpdateMagazineVisibility()
{
	if (m_magazine_hide_bid == BI_NONE)
		return;
	IKinematics *K = m_stm->Visual()->dcast_PKinematics();
	R_ASSERT(K);
	bool visibility = std::find(m_magazine_hide_anm.begin(), m_magazine_hide_anm.end(), m_current_mid) == m_magazine_hide_anm.end();
	if (K->LL_GetBoneVisible(m_magazine_hide_bid) != visibility)
	{
		K->LL_SetBoneVisible(m_magazine_hide_bid, visibility, TRUE);
	}
}

#if 0
bool CWeaponStatMgun::SStmAnimWeapon::IsMagazine(CObject *O)
{
	return (O && O->cNameSect() == m_magazine_sec);
}

void CWeaponStatMgun::SStmAnimWeapon::CreateMagazine()
{
	if (m_magazine_sec.size() == 0)
		return;
	CSE_Abstract *D = F_entity_Create(m_magazine_sec.c_str());
	R_ASSERT(D);
	D->s_name._set(m_magazine_sec.c_str());
	D->set_name_replace("");
	D->s_RP = 0xff;
	D->ID = 0xffff;
	D->ID_Parent = m_stm->ID();
	D->ID_Phantom = 0xffff;
	D->s_flags.assign(M_SPAWN_OBJECT_LOCAL);
	D->RespawnTime = 0;

	NET_Packet P;
	D->Spawn_Write(P, TRUE);
	Level().Send(P, net_flags(TRUE));
	F_entity_Destroy(D);
}

void CWeaponStatMgun::SStmAnimWeapon::LaunchMagazine(CObject *O)
{
	CCustomRocket *mag = smart_cast<CCustomRocket *>(O);
	R_ASSERT(mag);
	Fmatrix xfm = (m_magazine_hide_bid != BI_NONE) ? Fmatrix().mul_43(m_stm->XFORM(), m_stm->Visual()->dcast_PKinematics()->LL_GetTransform(m_magazine_hide_bid)) : m_stm->XFORM();
	Fvector vel;
	m_stm->XFORM().transform_dir(vel, m_magazine_dir);
	vel.normalize_safe().mul(m_magazine_vel);
	mag->SetLaunchParams(xfm, vel, Fvector().set(0, 0, 0));

	xr_vector<CCustomRocket *>::iterator I = std::find(m_magazine_object.begin(), m_magazine_object.end(), mag);
	VERIFY(I != m_magazine_object.end());
	m_magazine_object.erase(I);
	m_magazine_launch.push_back(mag);

	NET_Packet P;
	m_stm->u_EventGen(P, GE_LAUNCH_ROCKET, m_stm->ID());
	P.w_u16(u16(O->ID()));
	m_stm->u_EventSend(P);
}

void CWeaponStatMgun::SStmAnimWeapon::AttachMagazine(u16 id, CGameObject *parent)
{
	CCustomRocket *mag = smart_cast<CCustomRocket *>(Level().Objects.net_Find(id));
	VERIFY2(mag, m_stm->cNameSect_str());
	mag->H_SetParent(parent);
	m_magazine_object.push_back(mag);
}

void CWeaponStatMgun::SStmAnimWeapon::DetachMagazine(u16 id)
{
	CCustomRocket *mag = smart_cast<CCustomRocket *>(Level().Objects.net_Find(id));
	VERIFY2(mag, m_stm->cNameSect_str());
	{
		xr_vector<CCustomRocket *>::iterator I = std::find(m_magazine_object.begin(), m_magazine_object.end(), mag);
		if (I != m_magazine_object.end())
		{
			(*I)->H_SetParent(nullptr);
			m_magazine_object.erase(I);
		}
	}
	{
		xr_vector<CCustomRocket *>::iterator I = std::find(m_magazine_launch.begin(), m_magazine_launch.end(), mag);
		if (I != m_magazine_launch.end())
		{
			(*I)->H_SetParent(nullptr);
			m_magazine_launch.erase(I);
		}
	}
}
#endif
#endif