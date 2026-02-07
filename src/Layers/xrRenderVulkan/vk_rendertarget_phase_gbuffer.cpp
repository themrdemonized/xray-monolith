// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// SPDX-License-Identifier: MIT

// ============================================================================
// vk_rendertarget_phase_gbuffer.cpp
// ============================================================================
//
// Phase 2.21: G-Buffer Pass Rendering
//
// Implements the G-Buffer pass - renders scene geometry to Multiple Render
// Targets (MRT) for deferred shading.
//
// Process:
// 1. Begin rendering to 4 MRT + depth (Position, Normal, Color, Material)
// 2. Load G-Buffer shaders (gbuffer.vert.spv, gbuffer.frag.spv)
// 3. Configure pipeline (4 color attachments, depth test, backface culling)
// 4. Render all level geometry (visuals, models)
// 5. Fill G-Buffer textures for lighting pass
//
// ============================================================================

#include "stdafx.h"
#include "vk_rendertarget.h"
#include "vk_shaders.h"
#include "vk_pipeline.h"
#include "vk_swapchain.h"
#include "vk_material.h"
#include "HW_Vulkan.h"
#include "vk_R_Backend.h"
#include "vk_Visual.h"
#include "rvk.h"
#include "../../Include/xrRender/Kinematics.h"
#include <functional>

namespace VK
{

// ============================================================================
// Push Constants for G-Buffer
// ============================================================================
struct GBufferPushConstants
{
    Fmatrix u_Model;       // Model matrix (local → world)
    Fmatrix u_View;        // View matrix (world → eye)
    Fmatrix u_Projection;  // Projection matrix (eye → clip)
    float   u_UVScale;     // offset 192: UV scale (1/1024 for SHORT2, 1.0 for FLOAT2)
    u32     _pad196;       // offset 196: reserved (u_SkinMode in skinned pipeline)
    float   u_AlphaRef;    // offset 200: Alpha test threshold (-1.0 = disabled, 0.5 = enabled)
    // Total: 204 bytes (3 x 64 bytes + 4 + 4 + 4)
};

// ============================================================================
// GetGBufferPipeline() - Get or create G-Buffer pipeline
// ============================================================================
VkPipeline CRenderTarget::GetGBufferPipeline(u32 stride)
{
    // Return cached pipeline for this stride if exists
    auto it = m_GBufferPipelines.find(stride);
    if (it != m_GBufferPipelines.end())
        return it->second;

    Msg("[Vulkan] Creating G-Buffer pipeline for stride %u...", stride);

    // ========================================================================
    // Step 1: Load shaders (tree stride=12 uses dedicated vertex shader)
    // ========================================================================
    VkShaderModule vertShader = VK_NULL_HANDLE;
    VkShaderModule fragShader = g_ShaderManager->Load("gbuffer.frag.spv");

    if (stride == 12)
        vertShader = g_ShaderManager->Load("gbuffer_tree.vert.spv");
    else
        vertShader = g_ShaderManager->Load("gbuffer.vert.spv");

    if (vertShader == VK_NULL_HANDLE || fragShader == VK_NULL_HANDLE) {
        Msg("![Vulkan] Failed to load G-Buffer shaders (stride %u)", stride);
        return VK_NULL_HANDLE;
    }

    // ========================================================================
    // Step 2: Configure pipeline for G-Buffer rendering
    // ========================================================================
    PipelineConfig config;
    config.vertShader = vertShader;
    config.fragShader = fragShader;
    config.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    config.cullMode = VK_CULL_MODE_NONE;  // No culling - X-Ray geometry has mixed winding
    config.depthTest = true;
    config.depthWrite = true;
    config.depthCompareOp = VK_COMPARE_OP_LESS;

    // No blending (opaque geometry only)
    config.blendEnable = false;

    // Multiple Render Targets (MRT) - 4 color attachments
    config.colorAttachmentCount = 4;
    config.colorFormats[0] = VK_FORMAT_R32G32B32A32_SFLOAT;  // rt_Position
    config.colorFormats[1] = VK_FORMAT_R32G32B32A32_SFLOAT;  // rt_Normal
    config.colorFormats[2] = VK_FORMAT_R8G8B8A8_SRGB;        // rt_Color
    config.colorFormats[3] = VK_FORMAT_R8G8B8A8_UNORM;       // rt_Material
    config.depthFormat = VK_FORMAT_D32_SFLOAT;               // Depth buffer

    if (stride == 12)
    {
        // Trees: position-only vertices (FLOAT3, 12 bytes)
        // Use custom vertex input with single attribute
        config.useDefaultVertexInput = false;
        config.useCustomVertexInput = true;

        config.customBinding.binding = 0;
        config.customBinding.stride = 12;
        config.customBinding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        config.customAttributes[0].binding = 0;
        config.customAttributes[0].location = 0;
        config.customAttributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        config.customAttributes[0].offset = 0;
        config.customAttributeCount = 1;
    }
    else
    {
        // Standard geometry: use default vertex input (pos + normal + uv)
        config.useDefaultVertexInput = true;
        config.vertexStride = stride;
    }

    // ========================================================================
    // Step 3: Create pipeline
    // ========================================================================
    VkPipeline pipeline = g_PipelineManager->GetOrCreate(config);

    if (pipeline == VK_NULL_HANDLE) {
        Msg("![Vulkan] Failed to create G-Buffer pipeline for stride %u", stride);
    } else {
        m_GBufferPipelines[stride] = pipeline;
        Msg("[Vulkan] G-Buffer pipeline created successfully (stride %u)", stride);
    }

    return pipeline;
}

// ============================================================================
// GetGBufferPipelineSkinned() - Get or create skinned G-Buffer pipeline
// ============================================================================
VkPipeline CRenderTarget::GetGBufferPipelineSkinned(u32 stride)
{
    // Return cached pipeline for this stride if exists
    auto it = m_GBufferPipelinesSkinned.find(stride);
    if (it != m_GBufferPipelinesSkinned.end())
        return it->second;

    Msg("[Vulkan] Creating skinned G-Buffer pipeline for stride %u...", stride);

    // ========================================================================
    // Step 1: Load skinned vertex shader
    // ========================================================================
    VkShaderModule vertShader = g_ShaderManager->Load("gbuffer_skinned.vert.spv");
    VkShaderModule fragShader = g_ShaderManager->Load("gbuffer.frag.spv");

    if (vertShader == VK_NULL_HANDLE || fragShader == VK_NULL_HANDLE) {
        Msg("![Vulkan] Failed to load skinned G-Buffer shaders (stride %u)", stride);
        return VK_NULL_HANDLE;
    }

    // ========================================================================
    // Step 2: Configure pipeline with skinned vertex input
    // ========================================================================
    PipelineConfig config;
    config.vertShader = vertShader;
    config.fragShader = fragShader;
    config.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    config.cullMode = VK_CULL_MODE_NONE;
    config.depthTest = true;
    config.depthWrite = true;
    config.depthCompareOp = VK_COMPARE_OP_LESS;
    config.blendEnable = false;

    // MRT - same as non-skinned G-Buffer
    config.colorAttachmentCount = 4;
    config.colorFormats[0] = VK_FORMAT_R32G32B32A32_SFLOAT;  // rt_Position
    config.colorFormats[1] = VK_FORMAT_R32G32B32A32_SFLOAT;  // rt_Normal
    config.colorFormats[2] = VK_FORMAT_R8G8B8A8_SRGB;        // rt_Color
    config.colorFormats[3] = VK_FORMAT_R8G8B8A8_UNORM;       // rt_Material
    config.depthFormat = VK_FORMAT_D32_SFLOAT;

    // Use custom vertex input for skinned attributes
    config.useDefaultVertexInput = false;
    config.useCustomVertexInput = true;

    VkVertexInputBindingDescription skinnedBinding = {};
    VkVertexInputAttributeDescription skinnedAttrs[6] = {};
    u32 skinnedAttrCount = 0;
    VkPipelineVertexInputStateCreateInfo tempInfo = {};

    g_PipelineManager->GetSkinnedVertexInputState(tempInfo, skinnedBinding, skinnedAttrs, skinnedAttrCount, stride);

    config.customBinding = skinnedBinding;
    for (u32 i = 0; i < skinnedAttrCount && i < 8; ++i)
        config.customAttributes[i] = skinnedAttrs[i];
    config.customAttributeCount = skinnedAttrCount;
    config.vertexStride = stride;

    // ========================================================================
    // Step 3: Create pipeline
    // ========================================================================
    VkPipeline pipeline = g_PipelineManager->GetOrCreate(config);

    if (pipeline == VK_NULL_HANDLE) {
        Msg("![Vulkan] Failed to create skinned G-Buffer pipeline for stride %u", stride);
    } else {
        m_GBufferPipelinesSkinned[stride] = pipeline;
        Msg("[Vulkan] Skinned G-Buffer pipeline created successfully (stride %u, attrs %u)", stride, skinnedAttrCount);
    }

    return pipeline;
}

// ============================================================================
// Phase G-Buffer: Render Scene Geometry
// ============================================================================
void CRenderTarget::phase_gbuffer()
{
    static u32 s_gbuf_crashCount = 0;
    static u32 s_lastDiagFrame = 0;

    if (g_bDeviceLost) return;

    // ========================================================================
    // Step 1: Get command buffer
    // ========================================================================
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    __try {
        cmd = RCache.GetCommandBuffer();
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Msg("! phase_gbuffer: CRASH in GetCommandBuffer at frame %u, exc=0x%08X",
            Device.dwFrame, GetExceptionCode());
        FlushLog();
        return;
    }
    if (cmd == VK_NULL_HANDLE) return;

    // ========================================================================
    // Step 2: Validate all resources before ANY Vulkan call
    // ========================================================================
    VkImage imgPos = rt_Position.GetImage();
    VkImage imgNorm = rt_Normal.GetImage();
    VkImage imgColor = rt_Color.GetImage();
    VkImage imgMat = rt_Material.GetImage();
    VkImageView viewPos = rt_Position.GetView();
    VkImageView viewNorm = rt_Normal.GetView();
    VkImageView viewColor = rt_Color.GetView();
    VkImageView viewMat = rt_Material.GetView();
    VkImageView viewDepth = Swapchain.m_DepthView;
    VkImage imgDepth = Swapchain.m_DepthImage;

    if (!imgPos || !imgNorm || !imgColor || !imgMat) {
        Msg("! phase_gbuffer: NULL RT image at frame %u: pos=%p norm=%p col=%p mat=%p",
            Device.dwFrame, imgPos, imgNorm, imgColor, imgMat);
        return;
    }
    if (!viewPos || !viewNorm || !viewColor || !viewMat) {
        Msg("! phase_gbuffer: NULL RT view at frame %u: pos=%p norm=%p col=%p mat=%p",
            Device.dwFrame, viewPos, viewNorm, viewColor, viewMat);
        return;
    }
    if (!imgDepth || !viewDepth) {
        Msg("! phase_gbuffer: NULL depth at frame %u: img=%p view=%p",
            Device.dwFrame, imgDepth, viewDepth);
        return;
    }

    // Periodic diagnostics (every 500 frames)
    bool bDiag = (Device.dwFrame - s_lastDiagFrame > 500);
    if (bDiag) {
        Msg("[gbuf-diag] frame=%u cmd=%p rt_pos(img=%p view=%p) rt_norm(img=%p view=%p) "
            "rt_col(img=%p view=%p) rt_mat(img=%p view=%p) depth(img=%p view=%p) size=%ux%u created=%d",
            Device.dwFrame, cmd,
            imgPos, viewPos, imgNorm, viewNorm,
            imgColor, viewColor, imgMat, viewMat,
            imgDepth, viewDepth, m_Width, m_Height, (int)m_bCreated);
        s_lastDiagFrame = Device.dwFrame;
    }

    // ========================================================================
    // Step 2b: Validate image handles via vkGetImageMemoryRequirements
    //          If an image was destroyed, this call will crash — caught by SEH
    // ========================================================================
    {
        VkMemoryRequirements memReq = {};
        bool imagesValid = true;

        __try {
            vkGetImageMemoryRequirements(VulkanHW.m_Device, imgPos, &memReq);
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            Msg("! phase_gbuffer: rt_Position image INVALID (destroyed?) at frame %u, img=%p",
                Device.dwFrame, imgPos);
            FlushLog();
            imagesValid = false;
        }

        __try {
            vkGetImageMemoryRequirements(VulkanHW.m_Device, imgNorm, &memReq);
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            Msg("! phase_gbuffer: rt_Normal image INVALID at frame %u, img=%p",
                Device.dwFrame, imgNorm);
            FlushLog();
            imagesValid = false;
        }

        __try {
            vkGetImageMemoryRequirements(VulkanHW.m_Device, imgColor, &memReq);
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            Msg("! phase_gbuffer: rt_Color image INVALID at frame %u, img=%p",
                Device.dwFrame, imgColor);
            FlushLog();
            imagesValid = false;
        }

        __try {
            vkGetImageMemoryRequirements(VulkanHW.m_Device, imgMat, &memReq);
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            Msg("! phase_gbuffer: rt_Material image INVALID at frame %u, img=%p",
                Device.dwFrame, imgMat);
            FlushLog();
            imagesValid = false;
        }

        __try {
            vkGetImageMemoryRequirements(VulkanHW.m_Device, imgDepth, &memReq);
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            Msg("! phase_gbuffer: depth image INVALID at frame %u, img=%p",
                Device.dwFrame, imgDepth);
            FlushLog();
            imagesValid = false;
        }

        if (!imagesValid) {
            Msg("! phase_gbuffer: one or more images INVALID, skipping frame %u", Device.dwFrame);
            FlushLog();
            return;
        }
    }

    // ========================================================================
    // Step 3: Transition render targets to COLOR_ATTACHMENT_OPTIMAL
    // ========================================================================
    __try {
        VkImageMemoryBarrier barriers[4] = {};

        // Wait for previous frame's shader reads AND color writes to complete
        // before transitioning. oldLayout=UNDEFINED is OK since we CLEAR every frame.
        barriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barriers[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barriers[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barriers[0].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barriers[0].newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        barriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barriers[0].image = imgPos;
        barriers[0].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barriers[0].subresourceRange.baseMipLevel = 0;
        barriers[0].subresourceRange.levelCount = 1;
        barriers[0].subresourceRange.baseArrayLayer = 0;
        barriers[0].subresourceRange.layerCount = 1;

        barriers[1] = barriers[0]; barriers[1].image = imgNorm;
        barriers[2] = barriers[0]; barriers[2].image = imgColor;
        barriers[3] = barriers[0]; barriers[3].image = imgMat;

        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            0, 0, nullptr, 0, nullptr, 4, barriers);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Msg("! phase_gbuffer: CRASH in color barrier at frame %u, exc=0x%08X",
            Device.dwFrame, GetExceptionCode());
        FlushLog();
        return;
    }

    // ========================================================================
    // Step 4: Transition depth buffer to DEPTH_ATTACHMENT_OPTIMAL
    // ========================================================================
    __try {
        VkImageMemoryBarrier depthBarrier = {};
        depthBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        // Wait for previous depth reads/writes before transitioning
        depthBarrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        depthBarrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        depthBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        depthBarrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        depthBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        depthBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        depthBarrier.image = imgDepth;
        depthBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        depthBarrier.subresourceRange.baseMipLevel = 0;
        depthBarrier.subresourceRange.levelCount = 1;
        depthBarrier.subresourceRange.baseArrayLayer = 0;
        depthBarrier.subresourceRange.layerCount = 1;

        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
            0, 0, nullptr, 0, nullptr, 1, &depthBarrier);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Msg("! phase_gbuffer: CRASH in depth barrier at frame %u, exc=0x%08X",
            Device.dwFrame, GetExceptionCode());
        FlushLog();
        return;
    }

    // ========================================================================
    // Step 5: Begin rendering to G-Buffer (4 MRT + depth)
    // ========================================================================
    __try {
        VkRenderingAttachmentInfo colorAttachments[4] = {};

        colorAttachments[0].sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        colorAttachments[0].imageView = viewPos;
        colorAttachments[0].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorAttachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachments[0].clearValue.color = {{0.0f, 0.0f, 0.0f, 1000.0f}};

        colorAttachments[1].sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        colorAttachments[1].imageView = viewNorm;
        colorAttachments[1].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorAttachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachments[1].clearValue.color = {{0.0f, 1.0f, 0.0f, 0.0f}};

        colorAttachments[2].sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        colorAttachments[2].imageView = viewColor;
        colorAttachments[2].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorAttachments[2].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachments[2].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachments[2].clearValue.color = {{0.0f, 0.0f, 0.0f, 1.0f}};

        colorAttachments[3].sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        colorAttachments[3].imageView = viewMat;
        colorAttachments[3].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorAttachments[3].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachments[3].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachments[3].clearValue.color = {{0.0f, 0.5f, 0.0f, 1.0f}};

        VkRenderingAttachmentInfo depthAttachment = {};
        depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        depthAttachment.imageView = viewDepth;
        depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        depthAttachment.clearValue.depthStencil = {1.0f, 0};

        VkRenderingInfo renderingInfo = {};
        renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        renderingInfo.renderArea.offset = {0, 0};
        renderingInfo.renderArea.extent = {m_Width, m_Height};
        renderingInfo.layerCount = 1;
        renderingInfo.colorAttachmentCount = 4;
        renderingInfo.pColorAttachments = colorAttachments;
        renderingInfo.pDepthAttachment = &depthAttachment;

        vkCmdBeginRendering(cmd, &renderingInfo);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Msg("! phase_gbuffer: CRASH in vkCmdBeginRendering at frame %u, exc=0x%08X "
            "views: pos=%p norm=%p col=%p mat=%p depth=%p size=%ux%u",
            Device.dwFrame, GetExceptionCode(),
            viewPos, viewNorm, viewColor, viewMat, viewDepth, m_Width, m_Height);
        FlushLog();
        return;
    }

    // ========================================================================
    // Step 6: Setup viewport and scissor
    // ========================================================================
    {
        VkViewport viewport = {};
        viewport.x = 0.0f;
        viewport.y = (float)m_Height;
        viewport.width = (float)m_Width;
        viewport.height = -(float)m_Height;
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(cmd, 0, 1, &viewport);

        VkRect2D scissor = {};
        scissor.offset = {0, 0};
        scissor.extent = {m_Width, m_Height};
        vkCmdSetScissor(cmd, 0, 1, &scissor);
    }

    // ========================================================================
    // Step 7: Get G-Buffer pipeline (default stride 32)
    // ========================================================================
    VkPipeline pipeline = VK_NULL_HANDLE;
    __try {
        pipeline = GetGBufferPipeline(32);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Msg("! phase_gbuffer: CRASH in GetGBufferPipeline at frame %u, exc=0x%08X",
            Device.dwFrame, GetExceptionCode());
        FlushLog();
        vkCmdEndRendering(cmd);
        return;
    }
    if (pipeline == VK_NULL_HANDLE) {
        Msg("![Vulkan] Failed to get G-Buffer pipeline");
        vkCmdEndRendering(cmd);
        return;
    }

    __try {
        RCache.set_Pipeline(pipeline);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Msg("! phase_gbuffer: CRASH in set_Pipeline at frame %u, exc=0x%08X",
            Device.dwFrame, GetExceptionCode());
        FlushLog();
        vkCmdEndRendering(cmd);
        return;
    }

    // ========================================================================
    // Step 8: Push constants (including UV scale for stride-32 default)
    // ========================================================================
    __try {
        Fmatrix mView = Device.mView;
        Fmatrix mProjection = Device.mProject;
        Fmatrix mWorld;
        mWorld.identity();

        GBufferPushConstants pushConstants;
        pushConstants.u_Model = mWorld;
        pushConstants.u_View = mView;
        pushConstants.u_Projection = mProjection;
        pushConstants.u_UVScale = 1.0f / 1024.0f;  // SHORT2 UV scale for stride-32
        pushConstants._pad196 = 0;
        pushConstants.u_AlphaRef = -1.0f;          // No alpha test for solid geometry

        VkPipelineLayout layout = g_PipelineManager->GetLayout();
        vkCmdPushConstants(cmd, layout,
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            0, sizeof(GBufferPushConstants),
            &pushConstants);

        // Ensure RCache.xforms.m_w is identity so per-visual push constants
        // in vkFVisual::Render() push the correct model matrix for static geometry
        RCache.set_xform_world(mWorld);

        // Track current stride for per-visual pipeline switching
        RCache.m_CurrentGBufStride = 32;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Msg("! phase_gbuffer: CRASH in pushConstants at frame %u, exc=0x%08X",
            Device.dwFrame, GetExceptionCode());
        FlushLog();
        vkCmdEndRendering(cmd);
        return;
    }

    // ========================================================================
    // Step 9: Scene Graph Rendering (with crash isolation)
    // ========================================================================
    // Step 9a: Static geometry (level brushes, trees, etc.)
    __try {
        RImplementation.r_dsgraph_render_graph(0, true);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        s_gbuf_crashCount++;
        Msg("! phase_gbuffer: CRASH in r_dsgraph_render_graph at frame %u (crash #%u), "
            "lstNormal=%u, exception 0x%08X",
            Device.dwFrame, s_gbuf_crashCount,
            (u32)RImplementation.lstNormal.size(), GetExceptionCode());
        FlushLog();
        RImplementation.lstNormal.clear();
    }

    // Step 9b: Dynamic objects (doors, boxes, NPCs, dropped items)
    __try {
        RImplementation.r_dsgraph_render_dynamic(true);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Msg("! phase_gbuffer: CRASH in r_dsgraph_render_dynamic at frame %u, "
            "lstMatrix=%u, exception 0x%08X",
            Device.dwFrame, (u32)RImplementation.lstMatrix.size(), GetExceptionCode());
        FlushLog();
        RImplementation.lstMatrix.clear();
    }

    // Step 9c: HUD visuals (weapons, hands) — render into G-Buffer with HUD projection
    // mapHUD now contains LEAF visuals (vkSkeletonX_PM, vkSkeletonX_ST, vkFVisual)
    // after hierarchy decomposition in add_leafs_HUD_VK. Bones were already calculated
    // during the add phase. Each leaf has its own Render() with pipeline/material binding.
    if (RImplementation.mapHUD.size() > 0)
    {
        __try {
            VkPipelineLayout layout = g_PipelineManager->GetLayout();

            // Switch to HUD projection (short far plane -> always in front of world)
            Fmatrix mProjectHud = Device.mProjectHud;
            vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_VERTEX_BIT,
                128, sizeof(Fmatrix), &mProjectHud);

            // Set viewport depth range for HUD (renders in front of everything)
            RImplementation.rmNear();

            static u32 s_hudRenderDiag = 0;
            bool bHudDiag = (s_hudRenderDiag < 5);
            if (bHudDiag) {
                Msg("[HUD] phase_gbuffer: rendering %u HUD leaf visuals, frame=%u",
                    (u32)RImplementation.mapHUD.size(), Device.dwFrame);
                s_hudRenderDiag++;
            }

            // Render each leaf visual directly
            for (u32 i = 0; i < RImplementation.mapHUD.size(); i++) {
                R_dsgraph::_MatrixItemS& item = RImplementation.mapHUD[i].val;
                if (!item.pVisual) continue;

                // Leaf visuals are vkRender_Visual subclasses (vkSkeletonX_PM/ST, vkFVisual)
                vkRender_Visual* pV = reinterpret_cast<vkRender_Visual*>(item.pVisual);

                if (bHudDiag) {
                    Msg("[HUD]   leaf[%u] ptr=%p type=%u matrix=(%f,%f,%f)",
                        i, pV, pV->Type, item.Matrix._41, item.Matrix._42, item.Matrix._43);
                }

                // Set world matrix
                RCache.set_xform_world(item.Matrix);
                vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_VERTEX_BIT,
                    0, sizeof(Fmatrix), &item.Matrix);

                // Render — each leaf type handles its own pipeline/material binding
                __try {
                    pV->Render(1.0f);
                } __except(EXCEPTION_EXECUTE_HANDLER) {
                    Msg("! [HUD] CRASH rendering leaf[%u] ptr=%p type=%u frame=%u exccode=0x%08X",
                        i, pV, pV->Type, Device.dwFrame, GetExceptionCode());
                }
            }
            RImplementation.mapHUD.clear();

            // Render camera-attached HUD visuals (binoculars, scopes with custom FOV)
            if (RImplementation.mapCamAttached.size() > 0)
            {
                // Camera-attached uses custom projection (83 deg FOV)
                Fmatrix camproj;
                camproj.build_projection(
                    deg2rad(83.f),
                    Device.fASPECT, VIEWPORT_NEAR,
                    g_pGamePersistent->Environment().CurrentEnv->far_plane);
                vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_VERTEX_BIT,
                    128, sizeof(Fmatrix), &camproj);

                for (u32 i = 0; i < RImplementation.mapCamAttached.size(); i++) {
                    R_dsgraph::_MatrixItemS& item = RImplementation.mapCamAttached[i].val;
                    if (!item.pVisual) continue;

                    vkRender_Visual* pV = reinterpret_cast<vkRender_Visual*>(item.pVisual);
                    RCache.set_xform_world(item.Matrix);
                    vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_VERTEX_BIT,
                        0, sizeof(Fmatrix), &item.Matrix);

                    __try {
                        pV->Render(1.0f);
                    } __except(EXCEPTION_EXECUTE_HANDLER) {
                        Msg("! [HUD] CRASH rendering camAttached[%u] ptr=%p", i, pV);
                    }
                }
                RImplementation.mapCamAttached.clear();
            }

            // Restore normal viewport depth range
            RImplementation.rmNormal();

            // Restore normal projection and identity world matrix
            Fmatrix mProject = Device.mProject;
            vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_VERTEX_BIT,
                128, sizeof(Fmatrix), &mProject);
            Fmatrix mIdentity;
            mIdentity.identity();
            RCache.set_xform_world(mIdentity);
            vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_VERTEX_BIT,
                0, sizeof(Fmatrix), &mIdentity);

        } __except(EXCEPTION_EXECUTE_HANDLER) {
            Msg("! phase_gbuffer: CRASH in HUD rendering at frame %u, "
                "mapHUD=%u, exception 0x%08X",
                Device.dwFrame, (u32)RImplementation.mapHUD.size(), GetExceptionCode());
            FlushLog();
            RImplementation.mapHUD.clear();
            RImplementation.mapCamAttached.clear();
        }
    }

    // ========================================================================
    // Step 10: End rendering
    // ========================================================================
    __try {
        vkCmdEndRendering(cmd);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Msg("! phase_gbuffer: CRASH in vkCmdEndRendering at frame %u, exc=0x%08X",
            Device.dwFrame, GetExceptionCode());
        FlushLog();
        return;
    }

    // ========================================================================
    // Step 11: Transition render targets to SHADER_READ_ONLY
    // ========================================================================
    __try {
        VkImageMemoryBarrier barriers[4] = {};

        barriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barriers[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barriers[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barriers[0].oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        barriers[0].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barriers[0].image = imgPos;
        barriers[0].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barriers[0].subresourceRange.baseMipLevel = 0;
        barriers[0].subresourceRange.levelCount = 1;
        barriers[0].subresourceRange.baseArrayLayer = 0;
        barriers[0].subresourceRange.layerCount = 1;

        barriers[1] = barriers[0]; barriers[1].image = imgNorm;
        barriers[2] = barriers[0]; barriers[2].image = imgColor;
        barriers[3] = barriers[0]; barriers[3].image = imgMat;

        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 4, barriers);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Msg("! phase_gbuffer: CRASH in final barrier at frame %u, exc=0x%08X",
            Device.dwFrame, GetExceptionCode());
        FlushLog();
    }
}

} // namespace VK
