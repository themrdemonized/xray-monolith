// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// SPDX-License-Identifier: MIT

#include "stdafx.h"
#include "vk_DetailManager.h"
#include "vk_R_Backend.h"
#include "rvk.h"
#include "vk_swapchain.h"
#include "vk_pipeline.h"
#include "../xrRender/DetailFormat.h"
#include "../../xrEngine/IGame_Persistent.h"
#include "../../xrEngine/Environment.h"

namespace VK
{

// ============================================================================
// Render - Main rendering entry point
// Ported from DX11 DetailManager.cpp::Render()
// ============================================================================
void CDetailManager::Render()
{
    if (!m_bCreated)
        return;
    if (0 == dtFS)
        return;
    if (objects.empty())
        return;
    if (m_Pipeline == VK_NULL_HANDLE)
        return;

    // Ensure visibility is up to date
    MT_SYNC();

    // Diagnostic: log detail render state periodically
    {
        static u32 s_detail_diag_frame = 0;
        if (RDEVICE.dwFrame > s_detail_diag_frame + 120)
        {
            s_detail_diag_frame = RDEVICE.dwFrame;
            u32 total_vis = 0;
            u32 total_items = 0;
            u32 valid_objs = 0;
            for (u32 i = 0; i < objects.size(); i++)
            {
                VK::CDetail* obj = objects[i];
                if (obj && obj->m_VertexBuffer && obj->m_IndexBuffer)
                    valid_objs++;
            }
            for (int a = 0; a < 3; a++)
            {
                for (u32 o = 0; o < m_visibles[a].size(); o++)
                {
                    total_vis += (u32)m_visibles[a][o].size();
                    for (u32 v = 0; v < m_visibles[a][o].size(); v++)
                    {
                        if (m_visibles[a][o][v])
                            total_items += (u32)m_visibles[a][o][v]->size();
                    }
                }
            }
            Msg("[Detail Render] frame=%u objs=%u valid_objs=%u vis_groups=%u items=%u m_frame_calc=%u m_frame_rendered=%u",
                RDEVICE.dwFrame, (u32)objects.size(), valid_objs, total_vis, total_items,
                m_frame_calc, m_frame_rendered);
        }
    }

    // Update wind animation
    float factor = g_pGamePersistent->Environment().wind_strength_factor;
    swing_current.lerp(swing_desc[0], swing_desc[1], factor);
    UpdateWindAnimation();

    // Setup view-projection matrix
    Fmatrix mVP;
    mVP.mul(RDEVICE.mProject, RDEVICE.mView);
    m_Constants.mViewProj = mVP;

    // Setup constants (scale, aniso, ambient)
    m_Constants.vConsts.set(
        1.0f,                                   // scale_x
        1.0f,                                   // scale_y
        g_pGamePersistent->Environment().CurrentEnv->sun_dir.y,  // l_aniso (sun angle)
        0.2f                                    // l_ambient
    );

    // Get current command buffer
    VkCommandBuffer cmd = RCache.GetCommandBuffer();
    if (cmd == VK_NULL_HANDLE)
        return;

    // ========================================================================
    // Get swapchain and depth resources
    // Use Swapchain depth buffer (same as sky/clouds passes)
    // ========================================================================
    VkImage swapchainImage = Swapchain.GetCurrentImage();
    VkImageView swapchainView = Swapchain.GetCurrentImageView();
    u32 swapWidth = Swapchain.GetWidth();
    u32 swapHeight = Swapchain.GetHeight();

    if (swapchainImage == VK_NULL_HANDLE || swapchainView == VK_NULL_HANDLE)
        return;

    VkImage depthImage = Swapchain.m_DepthImage;
    VkImageView depthView = Swapchain.m_DepthView;
    if (depthImage == VK_NULL_HANDLE || depthView == VK_NULL_HANDLE)
        return;

    // ========================================================================
    // Transition swapchain: PRESENT_SRC -> COLOR_ATTACHMENT_OPTIMAL
    // Transition depth: DEPTH_ATTACHMENT_OPTIMAL -> keep (read + write)
    // ========================================================================
    VkImageMemoryBarrier barriers[2] = {};

    barriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barriers[0].srcAccessMask = 0;
    barriers[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barriers[0].oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    barriers[0].newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[0].image = swapchainImage;
    barriers[0].subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

    barriers[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barriers[1].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    barriers[1].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    barriers[1].oldLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    barriers[1].newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    barriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[1].image = depthImage;
    barriers[1].subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 };

    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
        0, 0, nullptr, 0, nullptr, 2, barriers);

    // ========================================================================
    // Begin rendering
    // ========================================================================
    VkRenderingAttachmentInfo colorAttachment = {};
    colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttachment.imageView = swapchainView;
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

    VkRenderingAttachmentInfo depthAttachment = {};
    depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depthAttachment.imageView = depthView;
    depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

    VkRenderingInfo renderingInfo = {};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.renderArea.offset = {0, 0};
    renderingInfo.renderArea.extent = {swapWidth, swapHeight};
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &colorAttachment;
    renderingInfo.pDepthAttachment = &depthAttachment;

    vkCmdBeginRendering(cmd, &renderingInfo);

    // Setup viewport and scissor
    VkViewport viewport = {};
    viewport.x = 0.0f;
    viewport.y = (float)swapHeight;
    viewport.width = (float)swapWidth;
    viewport.height = -(float)swapHeight;  // Flip Y for Vulkan
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor = {};
    scissor.offset = {0, 0};
    scissor.extent = {swapWidth, swapHeight};
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    // ========================================================================
    // Bind pipeline and push constants
    // ========================================================================
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_Pipeline);

    VkPipelineLayout layout = VK::g_PipelineManager->GetLayout();
    vkCmdPushConstants(cmd, layout,
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        0, sizeof(m_Constants), &m_Constants);

    // ========================================================================
    // Render each object type
    // ========================================================================
    u32 total_draws = 0;
    u32 total_instances = 0;
    for (u32 obj_id = 0; obj_id < objects.size(); obj_id++)
    {
        VK::CDetail* obj = objects[obj_id];
        if (!obj || !obj->m_VertexBuffer || !obj->m_IndexBuffer)
            continue;

        // Render each animation type separately
        for (int anim = 0; anim < 3; anim++)
        {
            if (m_visibles[anim].size() <= obj_id)
                continue;

            xr_vector<SlotItemVec*>& vis_vec = m_visibles[anim][obj_id];
            if (vis_vec.empty())
                continue;

            // Build instance buffer for this (object, animation) combo
            m_InstanceBuffer.BeginUpdate();

            for (u32 i = 0; i < vis_vec.size(); i++)
            {
                SlotItemVec* items = vis_vec[i];
                if (!items)
                    continue;

                for (SlotItemVecIt it = items->begin(); it != items->end(); ++it)
                {
                    SlotItem* Item = *it;

                    m_InstanceBuffer.AddInstance(
                        Item->mRotY,
                        Item->c_sun,
                        Item->c_hemi,
                        Item->scale_calculated
                    );
                }
            }

            u32 instance_count = m_InstanceBuffer.EndUpdate();
            if (instance_count == 0)
                continue;

            // Bind base mesh vertex buffer
            VkBuffer vb = obj->m_VertexBuffer->GetHandle();
            VkDeviceSize vb_offset = 0;
            vkCmdBindVertexBuffers(cmd, 0, 1, &vb, &vb_offset);

            // Bind instance buffer
            VkBuffer ib_inst = m_InstanceBuffer.GetBuffer()->GetHandle();
            VkDeviceSize ib_offset = 0;
            vkCmdBindVertexBuffers(cmd, 1, 1, &ib_inst, &ib_offset);

            // Bind index buffer
            VkBuffer ib = obj->m_IndexBuffer->GetHandle();
            vkCmdBindIndexBuffer(cmd, ib, 0, VK_INDEX_TYPE_UINT16);

            // Draw indexed instanced
            vkCmdDrawIndexed(cmd, obj->m_IndexCount, instance_count, 0, 0, 0);
            total_draws++;
            total_instances += instance_count;
        }
    }

    // Diagnostic: log draw stats
    {
        static u32 s_draw_diag = 0;
        if (RDEVICE.dwFrame > s_draw_diag + 120)
        {
            s_draw_diag = RDEVICE.dwFrame;
            Msg("[Detail Draw] draws=%u instances=%u pipeline=%p constants_size=%u",
                total_draws, total_instances, (void*)m_Pipeline, (u32)sizeof(m_Constants));
        }
    }

    // ========================================================================
    // End rendering and transition back
    // ========================================================================
    vkCmdEndRendering(cmd);

    // Transition swapchain: COLOR_ATTACHMENT -> PRESENT_SRC
    // Depth stays in DEPTH_ATTACHMENT_OPTIMAL
    VkImageMemoryBarrier finalBarriers[2] = {};

    finalBarriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    finalBarriers[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    finalBarriers[0].dstAccessMask = 0;
    finalBarriers[0].oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    finalBarriers[0].newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    finalBarriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    finalBarriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    finalBarriers[0].image = swapchainImage;
    finalBarriers[0].subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

    finalBarriers[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    finalBarriers[1].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    finalBarriers[1].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    finalBarriers[1].oldLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    finalBarriers[1].newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    finalBarriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    finalBarriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    finalBarriers[1].image = depthImage;
    finalBarriers[1].subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 };

    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
        0, 0, nullptr, 0, nullptr, 2, finalBarriers);

    m_frame_rendered = RDEVICE.dwFrame;
}

} // namespace VK
