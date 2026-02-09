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
#include "../../xrEngine/IGame_Level.h"
#include "../../xrEngine/xr_object.h"
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

    // ========================================================================
    // GPU procedural generation path (no CPU cache needed, zero pop-in)
    // ========================================================================
    if (m_bGpuGenerationEnabled && m_GenPipeline.IsValid())
    {
        float factor = g_pGamePersistent->Environment().wind_strength_factor;
        swing_current.lerp(swing_desc[0], swing_desc[1], factor);
        UpdateWindAnimation();

        Fmatrix mVP;
        mVP.mul(RDEVICE.mProject, RDEVICE.mView);
        m_Constants.mViewProj = mVP;
        m_Constants.vConsts.set(1.0f, 1.0f,
            g_pGamePersistent->Environment().CurrentEnv->sun_dir.y, 0.2f);

        // Grass interaction: player + up to 3 nearest NPCs/mutants
        {
            Fvector camPos = RDEVICE.vCameraPosition_saved;
            m_Constants.vInteractors[0].set(camPos.x, camPos.y, camPos.z, 1.2f);

            // Clear NPC slots
            for (u32 s = 1; s < MAX_GRASS_INTERACTORS; s++)
                m_Constants.vInteractors[s].set(0, 0, 0, 0);

            // Find up to 3 nearest visible NPCs within interaction range
            if (g_pGameLevel)
            {
                const float MAX_NPC_DIST_SQ = 15.f * 15.f; // 15m max
                CObject* playerEnt = g_pGameLevel->CurrentEntity();

                // Track 3 closest NPCs (insertion sort by distance)
                struct { float distSq; CObject* obj; } closest[3];
                for (int c = 0; c < 3; c++) { closest[c].distSq = MAX_NPC_DIST_SQ; closest[c].obj = nullptr; }

                u32 objCount = g_pGameLevel->Objects.o_count();
                for (u32 i = 0; i < objCount; i++)
                {
                    CObject* O = g_pGameLevel->Objects.o_get_by_iterator(i);
                    if (!O || O == playerEnt) continue;
                    if (!O->getVisible()) continue;

                    Fvector pos = O->Position();
                    float dx = pos.x - camPos.x;
                    float dz = pos.z - camPos.z;
                    float dSq = dx * dx + dz * dz;

                    if (dSq >= closest[2].distSq) continue;

                    // Insert into sorted array
                    if (dSq < closest[0].distSq)
                    {
                        closest[2] = closest[1]; closest[1] = closest[0];
                        closest[0].distSq = dSq; closest[0].obj = O;
                    }
                    else if (dSq < closest[1].distSq)
                    {
                        closest[2] = closest[1];
                        closest[1].distSq = dSq; closest[1].obj = O;
                    }
                    else
                    {
                        closest[2].distSq = dSq; closest[2].obj = O;
                    }
                }

                for (int c = 0; c < 3; c++)
                {
                    if (closest[c].obj)
                    {
                        Fvector p = closest[c].obj->Position();
                        float r = closest[c].obj->Radius();
                        r = _max(r, 0.5f); // minimum interaction radius
                        r = _min(r, 2.0f); // cap to avoid huge pushback
                        m_Constants.vInteractors[1 + c].set(p.x, p.y, p.z, r);
                    }
                }
            }
        }

        RenderGpuGenerated();
        return;
    }

    // ========================================================================
    // Legacy CPU cache + GPU cull path
    // ========================================================================
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

    m_frame_rendered = RDEVICE.dwFrame;

    if (m_GpuDataDirty && m_AllInstancesSSBO)
        UploadStagingToSSBO();

    if (!m_bGpuDrivenEnabled || !m_CullPipeline.IsValid() || !m_FinalizePipeline.IsValid()
        || !m_AllInstancesSSBO || !m_VisibleSSBO || m_TotalGpuInstances == 0)
        return;

    float factor = g_pGamePersistent->Environment().wind_strength_factor;
    swing_current.lerp(swing_desc[0], swing_desc[1], factor);
    UpdateWindAnimation();

    Fmatrix mVP;
    mVP.mul(RDEVICE.mProject, RDEVICE.mView);
    m_Constants.mViewProj = mVP;
    m_Constants.vConsts.set(1.0f, 1.0f,
        g_pGamePersistent->Environment().CurrentEnv->sun_dir.y, 0.2f);

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
    float fade_start = fade_limit * 0.65f;  // Full grass up to 65% of range, then fade
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

// ============================================================================
// RenderGpuGenerated - GPU procedural grass generation + render
// Generates grass positions from heightmap + slot palette data entirely on GPU.
// No CPU cache, no pop-in.
// ============================================================================
void CDetailManager::RenderGpuGenerated()
{
    VkCommandBuffer cmd = RCache.GetCommandBuffer();
    if (cmd == VK_NULL_HANDLE)
        return;

    // Check all required resources
    if (!m_GenPipeline.IsValid() || !m_SlotDataSSBO || !m_ObjInfoSSBO ||
        !m_VisibleSSBO || !m_IndirectCmdBuf || !m_AtomicCounters ||
        m_HeightmapImage == VK_NULL_HANDLE || !m_GenUBO ||
        m_GenDescSet == VK_NULL_HANDLE)
        return;

    // ========================================================================
    // Phase 1: Compute camera slot range
    // GPU gen uses its own radius, independent of dm_size/dm_cache_line
    // (those are legacy cache vars that user.ltx can override to small values)
    // ========================================================================
    const float gen_radius = 200.0f;  // 200m grass radius (independent of r__detail_radius)
    const int gen_half = iFloor(gen_radius / dm_slot_size);  // 100 slots
    const int gen_line = gen_half + 1 + gen_half;            // 201 slots per dimension

    Fvector EYE = RDEVICE.vCameraPosition_saved;
    int cam_sx = iFloor(EYE.x / dm_slot_size + 0.5f);
    int cam_sz = iFloor(EYE.z / dm_slot_size + 0.5f);

    int slotMinSX = cam_sx - gen_half;
    int slotMinSZ = cam_sz - gen_half;
    int slotCountX = gen_line;
    int slotCountZ = gen_line;

    // Grid density: positions per slot
    u32 d_size = (u32)ceilf(dm_slot_size / _max(ps_current_detail_density, 0.1f));
    if (d_size < 2) d_size = 2;
    if (d_size > 10) d_size = 10;
    u32 posPerSlot = (d_size + 1) * (d_size + 1);
    u32 totalPositions = (u32)slotCountX * (u32)slotCountZ * posPerSlot;

    // ========================================================================
    // Phase 2: Update GenUBO with per-frame params
    // ========================================================================
    {
        DetailGenUBO ubo;
        ubo.hmOriginX = m_HMOriginX;
        ubo.hmOriginZ = m_HMOriginZ;
        ubo.hmInvScaleX = (m_HMWorldSizeX > 0) ? (1.0f / m_HMWorldSizeX) : 0.0f;
        ubo.hmInvScaleZ = (m_HMWorldSizeZ > 0) ? (1.0f / m_HMWorldSizeZ) : 0.0f;
        ubo.totalPositions = totalPositions;
        ubo.numObjTypes = (u32)objects.size();
        ubo.outputCapacity = GPU_OUTPUT_CAPACITY;
        ubo.posPerSlot = posPerSlot;
        ubo.dtOffsX = (float)dtH.offs_x;
        ubo.dtOffsZ = (float)dtH.offs_z;
        ubo.dtSizeX = (float)dtH.size_x;
        ubo.dtSizeZ = (float)dtH.size_z;
        ubo.slotSize = dm_slot_size;
        ubo.detailHeight = ps_current_detail_height;
        ubo.hmWidth = m_HeightmapW;
        ubo.hmHeight = m_HeightmapH;

        void* mapped = m_GenUBO->m_Mapped;
        if (mapped)
        {
            memcpy(mapped, &ubo, sizeof(ubo));
            m_GenUBO->Flush();
        }
    }

    // ========================================================================
    // Phase 3: Update generation descriptor set
    // ========================================================================
    {
        // Binding 0: Heightmap sampler
        VkDescriptorImageInfo hmInfo = {};
        hmInfo.imageView = m_HeightmapView;
        hmInfo.sampler = m_HeightmapSampler;
        hmInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        // Bindings 1-5: Storage buffers
        VkDescriptorBufferInfo bufInfos[5] = {};
        bufInfos[0].buffer = m_SlotDataSSBO->GetHandle();
        bufInfos[0].offset = 0;
        bufInfos[0].range = VK_WHOLE_SIZE;

        bufInfos[1].buffer = m_ObjInfoSSBO->GetHandle();
        bufInfos[1].offset = 0;
        bufInfos[1].range = VK_WHOLE_SIZE;

        bufInfos[2].buffer = m_VisibleSSBO->GetHandle();
        bufInfos[2].offset = 0;
        bufInfos[2].range = VK_WHOLE_SIZE;

        bufInfos[3].buffer = m_AtomicCounters->GetHandle();
        bufInfos[3].offset = 0;
        bufInfos[3].range = VK_WHOLE_SIZE;

        bufInfos[4].buffer = m_IndirectCmdBuf->GetHandle();
        bufInfos[4].offset = 0;
        bufInfos[4].range = VK_WHOLE_SIZE;

        // Binding 6: HZB texture (dummy if not available)
        VkDescriptorImageInfo hzbInfo = {};
        if (m_HZBView != VK_NULL_HANDLE && m_HZBSampler != VK_NULL_HANDLE)
        {
            hzbInfo.imageView = m_HZBView;
            hzbInfo.sampler = m_HZBSampler;
            hzbInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        }
        else
        {
            CVulkanTexture* white = g_MaterialManager ? g_MaterialManager->GetWhiteTexture() : nullptr;
            if (white && white->IsValid())
            {
                hzbInfo.imageView = white->GetView();
                hzbInfo.sampler = white->GetSampler();
                hzbInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            }
            else
                return;
        }

        // Binding 7: GenUBO
        VkDescriptorBufferInfo uboInfo = {};
        uboInfo.buffer = m_GenUBO->GetHandle();
        uboInfo.offset = 0;
        uboInfo.range = sizeof(DetailGenUBO);

        VkWriteDescriptorSet writes[9] = {};

        // binding 0: heightmap sampler
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = m_GenDescSet;
        writes[0].dstBinding = 0;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[0].descriptorCount = 1;
        writes[0].pImageInfo = &hmInfo;

        // bindings 1-5: storage buffers
        for (int i = 0; i < 5; i++)
        {
            writes[1 + i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[1 + i].dstSet = m_GenDescSet;
            writes[1 + i].dstBinding = 1 + i;
            writes[1 + i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[1 + i].descriptorCount = 1;
            writes[1 + i].pBufferInfo = &bufInfos[i];
        }

        // binding 6: HZB sampler
        writes[6].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[6].dstSet = m_GenDescSet;
        writes[6].dstBinding = 6;
        writes[6].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[6].descriptorCount = 1;
        writes[6].pImageInfo = &hzbInfo;

        // binding 7: UBO
        writes[7].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[7].dstSet = m_GenDescSet;
        writes[7].dstBinding = 7;
        writes[7].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[7].descriptorCount = 1;
        writes[7].pBufferInfo = &uboInfo;

        // binding 8: Trail map sampler
        VkDescriptorImageInfo trailInfo = {};
        if (m_TrailView != VK_NULL_HANDLE && m_TrailSampler != VK_NULL_HANDLE)
        {
            trailInfo.imageView = m_TrailView;
            trailInfo.sampler = m_TrailSampler;
            trailInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        }
        else
        {
            // Use white texture as dummy
            CVulkanTexture* white = g_MaterialManager ? g_MaterialManager->GetWhiteTexture() : nullptr;
            if (white && white->IsValid())
            {
                trailInfo.imageView = white->GetView();
                trailInfo.sampler = white->GetSampler();
                trailInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            }
            else
                return;
        }

        writes[8].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[8].dstSet = m_GenDescSet;
        writes[8].dstBinding = 8;
        writes[8].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[8].descriptorCount = 1;
        writes[8].pImageInfo = &trailInfo;

        vkUpdateDescriptorSets(VulkanHW.GetDevice(), 9, writes, 0, nullptr);
    }

    // ========================================================================
    // Phase 4: Build HZB from depth buffer (if available)
    // ========================================================================
    if (m_HZBImage != VK_NULL_HANDLE && m_HZBBuildPipeline.IsValid())
    {
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
    // Phase 4.5: Dispatch trail map update (fade + stamp interactors)
    // ========================================================================
    if (m_TrailImage != VK_NULL_HANDLE && m_TrailPipeline.IsValid())
    {
        // Barrier: previous gen compute read → trail compute write
        VkImageMemoryBarrier trailBar = {};
        trailBar.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        trailBar.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        trailBar.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        trailBar.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        trailBar.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        trailBar.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        trailBar.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        trailBar.image = m_TrailImage;
        trailBar.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &trailBar);

        // Build trail push constants
        TrailPushConstants trailPC;
        for (u32 i = 0; i < MAX_GRASS_INTERACTORS; i++)
            trailPC.interactors[i] = m_Constants.vInteractors[i];
        trailPC.originX = m_HMOriginX;
        trailPC.originZ = m_HMOriginZ;
        trailPC.texelSizeX = m_HMWorldSizeX / (float)m_HeightmapW;
        trailPC.texelSizeZ = m_HMWorldSizeZ / (float)m_HeightmapH;

        // Fade: half-life ~15 seconds. fadeRate = pow(0.5, dt/15)
        float dt = RDEVICE.fTimeDelta;
        trailPC.fadeRate = powf(0.5f, dt / 15.0f);
        trailPC.mapW = m_HeightmapW;
        trailPC.mapH = m_HeightmapH;
        trailPC._pad = 0;

        m_TrailPipeline.Bind(cmd);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_TrailPipelineLayout,
            0, 1, &m_TrailDescSet, 0, nullptr);
        vkCmdPushConstants(cmd, m_TrailPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT,
            0, sizeof(trailPC), &trailPC);

        u32 trailGroupsX = (m_HeightmapW + 15) / 16;
        u32 trailGroupsY = (m_HeightmapH + 15) / 16;
        vkCmdDispatch(cmd, trailGroupsX, trailGroupsY, 1);

        // Barrier: trail compute write → gen compute read (sampling)
        trailBar.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        trailBar.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &trailBar);
    }

    // ========================================================================
    // Phase 5: Clear buffers + pre-fill indirect commands
    // ========================================================================

    // Barrier: ensure previous frame's vertex reads of VisibleSSBO are done
    {
        VkBufferMemoryBarrier prevBar = {};
        prevBar.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        prevBar.srcAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
        prevBar.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        prevBar.buffer = m_VisibleSSBO->GetHandle();
        prevBar.offset = 0;
        prevBar.size = VK_WHOLE_SIZE;
        prevBar.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        prevBar.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 1, &prevBar, 0, nullptr);
    }

    // Clear VisibleSSBO to zero (prevents stale data from previous frame)
    vkCmdFillBuffer(cmd, m_VisibleSSBO->GetHandle(), 0, VK_WHOLE_SIZE, 0);

    // Pre-fill indirect commands with indexCount per object type
    {
        VkDrawIndexedIndirectCommand cmds[GPU_MAX_OBJ_TYPES] = {};
        for (u32 i = 0; i < objects.size() && i < GPU_MAX_OBJ_TYPES; i++)
        {
            if (objects[i])
                cmds[i].indexCount = objects[i]->m_IndexCount;
        }
        u32 cmdSize = _min((u32)objects.size(), (u32)GPU_MAX_OBJ_TYPES) * sizeof(VkDrawIndexedIndirectCommand);
        vkCmdUpdateBuffer(cmd, m_IndirectCmdBuf->GetHandle(), 0, cmdSize, cmds);
    }

    // Clear atomic counters
    vkCmdFillBuffer(cmd, m_AtomicCounters->GetHandle(), 0, VK_WHOLE_SIZE, 0);

    // Barrier: all transfer writes -> compute read/write
    {
        VkBufferMemoryBarrier bars[3] = {};

        bars[0].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        bars[0].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        bars[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        bars[0].buffer = m_VisibleSSBO->GetHandle();
        bars[0].offset = 0;
        bars[0].size = VK_WHOLE_SIZE;
        bars[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bars[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

        bars[1].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        bars[1].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        bars[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        bars[1].buffer = m_AtomicCounters->GetHandle();
        bars[1].offset = 0;
        bars[1].size = VK_WHOLE_SIZE;
        bars[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bars[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

        bars[2].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        bars[2].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        bars[2].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        bars[2].buffer = m_IndirectCmdBuf->GetHandle();
        bars[2].offset = 0;
        bars[2].size = VK_WHOLE_SIZE;
        bars[2].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bars[2].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 3, bars, 0, nullptr);
    }

    // ========================================================================
    // Phase 6: Build push constants + dispatch generation compute
    // ========================================================================
    Fmatrix mVP;
    mVP.mul(RDEVICE.mProject, RDEVICE.mView);

    Fvector4 frustumPlanes[6];
    ExtractFrustumPlanes(mVP, frustumPlanes);

    float fade_limit = gen_radius - 0.5f;  // 199.5m (independent of dm_fade)
    float fade_start = fade_limit * 0.65f;  // ~130m: full grass up to here, then fade
    float fade_limit_sq = fade_limit * fade_limit;
    float fade_start_sq = fade_start * fade_start;
    float fade_range_sq = fade_limit_sq - fade_start_sq;

    DetailGenPushConstants pc;
    pc.viewProj = mVP;
    for (int i = 0; i < 6; i++)
        pc.frustumPlanes[i] = frustumPlanes[i];
    pc.cameraPos.set(EYE.x, EYE.y, EYE.z, RDEVICE.fTimeGlobal);
    pc.fadeParams.set(fade_start_sq, fade_limit_sq, fade_range_sq, ps_current_detail_density);
    pc.slotMinSX = slotMinSX;
    pc.slotMinSZ = slotMinSZ;
    pc.slotCountX = slotCountX;
    pc.slotCountZ = slotCountZ;

    m_GenPipeline.Bind(cmd);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_GenPipelineLayout,
        0, 1, &m_GenDescSet, 0, nullptr);
    vkCmdPushConstants(cmd, m_GenPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT,
        0, sizeof(pc), &pc);

    u32 groupCount = (totalPositions + 255) / 256;
    vkCmdDispatch(cmd, groupCount, 1, 1);

    // ========================================================================
    // Phase 7: Copy atomic counters -> indirect instanceCount fields
    // ========================================================================
    {
        // Barrier: compute write -> transfer read
        VkMemoryBarrier memBar = {};
        memBar.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        memBar.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        memBar.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &memBar, 0, nullptr, 0, nullptr);

        // Copy each counter[i] -> indirect[i].instanceCount (offset 4 in each 20-byte cmd)
        u32 numObj = _min((u32)objects.size(), (u32)GPU_MAX_OBJ_TYPES);
        VkBufferCopy copies[GPU_MAX_OBJ_TYPES];
        for (u32 i = 0; i < numObj; i++)
        {
            copies[i].srcOffset = i * sizeof(u32);
            copies[i].dstOffset = i * sizeof(VkDrawIndexedIndirectCommand) + 4;
            copies[i].size = sizeof(u32);
        }
        vkCmdCopyBuffer(cmd, m_AtomicCounters->GetHandle(), m_IndirectCmdBuf->GetHandle(),
            numObj, copies);
    }

    // Barrier: compute+transfer -> vertex input + indirect read
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
        bufBars[1].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        bufBars[1].dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
        bufBars[1].buffer = m_IndirectCmdBuf->GetHandle();
        bufBars[1].offset = 0;
        bufBars[1].size = VK_WHOLE_SIZE;
        bufBars[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bufBars[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
            0, 0, nullptr, 2, bufBars, 0, nullptr);
    }

    // ========================================================================
    // Phase 8: Graphics rendering
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
    // Phase 9: Indirect draws per object type
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

        // Indirect draw
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
        static u32 s_gen_diag = 0;
        if (RDEVICE.dwFrame > s_gen_diag + 120)
        {
            s_gen_diag = RDEVICE.dwFrame;
            Msg("[Detail GPU Gen] draws=%u totalPositions=%u dispatch=(%u,1,1) objTypes=%u slotsInRange=%ux%u",
                total_draws, totalPositions, groupCount, (u32)objects.size(),
                slotCountX, slotCountZ);
        }
    }
}

} // namespace VK
