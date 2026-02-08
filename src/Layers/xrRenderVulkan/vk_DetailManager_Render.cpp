// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// SPDX-License-Identifier: MIT

#include "stdafx.h"
#include "vk_DetailManager.h"
#include "vk_R_Backend.h"
#include "rvk.h"
#include "vk_swapchain.h"
#include "vk_pipeline.h"
#include "vk_descriptors.h"
#include "vk_material.h"
#include "vk_rendertarget.h"
#include "HW_Vulkan.h"
#include "../xrRender/DetailFormat.h"
#include "../../xrEngine/IGame_Persistent.h"
#include "../../xrEngine/Environment.h"

namespace VK
{

// ============================================================================
// BindDetailTexture - Bind a detail object's texture to Set 1 (PerMaterial)
// Allocates a fresh descriptor set each frame (pool is reset per-frame).
// ============================================================================
static bool BindDetailTexture(VkCommandBuffer cmd, CVulkanTexture* tex)
{
    if (!g_DescriptorManager || !g_MaterialManager || !tex || !tex->IsValid())
        return false;

    VkDescriptorSet descSet = g_DescriptorManager->AllocatePerMaterial();
    if (descSet == VK_NULL_HANDLE)
        return false;

    CVulkanTexture* white = g_MaterialManager->GetWhiteTexture();
    CVulkanTexture* defNormal = g_MaterialManager->GetDefaultNormal();
    if (!white || !defNormal) return false;

    // Fill all 8 bindings (PerMaterial layout expects 8 combined_image_sampler)
    CVulkanTexture* textures[8] = {
        tex,        // 0: Diffuse
        defNormal,  // 1: Normal
        white,      // 2: Specular
        white,      // 3: Mask
        white,      // 4: Detail R
        white,      // 5: Detail G
        white,      // 6: Detail B
        white       // 7: Detail A
    };

    VkDescriptorImageInfo imageInfos[8] = {};
    VkWriteDescriptorSet writes[8] = {};
    for (int i = 0; i < 8; i++)
    {
        imageInfos[i].sampler = textures[i]->GetSampler();
        imageInfos[i].imageView = textures[i]->GetView();
        imageInfos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = descSet;
        writes[i].dstBinding = i;
        writes[i].dstArrayElement = 0;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[i].descriptorCount = 1;
        writes[i].pImageInfo = &imageInfos[i];
    }

    vkUpdateDescriptorSets(VulkanHW.GetDevice(), 8, writes, 0, nullptr);

    VkPipelineLayout layout = VK::g_PipelineManager->GetLayout();
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout,
        1, 1, &descSet, 0, nullptr);  // Set 1 = PerMaterial

    return true;
}

// ============================================================================
// Render - Main rendering entry point (dispatches to GPU or CPU path)
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

    // Cache management (slot decompression) must run BEFORE GPU readiness check,
    // because cache_Decompress() populates m_StagingInstances and sets m_GpuDataDirty.
    if (m_frame_calc != RDEVICE.dwFrame)
    {
        MT.Enter();
        if (m_frame_calc != RDEVICE.dwFrame)
        {
            if ((m_frame_rendered + 1) == RDEVICE.dwFrame)
            {
                Fvector EYE = RDEVICE.vCameraPosition_saved;
                int s_x = iFloor(EYE.x / dm_slot_size + .5f);
                int s_z = iFloor(EYE.z / dm_slot_size + .5f);
                cache_Update(s_x, s_z, EYE, dm_max_decompress);
                m_frame_calc = RDEVICE.dwFrame;
            }
        }
        MT.Leave();
    }

    // Always track frame for consecutive-frame guard, even if GPU path isn't ready yet.
    // Without this, m_frame_rendered stays at initial 0xFFFFFFFF and the guard
    // (m_frame_rendered + 1) == dwFrame never passes → cache_Update never runs.
    m_frame_rendered = RDEVICE.dwFrame;

    // Upload SSBO if dirty (cache_Decompress may have set m_GpuDataDirty)
    if (m_GpuDataDirty && m_AllInstancesSSBO)
        UploadStagingToSSBO();

    // Check GPU resources are ready
    if (!m_bGpuDrivenEnabled || !m_CullPipeline.IsValid() || !m_FinalizePipeline.IsValid()
        || !m_AllInstancesSSBO || !m_VisibleSSBO || m_TotalGpuInstances == 0)
        return;

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
        1.0f,
        1.0f,
        g_pGamePersistent->Environment().CurrentEnv->sun_dir.y,
        0.2f
    );

    RenderGpuDriven();
}

// ============================================================================
// RenderGpuDriven - GPU compute cull + indirect draw path
// ============================================================================
void CDetailManager::RenderGpuDriven()
{
    VkCommandBuffer cmd = RCache.GetCommandBuffer();
    if (cmd == VK_NULL_HANDLE)
        return;

    if (m_TotalGpuInstances == 0)
        return;

    // ========================================================================
    // Phase 1: Update compute descriptor set with current buffer handles
    // ========================================================================
    {
        VkDescriptorBufferInfo bufInfos[4] = {};
        bufInfos[0].buffer = m_AllInstancesSSBO->GetHandle();
        bufInfos[0].offset = 0;
        bufInfos[0].range = VK_WHOLE_SIZE;

        bufInfos[1].buffer = m_VisibleSSBO->GetHandle();
        bufInfos[1].offset = 0;
        bufInfos[1].range = VK_WHOLE_SIZE;

        bufInfos[2].buffer = m_AtomicCounters->GetHandle();
        bufInfos[2].offset = 0;
        bufInfos[2].range = VK_WHOLE_SIZE;

        bufInfos[3].buffer = m_IndirectCmdBuf->GetHandle();
        bufInfos[3].offset = 0;
        bufInfos[3].range = VK_WHOLE_SIZE;

        VkWriteDescriptorSet writes[5] = {};
        for (int i = 0; i < 4; i++)
        {
            writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[i].dstSet = m_ComputeDescSet;
            writes[i].dstBinding = i;
            writes[i].dstArrayElement = 0;
            writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[i].descriptorCount = 1;
            writes[i].pBufferInfo = &bufInfos[i];
        }

        // binding 4: HZB texture (use a dummy if not available)
        VkDescriptorImageInfo hzbInfo = {};
        if (m_HZBView != VK_NULL_HANDLE && m_HZBSampler != VK_NULL_HANDLE)
        {
            hzbInfo.imageView = m_HZBView;
            hzbInfo.sampler = m_HZBSampler;
            hzbInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        }
        else
        {
            // Use white texture as dummy
            CVulkanTexture* white = g_MaterialManager ? g_MaterialManager->GetWhiteTexture() : nullptr;
            if (white && white->IsValid())
            {
                hzbInfo.imageView = white->GetView();
                hzbInfo.sampler = white->GetSampler();
                hzbInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            }
            else
            {
                // Can't proceed without a valid image for binding 4
                return;
            }
        }

        writes[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[4].dstSet = m_ComputeDescSet;
        writes[4].dstBinding = 4;
        writes[4].dstArrayElement = 0;
        writes[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[4].descriptorCount = 1;
        writes[4].pImageInfo = &hzbInfo;

        vkUpdateDescriptorSets(VulkanHW.GetDevice(), 5, writes, 0, nullptr);
    }

    // ========================================================================
    // Phase 1.5: Build HZB from depth buffer (if available)
    // ========================================================================
    if (m_HZBImage != VK_NULL_HANDLE && m_HZBBuildPipeline.IsValid())
    {
        // Barrier: depth write -> shader read (for HZB build to sample depth)
        VkImageMemoryBarrier depthToRead = {};
        depthToRead.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        depthToRead.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        depthToRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        depthToRead.oldLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        depthToRead.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        depthToRead.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        depthToRead.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        depthToRead.image = Swapchain.m_DepthImage;
        depthToRead.subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 };

        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &depthToRead);

        BuildHZB(cmd);

        // Barrier: HZB compute write -> cull shader read
        VkImageMemoryBarrier hzbToRead = {};
        hzbToRead.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        hzbToRead.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        hzbToRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        hzbToRead.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        hzbToRead.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        hzbToRead.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        hzbToRead.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        hzbToRead.image = m_HZBImage;
        hzbToRead.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, m_HZBMipLevels, 0, 1 };

        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &hzbToRead);

        // Transition depth back to attachment optimal for later rendering
        VkImageMemoryBarrier depthBack = {};
        depthBack.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        depthBack.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        depthBack.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
        depthBack.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        depthBack.newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        depthBack.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        depthBack.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        depthBack.image = Swapchain.m_DepthImage;
        depthBack.subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 };

        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
            0, 0, nullptr, 0, nullptr, 1, &depthBack);
    }

    // ========================================================================
    // Phase 2: Pre-fill indirect commands with indexCount per object type
    // and clear atomic counters
    // ========================================================================
    {
        // Build indirect commands on CPU with correct indexCount, everything else 0
        VkDrawIndexedIndirectCommand cmds[GPU_MAX_OBJ_TYPES] = {};
        for (u32 i = 0; i < objects.size() && i < GPU_MAX_OBJ_TYPES; i++)
        {
            if (objects[i])
                cmds[i].indexCount = objects[i]->m_IndexCount;
        }
        // Inline update into command buffer — matches TRANSFER_WRITE barrier below,
        // avoids host→device sync issues. Data must be < 65536 bytes (Vulkan spec).
        u32 cmdSize = (u32)objects.size() * sizeof(VkDrawIndexedIndirectCommand);
        vkCmdUpdateBuffer(cmd, m_IndirectCmdBuf->GetHandle(), 0, cmdSize, cmds);
    }

    vkCmdFillBuffer(cmd, m_AtomicCounters->GetHandle(), 0, VK_WHOLE_SIZE, 0);

    // Barrier: transfer writes (atomic counters clear + indirect prefill) -> compute read
    {
        VkBufferMemoryBarrier bars[2] = {};

        bars[0].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        bars[0].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        bars[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        bars[0].buffer = m_AtomicCounters->GetHandle();
        bars[0].offset = 0;
        bars[0].size = VK_WHOLE_SIZE;
        bars[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bars[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

        bars[1].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        bars[1].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        bars[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        bars[1].buffer = m_IndirectCmdBuf->GetHandle();
        bars[1].offset = 0;
        bars[1].size = VK_WHOLE_SIZE;
        bars[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bars[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 2, bars, 0, nullptr);
    }

    // ========================================================================
    // Phase 3: Build push constants for compute
    // ========================================================================
    Fmatrix mVP;
    mVP.mul(RDEVICE.mProject, RDEVICE.mView);

    Fvector4 frustumPlanes[6];
    ExtractFrustumPlanes(mVP, frustumPlanes);

    float fade_limit = dm_fade;
    float fade_start = 1.0f;
    float fade_limit_sq = fade_limit * fade_limit;
    float fade_start_sq = fade_start * fade_start;
    float fade_range_sq = fade_limit_sq - fade_start_sq;

    // Pack into push constant struct
    struct {
        DetailCullConstants cull;
        DetailCullCounts    counts;
    } pushData;

    pushData.cull.viewProj = mVP;
    for (int i = 0; i < 6; i++)
        pushData.cull.frustumPlanes[i] = frustumPlanes[i];
    pushData.cull.cameraPos.set(RDEVICE.vCameraPosition_saved.x,
                                RDEVICE.vCameraPosition_saved.y,
                                RDEVICE.vCameraPosition_saved.z, 0.0f);
    pushData.cull.fadeParams.set(fade_start_sq, fade_limit_sq, fade_range_sq, RDEVICE.fTimeGlobal);

    pushData.counts.totalInstances = m_TotalGpuInstances;
    pushData.counts.numObjTypes = (u32)objects.size();
    pushData.counts._pad0 = GPU_OUTPUT_CAPACITY;  // outputCapacity for section sizing
    pushData.counts._pad1 = 0;

    // ========================================================================
    // Phase 4: Dispatch cull compute shader
    // ========================================================================
    m_CullPipeline.Bind(cmd);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_ComputeLayout,
        0, 1, &m_ComputeDescSet, 0, nullptr);
    vkCmdPushConstants(cmd, m_ComputeLayout, VK_SHADER_STAGE_COMPUTE_BIT,
        0, sizeof(pushData), &pushData);

    u32 groupCount = (m_TotalGpuInstances + 255) / 256;
    vkCmdDispatch(cmd, groupCount, 1, 1);

    // Barrier: compute write -> compute read (for finalize)
    {
        VkMemoryBarrier memBar = {};
        memBar.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        memBar.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        memBar.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &memBar, 0, nullptr, 0, nullptr);
    }

    // ========================================================================
    // Phase 5: Dispatch finalize (1 workgroup of 64 threads)
    // ========================================================================
    m_FinalizePipeline.Bind(cmd);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_ComputeLayout,
        0, 1, &m_ComputeDescSet, 0, nullptr);
    vkCmdPushConstants(cmd, m_ComputeLayout, VK_SHADER_STAGE_COMPUTE_BIT,
        0, sizeof(pushData), &pushData);
    vkCmdDispatch(cmd, 1, 1, 1);

    // Barrier: compute write -> vertex input + indirect read
    {
        VkBufferMemoryBarrier bufBars[2] = {};

        bufBars[0].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        bufBars[0].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        bufBars[0].dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
        bufBars[0].buffer = m_VisibleSSBO->GetHandle();
        bufBars[0].offset = 0;
        bufBars[0].size = VK_WHOLE_SIZE;
        bufBars[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bufBars[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

        bufBars[1].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        bufBars[1].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        bufBars[1].dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
        bufBars[1].buffer = m_IndirectCmdBuf->GetHandle();
        bufBars[1].offset = 0;
        bufBars[1].size = VK_WHOLE_SIZE;
        bufBars[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bufBars[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
            0, 0, nullptr, 2, bufBars, 0, nullptr);
    }

    // ========================================================================
    // Phase 6: Graphics rendering (same rt_HDR + depth setup)
    // ========================================================================
    VkImage hdrImage = RTarget->rt_HDR.m_Image;
    VkImageView hdrView = RTarget->rt_HDR.m_ImageView;
    u32 hdrWidth = RTarget->rt_HDR.m_Width;
    u32 hdrHeight = RTarget->rt_HDR.m_Height;

    if (hdrImage == VK_NULL_HANDLE || hdrView == VK_NULL_HANDLE)
        return;

    VkImage depthImage = Swapchain.m_DepthImage;
    VkImageView depthView = Swapchain.m_DepthView;
    if (depthImage == VK_NULL_HANDLE || depthView == VK_NULL_HANDLE)
        return;

    // Pre-rendering barriers
    VkImageMemoryBarrier barriers[2] = {};
    barriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barriers[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barriers[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barriers[0].oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barriers[0].newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[0].image = hdrImage;
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

    // Begin rendering
    VkRenderingAttachmentInfo colorAttachment = {};
    colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttachment.imageView = hdrView;
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
    renderingInfo.renderArea.extent = {hdrWidth, hdrHeight};
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &colorAttachment;
    renderingInfo.pDepthAttachment = &depthAttachment;

    vkCmdBeginRendering(cmd, &renderingInfo);

    // Viewport and scissor
    VkViewport viewport = {};
    viewport.x = 0.0f;
    viewport.y = (float)hdrHeight;
    viewport.width = (float)hdrWidth;
    viewport.height = -(float)hdrHeight;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor = {};
    scissor.offset = {0, 0};
    scissor.extent = {hdrWidth, hdrHeight};
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    // Bind graphics pipeline and push constants
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_Pipeline);

    VkPipelineLayout gfxLayout = VK::g_PipelineManager->GetLayout();
    vkCmdPushConstants(cmd, gfxLayout,
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        0, sizeof(m_Constants), &m_Constants);

    // ========================================================================
    // Phase 7: Indirect draws per object type
    // Each object type's visible instances are at offset objId * sectionSize
    // in m_VisibleSSBO, with instanceCount in the indirect command buffer.
    // ========================================================================
    u32 sectionSize = GPU_OUTPUT_CAPACITY / _max((u32)objects.size(), 1u);
    u32 total_draws = 0;

    for (u32 obj_id = 0; obj_id < objects.size(); obj_id++)
    {
        VK::CDetail* obj = objects[obj_id];
        if (!obj || !obj->m_VertexBuffer || !obj->m_IndexBuffer)
            continue;

        // Bind texture
        CVulkanTexture* detailTex = obj->m_VkTexture;
        if (!detailTex && g_MaterialManager)
            detailTex = g_MaterialManager->GetWhiteTexture();
        if (!BindDetailTexture(cmd, detailTex))
            continue;

        // Bind base mesh vertex buffer (binding 0)
        VkBuffer vb = obj->m_VertexBuffer->GetHandle();
        VkDeviceSize vb_offset = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &vb, &vb_offset);

        // Bind instance buffer from VisibleSSBO at this object's section (binding 1)
        VkBuffer visBuf = m_VisibleSSBO->GetHandle();
        VkDeviceSize instOffset = (VkDeviceSize)obj_id * sectionSize * sizeof(DetailInstance);
        vkCmdBindVertexBuffers(cmd, 1, 1, &visBuf, &instOffset);

        // Bind index buffer
        VkBuffer ib = obj->m_IndexBuffer->GetHandle();
        vkCmdBindIndexBuffer(cmd, ib, 0, VK_INDEX_TYPE_UINT16);

        // Indirect draw — the finalize shader wrote instanceCount into the command
        // We need to patch indexCount on CPU side since compute doesn't know it.
        // Instead of reading back, we use vkCmdDrawIndexedIndirect with the command
        // that has instanceCount from GPU and we pre-fill indexCount.
        //
        // Since the finalize shader wrote indexCount=0, we use a simpler approach:
        // vkCmdDrawIndexedIndirect reads from the buffer. We need indexCount pre-filled.
        // We'll use a CPU-side pre-fill of indexCount into the indirect buffer before compute.

        // For now, use direct draw with indirect instance count:
        // We read instanceCount from indirect buffer via indirect draw
        VkDeviceSize indirectOffset = obj_id * sizeof(VkDrawIndexedIndirectCommand);
        vkCmdDrawIndexedIndirect(cmd, m_IndirectCmdBuf->GetHandle(), indirectOffset, 1,
            sizeof(VkDrawIndexedIndirectCommand));
        total_draws++;
    }

    vkCmdEndRendering(cmd);

    // Post-rendering barriers
    VkImageMemoryBarrier finalBarriers[2] = {};
    finalBarriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    finalBarriers[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    finalBarriers[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    finalBarriers[0].oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    finalBarriers[0].newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    finalBarriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    finalBarriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    finalBarriers[0].image = hdrImage;
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

    // Diagnostic
    {
        static u32 s_gpu_diag = 0;
        if (RDEVICE.dwFrame > s_gpu_diag + 120)
        {
            s_gpu_diag = RDEVICE.dwFrame;
            Msg("[Detail GPU] draws=%u gpuInstances=%u dispatch=(%u,1,1) objTypes=%u",
                total_draws, m_TotalGpuInstances, (m_TotalGpuInstances + 255) / 256,
                (u32)objects.size());
        }
    }
}

} // namespace VK
