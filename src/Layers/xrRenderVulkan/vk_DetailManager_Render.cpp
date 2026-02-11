// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// Licensed under the same terms as X-Ray Engine (see root License.txt)

#include "stdafx.h"
#include "vk_DetailManager.h"
#include "vk_R_Backend.h"
#include "rvk.h"
#include "vk_swapchain.h"
#include "vk_pipeline.h"
#include "vk_descriptors.h"
#include "vk_material.h"
#include "vk_rendertarget.h"
#include "vk_barriers.h"
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

    DescriptorWriter writer(descSet);
    for (int i = 0; i < 8; i++)
        writer.ImageSampler(i, textures[i]->GetView(), textures[i]->GetSampler());
    writer.Flush();

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
        CVulkanTexture* white = g_MaterialManager ? g_MaterialManager->GetWhiteTexture() : nullptr;
        if (!white || !white->IsValid())
            return;

        // Determine trail map texture (or use white as dummy)
        VkImageView   trailView;
        VkSampler     trailSampler;
        VkImageLayout trailLayout;
        if (m_TrailView != VK_NULL_HANDLE && m_TrailSampler != VK_NULL_HANDLE)
        {
            trailView    = m_TrailView;
            trailSampler = m_TrailSampler;
            trailLayout  = VK_IMAGE_LAYOUT_GENERAL;
        }
        else
        {
            trailView    = white->GetView();
            trailSampler = white->GetSampler();
            trailLayout  = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        }

        DescriptorWriter(m_GenDescSet)
            .ImageSampler(0, m_HeightmapView, m_HeightmapSampler)
            .StorageBuffer(1, m_SlotDataSSBO->GetHandle(), VK_WHOLE_SIZE)
            .StorageBuffer(2, m_ObjInfoSSBO->GetHandle(), VK_WHOLE_SIZE)
            .StorageBuffer(3, m_VisibleSSBO->GetHandle(), VK_WHOLE_SIZE)
            .StorageBuffer(4, m_AtomicCounters->GetHandle(), VK_WHOLE_SIZE)
            .StorageBuffer(5, m_IndirectCmdBuf->GetHandle(), VK_WHOLE_SIZE)
            .ImageSampler(6, white->GetView(), white->GetSampler())
            .UniformBuffer(7, m_GenUBO->GetHandle(), sizeof(DetailGenUBO))
            .ImageSampler(8, trailView, trailSampler, trailLayout)
            .Flush();
    }

    // ========================================================================
    // Phase 4: Dispatch trail map update (fade + stamp interactors)
    // ========================================================================
    if (m_TrailImage != VK_NULL_HANDLE && m_TrailPipeline.IsValid())
    {
        // Barrier: previous gen compute read -> trail compute write
        ImageBarrier(cmd, m_TrailImage, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);

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

        // Barrier: trail compute write -> gen compute read (sampling)
        ImageBarrier(cmd, m_TrailImage, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
    }

    // ========================================================================
    // Phase 5: Clear buffers + pre-fill indirect commands
    // ========================================================================

    // Barrier: ensure previous frame's vertex reads of VisibleSSBO are done
    BufferBarrier(cmd, m_VisibleSSBO->GetHandle(),
        VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT, VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT,
        VK_PIPELINE_STAGE_2_TRANSFER_BIT,     VK_ACCESS_2_TRANSFER_WRITE_BIT);

    // Clear VisibleSSBO to zero (prevents stale data from previous frame)
    vkCmdFillBuffer(cmd, m_VisibleSSBO->GetHandle(), 0, VK_WHOLE_SIZE, 0);

    // Pre-fill indirect commands with indexCount per object type
    {
        u32 sectionSize = GPU_OUTPUT_CAPACITY / _max((u32)objects.size(), 1u);
        VkDrawIndexedIndirectCommand cmds[GPU_MAX_OBJ_TYPES] = {};
        for (u32 i = 0; i < objects.size() && i < GPU_MAX_OBJ_TYPES; i++)
        {
            if (!objects[i]) continue;

            if (m_bMultiDrawEnabled)
            {
                // Bindless path: use merged geometry offsets
                cmds[i].indexCount    = m_ObjGeomInfo[i].indexCount;
                cmds[i].firstIndex    = m_ObjGeomInfo[i].firstIndex;
                cmds[i].vertexOffset  = m_ObjGeomInfo[i].vertexOffset;
                cmds[i].firstInstance = i * sectionSize;
            }
            else
            {
                // Fallback path: per-object buffers, instance offset set at bind time
                cmds[i].indexCount = objects[i]->m_IndexCount;
            }
        }
        u32 cmdSize = _min((u32)objects.size(), (u32)GPU_MAX_OBJ_TYPES) * sizeof(VkDrawIndexedIndirectCommand);
        vkCmdUpdateBuffer(cmd, m_IndirectCmdBuf->GetHandle(), 0, cmdSize, cmds);
    }

    // Clear atomic counters
    vkCmdFillBuffer(cmd, m_AtomicCounters->GetHandle(), 0, VK_WHOLE_SIZE, 0);

    // Barrier: all transfer writes -> compute read/write
    BufferBarrier(cmd, m_VisibleSSBO->GetHandle(),
        VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    BufferBarrier(cmd, m_AtomicCounters->GetHandle(),
        VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    BufferBarrier(cmd, m_IndirectCmdBuf->GetHandle(),
        VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);

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
        MemoryBarrier(cmd,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
            VK_PIPELINE_STAGE_2_TRANSFER_BIT,       VK_ACCESS_2_TRANSFER_READ_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT);

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
    BufferBarrier(cmd, m_VisibleSSBO->GetHandle(),
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
        VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT,   VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT);
    BufferBarrier(cmd, m_IndirectCmdBuf->GetHandle(),
        VK_PIPELINE_STAGE_2_TRANSFER_BIT,    VK_ACCESS_2_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT, VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT);

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
    ImageBarrier(cmd, hdrImage,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    ImageBarrier(cmd, depthImage,
        VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        VK_IMAGE_ASPECT_DEPTH_BIT);

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

    // ========================================================================
    // Phase 9: Indirect draws — bindless (1 call) or fallback (per-type loop)
    // ========================================================================
    u32 total_draws = 0;

    if (m_bMultiDrawEnabled && m_MergedVB && m_MergedIB && m_BindlessPipeline != VK_NULL_HANDLE)
    {
        // ---- Bindless path: single multi-draw call ----
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_BindlessPipeline);

        vkCmdPushConstants(cmd, m_BindlessPipelineLayout,
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            0, sizeof(m_Constants), &m_Constants);

        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_BindlessPipelineLayout,
            1, 1, &m_BindlessTexSet, 0, nullptr);  // Set 1 = texture array

        // Bind merged geometry (binding 0) + instance data (binding 1)
        VkBuffer vertexBuffers[2] = { m_MergedVB->GetHandle(), m_VisibleSSBO->GetHandle() };
        VkDeviceSize offsets[2] = { 0, 0 };
        vkCmdBindVertexBuffers(cmd, 0, 2, vertexBuffers, offsets);

        vkCmdBindIndexBuffer(cmd, m_MergedIB->GetHandle(), 0, VK_INDEX_TYPE_UINT16);

        u32 numObjTypes = _min((u32)objects.size(), (u32)GPU_MAX_OBJ_TYPES);
        vkCmdDrawIndexedIndirect(cmd, m_IndirectCmdBuf->GetHandle(), 0,
            numObjTypes, sizeof(VkDrawIndexedIndirectCommand));
        total_draws = 1;
    }
    else
    {
        // ---- Fallback path: per-type loop (unchanged) ----
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_Pipeline);

        VkPipelineLayout gfxLayout = VK::g_PipelineManager->GetLayout();
        vkCmdPushConstants(cmd, gfxLayout,
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            0, sizeof(m_Constants), &m_Constants);

        u32 sectionSize = GPU_OUTPUT_CAPACITY / _max((u32)objects.size(), 1u);

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
    }

    vkCmdEndRendering(cmd);

    // Post-rendering barriers
    ImageBarrier(cmd, hdrImage,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    ImageBarrier(cmd, depthImage,
        VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        VK_IMAGE_ASPECT_DEPTH_BIT);

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
