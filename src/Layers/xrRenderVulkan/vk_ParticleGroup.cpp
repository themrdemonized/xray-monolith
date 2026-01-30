// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// SPDX-License-Identifier: MIT

// ============================================================================
// vk_ParticleGroup.cpp - Particle group implementation
// ============================================================================

#include "stdafx.h"
#include "vk_ParticleGroup.h"
#include "vk_ParticleEffect.h"
#include "../xrRender/ParticleGroup.h"  // PS::CPGDef full definition
#include "../xrRender/PSLibrary.h"       // For PSLibrary.FindPED()

// ============================================================================
// vkCParticleGroup - Constructor
// ============================================================================
vkCParticleGroup::vkCParticleGroup()
    : m_Def(nullptr)
{
    // Initialize visibility data
    vis.box.set(Fvector{-10, -10, -10}, Fvector{10, 10, 10});
    vis.sphere.P.set(0, 0, 0);
    vis.sphere.R = 10.f;
}

// ============================================================================
// vkCParticleGroup - Destructor
// ============================================================================
vkCParticleGroup::~vkCParticleGroup()
{
    ClearChildren();
    OnDeviceDestroy();
}

// ============================================================================
// Compile - Initialize from group definition
// ============================================================================
BOOL vkCParticleGroup::Compile(PS::CPGDef* def)
{
    if (!def) {
        Msg("![Vulkan] Cannot compile particle group: definition is nullptr");
        return FALSE;
    }

    m_Def = def;

    // Clear existing children
    ClearChildren();

    // Create child effects from definition
    for (auto& effectDef : def->m_Effects)
    {
        if (!effectDef->m_Flags.is(PS::CPGDef::SEffect::flEnabled)) {
            continue;  // Skip disabled effects
        }

        if (effectDef->m_EffectName.size() == 0) {
            Msg("![Vulkan] Child effect has no name in group %s", def->m_Name.c_str());
            continue;
        }

        // Find particle effect definition
        PS::CPEDef* pedDef = PSLibrary.FindPED(effectDef->m_EffectName.c_str());
        if (!pedDef) {
            Msg("![Vulkan] Child effect not found: %s (in group %s)",
                effectDef->m_EffectName.c_str(), def->m_Name.c_str());
            continue;
        }

        // Create particle effect instance
        vkCParticleEffect* effect = xr_new<vkCParticleEffect>();
        if (!effect->Compile(pedDef)) {
            Msg("![Vulkan] Failed to compile child effect: %s", effectDef->m_EffectName.c_str());
            xr_delete(effect);
            continue;
        }

        // Add child with offset (default: no offset)
        Fvector offset = {0.f, 0.f, 0.f};
        AddChild(effect, offset);

        Msg("[Vulkan] Added child effect to group: %s", effectDef->m_EffectName.c_str());
    }

    Msg("[Vulkan] Particle group compiled: %s (%u children)",
        def->m_Name.c_str(), items.size());

    return TRUE;
}

// ============================================================================
// Render - Render all child effects
// ============================================================================
void vkCParticleGroup::Render(float LOD)
{
    if (!IsPlaying() || items.empty()) {
        return;
    }

    // Render each child effect
    for (auto& item : items) {
        if (item.pVisual) {
            item.pVisual->Render(LOD);
        }
    }
}

// ============================================================================
// AddChild - Add child effect/group
// ============================================================================
void vkCParticleGroup::AddChild(vkParticleCustom* pVisual, const Fvector& offset)
{
    if (!pVisual) {
        return;
    }

    SItem item;
    item.pVisual = pVisual;
    item.offset = offset;
    item.fAge = 0.f;

    items.push_back(item);
}

// ============================================================================
// ClearChildren - Remove all children
// ============================================================================
void vkCParticleGroup::ClearChildren()
{
    items.clear();
}

// ============================================================================
// Play - Start playing group
// ============================================================================
void vkCParticleGroup::Play()
{
    m_RT_Flags.set(flRT_Playing, TRUE);

    // Play all children
    for (auto& item : items) {
        if (item.pVisual) {
            item.pVisual->Play();
        }
    }

    Msg("[Vulkan] Particle group playing: %s (%u children)", Name().c_str(), items.size());
}

// ============================================================================
// Stop - Stop playing group
// ============================================================================
void vkCParticleGroup::Stop(BOOL bDeferredStop)
{
    if (bDeferredStop) {
        m_RT_Flags.set(flRT_DeferredStop, TRUE);
    } else {
        m_RT_Flags.set(flRT_Playing, FALSE);
    }

    // Stop all children
    for (auto& item : items) {
        if (item.pVisual) {
            item.pVisual->Stop(bDeferredStop);
        }
    }

    Msg("[Vulkan] Particle group stopped: %s", Name().c_str());
}

// ============================================================================
// OnFrame - Update with elapsed time
// ============================================================================
void vkCParticleGroup::OnFrame(u32 dt)
{
    if (!IsPlaying()) return;

    for (auto& item : items) {
        if (item.pVisual) {
            item.pVisual->OnFrame(dt);
        }
        item.fAge += dt / 1000.f;
    }
}

// ============================================================================
// ParticlesCount - Total particle count across children
// ============================================================================
u32 vkCParticleGroup::ParticlesCount()
{
    u32 count = 0;
    for (auto& item : items) {
        if (item.pVisual) {
            count += item.pVisual->ParticlesCount();
        }
    }
    return count;
}

// ============================================================================
// GetTimeLimit - Get time limit
// ============================================================================
float vkCParticleGroup::GetTimeLimit()
{
    return -1.f;  // No time limit by default
}

// ============================================================================
// Name - Get group name
// ============================================================================
const shared_str vkCParticleGroup::Name()
{
    if (m_Def) {
        return m_Def->m_Name;
    }
    return shared_str("");
}

// ============================================================================
// OnDeviceCreate - Device creation
// ============================================================================
void vkCParticleGroup::OnDeviceCreate()
{
    // Notify all children
    for (auto& item : items) {
        if (item.pVisual) {
            item.pVisual->OnDeviceCreate();
        }
    }
}

// ============================================================================
// OnDeviceDestroy - Device destruction
// ============================================================================
void vkCParticleGroup::OnDeviceDestroy()
{
    // Notify all children
    for (auto& item : items) {
        if (item.pVisual) {
            item.pVisual->OnDeviceDestroy();
        }
    }
}
