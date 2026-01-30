// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// SPDX-License-Identifier: MIT

// ============================================================================
// vk_ParticleEffect.h - Vulkan particle effect rendering
// ============================================================================
//
// Single particle effect implementation with CPU simulation.
// Renders particles as billboards using dynamic vertex buffer.
//
// ============================================================================

#pragma once

#include "vk_ParticleCustom.h"

namespace PS
{
    struct CPEDef;
}

namespace VK
{
    class CVulkanTexture;
}

// ============================================================================
// Particle Vertex Format (24 bytes)
// ============================================================================
struct VkParticleVertex
{
    float pos[3];           // Position (12 bytes) - world space
    uint32_t color;         // Color (4 bytes) - RGBA8 packed
    float uv[2];            // Texture coordinates (8 bytes)

    // Total: 24 bytes per vertex

    static VkVertexInputBindingDescription GetBindingDescription();
    static std::array<VkVertexInputAttributeDescription, 3> GetAttributeDescriptions();
};

// ============================================================================
// vkCParticleEffect - Single particle effect
// ============================================================================
class vkCParticleEffect : public vkParticleCustom
{
public:
    // Definition (shared, non-owned)
    PS::CPEDef* m_Def = nullptr;

    // Particle manager handle
    int m_HandleEffect = -1;

    // Dynamic vertex buffer for particles
    VK::CVulkanBuffer* m_dynamicVB = nullptr;

    // Particle texture
    VK::CVulkanTexture* m_texture = nullptr;

    // Max particles that can be rendered
    u32 m_maxParticles = 10000;

    // Runtime flags (flRT_Playing, flRT_HUDmode, etc.)
    Flags8 m_RT_Flags;

    // Elapsed time since start
    float m_fElapsedTime = 0.f;

    // Time limit for this effect (if any)
    float m_fTimeLimit = -1.f;

    // World transformation
    Fmatrix m_XFORM;

public:
    vkCParticleEffect();
    virtual ~vkCParticleEffect();

    // ========================================================================
    // Compilation and Initialization
    // ========================================================================

    // Compile from particle definition
    BOOL Compile(PS::CPEDef* def);

    // ========================================================================
    // Rendering
    // ========================================================================

    // Main render method
    virtual void Render(float LOD) override;

    // ========================================================================
    // Lifecycle Control
    // ========================================================================

    // IParticleCustom interface
    virtual void Play() override;
    virtual void Stop(BOOL bDeferredStop = TRUE) override;
    virtual BOOL IsPlaying() override { return m_RT_Flags.is(flRT_Playing); }
    virtual void SetHudMode(BOOL b) override { m_RT_Flags.set(flRT_HUDmode, b); }
    virtual BOOL GetHudMode() override { return m_RT_Flags.is(flRT_HUDmode); }
    virtual void OnFrame(u32 dt) override;
    virtual u32 ParticlesCount() override;
    virtual float GetTimeLimit() override;
    virtual const shared_str Name() override;

    // Update parent transformation (world matrix)
    virtual void UpdateParent(const Fmatrix& m, const Fvector& velocity, BOOL bXFORM) override
    {
        m_XFORM = m;
    }

    // Property Access
    PS::CPEDef* GetDefinition() const { return m_Def; }
    int GetHandleEffect() const { return m_HandleEffect; }

    // ========================================================================
    // Device Management
    // ========================================================================

    virtual void OnDeviceCreate() override;
    virtual void OnDeviceDestroy() override;

    // Create pipeline on-demand (lazy initialization)
    bool CreatePipeline();
    bool IsPipelineReady() const { return m_pipeline != VK_NULL_HANDLE; }

private:
    // Helper methods
    void GenerateBillboardQuads(VkParticleVertex* vertices, u32 particleCount);
    void BindResources(VkCommandBuffer cmd);
    void UpdateDynamicBuffer(VkParticleVertex* data, u32 vertexCount);

    enum
    {
        flRT_Playing = (1 << 0),      // Effect is actively playing
        flRT_DeferredStop = (1 << 1), // Stop after particles die
        flRT_XFORM = (1 << 2),        // Transform matrix is set
        flRT_HUDmode = (1 << 3),      // Render in HUD space
    };
};
