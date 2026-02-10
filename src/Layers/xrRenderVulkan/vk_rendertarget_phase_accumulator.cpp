// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// Licensed under the same terms as X-Ray Engine (see root License.txt)

#include "stdafx.h"
#include "vk_rendertarget.h"
#include "HW_Vulkan.h"
#include "vk_R_Backend.h"
#include "vk_swapchain.h"

namespace VK
{

// ============================================================================
// phase_accumulator() - Setup for light accumulation
// ============================================================================
//
// Эта фаза подготавливает rt_Accumulator для накопления света:
// 1. Переводит G-Buffer RTs в SHADER_READ layout
// 2. Очищает rt_Accumulator (один раз за кадр)
// 3. Начинает rendering в rt_Accumulator
// 4. Настраивает stencil test и additive blending
//
// После этого можно вызывать accum_direct/point/spot для добавления света.
// ============================================================================

void CRenderTarget::phase_accumulator()
{
    VkCommandBuffer cmd = RCache.GetCommandBuffer();

    // ========================================================================
    // Step 1: Transition G-Buffer RTs to SHADER_READ
    // ========================================================================
    // После phase_scene_end() все G-Buffer RTs в layout COLOR_ATTACHMENT_OPTIMAL
    // Теперь нужно перевести их в SHADER_READ_ONLY_OPTIMAL для чтения в шейдерах

    VkImageMemoryBarrier2 barriers[5] = {};

    // rt_Position
    barriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barriers[0].srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    barriers[0].srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    barriers[0].dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    barriers[0].dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
    barriers[0].oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barriers[0].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[0].image = rt_Position.m_Image;
    barriers[0].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barriers[0].subresourceRange.baseMipLevel = 0;
    barriers[0].subresourceRange.levelCount = 1;
    barriers[0].subresourceRange.baseArrayLayer = 0;
    barriers[0].subresourceRange.layerCount = 1;

    // rt_Normal
    barriers[1] = barriers[0];
    barriers[1].image = rt_Normal.m_Image;

    // rt_Color (albedo)
    barriers[2] = barriers[0];
    barriers[2].image = rt_Color.m_Image;

    // rt_Material (PBR)
    barriers[3] = barriers[0];
    barriers[3].image = rt_Material.m_Image;

    // Depth buffer (используем read-only)
    barriers[4].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barriers[4].srcStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                               VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
    barriers[4].srcAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    barriers[4].dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                               VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
    barriers[4].dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
    barriers[4].oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    barriers[4].newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    barriers[4].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[4].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[4].image = Swapchain.m_DepthImage;
    barriers[4].subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    barriers[4].subresourceRange.baseMipLevel = 0;
    barriers[4].subresourceRange.levelCount = 1;
    barriers[4].subresourceRange.baseArrayLayer = 0;
    barriers[4].subresourceRange.layerCount = 1;

    VkDependencyInfo dependencyInfo = {};
    dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependencyInfo.imageMemoryBarrierCount = 5;
    dependencyInfo.pImageMemoryBarriers = barriers;

    vkCmdPipelineBarrier2(cmd, &dependencyInfo);

    // ========================================================================
    // Step 2: Transition rt_Accumulator: UNDEFINED → COLOR_ATTACHMENT
    // ========================================================================

    VkImageMemoryBarrier2 accumBarrier = {};
    accumBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    accumBarrier.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
    accumBarrier.srcAccessMask = VK_ACCESS_2_NONE;
    accumBarrier.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    accumBarrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    accumBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;  // First use per frame
    accumBarrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    accumBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    accumBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    accumBarrier.image = rt_Accumulator.m_Image;
    accumBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    accumBarrier.subresourceRange.baseMipLevel = 0;
    accumBarrier.subresourceRange.levelCount = 1;
    accumBarrier.subresourceRange.baseArrayLayer = 0;
    accumBarrier.subresourceRange.layerCount = 1;

    VkDependencyInfo accumDependency = {};
    accumDependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    accumDependency.imageMemoryBarrierCount = 1;
    accumDependency.pImageMemoryBarriers = &accumBarrier;

    vkCmdPipelineBarrier2(cmd, &accumDependency);

    // ========================================================================
    // Step 3: Clear rt_Accumulator (once per frame)
    // ========================================================================

    if (Device.dwFrame != dwAccumulatorClearMark) {
        dwAccumulatorClearMark = Device.dwFrame;

        VkClearColorValue clearColor = {};
        clearColor.float32[0] = 0.0f;
        clearColor.float32[1] = 0.0f;
        clearColor.float32[2] = 0.0f;
        clearColor.float32[3] = 0.0f;

        VkImageSubresourceRange range = {};
        range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        range.baseMipLevel = 0;
        range.levelCount = 1;
        range.baseArrayLayer = 0;
        range.layerCount = 1;

        vkCmdClearColorImage(cmd, rt_Accumulator.m_Image,
                            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                            &clearColor, 1, &range);

        // Accumulator cleared
    }

    // ========================================================================
    // Step 4: Begin Rendering (rt_Accumulator)
    // ========================================================================

    VkRenderingAttachmentInfo colorAttachment = {};
    colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttachment.imageView = rt_Accumulator.m_ImageView;
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;  // Keep previous content (additive)
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

    // Depth buffer - read-only (reuse from G-Buffer pass)
    VkRenderingAttachmentInfo depthAttachment = {};
    depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depthAttachment.imageView = Swapchain.m_DepthImageView;
    depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

    VkRenderingAttachmentInfo stencilAttachment = depthAttachment;

    VkRenderingInfo renderingInfo = {};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.renderArea.offset = {0, 0};
    renderingInfo.renderArea.extent = {m_Width, m_Height};
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &colorAttachment;
    renderingInfo.pDepthAttachment = &depthAttachment;
    renderingInfo.pStencilAttachment = &stencilAttachment;

    vkCmdBeginRendering(cmd, &renderingInfo);

    // ========================================================================
    // Step 5: Setup pipeline state
    // ========================================================================

    // Viewport
    VkViewport viewport = {};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (float)m_Width;
    viewport.height = (float)m_Height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    // Scissor
    VkRect2D scissor = {};
    scissor.offset = {0, 0};
    scissor.extent = {m_Width, m_Height};
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    // Stencil test (render only where geometry exists)
    // stencil >= 1 means geometry was rendered there
    vkCmdSetStencilTestEnable(cmd, VK_TRUE);
    vkCmdSetStencilOp(cmd, VK_STENCIL_FACE_FRONT_AND_BACK,
                     VK_STENCIL_OP_KEEP,         // fail
                     VK_STENCIL_OP_KEEP,         // pass
                     VK_STENCIL_OP_KEEP,         // depth fail
                     VK_COMPARE_OP_LESS_OR_EQUAL); // compare op

    vkCmdSetStencilCompareMask(cmd, VK_STENCIL_FACE_FRONT_AND_BACK, 0xff);
    vkCmdSetStencilWriteMask(cmd, VK_STENCIL_FACE_FRONT_AND_BACK, 0x00);  // Don't modify stencil
    vkCmdSetStencilReference(cmd, VK_STENCIL_FACE_FRONT_AND_BACK, 0x01);

    // Depth test (read-only)
    vkCmdSetDepthTestEnable(cmd, VK_FALSE);  // No depth testing for fullscreen passes
    vkCmdSetDepthWriteEnable(cmd, VK_FALSE);

    // Additive blending (ONE + ONE) is configured via pipeline state
    // in PipelineConfig when lighting pipelines are created.
    // No dynamic blend state needed here.

    // phase_accumulator() setup complete
}

} // namespace VK
