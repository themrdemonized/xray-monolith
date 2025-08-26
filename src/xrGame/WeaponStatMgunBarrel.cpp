#include "stdafx.h"
#ifdef STATIONARYMGUN_NEW
#include "WeaponStatMgun.h"
#include "../Include/xrRender/Kinematics.h"
#include "../xrphysics/PhysicsShell.h"

SStmBarrel::SStmBarrel(CWeaponStatMgun *stm, LPCSTR name)
{
    m_stm = stm;
    m_name._set(name);

    fShotTimeCounter = 0.0F;
    fOneShotTime = 0.0F;
    iAmmoElapsed = 0;

    m_fire_bid = BI_NONE;
    m_fire_pos.set(0, 0, 0);
    m_fire_dir.set(0, 0, 1);
    m_drop_bid = BI_NONE;
    m_bLightShotEnabled = true;
    m_pFlameParticles = nullptr;
}

SStmBarrel::~SStmBarrel()
{
}

void SStmBarrel::reinit()
{
    m_pFlameParticles = nullptr;
}

void SStmBarrel::Load(LPCSTR section)
{
    if (pSettings->line_exist(section, "rpm_range"))
    {
        Fvector2 rpm_range = pSettings->r_fvector2(section, "rpm_range");
        VERIFY(rpm_range.x > 0.0F);
        VERIFY(rpm_range.y > 0.0F);
        fOneShotTime = 60.0F / ::Random.randF(rpm_range.x, rpm_range.y);
    }
    else
    {
        fOneShotTime = pSettings->r_float(section, "rpm");
        VERIFY(fOneShotTime > 0.0F);
        fOneShotTime = 60.0F / fOneShotTime;
    }

    m_bLightShotEnabled = !READ_IF_EXISTS(pSettings, r_bool, section, "light_disabled", false);
    LoadLights(section, "");
    LoadShellParticles(section, Name());
    LoadFlameParticles(section, Name());
}

BOOL SStmBarrel::net_Spawn(CSE_Abstract *DC)
{
    IKinematics *K = m_stm->Visual()->dcast_PKinematics();
    CInifile *ini = K->LL_UserData();
    m_fire_bid = ini->line_exist(Name(), "fire_bone") ? K->LL_BoneID(ini->r_string(Name(), "fire_bone")) : BI_NONE;
    m_drop_bid = ini->line_exist(Name(), "drop_bone") ? K->LL_BoneID(ini->r_string(Name(), "drop_bone")) : BI_NONE;
    return TRUE;
}

void SStmBarrel::net_Export(NET_Packet &P)
{
    P.w_u16(u16(iAmmoElapsed & 0xffff));
}

void SStmBarrel::net_Import(NET_Packet &P)
{
    iAmmoElapsed = P.r_u16();
}

void SStmBarrel::UpdateEx()
{
    VERIFY3(m_fire_bid != BI_NONE, "Fire bone incorrect.", m_stm->cNameSect_str());
    IKinematics *K = m_stm->Visual()->dcast_PKinematics();
    m_fire_xfm.mul_43(m_stm->XFORM(), K->LL_GetTransform(m_fire_bid));
    m_fire_pos.set(0, 0, 0);
    m_fire_xfm.transform_tiny(m_fire_pos);
    m_fire_dir.set(0, 0, 1);
    m_fire_xfm.transform_dir(m_fire_dir);
}

void SStmBarrel::StartParticles(CParticlesObject *&pParticles, LPCSTR particles_name, const Fvector &pos, const Fvector &vel, bool auto_remove_flag)
{
    if (particles_name == nullptr)
        return;
    if (pParticles != nullptr)
    {
        UpdateParticles(pParticles, pos, vel);
        return;
    }
    pParticles = CParticlesObject::Create(particles_name, (BOOL)auto_remove_flag);
    UpdateParticles(pParticles, pos, vel);
    pParticles->Play(false);
}

void SStmBarrel::StopParticles(CParticlesObject *&pParticles)
{
    if (pParticles == nullptr)
        return;
    pParticles->Stop();
    CParticlesObject::Destroy(pParticles);
}

void SStmBarrel::UpdateParticles(CParticlesObject *&pParticles, const Fvector &pos, const Fvector &vel)
{
    if (!pParticles)
        return;
    pParticles->SetXFORM(Fmatrix().set(m_fire_xfm).translate_over(pos));
    if (!pParticles->IsAutoRemove() && !pParticles->IsLooped() && !pParticles->PSI_alive())
    {
        pParticles->Stop();
        CParticlesObject::Destroy(pParticles);
    }
}

void SStmBarrel::LoadShellParticles(LPCSTR section, LPCSTR prefix)
{
    string256 full_name;

    strconcat(sizeof(full_name), full_name, prefix, "@shell_particles");
    m_sShellParticles._set("");
    if (pSettings->line_exist(section, full_name))
        m_sShellParticles = pSettings->r_string(section, full_name);
    else
        m_sShellParticles = pSettings->r_string(section, "shell_particles");
}

void SStmBarrel::LoadFlameParticles(LPCSTR section, LPCSTR prefix)
{
    string256 full_name;

    strconcat(sizeof(full_name), full_name, prefix, "@flame_particles");
    m_sFlameParticles._set("");
    if (pSettings->line_exist(section, full_name))
        m_sFlameParticles = pSettings->r_string(section, full_name);
    else
        m_sFlameParticles = pSettings->r_string(section, "flame_particles");

    strconcat(sizeof(full_name), full_name, prefix, "@smoke_particles");
    m_sSmokeParticles._set("");
    if (pSettings->line_exist(section, full_name))
        m_sSmokeParticles = pSettings->r_string(section, full_name);
    else
        m_sSmokeParticles = pSettings->r_string(section, "smoke_particles");
}

void SStmBarrel::StartFlameParticles()
{
    if (0 == m_sFlameParticles.size())
        return;
    if (m_pFlameParticles && m_pFlameParticles->IsLooped() && m_pFlameParticles->IsPlaying())
    {
        UpdateFlameParticles();
        return;
    }
    StopFlameParticles();
    m_pFlameParticles = CParticlesObject::Create(m_sFlameParticles.c_str(), FALSE);
    UpdateFlameParticles();
    m_pFlameParticles->Play(false);
}

void SStmBarrel::StopFlameParticles()
{
    if (0 == m_sFlameParticles.size())
        return;
    if (m_pFlameParticles == nullptr)
        return;
    m_pFlameParticles->SetAutoRemove(true);
    m_pFlameParticles->Stop();
    m_pFlameParticles = nullptr;
}

void SStmBarrel::UpdateFlameParticles()
{
    if (m_pFlameParticles == nullptr)
        return;
    m_pFlameParticles->SetXFORM(Fmatrix().set(m_fire_xfm).translate_over(m_fire_pos));
    if (!m_pFlameParticles->IsLooped() && !m_pFlameParticles->IsPlaying() && !m_pFlameParticles->PSI_alive())
    {
        m_pFlameParticles->Stop();
        CParticlesObject::Destroy(m_pFlameParticles);
    }
}

void SStmBarrel::StartSmokeParticles(const Fvector &play_pos, const Fvector &parent_vel)
{
    if (0 == m_sSmokeParticles.size())
        return;
    CParticlesObject *pSmokeParticles = nullptr;
    StartParticles(pSmokeParticles, m_sSmokeParticles.c_str(), play_pos, parent_vel, true);
}

void SStmBarrel::LoadLights(LPCSTR section, LPCSTR prefix)
{
    string256 full_name;
    if (m_bLightShotEnabled)
    {
        Fvector clr = pSettings->r_fvector3(section, strconcat(sizeof(full_name), full_name, prefix, "light_color"));
        light_base_color.set(clr.x, clr.y, clr.z, 1);
        light_base_range = pSettings->r_float(section, strconcat(sizeof(full_name), full_name, prefix, "light_range"));
        light_var_color = pSettings->r_float(section, strconcat(sizeof(full_name), full_name, prefix, "light_var_color"));
        light_var_range = pSettings->r_float(section, strconcat(sizeof(full_name), full_name, prefix, "light_var_range"));
        light_lifetime = pSettings->r_float(section, strconcat(sizeof(full_name), full_name, prefix, "light_time"));
        light_time = -1.f;
    }
}

void SStmBarrel::Light_Create()
{
    light_render = ::Render->light_create();
    if (::Render->get_generation() == IRender_interface::GENERATION_R2)
        light_render->set_shadow(true);
    else
        light_render->set_shadow(false);
}

void SStmBarrel::Light_Destroy()
{
    if (light_render)
        light_render.destroy();
}

void SStmBarrel::Light_Start()
{
    if (light_render == nullptr)
        Light_Create();
    if (Device.dwFrame != light_frame)
    {
        light_frame = Device.dwFrame;
        light_time = light_lifetime;
        light_build_color.set(Random.randFs(light_var_color, light_base_color.r), Random.randFs(light_var_color, light_base_color.g), Random.randFs(light_var_color, light_base_color.b), 1);
        light_build_range = Random.randFs(light_var_range, light_base_range);
    }
}

void SStmBarrel::Light_Render(const Fvector &P)
{
    float light_scale = light_time / light_lifetime;
    R_ASSERT(light_render);
    light_render->set_position(P);
    light_render->set_color(light_build_color.r * light_scale, light_build_color.g * light_scale, light_build_color.b * light_scale);
    light_render->set_range(fmaxf(.1f, (light_build_range * light_scale)));
    if (!light_render->get_active())
    {
        light_render->set_active(true);
    }
}

void SStmBarrel::RenderLight()
{
    if (light_render && light_time > 0)
    {
        Light_Render(m_fire_pos);
    }
}

void SStmBarrel::UpdateLight()
{
    if (light_render && light_time > 0)
    {
        light_time -= Device.fTimeDelta;
        if (light_time <= 0)
            StopLight();
    }
}

void SStmBarrel::StopLight()
{
    if (light_render)
    {
        light_render->set_active(false);
    }
}

void SStmBarrel::OnShellDrop(const Fvector &play_pos, const Fvector &parent_vel)
{
    if (m_sShellParticles.size() == 0)
        return;
    CParticlesObject *pShellParticles = CParticlesObject::Create(m_sShellParticles.c_str(), TRUE);
    pShellParticles->UpdateParent(Fmatrix().set(m_fire_xfm).translate_over(play_pos), parent_vel);
    pShellParticles->Play(false);
}

void SStmBarrel::SetAmmoElapsed(int num)
{
    iAmmoElapsed = max(num, 0);
}

/*----------------------------------------------------------------------------------------------------
    APIs
----------------------------------------------------------------------------------------------------*/
SStmBarrel *CWeaponStatMgun::Barrel(LPCSTR name)
{
    if (name == nullptr)
        return nullptr;
    for (auto i : m_barrels)
    {
        if (strcmp(i.Name(), name) == 0)
        {
            return &i;
        }
    }
    return nullptr;
}

float CWeaponStatMgun::BarrelRPM(LPCSTR name)
{
    SStmBarrel *brl = Barrel(name);
    return (brl) ? brl->fOneShotTime * 60.0F : 0.0F;
}
#endif