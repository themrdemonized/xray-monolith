#include "stdafx.h"
#ifdef CAR_NEW
#include "Car.h"
#include "CarDrone.h"

#include "Level.h"
#include "../Include/xrRender/Kinematics.h"
#include "../xrphysics/IPHWorld.h"

#include "script_game_object.h"
#include "CameraFirstEye.h"
#include "cameralook.h"
#include "xr_level_controller.h"

float DAMPING_ESP_VEL = 0.01;
float DAMPING_ESP_ANG = deg2rad(1.0);

CCarDrone::SCarDroneBone::SCarDroneBone()
{
    bid = BI_NONE;
    E = nullptr;
    J = nullptr;
    clock = false;
    force = false;
}

CCarDrone::CCarDrone(CCar* obj)
{
    car = obj;

    m_rotor_force = 0.0F;
    m_rotor_speed = 0.0F;

    m_control_press_ele_up = false;
    m_control_press_ele_dw = false;
    m_control_press_yaw_rs = false;
    m_control_press_yaw_ls = false;
    m_control_press_pit_fs = false;
    m_control_press_pit_bs = false;
    m_control_press_rol_rs = false;
    m_control_press_rol_ls = false;

    m_control_ele = eControlEle_NA;
    m_control_yaw = eControlYaw_NA;
    m_control_pit = eControlPit_NA;
    m_control_rol = eControlRol_NA;

    m_control_ele_force = 0.0F;
    m_control_yaw_force = 0.0F;
    m_control_pit_force = 0.0F;
    m_control_rol_force = 0.0F;

    m_damping_velocity = 0.0F;
    m_damping_rotation.set(0.0F, 0.0F, 0.0F);

    m_power_efficiency = 1.0F;

    Load(car->cNameSect_str());
}

CCarDrone::~CCarDrone()
{
    m_rotor_bones.clear();
    m_drive_bones.clear();
}

void CCarDrone::Load(LPCSTR section)
{
    m_control_ele_force = READ_IF_EXISTS(pSettings, r_float, section, "control_ele_force", 0.0F);
    m_control_pit_force = READ_IF_EXISTS(pSettings, r_float, section, "control_pit_force", 0.0F);
    m_control_rol_force = READ_IF_EXISTS(pSettings, r_float, section, "control_rol_force", 0.0F);
    m_control_yaw_force = deg2rad(READ_IF_EXISTS(pSettings, r_float, section, "control_yaw_force", 0.0F));

    m_damping_velocity = READ_IF_EXISTS(pSettings, r_float, section, "damping_velocity", 0.0F);
    m_damping_rotation.set(READ_IF_EXISTS(pSettings, r_fvector3, section, "damping_rotation", m_damping_rotation));
    m_damping_rotation.x = deg2rad(m_damping_rotation.x);
    m_damping_rotation.y = deg2rad(m_damping_rotation.y);
    m_damping_rotation.z = deg2rad(m_damping_rotation.z);

    IKinematics* K = car->Visual()->dcast_PKinematics();
    CInifile* ini = K->LL_UserData();
    const LPCSTR cfg = "drone_definition";

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
                m_rotor_bones.push_back(SCarDroneBone());
                SCarDroneBone& I = m_rotor_bones.back();
                I.bid = bone_id;
                I.E = car->m_pPhysicsShell->get_Element(bone_id);
                I.J = car->m_pPhysicsShell->get_Joint(bone_id);

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
                        if (strcmp(key, "clock") == 0)
                        {
                            I.clock = strcmp(val, "true") == 0;
                        }
                    }
                }

                I.E->set_DynamicLimits(default_l_limit, default_w_limit * 1000.F);
            }
        }
        R_ASSERT3(m_rotor_bones.size(), __FUNCTION__ " No m_rotor_bones", car->cNameSect_str());
    }
    {
        m_drive_bones.clear();
        LPCSTR str = READ_IF_EXISTS(ini, r_string, cfg, "drive_bones", NULL);
        int n = _GetItemCount(str);
        string64 bone_name;
        for (int k = 0; k < n; ++k)
        {
            memset(bone_name, 0, sizeof(bone_name));
            _GetItem(str, k, bone_name);
            u16 bone_id = K->LL_BoneID(bone_name);
            if (bone_id != BI_NONE)
            {
                m_drive_bones.push_back(SCarDroneBone());
                SCarDroneBone& I = m_drive_bones.back();
                I.bid = bone_id;
                I.E = car->m_pPhysicsShell->get_Element(bone_id);
            }
        }
        R_ASSERT3(m_drive_bones.size(), __FUNCTION__ " No m_drive_bones", car->cNameSect_str());
    }

    m_rotor_force = READ_IF_EXISTS(ini, r_float, cfg, "rotor_force", 0.0F);
    m_rotor_speed = READ_IF_EXISTS(ini, r_float, cfg, "rotor_speed", 0.0F);
}

bool CCarDrone::attach_Actor(CGameObject* actor)
{
    ControlReset();
    return true;
}

void CCarDrone::detach_Actor()
{
    ControlReset();
}

void CCarDrone::PhDataUpdate(float step)
{
    /* Only update physic. Don't calculate bones. */
    if (car->m_pPhysicsShell == nullptr)
        return;

    RotorUpdate();
    if (car->b_engine_on && m_drive_bones.size())
    {
        float mass = car->m_pPhysicsShell->getMass();

        /* Hovering in mid air. */
        {
            float force = mass * physics_world()->Gravity() / m_drive_bones.size();
            vec.set(0.0F, 1.0F, 0.0F).mul(force);
            for (auto& I : m_drive_bones)
            {
                I.E->applyForce(vec.x, vec.y, vec.z);
            }
        }

        /* Movement damping. */
        if (m_damping_velocity > EPS_L)
        {
            if (m_control_ele == eControlEle_NA && m_control_pit == eControlPit_NA && m_control_rol == eControlRol_NA)
            {
                car->m_pPhysicsShell->get_LinearVel(cur_velocity);
                if (cur_velocity.magnitude() > DAMPING_ESP_VEL)
                {
                    float force = mass * __min(cur_velocity.magnitude(), m_damping_velocity);
                    new_velocity.invert(cur_velocity).normalize_safe().mul(force);
                    car->m_pPhysicsShell->applyForce(new_velocity.x, new_velocity.y, new_velocity.z);
                }
            }
        }

        /* Rotation damping. */
        {
            car->m_pPhysicsShell->get_AngularVel(cur_angular);
            new_angular.set(0.0F, 0.0F, 0.0F);
            bool apply = false;
            if (m_damping_rotation.x > EPS_L && _abs(cur_angular.x) > DAMPING_ESP_ANG)
            {
                new_angular.x = mass * __min(_abs(cur_angular.x), m_damping_rotation.x) * (cur_angular.x < 0 ? 1 : -1);
                apply = true;
            }
            if (m_damping_rotation.y > EPS_L && _abs(cur_angular.y) > DAMPING_ESP_ANG && m_control_yaw == eControlYaw_NA)
            {
                new_angular.y = mass * __min(_abs(cur_angular.y), m_damping_rotation.y) * (cur_angular.y < 0 ? 1 : -1);
                apply = true;
            }
            if (m_damping_rotation.z > EPS_L && _abs(cur_angular.z) > DAMPING_ESP_ANG)
            {
                new_angular.z = mass * __min(_abs(cur_angular.z), m_damping_rotation.z) * (cur_angular.z < 0 ? 1 : -1);
                apply = true;
            }
            if (apply)
            {
                car->m_pPhysicsShell->applyTorque(new_angular.x, new_angular.y, new_angular.z);
            }
        }

        switch (m_control_ele)
        {
        case eControlEle_UP:
        case eControlEle_DW:
        {
            float force = mass * GetPowerEfficiency() * m_control_ele_force / m_drive_bones.size();
            vec.set(0.0F, 1.0F, 0.0F).mul((m_control_ele == eControlEle_UP) ? force : -force);
            for (auto& I : m_drive_bones)
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
            float force = mass * GetPowerEfficiency() * m_control_pit_force / m_drive_bones.size();
            vec.set(0.0F, 0.0F, 1.0F).mul((m_control_pit == eControlPit_FS) ? force : -force);
            for (auto& I : m_drive_bones)
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
            float force = mass * GetPowerEfficiency() * m_control_rol_force / m_drive_bones.size();
            vec.set(1.0F, 0.0F, 0.0F).mul((m_control_rol == eControlRol_RS) ? force : -force);
            for (auto& I : m_drive_bones)
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
            float force = mass * GetPowerEfficiency() * m_control_yaw_force;
            vec.set(0.0F, 1.0F, 0.0F).mul((m_control_yaw == eControlYaw_RS) ? force : -force);
            car->m_pPhysicsShell->applyRelTorque(vec.x, vec.y, vec.z);
            break;
        }
        }
    }
}

void CCarDrone::RotorUpdate()
{
    if (car->b_engine_on)
    {
        for (auto& I : m_rotor_bones)
        {
            if (I.force != true)
            {
                I.force = true;
                I.J->SetForceAndVelocity(m_rotor_force, m_rotor_speed * PI_MUL_2 * (I.clock ? 1 : -1), 1);
            }
        }
    }
    else
    {
        for (auto& I : m_rotor_bones)
        {
            if (I.force == true)
            {
                I.force = false;
                I.J->SetForceAndVelocity(0, 0, 1);
            }
        }
    }
}

void CCarDrone::ControlReset()
{
    ControlPressEleUp(false);
    ControlPressEleDw(false);
    ControlPressYawRs(false);
    ControlPressYawLs(false);
    ControlPressPitFs(false);
    ControlPressPitBs(false);
    ControlPressRolRs(false);
    ControlPressRolLs(false);
    m_control_ele = eControlEle_NA;
    m_control_yaw = eControlYaw_NA;
    m_control_pit = eControlPit_NA;
    m_control_rol = eControlPit_NA;
}

void CCarDrone::ControlPressEleUp(bool status)
{
    m_control_press_ele_up = status;
    if (status)
    {
        m_control_ele = (m_control_press_ele_dw) ? eControlEle_NA : eControlEle_UP;
    }
    else
    {
        m_control_ele = (m_control_press_ele_dw) ? eControlEle_DW : eControlEle_NA;
    }
}

void CCarDrone::ControlPressEleDw(bool status)
{
    m_control_press_ele_dw = status;
    if (status)
    {
        m_control_ele = (m_control_press_ele_up) ? eControlEle_NA : eControlEle_DW;
    }
    else
    {
        m_control_ele = (m_control_press_ele_up) ? eControlEle_UP : eControlEle_NA;
    }
}

void CCarDrone::ControlPressYawRs(bool status)
{
    m_control_press_yaw_rs = status;
    if (status)
    {
        m_control_yaw = (m_control_press_yaw_ls) ? eControlYaw_NA : eControlYaw_RS;
    }
    else
    {
        m_control_yaw = (m_control_press_yaw_ls) ? eControlYaw_LS : eControlYaw_NA;
    }
}

void CCarDrone::ControlPressYawLs(bool status)
{
    m_control_press_yaw_ls = status;
    if (status)
    {
        m_control_yaw = (m_control_press_yaw_rs) ? eControlYaw_NA : eControlYaw_LS;
    }
    else
    {
        m_control_yaw = (m_control_press_yaw_rs) ? eControlYaw_RS : eControlYaw_NA;
    }
}

void CCarDrone::ControlPressPitFs(bool status)
{
    m_control_press_pit_fs = status;
    if (status)
    {
        m_control_pit = (m_control_press_pit_bs) ? eControlPit_NA : eControlPit_FS;
    }
    else
    {
        m_control_pit = (m_control_press_pit_bs) ? eControlPit_BS : eControlPit_NA;
    }
}

void CCarDrone::ControlPressPitBs(bool status)
{
    m_control_press_pit_bs = status;
    if (status)
    {
        m_control_pit = (m_control_press_pit_fs) ? eControlPit_NA : eControlPit_BS;
    }
    else
    {
        m_control_pit = (m_control_press_pit_fs) ? eControlPit_FS : eControlPit_NA;
    }
}

void CCarDrone::ControlPressRolRs(bool status)
{
    m_control_press_rol_rs = status;
    if (status)
    {
        m_control_rol = (m_control_press_rol_ls) ? eControlRol_NA : eControlRol_RS;
    }
    else
    {
        m_control_rol = (m_control_press_rol_ls) ? eControlRol_LS : eControlRol_NA;
    }
}

void CCarDrone::ControlPressRolLs(bool status)
{
    m_control_press_rol_ls = status;
    if (status)
    {
        m_control_rol = (m_control_press_rol_rs) ? eControlRol_NA : eControlRol_LS;
    }
    else
    {
        m_control_rol = (m_control_press_rol_rs) ? eControlRol_RS : eControlRol_NA;
    }
}

/*----------------------------------------------------------------------------------------------------
    IR
----------------------------------------------------------------------------------------------------*/
void CCar::CCarDrone_OnKeyboardPress(int dik)
{
    switch (dik)
    {
        /* Movement. */
    case kACCEL:
    case kSPRINT_TOGGLE:
        m_car_drone->ControlPressEleUp(true);
        break;
    case kCROUCH:
        m_car_drone->ControlPressEleDw(true);
        break;
    case kL_LOOKOUT:
        m_car_drone->ControlPressYawLs(true);
        break;
    case kR_LOOKOUT:
        m_car_drone->ControlPressYawRs(true);
        break;
    case kFWD:
        m_car_drone->ControlPressPitFs(true);
        break;
    case kBACK:
        m_car_drone->ControlPressPitBs(true);
        break;
    case kL_STRAFE:
        m_car_drone->ControlPressRolLs(true);
        break;
    case kR_STRAFE:
        m_car_drone->ControlPressRolRs(true);
        break;
    };
}

void CCar::CCarDrone_OnKeyboardRelease(int dik)
{
    switch (dik)
    {
        /* Movement. */
    case kACCEL:
    case kSPRINT_TOGGLE:
        m_car_drone->ControlPressEleUp(false);
        break;
    case kCROUCH:
        m_car_drone->ControlPressEleDw(false);
        break;
    case kL_LOOKOUT:
        m_car_drone->ControlPressYawLs(false);
        break;
    case kR_LOOKOUT:
        m_car_drone->ControlPressYawRs(false);
        break;
    case kFWD:
        m_car_drone->ControlPressPitFs(false);
        break;
    case kBACK:
        m_car_drone->ControlPressPitBs(false);
        break;
    case kL_STRAFE:
        m_car_drone->ControlPressRolLs(false);
        break;
    case kR_STRAFE:
        m_car_drone->ControlPressRolRs(false);
        break;
        /* Action. */
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

void CCar::CCarDrone_OnKeyboardHold(int dik)
{
}

float CCar::CCarDrone_GetPowerEfficiency()
{
    return (m_car_drone) ? m_car_drone->GetPowerEfficiency() : 0.0F;
}

void CCar::CCarDrone_SetPowerEfficiency(int val)
{
    if (m_car_drone)
    {
        m_car_drone->SetPowerEfficiency(val);
    }
}
#endif