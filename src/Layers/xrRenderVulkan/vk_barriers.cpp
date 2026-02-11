// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// Licensed under the same terms as X-Ray Engine (see root License.txt)

#include "stdafx.h"
#include "vk_barriers.h"

namespace VK
{

// ---------------------------------------------------------------------------
// Layout -> stage/access derivation (matches CRT::TransitionLayout logic)
// ---------------------------------------------------------------------------
static void DeriveStageAccess(VkImageLayout layout, bool isSrc,
    VkPipelineStageFlags2& stage, VkAccessFlags2& access)
{
    switch (layout)
    {
    case VK_IMAGE_LAYOUT_UNDEFINED:
        stage  = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
        access = 0;
        break;

    case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
        stage  = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        access = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT;
        break;

    case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
    case VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL:
        stage  = isSrc ? VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT
                       : VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT;
        access = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT
               | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
        break;

    case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
        stage  = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        access = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
        break;

    case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
        stage  = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        access = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        break;

    case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
        stage  = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        access = VK_ACCESS_2_TRANSFER_READ_BIT;
        break;

    case VK_IMAGE_LAYOUT_GENERAL:
        stage  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        access = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
        break;

    case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
        stage  = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
        access = 0;
        break;

    default:
        stage  = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        access = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
        break;
    }
}

// ---------------------------------------------------------------------------
// ImageBarrier
// ---------------------------------------------------------------------------
void ImageBarrier(VkCommandBuffer cmd, VkImage image,
    VkImageLayout oldLayout, VkImageLayout newLayout,
    VkImageAspectFlags aspect, u32 layerCount, u32 mipLevels)
{
    VkPipelineStageFlags2 srcStage, dstStage;
    VkAccessFlags2 srcAccess, dstAccess;
    DeriveStageAccess(oldLayout, true,  srcStage, srcAccess);
    DeriveStageAccess(newLayout, false, dstStage, dstAccess);

    VkImageMemoryBarrier2 barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barrier.srcStageMask  = srcStage;
    barrier.srcAccessMask = srcAccess;
    barrier.dstStageMask  = dstStage;
    barrier.dstAccessMask = dstAccess;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask     = aspect;
    barrier.subresourceRange.baseMipLevel   = 0;
    barrier.subresourceRange.levelCount     = mipLevels;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount     = layerCount;

    VkDependencyInfo dep = {};
    dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep.imageMemoryBarrierCount = 1;
    dep.pImageMemoryBarriers = &barrier;

    vkCmdPipelineBarrier2(cmd, &dep);
}

// ---------------------------------------------------------------------------
// ImageBarriers (batch)
// ---------------------------------------------------------------------------
void ImageBarriers(VkCommandBuffer cmd, u32 count, const VkImage* images,
    VkImageLayout oldLayout, VkImageLayout newLayout,
    VkImageAspectFlags aspect, u32 layerCount)
{
    if (count == 0) return;

    VkPipelineStageFlags2 srcStage, dstStage;
    VkAccessFlags2 srcAccess, dstAccess;
    DeriveStageAccess(oldLayout, true,  srcStage, srcAccess);
    DeriveStageAccess(newLayout, false, dstStage, dstAccess);

    constexpr u32 MAX_BATCH = 8;
    VkImageMemoryBarrier2 barriers[MAX_BATCH] = {};
    u32 n = (count < MAX_BATCH) ? count : MAX_BATCH;

    for (u32 i = 0; i < n; i++)
    {
        barriers[i].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        barriers[i].srcStageMask  = srcStage;
        barriers[i].srcAccessMask = srcAccess;
        barriers[i].dstStageMask  = dstStage;
        barriers[i].dstAccessMask = dstAccess;
        barriers[i].oldLayout = oldLayout;
        barriers[i].newLayout = newLayout;
        barriers[i].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barriers[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barriers[i].image = images[i];
        barriers[i].subresourceRange.aspectMask     = aspect;
        barriers[i].subresourceRange.baseMipLevel   = 0;
        barriers[i].subresourceRange.levelCount     = 1;
        barriers[i].subresourceRange.baseArrayLayer = 0;
        barriers[i].subresourceRange.layerCount     = layerCount;
    }

    VkDependencyInfo dep = {};
    dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep.imageMemoryBarrierCount = n;
    dep.pImageMemoryBarriers = barriers;

    vkCmdPipelineBarrier2(cmd, &dep);
}

// ---------------------------------------------------------------------------
// BufferBarrier
// ---------------------------------------------------------------------------
void BufferBarrier(VkCommandBuffer cmd, VkBuffer buffer,
    VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
    VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess,
    VkDeviceSize offset, VkDeviceSize size)
{
    VkBufferMemoryBarrier2 barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
    barrier.srcStageMask  = srcStage;
    barrier.srcAccessMask = srcAccess;
    barrier.dstStageMask  = dstStage;
    barrier.dstAccessMask = dstAccess;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer = buffer;
    barrier.offset = offset;
    barrier.size   = size;

    VkDependencyInfo dep = {};
    dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep.bufferMemoryBarrierCount = 1;
    dep.pBufferMemoryBarriers = &barrier;

    vkCmdPipelineBarrier2(cmd, &dep);
}

// ---------------------------------------------------------------------------
// MemoryBarrier
// ---------------------------------------------------------------------------
void MemoryBarrier(VkCommandBuffer cmd,
    VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
    VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess)
{
    VkMemoryBarrier2 barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
    barrier.srcStageMask  = srcStage;
    barrier.srcAccessMask = srcAccess;
    barrier.dstStageMask  = dstStage;
    barrier.dstAccessMask = dstAccess;

    VkDependencyInfo dep = {};
    dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep.memoryBarrierCount = 1;
    dep.pMemoryBarriers = &barrier;

    vkCmdPipelineBarrier2(cmd, &dep);
}

} // namespace VK
