// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// SPDX-License-Identifier: MIT

// ============================================================================
// PS::CPEDef and PS::CPGDef implementations for Vulkan renderer
// Loads particle definitions from .pe/.pg files and creates Vulkan shaders
// ============================================================================

#include "stdafx.h"
#pragma hdrstop

#include "../xrRender/ParticleEffectDef.h"
#include "../xrRender/ParticleGroup.h"
#include "vk_shader.h"  // For CVulkanShader

// External Vulkan shader manager
extern VK::CVulkanShaderManager* g_VulkanShaderManager;

// ============================================================================
// PS::CPEDef - Particle Effect Definition
// ============================================================================

namespace PS
{

CPEDef::CPEDef()
{
    // Initialize to defaults
    m_MaxParticles = 1000;
    m_CachedShader = nullptr;
    m_Flags.zero();
    m_uStep = 30;  // 30ms update rate
    m_fStep = 0.03f;
    m_fTimeLimit = 0.0f;
    m_VelocityScale.set(1.f, 1.f, 1.f);
    m_APDefaultRotation.set(0.f, 0.f, 0.f);
    m_fCollideOneMinusFriction = 0.5f;
    m_fCollideResilience = 0.5f;
    m_fCollideSqrCutoff = 0.01f;
    m_Frame.InitDefault();
}

CPEDef::~CPEDef()
{
    DestroyShader();
}

// ============================================================================
// Load2 - Load particle effect from ini file
// ============================================================================
int CPEDef::Load2(CInifile& ini)
{
    if (!ini.section_exist("_effect"))
    {
        Msg("![Vulkan] Particle effect missing '_effect' section");
        return 0;
    }

    // ========================================================================
    // Basic parameters
    // ========================================================================
    if (ini.line_exist("_effect", "name"))
        m_Name = ini.r_string("_effect", "name");

    if (ini.line_exist("_effect", "max_particles"))
        m_MaxParticles = ini.r_u32("_effect", "max_particles");

    if (ini.line_exist("_effect", "time_limit"))
        m_fTimeLimit = ini.r_float("_effect", "time_limit");

    // ========================================================================
    // Shader and texture
    // ========================================================================
    if (ini.line_exist("_effect", "shader"))
        m_ShaderName = ini.r_string("_effect", "shader");
    else
        m_ShaderName = "particle";  // Default particle shader

    if (ini.line_exist("_effect", "texture"))
        m_TextureName = ini.r_string("_effect", "texture");
    else
        m_TextureName = "pfx\\pfx_default";  // Default particle texture

    // ========================================================================
    // Flags
    // ========================================================================
    if (ini.line_exist("_effect", "flags"))
    {
        u32 flags = ini.r_u32("_effect", "flags");
        m_Flags.assign(flags);
    }
    else
    {
        // Default flags: sprite, animated
        m_Flags.set(dfSprite, TRUE);
        m_Flags.set(dfAnimated, TRUE);
    }

    // ========================================================================
    // Frame/animation parameters
    // ========================================================================
    if (ini.line_exist("_effect", "frame_dim_x"))
        m_Frame.m_iFrameDimX = ini.r_s32("_effect", "frame_dim_x");

    if (ini.line_exist("_effect", "frame_count"))
        m_Frame.m_iFrameCount = ini.r_s32("_effect", "frame_count");

    if (ini.line_exist("_effect", "frame_speed"))
        m_Frame.m_fSpeed = ini.r_float("_effect", "frame_speed");

    if (ini.line_exist("_effect", "frame_tex_size"))
    {
        Fvector2 tex_size;
        tex_size = ini.r_fvector2("_effect", "frame_tex_size");
        m_Frame.m_fTexSize = tex_size;
    }

    // ========================================================================
    // Update rate
    // ========================================================================
    if (ini.line_exist("_effect", "update_step_ms"))
    {
        m_uStep = ini.r_u32("_effect", "update_step_ms");
        m_fStep = (float)m_uStep / 1000.f;
    }

    // ========================================================================
    // Velocity scale
    // ========================================================================
    if (ini.line_exist("_effect", "velocity_scale"))
    {
        m_VelocityScale = ini.r_fvector3("_effect", "velocity_scale");
    }

    // ========================================================================
    // Collision parameters
    // ========================================================================
    if (ini.line_exist("_effect", "collision_friction"))
        m_fCollideOneMinusFriction = 1.0f - ini.r_float("_effect", "collision_friction");

    if (ini.line_exist("_effect", "collision_resilience"))
        m_fCollideResilience = ini.r_float("_effect", "collision_resilience");

    if (ini.line_exist("_effect", "collision_cutoff"))
        m_fCollideSqrCutoff = ini.r_float("_effect", "collision_cutoff");

    // ========================================================================
    // Align to path rotation
    // ========================================================================
    if (ini.line_exist("_effect", "align_to_path_rot"))
    {
        m_APDefaultRotation = ini.r_fvector3("_effect", "align_to_path_rot");
    }

    // ========================================================================
    // Actions (particle behaviors)
    // ========================================================================
    // TODO: Load particle actions from ini file
    // For now, particles will use default behavior (gravity + fade out)

    Msg("[Vulkan] Loaded particle effect: %s (max=%u, shader=%s, texture=%s)",
        m_Name.c_str(), m_MaxParticles, m_ShaderName.c_str(), m_TextureName.c_str());

    return 1; // Success
}

// ============================================================================
// CreateShader - Create Vulkan shader for particle rendering
// ============================================================================
void CPEDef::CreateShader()
{
    if (m_ShaderName.size() == 0 || m_TextureName.size() == 0)
    {
        Msg("![Vulkan] Cannot create particle shader: missing shader/texture name for %s",
            m_Name.c_str());
        return;
    }

    if (!g_VulkanShaderManager)
    {
        Msg("![Vulkan] Cannot create particle shader: shader manager not initialized");
        return;
    }

    // Create Vulkan shader with particle texture
    VK::CVulkanShader* shader = g_VulkanShaderManager->CreateShader(
        m_ShaderName.c_str(),
        m_TextureName.c_str()
    );

    if (!shader)
    {
        Msg("![Vulkan] Failed to create shader for particle effect: %s", m_Name.c_str());
        return;
    }

    // Cache shader pointer
    // Note: m_CachedShader is ref_shader in original, but for Vulkan we store CVulkanShader*
    // This is a workaround since we can't modify the base class
    m_CachedShader = (void*)shader;

    Msg("[Vulkan] Particle shader created: %s -> %s + %s",
        m_Name.c_str(), m_ShaderName.c_str(), m_TextureName.c_str());
}

// ============================================================================
// DestroyShader - Release Vulkan shader
// ============================================================================
void CPEDef::DestroyShader()
{
    if (m_CachedShader)
    {
        // Note: Shader is managed by g_VulkanShaderManager, we just clear the pointer
        m_CachedShader = nullptr;
    }
}

// ============================================================================
// Name - Get particle effect name
// ============================================================================
const char* CPEDef::Name() const
{
    return m_Name.c_str();
}

} // namespace PS

// ============================================================================
// PS::CPGDef - Particle Group Definition
// ============================================================================

namespace PS
{

CPGDef::CPGDef()
{
    m_Flags.zero();
    m_fTimeLimit = 0.f;
}

CPGDef::~CPGDef()
{
    // Clean up effects
    for (auto& effect : m_Effects)
    {
        xr_delete(effect);
    }
    m_Effects.clear();
}

// ============================================================================
// Load2 - Load particle group from ini file
// ============================================================================
int CPGDef::Load2(CInifile& ini)
{
    if (!ini.section_exist("_group"))
    {
        Msg("![Vulkan] Particle group missing '_group' section");
        return 0;
    }

    // Read group name
    if (ini.line_exist("_group", "name"))
        m_Name = ini.r_string("_group", "name");

    // Read time limit
    if (ini.line_exist("_group", "time_limit"))
        m_fTimeLimit = ini.r_float("_group", "time_limit");

    // Read flags
    if (ini.line_exist("_group", "flags"))
        m_Flags.assign(ini.r_u32("_group", "flags"));

    // ========================================================================
    // Read child effects
    // ========================================================================
    if (ini.line_exist("_group", "effect_count"))
    {
        u32 effectCount = ini.r_u32("_group", "effect_count");

        for (u32 i = 0; i < effectCount; i++)
        {
            string64 section_name;
            xr_sprintf(section_name, "_effect_%u", i);

            if (!ini.section_exist(section_name))
            {
                Msg("![Vulkan] Missing section %s in particle group %s", section_name, m_Name.c_str());
                continue;
            }

            // Create effect definition
            SEffect* effect = xr_new<SEffect>();

            // Read effect name
            if (ini.line_exist(section_name, "effect_name"))
                effect->m_EffectName = ini.r_string(section_name, "effect_name");

            // Read time range
            if (ini.line_exist(section_name, "time0"))
                effect->m_Time0 = ini.r_float(section_name, "time0");

            if (ini.line_exist(section_name, "time1"))
                effect->m_Time1 = ini.r_float(section_name, "time1");

            // Read flags
            if (ini.line_exist(section_name, "flags"))
                effect->m_Flags.assign(ini.r_u32(section_name, "flags"));
            else
                effect->m_Flags.set(SEffect::flEnabled, TRUE);  // Default: enabled

            // Read child event effects (optional)
            if (ini.line_exist(section_name, "on_play_child"))
                effect->m_OnPlayChildName = ini.r_string(section_name, "on_play_child");

            if (ini.line_exist(section_name, "on_birth_child"))
                effect->m_OnBirthChildName = ini.r_string(section_name, "on_birth_child");

            if (ini.line_exist(section_name, "on_dead_child"))
                effect->m_OnDeadChildName = ini.r_string(section_name, "on_dead_child");

            // Add to group
            m_Effects.push_back(effect);

            Msg("[Vulkan] Loaded child effect: %s (time: %.2f-%.2f)",
                effect->m_EffectName.c_str(), effect->m_Time0, effect->m_Time1);
        }
    }

    Msg("[Vulkan] Loaded particle group: %s (%u effects, time_limit=%.2f)",
        m_Name.c_str(), m_Effects.size(), m_fTimeLimit);

    return 1; // Success
}

} // namespace PS
