#include "stdafx.h"

#ifdef PROJECTOR_NEW
#include "searchlight.h"
#include "script_entity_action.h"
#include "xrServer_Objects_ALife.h"
#include "../Include/xrRender/Kinematics.h"
#include "game_object_space.h"

/*----------------------------------------------------------------------------------------------------
	SProjectorLight
----------------------------------------------------------------------------------------------------*/
SProjectorLight::SProjectorLight(CProjector *obj, LPCSTR sec)
{
	m_sec._set(sec);
	O = obj;
	m_lanim = nullptr;
	m_light = ::Render->light_create();
	m_light->set_type(IRender_Light::SPOT);
	m_light->set_shadow(true);
	m_glow = ::Render->glow_create();
	m_brightness = 0.0F;
	m_light_bid = BI_NONE;
	m_guide_bid = BI_NONE;
}

SProjectorLight::~SProjectorLight()
{
	m_light.destroy();
	m_glow.destroy();
}

bool SProjectorLight::IsSection(LPCSTR sec)
{
	return sec && strcmp(sec, m_sec.c_str());
}

void SProjectorLight::Load(const CInifile *ini, LPCSTR sec)
{
	if (ini->line_exist(sec, "color_animator"))
	{
		SetLanim(READ_IF_EXISTS(ini, r_string, sec, "color_animator", nullptr));
	}
	if (ini->line_exist(sec, "type"))
	{
		u16 val = ini->r_u16(sec, "type");
		switch (val)
		{
		case IRender_Light::LT::DIRECT:
		case IRender_Light::LT::POINT:
		case IRender_Light::LT::SPOT:
			SetType(val);
			break;
		default:
			break;
		}
	}
	SetColor(READ_IF_EXISTS(ini, r_fcolor, sec, "color", Fcolor().set(0.0F, 0.0F, 0.0F, 0.0F)));
	SetRange(READ_IF_EXISTS(ini, r_float, sec, "range", 0.0F));
	SetCone(deg2rad(READ_IF_EXISTS(ini, r_float, sec, "spot_angle", 0.0F)));
	SetTexture(READ_IF_EXISTS(ini, r_string, sec, "spot_texture", nullptr));

	SetGlowRange(READ_IF_EXISTS(ini, r_float, sec, "glow_radius", 0.0F));
	SetGlowTexture(READ_IF_EXISTS(ini, r_string, sec, "glow_texture", nullptr));

	IKinematics *K = O->Visual()->dcast_PKinematics();
	m_light_bid = ini->line_exist(sec, "light_bone") ? K->LL_BoneID(ini->r_string(sec, "light_bone")) : BI_NONE;
	VERIFY(m_light_bid != BI_NONE);
	m_guide_bid = ini->line_exist(sec, "guide_bone") ? K->LL_BoneID(ini->r_string(sec, "guide_bone")) : BI_NONE;
	if (m_guide_bid != BI_NONE)
	{
		K->LL_SetBoneVisible(m_guide_bid, FALSE, TRUE);
	}
}

void SProjectorLight::UpdateCL()
{
	if (m_lanim)
	{
		int frame;
		u32 clr = m_lanim->CalculateBGR(Device.fTimeGlobal, frame);

		Fcolor fclr;
		fclr.set((float)color_get_B(clr), (float)color_get_G(clr), (float)color_get_R(clr), 1.f);
		fclr.mul_rgb(m_brightness / 255.0F);
		m_light->set_color(fclr);
		m_glow->set_color(fclr);
	}

	Fmatrix xfm = Fmatrix().mul_43(O->XFORM(), O->Visual()->dcast_PKinematics()->LL_GetTransform(m_light_bid));
	m_light->set_rotation(xfm.k, xfm.i);
	m_light->set_position(xfm.c);
	m_glow->set_position(xfm.c);
	m_glow->set_direction(xfm.k);
}

void SProjectorLight::Switch(bool val)
{
	if (m_light->get_active() != val)
	{
		m_light->set_active(val);
		m_glow->set_active(val);
		if (m_guide_bid != BI_NONE)
		{
			O->Visual()->dcast_PKinematics()->LL_SetBoneVisible(m_guide_bid, (val) ? TRUE : FALSE, TRUE);
		}
	}
}

void SProjectorLight::SetLanim(LPCSTR str)
{
	m_lanim = LALib.FindItem(str);
}

void SProjectorLight::SetType(int type)
{
	m_light->set_type((IRender_Light::LT)type);
}

void SProjectorLight::SetCone(float val)

{
	m_light->set_cone(val);
}

void SProjectorLight::SetColor(Fcolor clr)

{
	m_brightness = clr.intensity();
	m_light->set_color(clr);
	m_glow->set_color(clr);
}

IC void SProjectorLight::SetRange(float val)

{
	m_light->set_range(val);
}

void SProjectorLight::SetTexture(LPCSTR str)

{
	if (str && strlen(str))
	{
		m_light->set_texture(str);
	}
}

void SProjectorLight::SetGlowRange(float val)

{
	m_glow->set_radius(val);
}

void SProjectorLight::SetGlowTexture(LPCSTR str)

{
	if (str && strlen(str))
	{
		m_glow->set_texture(str);
	}
}

/*----------------------------------------------------------------------------------------------------
	SProjectorControl
----------------------------------------------------------------------------------------------------*/
SProjectorControl::SProjectorControl(CProjector *obj, LPCSTR sec)
{
	m_sec._set(sec);
	O = obj;

	m_rotate_x_bone = BI_NONE;
	m_rotate_y_bone = BI_NONE;
	m_rotate_x_speed = 0.0F;
	m_rotate_y_speed = 0.0F;
	m_tgt_x_rot = 0.0F;
	m_tgt_y_rot = 0.0F;
	m_cur_x_rot = 0.0F;
	m_cur_y_rot = 0.0F;
	m_bind_x_rot = 0.0F;
	m_bind_y_rot = 0.0F;
	m_lim_x_rot.set(0.0F, 0.0F);
	m_lim_y_rot.set(0.0F, 0.0F);
	m_desire_dir.set(0.0F, 0.0F, 0.0F);
	m_desire_ang.set(0.0F, 0.0F, 0.0F);
	m_enable_ang = false;
}

SProjectorControl::~SProjectorControl()
{
}

bool SProjectorControl::IsSection(LPCSTR sec)
{
	return sec && strcmp(sec, m_sec.c_str());
}

void SProjectorControl::Load(const CInifile *ini, LPCSTR sec)
{
	IKinematics *K = O->Visual()->dcast_PKinematics();
	m_rotate_x_bone = K->LL_BoneID(ini->r_string(sec, "rotate_x_bone"));
	VERIFY(m_rotate_x_bone != BI_NONE);
	m_rotate_y_bone = K->LL_BoneID(ini->r_string(sec, "rotate_y_bone"));
	VERIFY(m_rotate_y_bone != BI_NONE);
	m_rotate_x_speed = READ_IF_EXISTS(ini, r_float, sec, "rotate_x_speed", 10.0F);
	m_rotate_y_speed = READ_IF_EXISTS(ini, r_float, sec, "rotate_y_speed", 10.0F);

	CBoneData &bdX = K->LL_GetData(m_rotate_x_bone);
	VERIFY(bdX.IK_data.type == jtJoint);
	m_lim_x_rot.set(bdX.IK_data.limits[0].limit.x, bdX.IK_data.limits[0].limit.y);
	CBoneData &bdY = K->LL_GetData(m_rotate_y_bone);
	VERIFY(bdY.IK_data.type == jtJoint);
	m_lim_y_rot.set(bdY.IK_data.limits[1].limit.x, bdY.IK_data.limits[1].limit.y);

	xr_vector<Fmatrix> matrices;
	K->LL_GetBindTransform(matrices);
	m_i_bind_x_xform.invert(matrices[m_rotate_x_bone]);
	m_i_bind_y_xform.invert(matrices[m_rotate_y_bone]);
	m_bind_x_rot = matrices[m_rotate_x_bone].k.getP();
	m_bind_y_rot = matrices[m_rotate_y_bone].k.getH();
}

void SProjectorControl::UpdateCL()
{
	Fmatrix XFi = Fmatrix().invert(O->XFORM());
	Fvector dep;
	{
		if (m_enable_ang)
		{
			dep.setHP(m_desire_ang.x, m_desire_ang.y);
		}
		else
		{
			XFi.transform_dir(dep, m_desire_dir);
		}
		m_i_bind_y_xform.transform_dir(dep);
		dep.normalize();
		m_tgt_x_rot = angle_normalize_signed(m_bind_x_rot - dep.getP());
		clamp(m_tgt_x_rot, -m_lim_x_rot.y, -m_lim_x_rot.x);
	}
	{
		if (m_enable_ang)
		{
			dep.setHP(m_desire_ang.x, m_desire_ang.y);
		}
		else
		{
			XFi.transform_dir(dep, m_desire_dir);
		}
		m_i_bind_y_xform.transform_dir(dep);
		dep.normalize();
		m_tgt_y_rot = angle_normalize_signed(m_bind_y_rot - dep.getH());
		ClampRotationHorz(m_tgt_y_rot, m_cur_y_rot, -m_lim_y_rot.y, -m_lim_y_rot.x);
	}
	m_cur_x_rot = angle_inertion_var(m_cur_x_rot, m_tgt_x_rot, m_rotate_x_speed, m_rotate_x_speed, PI, Device.fTimeDelta);
	m_cur_y_rot = angle_inertion_var(m_cur_y_rot, m_tgt_y_rot, m_rotate_y_speed, m_rotate_y_speed, PI, Device.fTimeDelta);
}

void SProjectorControl::SetBoneCallbacks(bool enable)
{
	if (enable)
	{
		CBoneInstance &biX = smart_cast<IKinematics *>(O->Visual())->LL_GetBoneInstance(m_rotate_x_bone);
		biX.set_callback(bctCustom, BoneCallbackX, this);
		CBoneInstance &biY = smart_cast<IKinematics *>(O->Visual())->LL_GetBoneInstance(m_rotate_y_bone);
		biY.set_callback(bctCustom, BoneCallbackY, this);
	}
	else
	{
		CBoneInstance &biX = smart_cast<IKinematics *>(O->Visual())->LL_GetBoneInstance(m_rotate_x_bone);
		biX.set_callback(bctPhysics, O->PPhysicsShell()->GetBonesCallback(), O->PPhysicsShell()->get_Element(m_rotate_x_bone));
		CBoneInstance &biY = smart_cast<IKinematics *>(O->Visual())->LL_GetBoneInstance(m_rotate_y_bone);
		biY.set_callback(bctPhysics, O->PPhysicsShell()->GetBonesCallback(), O->PPhysicsShell()->get_Element(m_rotate_y_bone));
	}
}

void SProjectorControl::ClampRotationHorz(float &tgt_val, const float &cur_val, const float &lim_min, const float &lim_max)
{
	/* Rotating limit must be lesser than 180 in both direction. */
	if (abs(lim_min) < PI || abs(lim_max) < PI)
	{
		/* Target is outside of rotating limit. Clamp to the closest limit. */
		if (tgt_val < lim_min || tgt_val > lim_max)
		{
			if (angle_difference(tgt_val, lim_min) < angle_difference(tgt_val, lim_max))
				tgt_val = lim_min;
			else
				tgt_val = lim_max;
		}
		/* If the rotation to reach tgt_val from cur_val crosses 180 degrees in the back, make it swings around 0. */
		if (abs(tgt_val - cur_val) >= PI)
		{
			tgt_val = 0.0F;
		}
	}
}

void _BCL SProjectorControl::BoneCallbackX(CBoneInstance *B)
{
	SProjectorControl *P = static_cast<SProjectorControl *>(B->callback_param());
	B->mTransform.mulB_43(Fmatrix().rotateX(P->m_cur_x_rot));
}

void _BCL SProjectorControl::BoneCallbackY(CBoneInstance *B)
{
	SProjectorControl *P = static_cast<SProjectorControl *>(B->callback_param());
	B->mTransform.mulB_43(Fmatrix().rotateY(P->m_cur_y_rot));
}

void SProjectorControl::SetParam(int id, Fvector val)
{
	switch (id)
	{
	case CProjector::eDesiredPos:
	{
		Fvector vec = Fmatrix().mul_43(O->XFORM(), O->Visual()->dcast_PKinematics()->LL_GetTransform(m_rotate_y_bone)).c;
		m_desire_dir.sub(val, vec).normalize_safe();
		m_enable_ang = false;
		break;
	}
	case CProjector::eDesiredDir:
		m_desire_dir.set(val).normalize_safe();
		m_enable_ang = false;
		break;
	case CProjector::eDesiredAng:
		m_desire_ang.set(val.x, val.y, 0.0F);
		m_enable_ang = true;
		break;
	}
}
#endif