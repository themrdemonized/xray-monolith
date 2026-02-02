// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// SPDX-License-Identifier: MIT

#include "stdafx.h"
#include "vk_rendertarget.h"
#include "HW_Vulkan.h"
#include "vk_R_Backend.h"
#include "vk_geometry.h"
#include "vk_lighting.h"
#include "rvk.h"

namespace VK
{

// ============================================================================
// accum_direct_simple() - Simplified sun lighting без теней (for testing)
// ============================================================================

void CRenderTarget::accum_direct_simple()
{
    // Get sun light
    light* sun = (light*)RImplementation.Lights.sun_adapted._get();
    if (!sun) return;

    VkCommandBuffer cmd = RCache.GetCommandBuffer();

    // ========================================================================
    // Light parameters
    // ========================================================================

    Fvector L_dir, L_clr;
    float L_spec;

    L_clr.set(sun->color.r, sun->color.g, sun->color.b);
    L_spec = u_diffuse2s(L_clr);

    // Transform light direction to view space
    Device.mView.transform_dir(L_dir, sun->direction);
    L_dir.normalize();

    // sun params ready

    // ========================================================================
    // Bind shader and geometry
    // ========================================================================

    // Bind lighting pipeline через CVulkanLighting
    if (VK::g_VulkanLighting) {
        bool success = VK::g_VulkanLighting->BindAccumDirectSimple(cmd, this, L_dir, L_clr, L_spec);
        if (!success) {
            Msg("![Vulkan] Failed to bind accum_direct_simple pipeline");
            return;
        }
    } else {
        Msg("![Vulkan] VK::g_VulkanLighting is NULL");
        return;
    }

    // Bind fullscreen quad
    if (VK::g_VulkanGeometry) {
        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &VK::g_VulkanGeometry->m_FullscreenQuadVB, &offset);
        vkCmdBindIndexBuffer(cmd, VK::g_VulkanGeometry->m_FullscreenQuadIB, 0, VK_INDEX_TYPE_UINT16);
    } else {
        Msg("![Vulkan] VK::g_VulkanGeometry is NULL");
        return;
    }

    // ========================================================================
    // Draw fullscreen quad
    // ========================================================================

    vkCmdDrawIndexed(cmd, 6, 1, 0, 0, 0);

    // fullscreen quad drawn
}

// ============================================================================
// accum_direct() - Full implementation с тенями
// ============================================================================
//
// Портировано из r4_rendertarget_accum_direct.cpp
// Поддерживает:
// - Cascade shadow maps (near/middle/far)
// - PCF filtering с jitter
// - Cloud shadows
// - Stencil masking
//
// sub_phase:
//   SE_SUN_NEAR   - Near cascade
//   SE_SUN_MIDDLE - Middle cascade
//   SE_SUN_FAR    - Far cascade
//
// ============================================================================

void CRenderTarget::accum_direct(u32 sub_phase)
{
    // TODO Phase 2.15.3: Полная реализация с тенями
    // Пока вызываем упрощённую версию

    // accum_direct - calling simple version
    accum_direct_simple();

    /*
    // Полная версия (будет реализована позже):

    phase_accumulator();

    light* sun = (light*)RImplementation.Lights.sun_adapted._get();
    if (!sun) return;

    VkCommandBuffer cmd = RCache.GetCommandBuffer();

    // === Step 1: Masking pass (SE_SUN_NEAR only) ===
    if (sub_phase == SE_SUN_NEAR) {
        // Draw fullscreen quad with stencil masking
        // Sets stencil = dwLightMarkerID where lit
        RCache.set_Element(s_accum_mask->E[SE_MASK_DIRECT]);

        // Compute mask direction based on light intensity
        float intensity = 0.3f * sun->color.r +
                         0.48f * sun->color.g +
                         0.22f * sun->color.b;
        Fvector dir = L_dir;
        dir.normalize().mul(-_sqrt(intensity + EPS));

        RCache.set_c("Ldynamic_dir", dir.x, dir.y, dir.z, 0);

        // Stencil: if (stencil >= 1) stencil = dwLightMarkerID
        vkCmdSetStencilOp(cmd, VK_STENCIL_FACE_FRONT_AND_BACK,
                          VK_STENCIL_OP_KEEP,
                          VK_STENCIL_OP_REPLACE,
                          VK_STENCIL_OP_KEEP);
        vkCmdSetStencilReference(cmd, VK_STENCIL_FACE_FRONT_AND_BACK,
                                 dwLightMarkerID);

        RCache.Render(D3DPT_TRIANGLELIST, 0, 2);
    }

    // === Step 2: Lighting pass ===
    {
        // Compute shadow matrix
        Fmatrix m_shadow, m_clouds_shadow;
        ComputeShadowMatrix(sun, sub_phase, m_shadow);
        ComputeCloudsShadowMatrix(sun, m_clouds_shadow);

        // Light parameters
        Fvector L_dir, L_clr;
        float L_spec;
        L_clr.set(sun->color.r, sun->color.g, sun->color.b);
        L_spec = u_diffuse2s(L_clr);
        Device.mView.transform_dir(L_dir, sun->direction);
        L_dir.normalize();

        // Bind shader (with shadow map)
        u32 element = sub_phase;
        RCache.set_Element(s_accum_direct->E[element]);

        // Bind textures
        RCache.set_Texture("s_position", rt_Position);
        RCache.set_Texture("s_normal", rt_Normal);
        RCache.set_Texture("s_diffuse", rt_Color);
        RCache.set_Texture("s_material", rt_Material);
        RCache.set_Texture("s_smap", rt_smap_depth);
        RCache.set_Texture("s_lmap", rt_sunmask);

        // Constants
        RCache.set_c("Ldynamic_dir", L_dir.x, L_dir.y, L_dir.z, 0);
        RCache.set_c("Ldynamic_color", L_clr.x, L_clr.y, L_clr.z, L_spec);
        RCache.set_c("m_shadow", m_shadow);
        RCache.set_c("m_sunmask", m_clouds_shadow);

        // Stencil: render where stencil == dwLightMarkerID
        vkCmdSetStencilOp(cmd, VK_STENCIL_FACE_FRONT_AND_BACK,
                          VK_STENCIL_OP_KEEP, VK_STENCIL_OP_KEEP,
                          VK_STENCIL_OP_KEEP);
        vkCmdSetStencilReference(cmd, VK_STENCIL_FACE_FRONT_AND_BACK,
                                 dwLightMarkerID);

        // Draw fullscreen quad
        RCache.Render(D3DPT_TRIANGLELIST, 0, 2);
    }

    // NV stencil optimization
    if (RImplementation.o.nvstencil && sub_phase == SE_SUN_NEAR)
        u_stencil_optimize();
    */
}

} // namespace VK
