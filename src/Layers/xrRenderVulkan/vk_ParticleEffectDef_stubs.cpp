// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// SPDX-License-Identifier: MIT

// ============================================================================
// Stub implementations for PS::CPEDef and PS::CPGDef
// Minimal particle definition classes for Vulkan renderer
// Full physics/collision integration to be added in future phases
// ============================================================================

#include "stdafx.h"
#pragma hdrstop

#include "../xrRender/ParticleEffectDef.h"
#include "../xrRender/ParticleGroup.h"

// ============================================================================
// PS::CPEDef - Particle Effect Definition stub
// ============================================================================

namespace PS
{

CPEDef::CPEDef()
{
    // Stub constructor - initialize to defaults
    m_MaxParticles = 1000;
    m_CachedShader = nullptr;
}

CPEDef::~CPEDef()
{
    // Stub destructor
}

// Load from ini file (simplified version without physics/collision)
int CPEDef::Load2(CInifile& ini)
{
    // Read basic particle effect parameters from ini file
    // Skip physics/collision code that requires g_pGameLevel

    if (!ini.section_exist("_effect"))
        return 0;

    // Read particle count
    if (ini.line_exist("_effect", "max_particles"))
        m_MaxParticles = ini.r_u32("_effect", "max_particles");

    // Read effect name
    if (ini.line_exist("_effect", "name"))
        m_Name = ini.r_string("_effect", "name");

    // TODO: Read other particle parameters:
    // - Texture
    // - Velocity
    // - Lifetime
    // - Size
    // - Color over lifetime
    // etc.

    Msg("[Vulkan] Loaded particle effect stub: %s (max_particles=%u)",
        m_Name.c_str(), m_MaxParticles);

    return 1; // Success
}

const char* CPEDef::Name() const
{
    return m_Name.c_str();
}

} // namespace PS

// ============================================================================
// PS::CPGDef - Particle Group Definition stub
// ============================================================================

namespace PS
{

CPGDef::CPGDef()
{
    // Stub constructor
}

CPGDef::~CPGDef()
{
    // Stub destructor
}

// Load from ini file (simplified version)
int CPGDef::Load2(CInifile& ini)
{
    // Read basic particle group parameters

    if (!ini.section_exist("_group"))
        return 0;

    // Read group name
    if (ini.line_exist("_group", "name"))
        m_Name = ini.r_string("_group", "name");

    // TODO: Read particle effects that belong to this group
    // For now just load the group name

    Msg("[Vulkan] Loaded particle group stub: %s", m_Name.c_str());

    return 1; // Success
}

} // namespace PS
