// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// SPDX-License-Identifier: MIT

// ============================================================================
// vk_ParticleEffect.cpp - Particle effect rendering implementation
// ============================================================================
// TODO: Full particle integration with ParticleManager / PAPI
// Currently stubbed to allow compilation; rendering is not yet functional.

#include "stdafx.h"
#include "vk_ParticleEffect.h"
#include <array>

// ============================================================================
// VkParticleVertex implementation
// ============================================================================

VkVertexInputBindingDescription VkParticleVertex::GetBindingDescription()
{
    VkVertexInputBindingDescription bindingDescription{};
    bindingDescription.binding = 0;
    bindingDescription.stride = sizeof(VkParticleVertex);
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    return bindingDescription;
}

std::array<VkVertexInputAttributeDescription, 3> VkParticleVertex::GetAttributeDescriptions()
{
    std::array<VkVertexInputAttributeDescription, 3> attributeDescriptions{};

    // Position
    attributeDescriptions[0].binding = 0;
    attributeDescriptions[0].location = 0;
    attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[0].offset = offsetof(VkParticleVertex, pos);

    // Color
    attributeDescriptions[1].binding = 0;
    attributeDescriptions[1].location = 1;
    attributeDescriptions[1].format = VK_FORMAT_R8G8B8A8_UNORM;
    attributeDescriptions[1].offset = offsetof(VkParticleVertex, color);

    // UV
    attributeDescriptions[2].binding = 0;
    attributeDescriptions[2].location = 2;
    attributeDescriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
    attributeDescriptions[2].offset = offsetof(VkParticleVertex, uv);

    return attributeDescriptions;
}

// ============================================================================
// vkCParticleEffect - Constructor
// ============================================================================
vkCParticleEffect::vkCParticleEffect()
    : m_Def(nullptr),
      m_HandleEffect(-1),
      m_dynamicVB(nullptr),
      m_maxParticles(10000),
      m_fElapsedTime(0.f),
      m_fTimeLimit(-1.f)
{
    m_XFORM.identity();

    // Initialize visibility data
    vis.box.set(Fvector{-10, -10, -10}, Fvector{10, 10, 10});
    vis.sphere.P.set(0, 0, 0);
    vis.sphere.R = 10.f;
}

// ============================================================================
// vkCParticleEffect - Destructor
// ============================================================================
vkCParticleEffect::~vkCParticleEffect()
{
    OnDeviceDestroy();
    m_HandleEffect = -1;
}

// ============================================================================
// Compile - Initialize from definition
// ============================================================================
BOOL vkCParticleEffect::Compile(PS::CPEDef* def)
{
    if (!def) {
        Msg("![Vulkan] Cannot compile particle effect: definition is nullptr");
        return FALSE;
    }

    m_Def = def;
    m_maxParticles = 10000;

    Msg("[Vulkan] Particle effect compiled (stub)");
    return TRUE;
}

// ============================================================================
// Play - Start playing the effect
// ============================================================================
void vkCParticleEffect::Play()
{
    m_RT_Flags.set(flRT_Playing, TRUE);
    m_RT_Flags.set(flRT_DeferredStop, FALSE);
    m_fElapsedTime = 0.f;
}

// ============================================================================
// Stop - Stop playing the effect
// ============================================================================
void vkCParticleEffect::Stop(BOOL bDeferredStop)
{
    if (bDeferredStop) {
        m_RT_Flags.set(flRT_DeferredStop, TRUE);
    } else {
        m_RT_Flags.set(flRT_Playing, FALSE);
    }
}

// ============================================================================
// OnFrame - Update with elapsed time
// ============================================================================
void vkCParticleEffect::OnFrame(u32 dt)
{
    if (!IsPlaying()) {
        return;
    }

    m_fElapsedTime += dt / 1000.f;

    // Check time limit
    if (m_fTimeLimit > 0.f && m_fElapsedTime >= m_fTimeLimit) {
        Stop(FALSE);
    }
}

// ============================================================================
// Render - Main rendering method (stub)
// ============================================================================
void vkCParticleEffect::Render(float LOD)
{
    if (!IsPlaying() || !m_Def) {
        return;
    }

    // TODO: Implement actual particle rendering with ParticleManager integration
}

// ============================================================================
// GetTimeLimit
// ============================================================================
float vkCParticleEffect::GetTimeLimit()
{
    return m_fTimeLimit;
}

// ============================================================================
// Name - Get effect name
// ============================================================================
const shared_str vkCParticleEffect::Name()
{
    // TODO: Requires full PS::CPEDef definition
    // if (!m_Def) {
    //     return shared_str("");
    // }
    // return m_Def->m_Name;
    return shared_str("");  // Stub: return empty name for now
}

// ============================================================================
// ParticlesCount - Get current particle count
// ============================================================================
u32 vkCParticleEffect::ParticlesCount()
{
    // TODO: Query ParticleManager when integrated
    return 0;
}

// ============================================================================
// OnDeviceCreate - Device creation (stub)
// ============================================================================
void vkCParticleEffect::OnDeviceCreate()
{
    // TODO: Initialize Vulkan resources when particle rendering is implemented
    Msg("[Vulkan] Particle effect OnDeviceCreate (stub)");
}

// ============================================================================
// OnDeviceDestroy - Device destruction
// ============================================================================
void vkCParticleEffect::OnDeviceDestroy()
{
    DestroyPipeline();
    DestroyDescriptorSet();
}

// ============================================================================
// CreatePipeline - Create particle rendering pipeline on-demand (stub)
// ============================================================================
bool vkCParticleEffect::CreatePipeline()
{
    // TODO: Implement when particle rendering is connected
    return false;
}

// ============================================================================
// Private Helper Methods (stubs)
// ============================================================================

void vkCParticleEffect::GenerateBillboardQuads(VkParticleVertex* /*vertices*/, u32 /*particleCount*/)
{
    // TODO: Implement billboard generation
}

void vkCParticleEffect::BindResources(VkCommandBuffer /*cmd*/)
{
    // TODO: Implement resource binding
}

void vkCParticleEffect::UpdateDynamicBuffer(VkParticleVertex* /*data*/, u32 /*vertexCount*/)
{
    // TODO: Implement buffer update
}
