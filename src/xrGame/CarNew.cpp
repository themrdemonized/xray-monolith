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

void CCar::SetUseAction(LPCSTR txt)
{
	m_sUseAction._set(txt);
}

u16 CCar::GetCameraBone()
{
    if (IsScopeEnable())
    {
        return m_scopes.at(m_scope_active).camera_bone;
    }
    return (m_zoom_status) ? m_camera_bone_aim : m_camera_bone_def;
}

float CCar::GetZoomFactor()
{
    if (IsScopeEnable())
    {
        return m_scopes.at(m_scope_active).zoom_factor;
    }
    return (m_zoom_status) ? m_zoom_factor_aim : m_zoom_factor_def;
}

void CCar::SetScopeActive(u16 val)
{
    if (0 <= val && val < m_scopes.size())
    {
        m_scope_active = val;
    }
}

void CCar::ScopeOnMouseWheel(int direction)
{
    if (direction > 0)
    {
        SetScopeActive(m_scope_active + 1);
    }
    else
    {
        SetScopeActive(m_scope_active - 1);
    }
}

/*------------------------------------------------------------------------------------------------------------------------
    SVisualCamera
------------------------------------------------------------------------------------------------------------------------*/
CCar::SVisualCamera::SVisualCamera(CCar* obj)
{
    m_car = obj;
    m_enable = false;
    m_active = false;

    m_snap = false;
    m_rotate_x_bone = BI_NONE;
    m_rotate_y_bone = BI_NONE;
    m_rotate_x_speed = 0.0F;
    m_rotate_y_speed = 0.0F;
    m_cur_x_rot = 0.0F;
    m_cur_y_rot = 0.0F;
    m_tgt_x_rot = 0.0F;
    m_tgt_y_rot = 0.0F;
}

CCar::SVisualCamera::~SVisualCamera()
{

}

void CCar::SVisualCamera::net_Spawn(CSE_Abstract* DC)
{
    IKinematics* K = Car()->Visual()->dcast_PKinematics();
    CInifile* ini = K->LL_UserData();
    const LPCSTR def = "visual_camera_definition";
    if (!READ_IF_EXISTS(ini, r_bool, def, "enable", FALSE))
        return;

    m_enable = true;
    m_rotate_x_bone = K->LL_BoneID(ini->r_string(def, "rotate_x_bone"));
    VERIFY(m_rotate_x_bone != BI_NONE);
    m_rotate_y_bone = K->LL_BoneID(ini->r_string(def, "rotate_y_bone"));
    VERIFY(m_rotate_y_bone != BI_NONE);
    m_rotate_x_speed = deg2rad(READ_IF_EXISTS(ini, r_float, def, "rotate_x_speed", 20.0F));
    m_rotate_y_speed = deg2rad(READ_IF_EXISTS(ini, r_float, def, "rotate_y_speed", 20.0F));
    m_snap = !!READ_IF_EXISTS(ini, r_bool, def, "snap", FALSE);

    CBoneData& bdX = K->LL_GetData(m_rotate_x_bone);
    VERIFY(bdX.IK_data.type == jtJoint);
    m_lim_x_rot.set(bdX.IK_data.limits[0].limit.x, bdX.IK_data.limits[0].limit.y);
    CBoneData& bdY = K->LL_GetData(m_rotate_y_bone);
    VERIFY(bdY.IK_data.type == jtJoint);
    m_lim_y_rot.set(bdY.IK_data.limits[1].limit.x, bdY.IK_data.limits[1].limit.y);
}

bool CCar::SVisualCamera::attach_Actor(CGameObject* obj)
{
    if (IsEnable() == false)
        return true;
    Activate(true);
    SetBoneCallbacks(true);
    return true;
}

void CCar::SVisualCamera::detach_Actor()
{
    if (IsEnable() == false)
        return;
    Activate(false);
    SetBoneCallbacks(false);
}

void CCar::SVisualCamera::VisualUpdate(float fov)
{
    if (IsEnable() == false)
        return;
    if (IsActive() == false)
        return;
    UpdateDir();
    UpdateCam();
}

void CCar::SVisualCamera::UpdateDir()
{
    {
        m_tgt_x_rot = angle_normalize_signed(-m_desire_ang.x);
        clamp(m_tgt_x_rot, -m_lim_x_rot.y, -m_lim_x_rot.x);
    }
    {
        m_tgt_y_rot = angle_normalize_signed(-m_desire_ang.y);
        clamp(m_tgt_y_rot, -m_lim_y_rot.y, -m_lim_y_rot.x);
    }

    if (Snap())
    {
        m_cur_x_rot = m_tgt_x_rot;
        m_cur_y_rot = m_tgt_y_rot;
    }
    else
    {
        m_cur_x_rot = angle_inertion_var(m_cur_x_rot, m_tgt_x_rot, m_rotate_x_speed, m_rotate_x_speed, PI, Device.fTimeDelta);
        m_cur_y_rot = angle_inertion_var(m_cur_y_rot, m_tgt_y_rot, m_rotate_y_speed, m_rotate_y_speed, PI, Device.fTimeDelta);
    }
}

void CCar::SVisualCamera::UpdateCam()
{
    CCameraBase* cam = Car()->Camera();
    if (cam->tag == ectFirst)
    {
        cam->pitch = m_cur_x_rot;
        cam->yaw = m_cur_y_rot;
    }
}

void _BCL CCar::SVisualCamera::BoneCallbackX(CBoneInstance* B)
{
    CCar::SVisualCamera* P = static_cast<CCar::SVisualCamera*>(B->callback_param());
    B->mTransform.mulB_43(Fmatrix().rotateX(P->m_cur_x_rot));
}

void _BCL CCar::SVisualCamera::BoneCallbackY(CBoneInstance* B)
{
    CCar::SVisualCamera* P = static_cast<CCar::SVisualCamera*>(B->callback_param());
    B->mTransform.mulB_43(Fmatrix().rotateY(P->m_cur_y_rot));
}

void CCar::SVisualCamera::SetBoneCallbacks(bool val)
{
    CBoneInstance& biX = Car()->Visual()->dcast_PKinematics()->LL_GetBoneInstance(m_rotate_x_bone);
    CBoneInstance& biY = Car()->Visual()->dcast_PKinematics()->LL_GetBoneInstance(m_rotate_y_bone);
    if (val)
    {
        biX.set_callback(bctCustom, BoneCallbackX, this);
        biY.set_callback(bctCustom, BoneCallbackY, this);
    }
    else
    {
        biX.set_callback(bctPhysics, Car()->PPhysicsShell()->GetBonesCallback(), Car()->PPhysicsShell()->get_Element(m_rotate_x_bone));
        biY.set_callback(bctPhysics, Car()->PPhysicsShell()->GetBonesCallback(), Car()->PPhysicsShell()->get_Element(m_rotate_y_bone));
    }
}

void CCar::SVisualCamera::Activate(bool val)
{
    if (IsEnable() == false)
        return;
    if (m_active != val)
    {
        m_active = val;
        SetBoneCallbacks(m_active);
    }
}

void CCar::SVisualCamera::SetDesireAngle(Fvector vec)
{
    m_desire_ang.set(vec.x, vec.y, 0);
}

void CCar::SVisualCamera::OnMouseMove(int dx, int dy)
{
    float scale = (Car()->Camera()->f_fov / g_fov) * psMouseSens * psMouseSensScale / 50.0F;
    if (dx)
    {
        float d = float(dx) * scale;
        m_desire_ang.y -= d;
        clamp(m_desire_ang.y, m_lim_y_rot.x, m_lim_y_rot.y);
    }
    if (dx)
    {
        float d = float(dy) * scale * (psMouseInvert.test(1) ? -1 : 1) * psMouseSensVerticalK * 0.75F;
        m_desire_ang.x -= d;
        clamp(m_desire_ang.x, m_lim_x_rot.x, m_lim_x_rot.y);
    }
    if (Snap())
    {
        UpdateCam();
    }
}
#endif