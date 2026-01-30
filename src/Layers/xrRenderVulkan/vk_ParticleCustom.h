// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// SPDX-License-Identifier: MIT

// ============================================================================
// vk_ParticleCustom.h - Base class for Vulkan particle rendering
// ============================================================================
//
// Phase 2.24: Particle System Implementation
//
// Vulkan-native particle rendering without DirectX dependencies.
// Supports CPU-simulated particles with GPU billboard generation.
//
// ============================================================================

#pragma once

#include "stdafx.h"
#include "vk_Visual.h"
#include "vk_buffer.h"
#include "../../Include/xrRender/ParticleCustom.h"

// Forward declarations
namespace PS
{
    struct CPEDef;
    struct CPGDef;
    struct SEmitter;
}

namespace VK
{
    class CVulkanBuffer;
}

// ============================================================================
// vkParticleCustom - Base class for all Vulkan particle visuals
// ============================================================================
// Equivalent to dxParticleCustom but uses Vulkan resources
class vkParticleCustom : public vkRender_Visual, public IParticleCustom
{
public:
    // Vulkan mesh for particle geometry
    VK_Render_Mesh geom;

    // Pipeline for rendering
    VkPipeline m_pipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;

    // Descriptor set for texture/sampler binding
    VkDescriptorSet m_descriptorSet = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;

    // Material/texture information
    shared_str m_textureName;
    VK::CVulkanBuffer* m_textureBuffer = nullptr;

public:
    vkParticleCustom();
    virtual ~vkParticleCustom();

    // ========================================================================
    // IParticleCustom interface
    // ========================================================================
    virtual IParticleCustom* dcast_ParticleCustom() override { return this; }

    // ========================================================================
    // Rendering interface
    // ========================================================================
    virtual void Render(float LOD) = 0;
    virtual void Copy(vkRender_Visual* pFrom) {}
    virtual void OnDeviceCreate() {}
    virtual void OnDeviceDestroy() {}

    // ========================================================================
    // Lifecycle
    // ========================================================================
    virtual void UpdateParent(const Fmatrix& m, const Fvector& velocity, BOOL bXFORM) {}

    // ========================================================================
    // Descriptor Set Management
    // ========================================================================

    // Static helper: Create descriptor set layout for particles
    static bool CreateDescriptorSetLayout(
        VkDevice device,
        VkDescriptorSetLayout& outLayout);

    // Allocate descriptor set from pool
    bool AllocateDescriptorSet(
        VkDevice device,
        VkDescriptorPool pool,
        VkDescriptorSetLayout layout);

    // Update texture binding in descriptor set
    void UpdateTextureBinding(
        VkDevice device,
        VkImageView textureView,
        VkSampler sampler);

    // Get descriptor set for binding
    VkDescriptorSet GetDescriptorSet() const { return m_descriptorSet; }

    // ========================================================================
    // Cleanup
    // ========================================================================
    void DestroyPipeline();
    void DestroyDescriptorSet();
};
