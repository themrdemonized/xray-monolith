#include "stdafx.h"
#include "searchlight.h"
#include "../xrEngine/LightAnimLibrary.h"
#include "script_entity_action.h"
#include "xrServer_Objects_ALife.h"
#include "../Include/xrRender/Kinematics.h"
#include "game_object_space.h"

#ifdef PROJECTOR_NEW
#include "../xrphysics/PhysicsShell.h"

#endif

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

CProjector::CProjector()
{
#ifdef PROJECTOR_NEW
	m_active = false;

	m_lights.clear();
	m_controls.clear();
#else
	light_render = ::Render->light_create();
	light_render->set_type(IRender_Light::SPOT);
	light_render->set_shadow(true);
	glow_render = ::Render->glow_create();
	lanim = 0;
	bone_x.id = BI_NONE;
	bone_y.id = BI_NONE;
#endif
}

CProjector::~CProjector()
{
#ifdef PROJECTOR_NEW
	m_lights.clear();
	m_controls.clear();
#else
	light_render.destroy();
	glow_render.destroy();
#endif
}

void CProjector::Load(LPCSTR section)
{
	inherited::Load(section);
}


#ifdef PROJECTOR_NEW
	/* Remove. */
#else
void CProjector::BoneCallbackX(CBoneInstance* B)
{
	CProjector* P = static_cast<CProjector*>(B->callback_param());

	Fmatrix M;
	M.setHPB(0.0f, P->_current.pitch, 0.0f);
	B->mTransform.mulB_43(M);
}

void CProjector::BoneCallbackY(CBoneInstance* B)
{
	CProjector* P = static_cast<CProjector*>(B->callback_param());

	float delta_yaw = angle_difference(P->_start.yaw, P->_current.yaw);
	if (angle_normalize_signed(P->_start.yaw - P->_current.yaw) > 0) delta_yaw = -delta_yaw;

	Fmatrix M;
	M.setHPB(-delta_yaw, 0.0, 0.0f);
	B->mTransform.mulB_43(M);
}
#endif

BOOL CProjector::net_Spawn(CSE_Abstract* DC)
{
	CSE_Abstract* e = (CSE_Abstract*)(DC);
	CSE_ALifeObjectProjector* slight = smart_cast<CSE_ALifeObjectProjector*>(e);
	R_ASSERT(slight);

	if (!inherited::net_Spawn(DC))
		return (FALSE);

	R_ASSERT(Visual() && smart_cast<IKinematics*>(Visual()));

#ifdef PROJECTOR_NEW
	CPHSkeleton::Spawn(DC);

	IKinematics *K = Visual()->dcast_PKinematics();
	CInifile *ini = K->LL_UserData();
	const LPCSTR def = "projector_definition";
	{
		LPCSTR str = READ_IF_EXISTS(ini, r_string, def, "lights", def);
		if (str && strlen(str))
		{
			int num = _GetItemCount(str);
			for (int idx = 0; idx < num; idx++)
			{
				string64 sec;
				_GetItem(str, idx, sec);
				if (sec && strlen(sec))
				{
					m_lights.emplace_back(this, sec);
					SProjectorLight &I = m_lights.back();
					I.Load(ini, ini->section_exist(sec) ? sec : def);
					continue;
				}
				break;
			}
		}
	}
	{
		LPCSTR str = READ_IF_EXISTS(ini, r_string, def, "controls", def);
		if (str && strlen(str))
		{
			int num = _GetItemCount(str);
			for (int idx = 0; idx < num; idx++)
			{
				string64 sec;
				_GetItem(str, idx, sec);
				if (sec && strlen(sec) && ini->section_exist(sec))
				{
					m_controls.emplace_back(this, sec);
					SProjectorControl &I = m_controls.back();
					I.Load(ini, sec);
					continue;
				}
				break;
			}
		}
	}
#else
	IKinematics* K = smart_cast<IKinematics*>(Visual());
	CInifile* pUserData = K->LL_UserData();
	R_ASSERT3(pUserData, "Empty Projector user data!", slight->get_visual());
	lanim = LALib.FindItem(pUserData->r_string("projector_definition", "color_animator"));
	guid_bone = K->LL_BoneID(pUserData->r_string("projector_definition", "guide_bone"));
	VERIFY(guid_bone!=BI_NONE);
	bone_x.id = K->LL_BoneID(pUserData->r_string("projector_definition", "rotation_bone_x"));
	VERIFY(bone_x.id!=BI_NONE);
	bone_y.id = K->LL_BoneID(pUserData->r_string("projector_definition", "rotation_bone_y"));
	VERIFY(bone_y.id!=BI_NONE);
	Fcolor clr = pUserData->r_fcolor("projector_definition", "color");
	fBrightness = clr.intensity();
	light_render->set_color(clr);
	light_render->set_range(pUserData->r_float("projector_definition", "range"));
	light_render->set_cone(deg2rad(pUserData->r_float("projector_definition", "spot_angle")));
	light_render->set_texture(pUserData->r_string("projector_definition", "spot_texture"));

	glow_render->set_texture(pUserData->r_string("projector_definition", "glow_texture"));
	glow_render->set_color(clr);
	glow_render->set_radius(pUserData->r_float("projector_definition", "glow_radius"));
#endif

	setVisible(TRUE);
	setEnabled(TRUE);

#ifdef PROJECTOR_NEW
	processing_activate();

	PPhysicsShell()->Enable();
	if (PPhysicsShell() && m_ignore_collision_flag)
	{
		CPhysicsShellHolder::active_ignore_collision();
	}

	{
		/* Hack. net_spawn() of CScriptBinderObjectWrapper runs first. This allows overriding configs read in engine net_Spawn(). */
		LPCSTR str = READ_IF_EXISTS(pSettings, r_string, cNameSect_str(), "net_spawn_after", nullptr);
		::luabind::functor<void> lua_function;
		if (str && ai().script_engine().functor(str, lua_function))
		{
			lua_function(lua_game_object());
		}
	}
#else
	TurnOn();

	//////////////////////////////////////////////////////////////////////////
	CBoneInstance& b_x = smart_cast<IKinematics*>(Visual())->LL_GetBoneInstance(bone_x.id);
	b_x.set_callback(bctCustom, BoneCallbackX, this);

	CBoneInstance& b_y = smart_cast<IKinematics*>(Visual())->LL_GetBoneInstance(bone_y.id);
	b_y.set_callback(bctCustom, BoneCallbackY, this);

	Direction().getHP(_current.yaw, _current.pitch);
	_start = _target = _current;

	//////////////////////////////////////////////////////////////////////////
#endif

	return TRUE;
}

void CProjector::shedule_Update(u32 dt)
{
	inherited::shedule_Update(dt);
#ifdef PROJECTOR_NEW
	CPHSkeleton::Update(dt);
#endif
}

#ifdef PROJECTOR_NEW
	/* Remove. */
#else
void CProjector::TurnOn()
{
	if (light_render->get_active()) return;

	light_render->set_active(true);
	glow_render->set_active(true);

	IKinematics* visual = smart_cast<IKinematics*>(Visual());

	visual->LL_SetBoneVisible(guid_bone, TRUE, TRUE);
	visual->CalculateBones_Invalidate();
	visual->CalculateBones(TRUE);
}

void CProjector::TurnOff()
{
	if (!light_render->get_active()) return;

	light_render->set_active(false);
	glow_render->set_active(false);

	smart_cast<IKinematics*>(Visual())->LL_SetBoneVisible(guid_bone, FALSE, TRUE);
}
#endif

void CProjector::UpdateCL()
{
	inherited::UpdateCL();

#ifdef PROJECTOR_NEW
	if (PPhysicsShell())
	{
		PPhysicsShell()->InterpolateGlobalTransform(&XFORM());
		Visual()->dcast_PKinematics()->CalculateBones();
	}

	if (IsActive())
	{
		for (auto &I : m_controls)
		{
			I.UpdateCL();
		}
	}

	for (auto &I : m_lights)
	{
		I.UpdateCL();
	}
#else
	// update light source
	if (light_render->get_active())
	{
		// calc color animator
		if (lanim)
		{
			int frame;
			// возвращает в формате BGR
			u32 clr = lanim->CalculateBGR(Device.fTimeGlobal, frame);

			Fcolor fclr;
			fclr.set((float)color_get_B(clr), (float)color_get_G(clr), (float)color_get_R(clr), 1.f);
			fclr.mul_rgb(fBrightness / 255.f);
			light_render->set_color(fclr);
			glow_render->set_color(fclr);
		}

		CBoneInstance& BI = smart_cast<IKinematics*>(Visual())->LL_GetBoneInstance(guid_bone);
		Fmatrix M;

		M.mul(XFORM(), BI.mTransform);

		light_render->set_rotation(M.k, M.i);
		light_render->set_position(M.c);
		glow_render->set_position(M.c);
		glow_render->set_direction(M.k);
	}

	// Update searchlight 
	angle_lerp(_current.yaw, _target.yaw, bone_x.velocity, Device.fTimeDelta);
	angle_lerp(_current.pitch, _target.pitch, bone_y.velocity, Device.fTimeDelta);
#endif
}


void CProjector::renderable_Render()
{
	inherited::renderable_Render();
}

BOOL CProjector::UsedAI_Locations()
{
	return (FALSE);
}

#ifdef PROJECTOR_NEW
	/* Remove. */
#else
bool CProjector::bfAssignWatch(CScriptEntityAction* tpEntityAction)
{
	if (!inherited::bfAssignWatch(tpEntityAction))
		return (false);

	CScriptWatchAction& l_tWatchAction = tpEntityAction->m_tWatchAction;

	(!l_tWatchAction.m_tpObjectToWatch)
		? SetTarget(l_tWatchAction.m_tTargetPoint)
		: SetTarget(l_tWatchAction.m_tpObjectToWatch->Position());

	float delta_yaw = angle_difference(_current.yaw, _target.yaw);
	float delta_pitch = angle_difference(_current.pitch, _target.pitch);

	bone_x.velocity = l_tWatchAction.vel_bone_x;
	float time = delta_yaw / bone_x.velocity;
	bone_y.velocity = (fis_zero(time, EPS_L) ? l_tWatchAction.vel_bone_y : delta_pitch / time);

	return false == (l_tWatchAction.m_bCompleted = ((delta_yaw < EPS_L) && (delta_pitch < EPS_L)));
}

bool CProjector::bfAssignObject(CScriptEntityAction* tpEntityAction)
{
	if (!inherited::bfAssignObject(tpEntityAction))
		return (false);

	CScriptObjectAction& l_tObjectAction = tpEntityAction->m_tObjectAction;

	if (l_tObjectAction.m_tGoalType == MonsterSpace::eObjectActionTurnOn) TurnOn();
	else if (l_tObjectAction.m_tGoalType == MonsterSpace::eObjectActionTurnOff) TurnOff();

	return (true);
}

void CProjector::SetTarget(const Fvector& target_pos)
{
	float th, tp;
	Fvector().sub(target_pos, Position()).getHP(th, tp);

	float delta_h;
	delta_h = angle_difference(th, _start.yaw);

	if (angle_normalize_signed(th - _start.yaw) > 0) delta_h = -delta_h;
	clamp(delta_h, -PI_DIV_2, PI_DIV_2);

	_target.yaw = angle_normalize(_start.yaw + delta_h);

	clamp(tp, -PI_DIV_2, PI_DIV_2);
	_target.pitch = tp;
}
#endif

Fvector CProjector::GetCurrentDirection()
{
#ifdef PROJECTOR_NEW
	return Fvector().set(0.0F, 0.0F, 0.0F);
#else
	return (Fvector().setHP(_current.yaw, _current.pitch));
#endif
}

#ifdef PROJECTOR_NEW
void CProjector::net_Destroy()
{
	inherited::net_Destroy();
	CPHUpdateObject::Deactivate();
	CPHSkeleton::RespawnInit();
	processing_deactivate();
}

void CProjector::SpawnInitPhysics(CSE_Abstract *D)
{
	CSE_PHSkeleton *so = smart_cast<CSE_PHSkeleton *>(D);
	R_ASSERT(so);
	IKinematics *K = Visual()->dcast_PKinematics();
	CreateSkeleton(D);
	K->CalculateBones_Invalidate();
	K->CalculateBones(TRUE);
	CPHUpdateObject::Activate();
	PPhysicsShell()->applyImpulse(Fvector().set(0.0F, -1.0F, 0.0F), 0.1F);
}

void CProjector::CreateSkeleton(CSE_Abstract *P)
{
	if (!Visual())
		return;
	IKinematics *K = Visual()->dcast_PKinematics();
	phys_shell_verify_object_model(*this);
	U16Vec fixed_bones;
	fixed_bones.push_back(K->LL_GetBoneRoot());
	m_pPhysicsShell = P_build_Shell(this, false, fixed_bones);
	m_pPhysicsShell->SetPrefereExactIntegration();
	ApplySpawnIniToPhysicShell(&P->spawn_ini(), m_pPhysicsShell, fixed_bones.size() > 0);
	ApplySpawnIniToPhysicShell(K->LL_UserData(), m_pPhysicsShell, fixed_bones.size() > 0);
}

void CProjector::net_Save(NET_Packet &P)
{
	inherited::net_Save(P);
	CPHSkeleton::SaveNetState(P);
	P.w_vec3(Position());
	Fvector ang;
	XFORM().getXYZ(ang);
	P.w_vec3(ang);
}

BOOL CProjector::net_SaveRelevant()
{
	return TRUE;
}

BOOL CProjector::AlwaysTheCrow()
{
	return TRUE;
}

bool CProjector::is_ai_obstacle() const
{
	return true;
}

void CProjector::Action(int id, u32 flags, LPCSTR sec)
{
	switch (id)
	{
	case eActive:
	{
		if (flags == 1 && m_active != true)
		{
			m_active = true;
			for (auto &I : m_controls)
			{
				I.SetBoneCallbacks(true);
			}
		}
		if (flags != 1 && m_active == true)
		{
			m_active = false;
			for (auto &I : m_controls)
			{
				I.SetBoneCallbacks(false);
			}
		}
		break;
	}
	case eSwitch:
	{
		for (auto &I : m_lights)
		{
			if (sec == nullptr || strlen(sec) == 0 || I.IsSection(sec))
			{
				I.Switch((flags == 1) ? true : false);
			}
		}
		break;
	}
	}
}

void CProjector::SetParam(int id, Fvector val, LPCSTR sec)
{
	for (auto &I : m_controls)
	{
		if (sec == nullptr || strlen(sec) == 0 || I.IsSection(sec))
		{
			I.SetParam(id, val);
		}
	}
}
#endif
