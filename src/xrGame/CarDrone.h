#pragma once
#include "stdafx.h"
#ifdef CAR_NEW
#include "Car.h"

class CCarDrone
{
private:
    struct SCarDroneBone
    {
        u16 bid;
        CPhysicsElement* E;
        CPhysicsJoint* J;
        bool clock;
        bool force;
        SCarDroneBone();
    };

    CCar* car;

    xr_vector<SCarDroneBone> m_drive_bones;
    xr_vector<SCarDroneBone> m_rotor_bones;
    float m_rotor_force;
    float m_rotor_speed;

    bool m_control_press_ele_up; /* Up */
    bool m_control_press_ele_dw; /* Down */
    bool m_control_press_pit_fs; /* Move forward */
    bool m_control_press_pit_bs; /* Move backward */
    bool m_control_press_rol_rs; /* Strafe right */
    bool m_control_press_rol_ls; /* Strafe left */
    bool m_control_press_yaw_rs; /* Turn right */
    bool m_control_press_yaw_ls; /* Turn left */

    u16 m_control_ele; /* Elevating */
    u16 m_control_pit; /* Pitch */
    u16 m_control_rol; /* Roll */
    u16 m_control_yaw; /* Yaw */

    float m_control_ele_force;
    float m_control_pit_force;
    float m_control_rol_force;
    float m_control_yaw_force;

    /* Make the drone comes to a stop faster. */
    float m_damping_velocity;
    /* Reduce wobbling when start/stop moving (x,z). Also yaw rotation comes to a stop faster (y). */
    Fvector m_damping_rotation;
    /* Simulate additional payload reducing control responsiveness.*/
    float m_power_efficiency;

    /* Variables for PhDataUpdate() */
    Fvector vec;
    Fvector cur_velocity;
    Fvector new_velocity;
    Fvector cur_angular;
    Fvector new_angular;

public:
    enum eControlEle
    {
        eControlEle_NA = 0,
        eControlEle_UP,
        eControlEle_DW,
    };
    enum eControlYaw
    {
        eControlYaw_NA = 0,
        eControlYaw_RS,
        eControlYaw_LS,
    };
    enum eControlPit
    {
        eControlPit_NA = 0,
        eControlPit_FS,
        eControlPit_BS,
    };
    enum eControlRol
    {
        eControlRol_NA = 0,
        eControlRol_RS,
        eControlRol_LS,
    };

    CCarDrone(CCar* obj);
    ~CCarDrone();

    void Load(LPCSTR section);
    bool attach_Actor(CGameObject* actor);
    void detach_Actor();
    void PhDataUpdate(float step);
    void RotorUpdate();

    void ControlReset();
    void ControlPressEleUp(bool status);
    void ControlPressEleDw(bool status);
    void ControlPressYawRs(bool status);
    void ControlPressYawLs(bool status);
    void ControlPressPitFs(bool status);
    void ControlPressPitBs(bool status);
    void ControlPressRolRs(bool status);
    void ControlPressRolLs(bool status);

    u16 GetControlEle() { return m_control_ele; };
    u16 GetControlYaw() { return m_control_yaw; };
    u16 GetControlPit() { return m_control_pit; };
    u16 GetControlRol() { return m_control_rol; };
    void SetControlEle(u16 val) { m_control_ele = val; };
    void SetControlYaw(u16 val) { m_control_yaw = val; };
    void SetControlPit(u16 val) { m_control_pit = val; };
    void SetControlRol(u16 val) { m_control_rol = val; };
    float GetControlEleScale() { return m_control_ele_force; };
    float GetControlYawScale() { return m_control_yaw_force; };
    float GetControlPitScale() { return m_control_pit_force; };
    float GetControlRolScale() { return m_control_rol_force; };
    void SetControlEleScale(float val) { m_control_ele_force = val; };
    void SetControlYawScale(float val) { m_control_yaw_force = val; };
    void SetControlPitScale(float val) { m_control_pit_force = val; };
    void SetControlRolScale(float val) { m_control_rol_force = val; };

    float GetPowerEfficiency() { return m_power_efficiency; };
    void SetPowerEfficiency(float val) { m_power_efficiency = val; };
};
#endif