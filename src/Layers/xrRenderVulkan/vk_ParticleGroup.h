// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// Licensed under the same terms as X-Ray Engine (see root License.txt)

// ============================================================================
// vk_ParticleGroup.h - Vulkan particle group rendering
// ============================================================================
//
// Group of particle effects/groups that render together.
// Used for complex effects composed of multiple particle elements.
//
// ============================================================================

#pragma once

#include "vk_ParticleCustom.h"

namespace PS
{
    struct CPGDef;
}

// ============================================================================
// vkCParticleGroup - Container for multiple particle effects
// ============================================================================
class vkCParticleGroup : public vkParticleCustom
{
public:
    // Particle group item
    struct SItem
    {
        vkParticleCustom* pVisual = nullptr;  // Child effect or group
        Fvector offset;                        // Position offset from parent
        float fAge = 0.f;                      // Age of this item
    };

    // Definition (shared, non-owned)
    PS::CPGDef* m_Def = nullptr;

    // Child items (effects and groups)
    xr_vector<SItem> items;

    // Runtime flags
    Flags8 m_RT_Flags;

public:
    vkCParticleGroup();
    virtual ~vkCParticleGroup();

    // ========================================================================
    // Compilation and Initialization
    // ========================================================================

    // Compile from particle group definition
    BOOL Compile(PS::CPGDef* def);

    // ========================================================================
    // Rendering
    // ========================================================================

    // Render all child effects
    virtual void Render(float LOD) override;

    // ========================================================================
    // Child Management
    // ========================================================================

    // Add child effect/group
    void AddChild(vkParticleCustom* pVisual, const Fvector& offset);

    // Remove all children
    void ClearChildren();

    // ========================================================================
    // Lifecycle Control
    // ========================================================================

    // IParticleCustom interface
    virtual void Play() override;
    virtual void Stop(BOOL bDeferredStop = TRUE) override;
    virtual BOOL IsPlaying() override { return m_RT_Flags.is(flRT_Playing); }
    virtual void OnFrame(u32 dt) override;
    virtual u32 ParticlesCount() override;
    virtual float GetTimeLimit() override;
    virtual const shared_str Name() override;
    virtual void SetHudMode(BOOL b) override { m_RT_Flags.set(flRT_HUDmode, b); }
    virtual BOOL GetHudMode() override { return m_RT_Flags.is(flRT_HUDmode); }
    virtual void UpdateParent(const Fmatrix& m, const Fvector& velocity, BOOL bXFORM) override;

    // ========================================================================
    // Device Management
    // ========================================================================

    virtual void OnDeviceCreate() override;
    virtual void OnDeviceDestroy() override;

private:
    enum
    {
        flRT_Playing = (1 << 0),       // Group is playing
        flRT_DeferredStop = (1 << 1),  // Stop after particles die
        flRT_HUDmode = (1 << 2),       // Render in HUD space
    };
};
