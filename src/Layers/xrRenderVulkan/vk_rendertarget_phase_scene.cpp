// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// Licensed under the same terms as X-Ray Engine (see root License.txt)

#include "stdafx.h"
#include "vk_rendertarget.h"
#include "vk_R_Backend.h"
#include "HW_Vulkan.h"
#include "vk_swapchain.h"
#include "vk_command_buffer.h"
#include "../../xrEngine/device.h"

namespace VK
{

// ============================================================================
// G-Buffer Pass - Phase Scene Begin
// ============================================================================

void CRenderTarget::phase_scene_begin()
{
    // Получаем текущий command buffer из RCache
    VkCommandBuffer cmd = RCache.m_Cmd;
    if (cmd == VK_NULL_HANDLE)
    {
        Msg("![Vulkan] phase_scene_begin: No active command buffer");
        return;
    }

    // ========================================================================
    // Image Layout Transitions - G-Buffer RTs to COLOR_ATTACHMENT_OPTIMAL
    // ========================================================================

    VkImageMemoryBarrier2 barriers[5];
    memset(barriers, 0, sizeof(barriers));
    u32 barrierCount = 0;

    // rt_Position: UNDEFINED → COLOR_ATTACHMENT_OPTIMAL
    barriers[barrierCount].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barriers[barrierCount].pNext = nullptr;
    barriers[barrierCount].srcStageMask = VK_PIPELINE_STAGE_2_NONE;
    barriers[barrierCount].srcAccessMask = VK_ACCESS_2_NONE;
    barriers[barrierCount].dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    barriers[barrierCount].dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    barriers[barrierCount].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barriers[barrierCount].newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barriers[barrierCount].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[barrierCount].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[barrierCount].image = rt_Position.m_Image;
    barriers[barrierCount].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barriers[barrierCount].subresourceRange.baseMipLevel = 0;
    barriers[barrierCount].subresourceRange.levelCount = 1;
    barriers[barrierCount].subresourceRange.baseArrayLayer = 0;
    barriers[barrierCount].subresourceRange.layerCount = 1;
    barrierCount++;

    // rt_Normal: UNDEFINED → COLOR_ATTACHMENT_OPTIMAL
    barriers[barrierCount].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barriers[barrierCount].pNext = nullptr;
    barriers[barrierCount].srcStageMask = VK_PIPELINE_STAGE_2_NONE;
    barriers[barrierCount].srcAccessMask = VK_ACCESS_2_NONE;
    barriers[barrierCount].dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    barriers[barrierCount].dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    barriers[barrierCount].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barriers[barrierCount].newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barriers[barrierCount].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[barrierCount].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[barrierCount].image = rt_Normal.m_Image;
    barriers[barrierCount].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barriers[barrierCount].subresourceRange.baseMipLevel = 0;
    barriers[barrierCount].subresourceRange.levelCount = 1;
    barriers[barrierCount].subresourceRange.baseArrayLayer = 0;
    barriers[barrierCount].subresourceRange.layerCount = 1;
    barrierCount++;

    // rt_Color: UNDEFINED → COLOR_ATTACHMENT_OPTIMAL
    barriers[barrierCount].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barriers[barrierCount].pNext = nullptr;
    barriers[barrierCount].srcStageMask = VK_PIPELINE_STAGE_2_NONE;
    barriers[barrierCount].srcAccessMask = VK_ACCESS_2_NONE;
    barriers[barrierCount].dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    barriers[barrierCount].dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    barriers[barrierCount].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barriers[barrierCount].newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barriers[barrierCount].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[barrierCount].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[barrierCount].image = rt_Color.m_Image;
    barriers[barrierCount].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barriers[barrierCount].subresourceRange.baseMipLevel = 0;
    barriers[barrierCount].subresourceRange.levelCount = 1;
    barriers[barrierCount].subresourceRange.baseArrayLayer = 0;
    barriers[barrierCount].subresourceRange.layerCount = 1;
    barrierCount++;

    // rt_Material: UNDEFINED → COLOR_ATTACHMENT_OPTIMAL
    barriers[barrierCount].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barriers[barrierCount].pNext = nullptr;
    barriers[barrierCount].srcStageMask = VK_PIPELINE_STAGE_2_NONE;
    barriers[barrierCount].srcAccessMask = VK_ACCESS_2_NONE;
    barriers[barrierCount].dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    barriers[barrierCount].dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    barriers[barrierCount].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barriers[barrierCount].newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barriers[barrierCount].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[barrierCount].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[barrierCount].image = rt_Material.m_Image;
    barriers[barrierCount].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barriers[barrierCount].subresourceRange.baseMipLevel = 0;
    barriers[barrierCount].subresourceRange.levelCount = 1;
    barriers[barrierCount].subresourceRange.baseArrayLayer = 0;
    barriers[barrierCount].subresourceRange.layerCount = 1;
    barrierCount++;

    // Depth buffer: UNDEFINED → DEPTH_ATTACHMENT_OPTIMAL
    barriers[barrierCount].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barriers[barrierCount].pNext = nullptr;
    barriers[barrierCount].srcStageMask = VK_PIPELINE_STAGE_2_NONE;
    barriers[barrierCount].srcAccessMask = VK_ACCESS_2_NONE;
    barriers[barrierCount].dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
    barriers[barrierCount].dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    barriers[barrierCount].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barriers[barrierCount].newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    barriers[barrierCount].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[barrierCount].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[barrierCount].image = Swapchain.m_DepthImage;
    barriers[barrierCount].subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    barriers[barrierCount].subresourceRange.baseMipLevel = 0;
    barriers[barrierCount].subresourceRange.levelCount = 1;
    barriers[barrierCount].subresourceRange.baseArrayLayer = 0;
    barriers[barrierCount].subresourceRange.layerCount = 1;
    barrierCount++;

    // Submit barrier
    VkDependencyInfo dependencyInfo;
    memset(&dependencyInfo, 0, sizeof(dependencyInfo));
    dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependencyInfo.pNext = nullptr;
    dependencyInfo.dependencyFlags = 0;
    dependencyInfo.memoryBarrierCount = 0;
    dependencyInfo.pMemoryBarriers = nullptr;
    dependencyInfo.bufferMemoryBarrierCount = 0;
    dependencyInfo.pBufferMemoryBarriers = nullptr;
    dependencyInfo.imageMemoryBarrierCount = barrierCount;
    dependencyInfo.pImageMemoryBarriers = barriers;

    vkCmdPipelineBarrier2(cmd, &dependencyInfo);

    // ========================================================================
    // Begin Dynamic Rendering - G-Buffer Pass
    // ========================================================================

    VkClearValue clearValues[5];
    memset(clearValues, 0, sizeof(clearValues));
    clearValues[0].color.float32[0] = 0.0f;
    clearValues[0].color.float32[1] = 0.0f;
    clearValues[0].color.float32[2] = 0.0f;
    clearValues[0].color.float32[3] = 0.0f;
    clearValues[1].color.float32[0] = 0.0f;
    clearValues[1].color.float32[1] = 0.0f;
    clearValues[1].color.float32[2] = 0.0f;
    clearValues[1].color.float32[3] = 0.0f;
    clearValues[2].color.float32[0] = 0.0f;
    clearValues[2].color.float32[1] = 0.0f;
    clearValues[2].color.float32[2] = 0.0f;
    clearValues[2].color.float32[3] = 0.0f;
    clearValues[3].color.float32[0] = 0.0f;
    clearValues[3].color.float32[1] = 0.0f;
    clearValues[3].color.float32[2] = 0.0f;
    clearValues[3].color.float32[3] = 0.0f;
    clearValues[4].depthStencil.depth = 1.0f;
    clearValues[4].depthStencil.stencil = 0;

    VkRenderingAttachmentInfo colorAttachments[4];
    memset(colorAttachments, 0, sizeof(colorAttachments));

    // rt_Position attachment
    colorAttachments[0].sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttachments[0].pNext = nullptr;
    colorAttachments[0].imageView = rt_Position.m_ImageView;
    colorAttachments[0].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachments[0].resolveMode = VK_RESOLVE_MODE_NONE;
    colorAttachments[0].resolveImageView = VK_NULL_HANDLE;
    colorAttachments[0].resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachments[0].clearValue = clearValues[0];

    // rt_Normal attachment
    colorAttachments[1].sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttachments[1].pNext = nullptr;
    colorAttachments[1].imageView = rt_Normal.m_ImageView;
    colorAttachments[1].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachments[1].resolveMode = VK_RESOLVE_MODE_NONE;
    colorAttachments[1].resolveImageView = VK_NULL_HANDLE;
    colorAttachments[1].resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachments[1].clearValue = clearValues[1];

    // rt_Color attachment
    colorAttachments[2].sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttachments[2].pNext = nullptr;
    colorAttachments[2].imageView = rt_Color.m_ImageView;
    colorAttachments[2].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachments[2].resolveMode = VK_RESOLVE_MODE_NONE;
    colorAttachments[2].resolveImageView = VK_NULL_HANDLE;
    colorAttachments[2].resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachments[2].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachments[2].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachments[2].clearValue = clearValues[2];

    // rt_Material attachment
    colorAttachments[3].sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttachments[3].pNext = nullptr;
    colorAttachments[3].imageView = rt_Material.m_ImageView;
    colorAttachments[3].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachments[3].resolveMode = VK_RESOLVE_MODE_NONE;
    colorAttachments[3].resolveImageView = VK_NULL_HANDLE;
    colorAttachments[3].resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachments[3].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachments[3].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachments[3].clearValue = clearValues[3];

    // Depth attachment
    VkRenderingAttachmentInfo depthAttachment;
    memset(&depthAttachment, 0, sizeof(depthAttachment));
    depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depthAttachment.pNext = nullptr;
    depthAttachment.imageView = Swapchain.m_DepthView;
    depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depthAttachment.resolveMode = VK_RESOLVE_MODE_NONE;
    depthAttachment.resolveImageView = VK_NULL_HANDLE;
    depthAttachment.resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.clearValue = clearValues[4];

    // Rendering info
    VkRenderingInfo renderingInfo;
    memset(&renderingInfo, 0, sizeof(renderingInfo));
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.pNext = nullptr;
    renderingInfo.flags = 0;
    renderingInfo.renderArea.offset.x = 0;
    renderingInfo.renderArea.offset.y = 0;
    renderingInfo.renderArea.extent.width = m_Width;
    renderingInfo.renderArea.extent.height = m_Height;
    renderingInfo.layerCount = 1;
    renderingInfo.viewMask = 0;
    renderingInfo.colorAttachmentCount = 4;
    renderingInfo.pColorAttachments = colorAttachments;
    renderingInfo.pDepthAttachment = &depthAttachment;
    renderingInfo.pStencilAttachment = nullptr;

    vkCmdBeginRendering(cmd, &renderingInfo);

    // ========================================================================
    // Set Render States
    // ========================================================================

    // Set viewport
    VkViewport viewport;
    memset(&viewport, 0, sizeof(viewport));
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(m_Width);
    viewport.height = static_cast<float>(m_Height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    // Set scissor
    VkRect2D scissor;
    memset(&scissor, 0, sizeof(scissor));
    scissor.offset.x = 0;
    scissor.offset.y = 0;
    scissor.extent.width = m_Width;
    scissor.extent.height = m_Height;
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    // Set cull mode: CULL_CCW (front-face culling)
    RCache.set_CullMode(CULL_CCW);

    // TODO: Stencil setup for light masking
    // RCache.set_Stencil(TRUE, D3DCMP_ALWAYS, 0x01, 0xff, 0x7f, ...);

    Msg("[Vulkan] phase_scene_begin: G-Buffer pass started (%dx%d, 4 color + depth)", m_Width, m_Height);
}

// ============================================================================
// G-Buffer Pass - Phase Scene End
// ============================================================================

void CRenderTarget::phase_scene_end()
{
    // Получаем текущий command buffer
    VkCommandBuffer cmd = RCache.m_Cmd;
    if (cmd == VK_NULL_HANDLE)
    {
        Msg("![Vulkan] phase_scene_end: No active command buffer");
        return;
    }

    // End dynamic rendering
    vkCmdEndRendering(cmd);

    // ========================================================================
    // Image Layout Transitions - G-Buffer RTs to SHADER_READ_ONLY
    // ========================================================================

    VkImageMemoryBarrier2 barriers[4];
    memset(barriers, 0, sizeof(barriers));
    u32 barrierCount = 0;

    // rt_Position: COLOR_ATTACHMENT → SHADER_READ_ONLY
    barriers[barrierCount].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barriers[barrierCount].pNext = nullptr;
    barriers[barrierCount].srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    barriers[barrierCount].srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    barriers[barrierCount].dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    barriers[barrierCount].dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
    barriers[barrierCount].oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barriers[barrierCount].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barriers[barrierCount].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[barrierCount].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[barrierCount].image = rt_Position.m_Image;
    barriers[barrierCount].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barriers[barrierCount].subresourceRange.baseMipLevel = 0;
    barriers[barrierCount].subresourceRange.levelCount = 1;
    barriers[barrierCount].subresourceRange.baseArrayLayer = 0;
    barriers[barrierCount].subresourceRange.layerCount = 1;
    barrierCount++;

    // rt_Normal: COLOR_ATTACHMENT → SHADER_READ_ONLY
    barriers[barrierCount].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barriers[barrierCount].pNext = nullptr;
    barriers[barrierCount].srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    barriers[barrierCount].srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    barriers[barrierCount].dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    barriers[barrierCount].dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
    barriers[barrierCount].oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barriers[barrierCount].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barriers[barrierCount].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[barrierCount].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[barrierCount].image = rt_Normal.m_Image;
    barriers[barrierCount].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barriers[barrierCount].subresourceRange.baseMipLevel = 0;
    barriers[barrierCount].subresourceRange.levelCount = 1;
    barriers[barrierCount].subresourceRange.baseArrayLayer = 0;
    barriers[barrierCount].subresourceRange.layerCount = 1;
    barrierCount++;

    // rt_Color: COLOR_ATTACHMENT → SHADER_READ_ONLY
    barriers[barrierCount].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barriers[barrierCount].pNext = nullptr;
    barriers[barrierCount].srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    barriers[barrierCount].srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    barriers[barrierCount].dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    barriers[barrierCount].dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
    barriers[barrierCount].oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barriers[barrierCount].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barriers[barrierCount].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[barrierCount].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[barrierCount].image = rt_Color.m_Image;
    barriers[barrierCount].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barriers[barrierCount].subresourceRange.baseMipLevel = 0;
    barriers[barrierCount].subresourceRange.levelCount = 1;
    barriers[barrierCount].subresourceRange.baseArrayLayer = 0;
    barriers[barrierCount].subresourceRange.layerCount = 1;
    barrierCount++;

    // rt_Material: COLOR_ATTACHMENT → SHADER_READ_ONLY
    barriers[barrierCount].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barriers[barrierCount].pNext = nullptr;
    barriers[barrierCount].srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    barriers[barrierCount].srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    barriers[barrierCount].dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    barriers[barrierCount].dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
    barriers[barrierCount].oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barriers[barrierCount].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barriers[barrierCount].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[barrierCount].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[barrierCount].image = rt_Material.m_Image;
    barriers[barrierCount].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barriers[barrierCount].subresourceRange.baseMipLevel = 0;
    barriers[barrierCount].subresourceRange.levelCount = 1;
    barriers[barrierCount].subresourceRange.baseArrayLayer = 0;
    barriers[barrierCount].subresourceRange.layerCount = 1;
    barrierCount++;

    // Submit barrier
    VkDependencyInfo dependencyInfo;
    memset(&dependencyInfo, 0, sizeof(dependencyInfo));
    dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependencyInfo.pNext = nullptr;
    dependencyInfo.dependencyFlags = 0;
    dependencyInfo.memoryBarrierCount = 0;
    dependencyInfo.pMemoryBarriers = nullptr;
    dependencyInfo.bufferMemoryBarrierCount = 0;
    dependencyInfo.pBufferMemoryBarriers = nullptr;
    dependencyInfo.imageMemoryBarrierCount = barrierCount;
    dependencyInfo.pImageMemoryBarriers = barriers;

    vkCmdPipelineBarrier2(cmd, &dependencyInfo);

    Msg("[Vulkan] phase_scene_end: G-Buffer pass ended, RTs ready for lighting");
}

} // namespace VK
