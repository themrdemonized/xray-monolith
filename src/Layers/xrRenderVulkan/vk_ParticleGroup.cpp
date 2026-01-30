// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// SPDX-License-Identifier: MIT

// ============================================================================
// vk_ParticleGroup.cpp - Particle group implementation
// ============================================================================

#include "stdafx.h"
#include "vk_ParticleGroup.h"
#include "../xrRender/ParticleGroup.h"  // PS::CPGDef full definition

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

    // TODO: Load child effects/groups from definition
    // for each child in def->children:
    //     Create child visual and add with offset

    Msg("[Vulkan] Particle group compiled: %s", def->m_Name.c_str());

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
        // TODO: Cast and call Play() on child if it's an effect
    }
}

// ============================================================================
// Stop - Stop playing group
// ============================================================================
void vkCParticleGroup::Stop(BOOL bDeferredStop)
{
    m_RT_Flags.set(flRT_Playing, FALSE);

    // Stop all children
    for (auto& item : items) {
        // TODO: Cast and call Stop() on child if it's an effect
    }
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
