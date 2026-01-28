// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// SPDX-License-Identifier: MIT

#pragma once
#include "vk_core.h"

namespace VK
{

// Vulkan Render Target
class CRT
{
public:
    VkImage       m_Image      = VK_NULL_HANDLE;
    VkImageView   m_ImageView  = VK_NULL_HANDLE;  // Full view (cube or 2D)
    VmaAllocation m_Allocation = VK_NULL_HANDLE;

    // For cubemaps: individual face views for rendering
    VkImageView   m_FaceViews[6] = {VK_NULL_HANDLE};  // +X, -X, +Y, -Y, +Z, -Z

    VkFormat      m_Format     = VK_FORMAT_UNDEFINED;
    u32           m_Width      = 0;
    u32           m_Height     = 0;

    bool          m_IsDepth    = false;
    bool          m_IsCubemap  = false;

public:
    void Create(VkFormat format, u32 width, u32 height, VkImageUsageFlags usage, bool isDepth = false);
    void CreateCube(VkFormat format, u32 width, u32 height, VkImageUsageFlags usage, bool isDepth = false);
    void Destroy();

    // Layout transitions
    void TransitionLayout(VkCommandBuffer cmd, VkImageLayout oldLayout, VkImageLayout newLayout);

    // Accessors
    VkImage GetImage() const { return m_Image; }
    VkImageView GetView() const { return m_ImageView; }
    VkSampler GetSampler() const { return m_Sampler; }  // For textures with samplers

    // Utility
    VkImageAspectFlags GetAspect() const;

private:
    VkSampler m_Sampler = VK_NULL_HANDLE;  // Optional: for textures that need sampling
};

} // namespace VK
