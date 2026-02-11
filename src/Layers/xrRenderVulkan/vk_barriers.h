// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// Licensed under the same terms as X-Ray Engine (see root License.txt)

#pragma once
#include "vk_core.h"

namespace VK
{

// Image barrier with auto-derived stage/access from layouts.
// Uses VkImageMemoryBarrier2 + vkCmdPipelineBarrier2 (Vulkan 1.3).
void ImageBarrier(VkCommandBuffer cmd, VkImage image,
    VkImageLayout oldLayout, VkImageLayout newLayout,
    VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT,
    u32 layerCount = 1, u32 mipLevels = 1);

// Batch image barriers - same transition for multiple images.
void ImageBarriers(VkCommandBuffer cmd, u32 count, const VkImage* images,
    VkImageLayout oldLayout, VkImageLayout newLayout,
    VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT, u32 layerCount = 1);

// Buffer barrier - explicit stage/access (no auto-derivation).
void BufferBarrier(VkCommandBuffer cmd, VkBuffer buffer,
    VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
    VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess,
    VkDeviceSize offset = 0, VkDeviceSize size = VK_WHOLE_SIZE);

// Memory barrier - global sync point.
void MemoryBarrier(VkCommandBuffer cmd,
    VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
    VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess);

} // namespace VK
