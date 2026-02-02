// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// SPDX-License-Identifier: MIT

#include "stdafx.h"
#include "SH_RT_Vulkan.h"
#include "HW_Vulkan.h"

namespace VK
{

// Создание render target
void CRT::Create(VkFormat format, u32 width, u32 height, VkImageUsageFlags usage, bool isDepth)
{
    m_Format = format;
    m_Width = width;
    m_Height = height;
    m_IsDepth = isDepth;

    // Создаём image
    VkImageCreateInfo imageInfo = {};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = usage;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    // Аллоцируем через VMA
    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    allocInfo.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;

    VK_CHECK(vmaCreateImage(VulkanHW.m_Allocator, &imageInfo, &allocInfo,
                            &m_Image, &m_Allocation, nullptr));

    // Создаём image view
    VkImageViewCreateInfo viewInfo = {};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_Image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = GetAspect();
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    VK_CHECK(vkCreateImageView(VulkanHW.m_Device, &viewInfo, nullptr, &m_ImageView));

    // Create sampler for shader sampling
    VkSamplerCreateInfo samplerInfo = {};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 1.0f;
    samplerInfo.compareEnable = isDepth ? VK_TRUE : VK_FALSE;
    samplerInfo.compareOp = isDepth ? VK_COMPARE_OP_LESS_OR_EQUAL : VK_COMPARE_OP_ALWAYS;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;

    VK_CHECK(vkCreateSampler(VulkanHW.m_Device, &samplerInfo, nullptr, &m_Sampler));

    Msg("[Vulkan] RT created: %dx%d, format %d, img=%p view=%p", width, height, format, m_Image, m_ImageView);
}

// Создание cubemap render target (Phase 2.16)
void CRT::CreateCube(VkFormat format, u32 width, u32 height, VkImageUsageFlags usage, bool isDepth)
{
    m_Format = format;
    m_Width = width;
    m_Height = height;
    m_IsDepth = isDepth;
    m_IsCubemap = true;

    // Создаём cubemap image (6 layers)
    VkImageCreateInfo imageInfo = {};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;  // CRITICAL for cubemaps!
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 6;  // 6 faces: +X, -X, +Y, -Y, +Z, -Z
    imageInfo.format = format;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = usage;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    // Аллоцируем через VMA
    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    allocInfo.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;

    VK_CHECK(vmaCreateImage(VulkanHW.m_Allocator, &imageInfo, &allocInfo,
                            &m_Image, &m_Allocation, nullptr));

    // Создаём cubemap image view (для sampling)
    VkImageViewCreateInfo cubeViewInfo = {};
    cubeViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    cubeViewInfo.image = m_Image;
    cubeViewInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;  // Full cubemap view
    cubeViewInfo.format = format;
    cubeViewInfo.subresourceRange.aspectMask = GetAspect();
    cubeViewInfo.subresourceRange.baseMipLevel = 0;
    cubeViewInfo.subresourceRange.levelCount = 1;
    cubeViewInfo.subresourceRange.baseArrayLayer = 0;
    cubeViewInfo.subresourceRange.layerCount = 6;  // All 6 faces

    VK_CHECK(vkCreateImageView(VulkanHW.m_Device, &cubeViewInfo, nullptr, &m_ImageView));

    // Создаём individual image views для каждой грани (для rendering)
    const char* faceNames[6] = {"+X", "-X", "+Y", "-Y", "+Z", "-Z"};
    for (u32 face = 0; face < 6; face++)
    {
        VkImageViewCreateInfo faceViewInfo = {};
        faceViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        faceViewInfo.image = m_Image;
        faceViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;  // 2D view of single face
        faceViewInfo.format = format;
        faceViewInfo.subresourceRange.aspectMask = GetAspect();
        faceViewInfo.subresourceRange.baseMipLevel = 0;
        faceViewInfo.subresourceRange.levelCount = 1;
        faceViewInfo.subresourceRange.baseArrayLayer = face;  // Specific face
        faceViewInfo.subresourceRange.layerCount = 1;

        VK_CHECK(vkCreateImageView(VulkanHW.m_Device, &faceViewInfo, nullptr, &m_FaceViews[face]));

        Msg("[Vulkan]   Cubemap face %s view created", faceNames[face]);
    }

    // Create sampler for cubemap sampling
    VkSamplerCreateInfo samplerInfo = {};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 1.0f;
    samplerInfo.compareEnable = isDepth ? VK_TRUE : VK_FALSE;
    samplerInfo.compareOp = isDepth ? VK_COMPARE_OP_LESS_OR_EQUAL : VK_COMPARE_OP_ALWAYS;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;

    VK_CHECK(vkCreateSampler(VulkanHW.m_Device, &samplerInfo, nullptr, &m_Sampler));

    Msg("[Vulkan] Cubemap RT created: %dx%dx6, format %d", width, height, format);
}

// Создание 3D texture render target (Phase 0.1 - 3D Fluid)
void CRT::Create3D(VkFormat format, u32 width, u32 height, u32 depth, VkImageUsageFlags usage)
{
    m_Format = format;
    m_Width = width;
    m_Height = height;
    m_Depth = depth;
    m_Is3D = true;

    // Создаём 3D image
    VkImageCreateInfo imageInfo = {};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_3D;  // CRITICAL: 3D image type
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = depth;  // Z-dimension
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = usage;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.flags = 0;

    // Аллоцируем через VMA
    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    allocInfo.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;

    VK_CHECK(vmaCreateImage(VulkanHW.m_Allocator, &imageInfo, &allocInfo,
                            &m_Image, &m_Allocation, nullptr));

    // Создаём 3D image view (для sampling и compute storage)
    VkImageViewCreateInfo viewInfo = {};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_Image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_3D;  // CRITICAL: 3D view type
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    VK_CHECK(vkCreateImageView(VulkanHW.m_Device, &viewInfo, nullptr, &m_ImageView));

    // Create sampler for 3D texture sampling
    VkSamplerCreateInfo samplerInfo = {};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 1.0f;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;

    VK_CHECK(vkCreateSampler(VulkanHW.m_Device, &samplerInfo, nullptr, &m_Sampler));

    Msg("[Vulkan] 3D RT created: %dx%dx%d, format %d", width, height, depth, format);
}

// Уничтожение render target
void CRT::Destroy()
{
    Msg("[Vulkan] RT Destroy: img=%p view=%p %dx%d fmt=%d", m_Image, m_ImageView, m_Width, m_Height, m_Format);
    // Destroy cubemap face views
    if (m_IsCubemap) {
        for (u32 i = 0; i < 6; i++) {
            if (m_FaceViews[i] != VK_NULL_HANDLE) {
                vkDestroyImageView(VulkanHW.m_Device, m_FaceViews[i], nullptr);
                m_FaceViews[i] = VK_NULL_HANDLE;
            }
        }
    }

    // Destroy sampler
    if (m_Sampler != VK_NULL_HANDLE) {
        vkDestroySampler(VulkanHW.m_Device, m_Sampler, nullptr);
        m_Sampler = VK_NULL_HANDLE;
    }

    // Destroy main image view
    if (m_ImageView != VK_NULL_HANDLE) {
        vkDestroyImageView(VulkanHW.m_Device, m_ImageView, nullptr);
        m_ImageView = VK_NULL_HANDLE;
    }

    // Destroy image
    if (m_Image != VK_NULL_HANDLE) {
        vmaDestroyImage(VulkanHW.m_Allocator, m_Image, m_Allocation);
        m_Image = VK_NULL_HANDLE;
        m_Allocation = VK_NULL_HANDLE;
    }

    m_IsCubemap = false;
    m_Is3D = false;
    m_Depth = 1;
}

// Layout transition
void CRT::TransitionLayout(VkCommandBuffer cmd, VkImageLayout oldLayout, VkImageLayout newLayout)
{
    VkImageMemoryBarrier2 barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = m_Image;
    barrier.subresourceRange.aspectMask = GetAspect();
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = m_IsCubemap ? 6 : 1;  // 6 layers for cubemaps

    // Определяем stage masks и access masks на основе layout
    VkPipelineStageFlags2 srcStage = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    VkPipelineStageFlags2 dstStage = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    VkAccessFlags2 srcAccess = 0;
    VkAccessFlags2 dstAccess = 0;

    // Source layout
    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED) {
        srcStage = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
        srcAccess = 0;
    } else if (oldLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
        srcStage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        srcAccess = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
        srcStage = VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
        srcAccess = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        srcStage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        srcAccess = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_GENERAL) {
        srcStage = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        srcAccess = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
    }

    // Destination layout
    if (newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
        dstStage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        dstAccess = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    } else if (newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
        dstStage = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT;
        dstAccess = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    } else if (newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        dstStage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        dstAccess = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
    } else if (newLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR) {
        dstStage = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
        dstAccess = 0;
    } else if (newLayout == VK_IMAGE_LAYOUT_GENERAL) {
        dstStage = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        dstAccess = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
    }

    barrier.srcStageMask = srcStage;
    barrier.srcAccessMask = srcAccess;
    barrier.dstStageMask = dstStage;
    barrier.dstAccessMask = dstAccess;

    VkDependencyInfo depInfo = {};
    depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    depInfo.imageMemoryBarrierCount = 1;
    depInfo.pImageMemoryBarriers = &barrier;

    vkCmdPipelineBarrier2(cmd, &depInfo);
}

// Получение aspect mask
VkImageAspectFlags CRT::GetAspect() const
{
    if (m_IsDepth) {
        // Проверяем есть ли stencil
        if (m_Format == VK_FORMAT_D32_SFLOAT_S8_UINT ||
            m_Format == VK_FORMAT_D24_UNORM_S8_UINT) {
            return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
        }
        return VK_IMAGE_ASPECT_DEPTH_BIT;
    }
    return VK_IMAGE_ASPECT_COLOR_BIT;
}

} // namespace VK
