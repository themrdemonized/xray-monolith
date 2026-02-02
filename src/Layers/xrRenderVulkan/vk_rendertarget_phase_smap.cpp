// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// SPDX-License-Identifier: MIT

#include "stdafx.h"
#include "vk_rendertarget.h"
#include "HW_Vulkan.h"
#include "vk_R_Backend.h"
#include "rvk.h"

namespace VK
{

// ============================================================================
// phase_smap_direct() - Render shadow map for directional light (sun)
// ============================================================================
//
// Эта фаза рендерит depth map для солнца (directional light).
// Используется cascade shadow maps с 3 splits: near/middle/far
//
// sub_phase:
//   SE_SUN_NEAR   - Near cascade (0..20m)
//   SE_SUN_MIDDLE - Middle cascade (20..40m)
//   SE_SUN_FAR    - Far cascade (40..150m)
//
// ============================================================================

void CRenderTarget::phase_smap_direct(light* sun, u32 sub_phase)
{
    if (!sun) {
        Msg("![Vulkan] phase_smap_direct: sun is NULL");
        return;
    }

    // Validate cascade index
    if (sub_phase >= RImplementation.m_sun_cascades.size()) {
        Msg("![Vulkan] phase_smap_direct: invalid cascade index %d", sub_phase);
        return;
    }

    const VK::SunCascade& cascade = RImplementation.m_sun_cascades[sub_phase];

    VkCommandBuffer cmd = RCache.GetCommandBuffer();

    // ========================================================================
    // Step 1: Transition shadow map to DEPTH_ATTACHMENT (only for first cascade)
    // ========================================================================

    if (sub_phase == 0)
    {
        VkImageMemoryBarrier2 barrier = {};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                               VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = rt_smap_depth.m_Image;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;

        VkDependencyInfo dependencyInfo = {};
        dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dependencyInfo.imageMemoryBarrierCount = 1;
        dependencyInfo.pImageMemoryBarriers = &barrier;

        vkCmdPipelineBarrier2(cmd, &dependencyInfo);
    }

    // ========================================================================
    // Step 2: Begin Rendering (depth-only pass) with cascade viewport
    // ========================================================================

    VkRenderingAttachmentInfo depthAttachment = {};
    depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depthAttachment.imageView = rt_smap_depth.m_ImageView;
    depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;  // LOAD - preserve other cascades!
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

    VkRenderingInfo renderingInfo = {};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.renderArea.offset = {(s32)cascade.posX, (s32)cascade.posY};
    renderingInfo.renderArea.extent = {cascade.viewport_size, cascade.viewport_size};
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 0;  // No color attachments (depth-only)
    renderingInfo.pDepthAttachment = &depthAttachment;

    vkCmdBeginRendering(cmd, &renderingInfo);

    // ========================================================================
    // Step 3: Setup viewport and scissor (cascade-specific region)
    // ========================================================================

    VkViewport viewport = {};
    viewport.x = (float)cascade.posX;
    viewport.y = (float)cascade.posY;
    viewport.width = (float)cascade.viewport_size;
    viewport.height = (float)cascade.viewport_size;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor = {};
    scissor.offset = {(s32)cascade.posX, (s32)cascade.posY};
    scissor.extent = {cascade.viewport_size, cascade.viewport_size};
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    // Cascade rendering to region

    // ========================================================================
    // Step 4: Use pre-calculated cascade shadow matrix
    // ========================================================================

    // The shadow matrix was already calculated by render_sun_cascade()
    // and stored in RImplementation.m_sun_cascades[sub_phase].xform
    RCache.set_xform_view(cascade.xform);

    Fmatrix identity;
    identity.identity();
    RCache.set_xform_project(identity);

    // ========================================================================
    // Step 5: Render geometry (shadow casters)
    // ========================================================================

    // Set world matrix to identity for scene geometry
    Fmatrix world_identity;
    world_identity.identity();
    RCache.set_xform_world(world_identity);

    // ========================================================================
    // Call render implementation to render shadow geometry
    // ========================================================================

    RImplementation.render_shadow_geometry(sub_phase);

    // Geometry rendered

    // ========================================================================
    // Step 6: End rendering
    // ========================================================================

    vkCmdEndRendering(cmd);

    // ========================================================================
    // Step 7: Transition shadow map to SHADER_READ (only for last cascade)
    // ========================================================================

    // All cascades share one shadow atlas, so transition only after the last cascade
    if (sub_phase == RImplementation.m_sun_cascades.size() - 1)
    {
        VkImageMemoryBarrier2 barrier = {};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                               VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = rt_smap_depth.m_Image;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;

        VkDependencyInfo dependencyInfo = {};
        dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dependencyInfo.imageMemoryBarrierCount = 1;
        dependencyInfo.pImageMemoryBarriers = &barrier;

        vkCmdPipelineBarrier2(cmd, &dependencyInfo);

        // Shadow atlas transitioned to SHADER_READ
    }

    // phase_smap_direct() complete
}

// phase_smap_point() and phase_smap_spot() implementations are in:
// - vk_rendertarget_accum_point.cpp
// - vk_rendertarget_accum_spot.cpp

} // namespace VK
