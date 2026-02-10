// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// Licensed under the same terms as X-Ray Engine (see root License.txt)

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
    u32           m_Depth      = 1;  // For 3D textures

    bool          m_IsDepth    = false;
    bool          m_IsCubemap  = false;
    bool          m_Is3D       = false;

public:
    void Create(VkFormat format, u32 width, u32 height, VkImageUsageFlags usage, bool isDepth = false);
    void CreateCube(VkFormat format, u32 width, u32 height, VkImageUsageFlags usage, bool isDepth = false);
    void Create3D(VkFormat format, u32 width, u32 height, u32 depth, VkImageUsageFlags usage);
    void Destroy();

    // Layout transitions
    void TransitionLayout(VkCommandBuffer cmd, VkImageLayout oldLayout, VkImageLayout newLayout);

    // Accessors
    VkImage GetImage() const { return m_Image; }
    VkImageView GetImageView() const { return m_ImageView; }
    VkImageView GetView() const { return m_ImageView; }  // Alias
    VkSampler GetSampler() const { return m_Sampler; }  // For textures with samplers

    u32 GetWidth() const { return m_Width; }
    u32 GetHeight() const { return m_Height; }
    u32 GetDepth() const { return m_Depth; }
    bool Is3D() const { return m_Is3D; }

    // Utility
    VkImageAspectFlags GetAspect() const;

private:
    VkSampler m_Sampler = VK_NULL_HANDLE;  // Optional: for textures that need sampling
};

} // namespace VK
