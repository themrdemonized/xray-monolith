// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// Licensed under the same terms as X-Ray Engine (see root License.txt)

// ============================================================================
// vk_ParticleTextures.h - Particle texture management
// ============================================================================

#pragma once

#include <vulkan/vulkan.h>

namespace VK
{
    class CVulkanBuffer;
    class CVulkanImage;
}

// ============================================================================
// Global white texture for particles (fallback)
// ============================================================================
class vkParticleTextureManager
{
public:
    // Create global white texture (4x4 white pixels)
    static bool CreateWhiteTexture(VkDevice device);
    static void DestroyWhiteTexture(VkDevice device);

    // Get white texture image view and sampler
    static VkImageView GetWhiteTextureView();
    static VkSampler GetWhiteTextureSampler();

private:
    static VkImage s_WhiteTextureImage;
    static VkImageView s_WhiteTextureView;
    static VkSampler s_WhiteTextureSampler;
    static VmaAllocation s_WhiteTextureAllocation;
    static bool s_WhiteTextureInitialized;

    // Helper to create a simple 4x4 white texture
    static bool CreateTextureImage(VkDevice device, VkImage& outImage, VmaAllocation& outAllocation);
    static bool CreateTextureImageView(VkDevice device, VkImage image, VkImageView& outView);
    static bool CreateTextureSampler(VkDevice device, VkSampler& outSampler);
};
