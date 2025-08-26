#include "stdafx.h"
#ifdef CAR_NEW
#include "Car.h"
#include "Level.h"
#include "../Include/xrRender/Kinematics.h"

#include "script_game_object.h"
#include "CameraFirstEye.h"
#include "cameralook.h"
#include "xr_level_controller.h"

bool CCar::is_ai_obstacle() const
{
	/* npcs will try to walk around car when it is on the way. */
	return true;
}

bool CCar::IsCameraZoom()
{
	return m_zoom_status;
}

void CCar::SetUseAction(LPCSTR txt)
{
	m_sUseAction._set(txt);
}

void CCar::LoadExplosiveSection(LPCSTR section, bool is_load_from_model_custom_data)
{
	if (is_load_from_model_custom_data)
	{
		CInifile *ini = Visual()->dcast_PKinematics()->LL_UserData();
		CExplosive::Load(ini, section);
	}
	else
	{
		CExplosive::Load(pSettings, section);
	}
}

void CCar::InitExplosiveSection()
{
	LoadExplosiveSection("explosion", true);
}

/*------------------------------------------------------------------------------------------------------------------------
    Fly
------------------------------------------------------------------------------------------------------------------------*/
void CCar::Fly_Load(LPCSTR section)
{
	m_control_neutral = READ_IF_EXISTS(pSettings, r_float, section, "control_neutral", 0.0F);
	m_control_ele_max = READ_IF_EXISTS(pSettings, r_float, section, "control_ele_max", 0.0F);
	m_control_pit_max = READ_IF_EXISTS(pSettings, r_float, section, "control_pit_max", 0.0F);
	m_control_rol_max = READ_IF_EXISTS(pSettings, r_float, section, "control_rol_max", 0.0F);
	m_control_yaw_max = deg2rad(READ_IF_EXISTS(pSettings, r_float, section, "control_yaw_max", 0.0F));
	m_control_ele_inc = READ_IF_EXISTS(pSettings, r_float, section, "control_ele_inc", 0.0F);
	m_control_pit_inc = READ_IF_EXISTS(pSettings, r_float, section, "control_pit_inc", 0.0F);
	m_control_rol_inc = READ_IF_EXISTS(pSettings, r_float, section, "control_rol_inc", 0.0F);
	m_control_yaw_inc = deg2rad(READ_IF_EXISTS(pSettings, r_float, section, "control_yaw_inc", 0.0F));

	m_fly_weight_min = READ_IF_EXISTS(pSettings, r_float, section, "fly_weight_min", 1.0F);
	m_fly_weight_min = (m_fly_weight_min > 0.0F) ? m_fly_weight_min : 1.0F;
}

BOOL CCar::Fly_net_Spawn(CSE_Abstract *DC)
{
	IKinematics *K = Visual()->dcast_PKinematics();
	CInifile *ini = K->LL_UserData();
	const LPCSTR fly_sec = "fly_definition";

	/* body_bone is for applying torque. */
	m_body_bid = ini->line_exist(fly_sec, "body_bone") ? K->LL_BoneID(ini->r_string(fly_sec, "body_bone")) : BI_NONE;

	{
		m_rotor_bones.clear();
		int n = ini->line_count("rotors");
		for (int k = 0; k < n; k++)
		{
			LPCSTR bone_name, str;
			ini->r_line("rotors", k, &bone_name, &str);
			u16 bone_id = K->LL_BoneID(bone_name);
			if (bone_id != BI_NONE)
			{
				m_rotor_bones.push_back(SCarFlyBone());
				SCarFlyBone &I = m_rotor_bones.back();
				I.E = m_pPhysicsShell->get_Element(bone_id);
				I.J = m_pPhysicsShell->get_Joint(bone_id);

				string64 tmp, key, val;
				for (int c = 0; c < _GetItemCount(str); ++c)
				{
					memset(tmp, 0, sizeof(tmp));
					_GetItem(str, c, tmp);
					if (strlen(tmp) && _GetItemCount(tmp, ':') == 2)
					{
						memset(key, 0, sizeof(key));
						memset(val, 0, sizeof(val));
						_GetItem(tmp, 0, key, ':');
						_GetItem(tmp, 1, val, ':');
						if (strcmp(key, "clockwise") == 0)
						{
							I.clockwise = strcmp(val, "true") == 0;
						}
					}
				}
			}
		}
		R_ASSERT3(m_rotor_bones.size(), "fly_definition no rotors", cNameSect_str());
	}
	{
		m_drive_bones.clear();
		LPCSTR str = READ_IF_EXISTS(ini, r_string, fly_sec, "drive_bones", NULL);
		int n = _GetItemCount(str);
		string64 bone_name;
		for (int k = 0; k < n; ++k)
		{
			memset(bone_name, 0, sizeof(bone_name));
			_GetItem(str, k, bone_name);
			u16 bone_id = K->LL_BoneID(bone_name);
			if (bone_id != BI_NONE)
			{
				m_drive_bones.push_back(SCarFlyBone());
				SCarFlyBone &I = m_drive_bones.back();
				I.bid = bone_id;
				I.E = m_pPhysicsShell->get_Element(bone_id);
			}
		}
		R_ASSERT3(m_drive_bones.size(), "fly_definition no drive_bones", cNameSect_str());
	}

	m_rotor_force_max = READ_IF_EXISTS(ini, r_float, fly_sec, "rotor_force_max", 0.0F);
	m_rotor_speed_max = READ_IF_EXISTS(ini, r_float, fly_sec, "rotor_speed_max", 0.0F);

	return TRUE;
}

bool CCar::Fly_attach_Actor(CGameObject *actor)
{
	FlyResetControl();
	return true;
}

void CCar::Fly_detach_Actor()
{
	FlyResetControl();
}

void CCar::Fly_VisualUpdate(float fov)
{
	if (m_pPhysicsShell)
	{
		m_pPhysicsShell->InterpolateGlobalTransform(&XFORM());
		Visual()->dcast_PKinematics()->CalculateBones();
	}

	m_car_sound->Update();
}

void CCar::Fly_PhDataUpdate(float step)
{
	/* Only update physic. Don't calculate bones. */
	if (m_pPhysicsShell == nullptr)
		return;

	Fly_RotorUpdate();
	if (b_engine_on && m_rotor_bones.size() && m_drive_bones.size())
	{
		CPhysicsElement *bone_body = m_pPhysicsShell->get_Element(m_body_bid);
		if (bone_body == nullptr)
		{
			bone_body = m_pPhysicsShell->get_ElementByStoreOrder(0);
		}

		float mass = m_pPhysicsShell->getMass();

		/* Make it float in mid air. Must be m_rotor_bones, not m_drive_bones. */
		{
			float force = mass * EffectiveGravity() / m_rotor_bones.size();
			Fvector vec = Fvector().set(0.0F, 1.0F, 0.0F).mul(force);
			for (auto &I : m_rotor_bones)
			{
				I.E->applyForce(vec.x, vec.y, vec.z);
			}
		}

		if (m_control_ele == eControlEle_NA && m_control_pit == eControlPit_NA && m_control_rol == eControlRol_NA)
		{
			Fvector velocity;
			m_pPhysicsShell->get_LinearVel(velocity);
			if (velocity.magnitude() > EPS_L)
			{
				float force = mass * FlyWeightScale() * __min(velocity.magnitude(), m_control_neutral);
				Fvector vec = Fvector().invert(velocity).normalize_safe().mul(force);
				m_pPhysicsShell->applyRelForce(vec.x, vec.y, vec.z);
			}
		}

		/* Control. */
		switch (m_control_ele)
		{
		case eControlEle_UP:
		case eControlEle_DW:
		{
			float force = mass * FlyWeightScale() * m_control_ele_max / m_drive_bones.size();
			Fvector vec = Fvector().set(0.0F, 1.0F, 0.0F).mul((m_control_ele == eControlEle_UP) ? force : -force);
			for (auto &I : m_drive_bones)
			{
				I.E->applyRelForce(vec.x, vec.y, vec.z);
			}
			break;
		}
		}

		switch (m_control_pit)
		{
		case eControlPit_FS:
		case eControlPit_BS:
		{
			float force = mass * FlyWeightScale() * m_control_pit_max / m_drive_bones.size();
			Fvector vec = Fvector().set(0.0F, 0.0F, 1.0F).mul((m_control_pit == eControlPit_FS) ? force : -force);
			for (auto &I : m_drive_bones)
			{
				I.E->applyRelForce(vec.x, vec.y, vec.z);
			}
			break;
		}
		}

		switch (m_control_rol)
		{
		case eControlRol_RS:
		case eControlRol_LS:
		{
			float force = mass * FlyWeightScale() * m_control_rol_max / m_drive_bones.size();
			Fvector vec = Fvector().set(1.0F, 0.0F, 0.0F).mul((m_control_rol == eControlRol_RS) ? force : -force);
			for (auto &I : m_drive_bones)
			{
				I.E->applyRelForce(vec.x, vec.y, vec.z);
			}
			break;
		}
		}

		switch (m_control_yaw)
		{
		case eControlYaw_RS:
		case eControlYaw_LS:
		{
			float force = mass * FlyWeightScale() * m_control_yaw_max;
			Fvector vec = Fvector().set(0.0F, 1.0F, 0.0F).mul((m_control_yaw == eControlYaw_RS) ? force : -force);
			bone_body->applyRelTorque(vec.x, vec.y, vec.z);
			break;
		}
		}
	}
}

void CCar::Fly_RotorUpdate()
{
	if (b_engine_on)
	{
		for (auto &I : m_rotor_bones)
		{
			if (I.spinning != true)
			{
				I.spinning = true;
				I.J->SetForceAndVelocity(m_rotor_force_max, m_rotor_speed_max * ((I.clockwise) ? 1 : -1), 1);
			}
		}
	}
	else
	{
		for (auto &I : m_rotor_bones)
		{
			if (I.spinning == true)
			{
				I.spinning = false;
				I.J->SetForceAndVelocity(0, 0, 1);
			}
		}
	}
}

float CCar::FlyWeightScale()
{
    return m_fly_weight_min / (m_fly_weight_min + m_fly_weight_add);
}

void CCar::SetFlyWeightAdd(float val)
{
	m_fly_weight_add = (val > 0.0F) ? val : 0.0F;
}

void CCar::FlyResetControl()
{
	m_control_ele = eControlEle_NA;
	m_control_yaw = eControlYaw_NA;
	m_control_pit = eControlPit_NA;
	m_control_rol = eControlPit_NA;
}

/*----------------------------------------------------------------------------------------------------
	IR
----------------------------------------------------------------------------------------------------*/
void CCar::Fly_OnMouseMove(int dx, int dy)
{
	CCameraBase *cam = Camera();
	float scale = (cam->f_fov / g_fov) * psMouseSens * psMouseSensScale / 50.0F;
	if (dx)
	{
		float d = float(dx) * scale;
		cam->Move((d < 0) ? kLEFT : kRIGHT, _abs(d));
	}
	if (dy)
	{
		float d = ((psMouseInvert.test(1)) ? -1 : 1) * float(dy) * scale * 3.0F / 4.0F;
		cam->Move((d > 0) ? kUP : kDOWN, _abs(d));
	}
}

void CCar::Fly_OnKeyboardPress(int dik)
{
	switch (dik)
	{
	/* Movement. */
	case kACCEL:
	case kSPRINT_TOGGLE:
		m_control_ele = (m_control_ele == eControlEle_DW) ? eControlEle_NA : eControlEle_UP;
		break;
	case kCROUCH:
		m_control_ele = (m_control_ele == eControlEle_UP) ? eControlEle_NA : eControlEle_DW;
		break;
	case kL_STRAFE:
		m_control_rol = (m_control_rol == eControlRol_RS) ? eControlRol_NA : eControlRol_LS;
		break;
	case kR_STRAFE:
		m_control_rol = (m_control_rol == eControlRol_LS) ? eControlRol_NA : eControlRol_RS;
		break;
	case kFWD:
		m_control_pit = (m_control_pit == eControlPit_BS) ? eControlPit_NA : eControlPit_FS;
		break;
	case kBACK:
		m_control_pit = (m_control_pit == eControlPit_FS) ? eControlPit_NA : eControlPit_BS;
		break;
	case kL_LOOKOUT:
		m_control_yaw = (m_control_yaw == eControlYaw_RS) ? eControlYaw_NA : eControlYaw_LS;
		break;
	case kR_LOOKOUT:
		m_control_yaw = (m_control_yaw == eControlYaw_LS) ? eControlYaw_NA : eControlYaw_RS;
		break;
	/* Action. */
	case kWPN_ZOOM:
		if (!psActorFlags.test(AF_AIM_TOGGLE))
		{
			m_zoom_status = true;
		}
		else
		{
			m_zoom_status = (m_zoom_status) ? false : true;
		}
		break;
	};
}

void CCar::Fly_OnKeyboardRelease(int dik)
{
	switch (dik)
	{
	/* Movement. */
	case kACCEL:
	case kSPRINT_TOGGLE:
		m_control_ele = (m_control_ele == eControlEle_UP) ? eControlEle_NA : eControlEle_DW;
		break;
	case kCROUCH:
		m_control_ele = (m_control_ele == eControlEle_DW) ? eControlEle_NA : eControlEle_UP;
		break;
	case kFWD:
		m_control_pit = (m_control_pit == eControlPit_FS) ? eControlPit_NA : eControlPit_BS;
		break;
	case kBACK:
		m_control_pit = (m_control_pit == eControlPit_BS) ? eControlPit_NA : eControlPit_FS;
		break;
	case kL_STRAFE:
		m_control_rol = (m_control_rol == eControlRol_LS) ? eControlRol_NA : eControlRol_RS;
		break;
	case kR_STRAFE:
		m_control_rol = (m_control_rol == eControlRol_RS) ? eControlRol_NA : eControlRol_LS;
		break;
	case kL_LOOKOUT:
		m_control_yaw = (m_control_yaw == eControlYaw_LS) ? eControlYaw_NA : eControlYaw_RS;
		break;
	case kR_LOOKOUT:
		m_control_yaw = (m_control_yaw == eControlYaw_RS) ? eControlYaw_NA : eControlYaw_LS;
		break;
	/* Action. */
	case kWPN_ZOOM:
		if (!psActorFlags.test(AF_AIM_TOGGLE))
		{
			m_zoom_status = false;
		}
		break;
	case kDETECTOR:
		SwitchEngine();
		break;
	/* Change camera. */
	case kCAM_1:
		OnCameraChange(ectFirst);
		break;
	case kCAM_2:
		OnCameraChange(ectChase);
		break;
	case kCAM_3:
		OnCameraChange(ectFree);
		break;
	};
}

void CCar::Fly_OnKeyboardHold(int dik)
{
}

/*----------------------------------------------------------------------------------------------------
	SCarFlyBone
----------------------------------------------------------------------------------------------------*/
CCar::SCarFlyBone::SCarFlyBone()
{
	bid = BI_NONE;
	E = nullptr;
	J = nullptr;
	clockwise = false;
	axis = 0;
	spinning = false;
}
#endif