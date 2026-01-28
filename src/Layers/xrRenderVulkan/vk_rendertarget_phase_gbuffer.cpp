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
#include "rvk.h"

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
    // Total: 192 bytes (3 x 64 bytes)
};

// ============================================================================
// GetGBufferPipeline() - Get or create G-Buffer pipeline
// ============================================================================
VkPipeline CRenderTarget::GetGBufferPipeline()
{
    // Return cached pipeline if exists
    if (m_GBufferPipeline != VK_NULL_HANDLE)
        return m_GBufferPipeline;

    Msg("[Vulkan] Creating G-Buffer pipeline...");

    // ========================================================================
    // Step 1: Load G-Buffer shaders
    // ========================================================================
    VkShaderModule vertShader = g_ShaderManager->Load("gbuffer.vert.spv");
    VkShaderModule fragShader = g_ShaderManager->Load("gbuffer.frag.spv");

    if (vertShader == VK_NULL_HANDLE || fragShader == VK_NULL_HANDLE) {
        Msg("![Vulkan] Failed to load G-Buffer shaders");
        return VK_NULL_HANDLE;
    }

    // ========================================================================
    // Step 2: Configure pipeline for G-Buffer rendering
    // ========================================================================
    PipelineConfig config = {};
    config.vertShader = vertShader;
    config.fragShader = fragShader;
    config.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    config.cullMode = VK_CULL_MODE_BACK_BIT;  // Backface culling

    // Depth testing (CRITICAL for correct occlusion)
    config.depthTest = true;
    config.depthWrite = true;   // Write depth values
    config.depthCompare = VK_COMPARE_OP_LESS;  // Standard depth test

    // No blending (opaque geometry only)
    config.blendEnable = false;

    // Multiple Render Targets (MRT) - 4 color attachments
    config.colorAttachmentCount = 4;
    config.colorFormats[0] = VK_FORMAT_R32G32B32A32_SFLOAT;  // rt_Position
    config.colorFormats[1] = VK_FORMAT_R32G32B32A32_SFLOAT;  // rt_Normal
    config.colorFormats[2] = VK_FORMAT_R8G8B8A8_SRGB;        // rt_Color
    config.colorFormats[3] = VK_FORMAT_R8G8B8A8_UNORM;       // rt_Material
    config.depthFormat = VK_FORMAT_D32_SFLOAT;               // Depth buffer

    // ========================================================================
    // Step 3: Create pipeline
    // ========================================================================
    m_GBufferPipeline = g_PipelineManager->GetOrCreate(config);

    if (m_GBufferPipeline == VK_NULL_HANDLE) {
        Msg("![Vulkan] Failed to create G-Buffer pipeline");
    } else {
        Msg("[Vulkan] G-Buffer pipeline created successfully");
    }

    return m_GBufferPipeline;
}

// ============================================================================
// Phase G-Buffer: Render Scene Geometry
// ============================================================================
//
// This is the CRITICAL method that fills the G-Buffer with geometry data.
// Currently this is the missing piece causing black screen.
//
// Output:
// - rt_Position: Eye-space positions (R32G32B32A32_SFLOAT)
// - rt_Normal: Eye-space normals (R32G32B32A32_SFLOAT)
// - rt_Color: Albedo / diffuse color (R8G8B8A8_SRGB)
// - rt_Material: PBR properties (R8G8B8A8_UNORM)
// - Depth buffer: Depth values (D32_SFLOAT)
//
// ============================================================================
void CRenderTarget::phase_gbuffer()
{
    Msg("[Vulkan] phase_gbuffer: Rendering geometry to G-Buffer");

    // ========================================================================
    // Step 1: Get command buffer
    // ========================================================================
    VkCommandBuffer cmd = RCache.GetCommandBuffer();

    // ========================================================================
    // Step 2: Transition render targets to COLOR_ATTACHMENT_OPTIMAL
    // ========================================================================
    {
        VkImageMemoryBarrier barriers[4] = {};

        // rt_Position
        barriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barriers[0].srcAccessMask = 0;
        barriers[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barriers[0].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barriers[0].newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        barriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barriers[0].image = rt_Position.GetImage();
        barriers[0].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barriers[0].subresourceRange.baseMipLevel = 0;
        barriers[0].subresourceRange.levelCount = 1;
        barriers[0].subresourceRange.baseArrayLayer = 0;
        barriers[0].subresourceRange.layerCount = 1;

        // rt_Normal
        barriers[1] = barriers[0];
        barriers[1].image = rt_Normal.GetImage();

        // rt_Color
        barriers[2] = barriers[0];
        barriers[2].image = rt_Color.GetImage();

        // rt_Material
        barriers[3] = barriers[0];
        barriers[3].image = rt_Material.GetImage();

        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            0, 0, nullptr, 0, nullptr, 4, barriers);
    }

    // ========================================================================
    // Step 3: Transition depth buffer to DEPTH_ATTACHMENT_OPTIMAL
    // ========================================================================
    {
        VkImageMemoryBarrier depthBarrier = {};
        depthBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        depthBarrier.srcAccessMask = 0;
        depthBarrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        depthBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        depthBarrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        depthBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        depthBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        depthBarrier.image = Swapchain.m_DepthImage;
        depthBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        depthBarrier.subresourceRange.baseMipLevel = 0;
        depthBarrier.subresourceRange.levelCount = 1;
        depthBarrier.subresourceRange.baseArrayLayer = 0;
        depthBarrier.subresourceRange.layerCount = 1;

        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
            0, 0, nullptr, 0, nullptr, 1, &depthBarrier);
    }

    // ========================================================================
    // Step 4: Begin rendering to G-Buffer (4 MRT + depth)
    // ========================================================================
    VkRenderingAttachmentInfo colorAttachments[4] = {};

    // Attachment 0: rt_Position (clear to far plane)
    colorAttachments[0].sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttachments[0].imageView = rt_Position.GetView();
    colorAttachments[0].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachments[0].clearValue.color = {{0.0f, 0.0f, 0.0f, 1000.0f}};  // Far plane

    // Attachment 1: rt_Normal (clear to up vector)
    colorAttachments[1].sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttachments[1].imageView = rt_Normal.GetView();
    colorAttachments[1].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachments[1].clearValue.color = {{0.0f, 1.0f, 0.0f, 0.0f}};  // Up vector

    // Attachment 2: rt_Color (clear to black)
    colorAttachments[2].sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttachments[2].imageView = rt_Color.GetView();
    colorAttachments[2].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachments[2].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachments[2].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachments[2].clearValue.color = {{0.0f, 0.0f, 0.0f, 1.0f}};  // Black

    // Attachment 3: rt_Material (clear to default PBR)
    colorAttachments[3].sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttachments[3].imageView = rt_Material.GetView();
    colorAttachments[3].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachments[3].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachments[3].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachments[3].clearValue.color = {{0.0f, 0.5f, 0.0f, 1.0f}};  // roughness=0.5

    // Depth attachment (clear to far)
    VkRenderingAttachmentInfo depthAttachment = {};
    depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depthAttachment.imageView = Swapchain.m_DepthView;
    depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.clearValue.depthStencil = {1.0f, 0};  // Clear to far (1.0)

    // Rendering info
    VkRenderingInfo renderingInfo = {};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.renderArea.offset = {0, 0};
    renderingInfo.renderArea.extent = {m_Width, m_Height};
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 4;
    renderingInfo.pColorAttachments = colorAttachments;
    renderingInfo.pDepthAttachment = &depthAttachment;

    vkCmdBeginRendering(cmd, &renderingInfo);

    // ========================================================================
    // Step 5: Setup viewport and scissor
    // ========================================================================
    VkViewport viewport = {};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (float)m_Width;
    viewport.height = (float)m_Height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor = {};
    scissor.offset = {0, 0};
    scissor.extent = {m_Width, m_Height};
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    // ========================================================================
    // Step 6: Get G-Buffer pipeline
    // ========================================================================
    VkPipeline pipeline = GetGBufferPipeline();
    if (pipeline == VK_NULL_HANDLE) {
        Msg("![Vulkan] Failed to get G-Buffer pipeline");
        vkCmdEndRendering(cmd);
        return;
    }

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

    // ========================================================================
    // Step 7: Render all level geometry
    // ========================================================================
    // Get view and projection matrices from Device
    Fmatrix mView = Device.mView;
    Fmatrix mProjection = Device.mProject;

    // For MVP, use identity world matrix (static level geometry)
    Fmatrix mWorld;
    mWorld.identity();

    // Push constants (MVP matrices)
    GBufferPushConstants pushConstants;
    pushConstants.u_Model = mWorld;
    pushConstants.u_View = mView;
    pushConstants.u_Projection = mProjection;

    VkPipelineLayout layout = g_PipelineManager->GetLayout();
    vkCmdPushConstants(cmd, layout,
        VK_SHADER_STAGE_VERTEX_BIT,
        0, sizeof(GBufferPushConstants),
        &pushConstants);

    Msg("[Vulkan] Rendering level geometry to G-Buffer...");

    // ========================================================================
    // Phase 2.22: Bind default material (Set 1 - PerMaterial)
    // ========================================================================
    if (g_MaterialManager && g_MaterialManager->GetDefaultMaterial()) {
        g_MaterialManager->GetDefaultMaterial()->Bind(cmd);
        Msg("[Vulkan] Default material bound (white texture)");
    } else {
        Msg("![Vulkan] Material Manager or default material not available");
    }

    // Render level visuals
    // This calls visual->Render() for all level geometry
    RImplementation.RenderLevelVisuals();

    // TODO Phase 2.23: Per-visual material binding
    // for (auto visual : Visuals) {
    //     CMaterial* mat = visual->GetMaterial();
    //     if (mat) mat->Bind(cmd);
    //     visual->Render();
    // }

    // ========================================================================
    // Step 8: End rendering
    // ========================================================================
    vkCmdEndRendering(cmd);

    // ========================================================================
    // Step 9: Transition render targets to SHADER_READ_ONLY
    // ========================================================================
    {
        VkImageMemoryBarrier barriers[4] = {};

        // rt_Position
        barriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barriers[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barriers[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barriers[0].oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        barriers[0].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barriers[0].image = rt_Position.GetImage();
        barriers[0].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barriers[0].subresourceRange.baseMipLevel = 0;
        barriers[0].subresourceRange.levelCount = 1;
        barriers[0].subresourceRange.baseArrayLayer = 0;
        barriers[0].subresourceRange.layerCount = 1;

        // rt_Normal
        barriers[1] = barriers[0];
        barriers[1].image = rt_Normal.GetImage();

        // rt_Color
        barriers[2] = barriers[0];
        barriers[2].image = rt_Color.GetImage();

        // rt_Material
        barriers[3] = barriers[0];
        barriers[3].image = rt_Material.GetImage();

        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 4, barriers);
    }

    // ========================================================================
    // Note: Depth buffer stays in DEPTH_ATTACHMENT_OPTIMAL
    // Forward pass will reuse it (read-only)
    // ========================================================================

    Msg("[Vulkan] phase_gbuffer complete - G-Buffer filled");

    // ========================================================================
    // Statistics
    // ========================================================================
    // TODO: Update render stats
    // RCache.stat.gbuffer_calls++;
    // RCache.stat.gbuffer_polys += polyCount;
}

} // namespace VK
