#pragma once
#include "vk_core.h"
#include <vector>

// Swapchain management
class CVulkanSwapchain
{
public:
    VkSwapchainKHR           m_Swapchain  = VK_NULL_HANDLE;
    std::vector<VkImage>     m_Images;
    std::vector<VkImageView> m_ImageViews;
    VkFormat                 m_Format     = VK_FORMAT_UNDEFINED;
    VkExtent2D               m_Extent     = {};
    u32                      m_ImageCount = 0;

    // Depth buffer
    VkImage       m_DepthImage      = VK_NULL_HANDLE;
    VkImageView   m_DepthView       = VK_NULL_HANDLE;
    VkImageView   m_DepthImageView  = VK_NULL_HANDLE;  // Alias for m_DepthView (for compatibility)
    VmaAllocation m_DepthAllocation = VK_NULL_HANDLE;
    VkFormat      m_DepthFormat     = VK_FORMAT_D32_SFLOAT;

    bool m_IsMinimized = false;

    // Whether any pass wrote to the swapchain this frame (set by phase_combine / EndUIPass)
    bool m_bRenderedThisFrame = false;

    // Current frame tracking
    VkImage       m_CurrentImage     = VK_NULL_HANDLE;
    VkImageView   m_CurrentImageView = VK_NULL_HANDLE;
    u32           m_CurrentImageIndex = 0;

public:
    void Create(u32 width, u32 height);
    void Destroy();
    void Recreate(u32 width, u32 height);

    u32 AcquireNextImage(VkSemaphore semaphore, VkFence fence = VK_NULL_HANDLE);
    void Present(VkSemaphore waitSemaphore, u32 imageIndex);

    bool ShouldRender() const { return !m_IsMinimized && m_Swapchain != VK_NULL_HANDLE; }

    // Accessors
    u32 GetWidth() const { return m_Extent.width; }
    u32 GetHeight() const { return m_Extent.height; }
    VkFormat GetFormat() const { return m_Format; }
    VkImage GetCurrentImage() const { return m_CurrentImage; }
    VkImageView GetCurrentImageView() const { return m_CurrentImageView; }

private:
    VkSurfaceFormatKHR ChooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats);
    VkPresentModeKHR ChoosePresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes);
    VkExtent2D ChooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities, u32 width, u32 height);

    // Depth buffer
    void CreateDepthResources();
    void DestroyDepthResources();
    VkFormat FindDepthFormat();
    VkFormat FindSupportedFormat(const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features);
};

// Глобальный экземпляр
extern CVulkanSwapchain Swapchain;
