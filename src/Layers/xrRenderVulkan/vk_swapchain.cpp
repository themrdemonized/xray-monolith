// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// Licensed under the same terms as X-Ray Engine (see root License.txt)

#include "stdafx.h"
#include "vk_swapchain.h"
#include "HW_Vulkan.h"

// VULKAN_DIAG
static void VulkanDiagWriteSwap(const char* msg) {
	HANDLE h = CreateFileA("D:\\anomaly\\appdata\\logs\\vulkan_diag.txt",
		FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS,
		FILE_ATTRIBUTE_NORMAL, NULL);
	if (h != INVALID_HANDLE_VALUE) { DWORD w; WriteFile(h, msg, (DWORD)strlen(msg), &w, NULL); WriteFile(h, "\r\n", 2, &w, NULL); FlushFileBuffers(h); CloseHandle(h); }
}
static struct DiagSwap1 { DiagSwap1() { VulkanDiagWriteSwap("[DIAG] vk_swapchain.cpp: before Swapchain"); } } g_diagSwap1;

// Глобальный экземпляр
CVulkanSwapchain Swapchain;
static struct DiagSwap2 { DiagSwap2() { VulkanDiagWriteSwap("[DIAG] vk_swapchain.cpp: after Swapchain"); } } g_diagSwap2;

// Выбор формата surface
VkSurfaceFormatKHR CVulkanSwapchain::ChooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats)
{
    // Предпочитаем BGRA8 UNORM (без автоматической гамма-коррекции)
    // X-Ray Engine D3D11 не использует sRGB конверсию, поэтому для совместимости
    // цветов используем UNORM формат. Иначе текстуры будут выглядеть слишком яркими
    // и с розоватым оттенком из-за двойной гамма-коррекции.
    for (const auto& format : availableFormats) {
        if (format.format == VK_FORMAT_B8G8R8A8_UNORM) {
            Msg("[Vulkan] Selected BGRA8 UNORM swapchain format (D3D11 compatible)");
            return format;
        }
    }

    // Fallback: BGRA8 SRGB (если UNORM недоступен)
    for (const auto& format : availableFormats) {
        if (format.format == VK_FORMAT_B8G8R8A8_SRGB &&
            format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            Msg("[Vulkan] WARNING: Using SRGB swapchain format, colors may differ from D3D11");
            return format;
        }
    }

    // Fallback: первый доступный
    Msg("[Vulkan] WARNING: Using fallback swapchain format");
    return availableFormats[0];
}

// Выбор present mode
VkPresentModeKHR CVulkanSwapchain::ChoosePresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes)
{
    // Предпочитаем MAILBOX (triple buffering, low latency)
    for (const auto& mode : availablePresentModes) {
        if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
            Msg("[Vulkan] Using MAILBOX present mode (triple buffering)");
            return mode;
        }
    }

    // Fallback: FIFO (vsync, always available)
    Msg("[Vulkan] Using FIFO present mode (vsync)");
    return VK_PRESENT_MODE_FIFO_KHR;
}

// Выбор extent
VkExtent2D CVulkanSwapchain::ChooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities, u32 width, u32 height)
{
    if (capabilities.currentExtent.width != UINT32_MAX) {
        return capabilities.currentExtent;
    }

    VkExtent2D actualExtent = { width, height };

    actualExtent.width = std::max(capabilities.minImageExtent.width,
                                   std::min(capabilities.maxImageExtent.width, actualExtent.width));
    actualExtent.height = std::max(capabilities.minImageExtent.height,
                                    std::min(capabilities.maxImageExtent.height, actualExtent.height));

    return actualExtent;
}

// Создание swapchain
void CVulkanSwapchain::Create(u32 width, u32 height)
{
    // Проверка на минимизацию
    if (width == 0 || height == 0) {
        Msg("[Vulkan] Window minimized, skipping swapchain creation");
        m_IsMinimized = true;
        return;
    }
    m_IsMinimized = false;

    // Получаем capabilities
    VkSurfaceCapabilitiesKHR capabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(VulkanHW.m_PhysicalDevice, VulkanHW.m_Surface, &capabilities);

    // Получаем форматы
    u32 formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(VulkanHW.m_PhysicalDevice, VulkanHW.m_Surface, &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(VulkanHW.m_PhysicalDevice, VulkanHW.m_Surface, &formatCount, formats.data());

    // Получаем present modes
    u32 presentModeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(VulkanHW.m_PhysicalDevice, VulkanHW.m_Surface, &presentModeCount, nullptr);
    std::vector<VkPresentModeKHR> presentModes(presentModeCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(VulkanHW.m_PhysicalDevice, VulkanHW.m_Surface, &presentModeCount, presentModes.data());

    // Выбираем параметры
    VkSurfaceFormatKHR surfaceFormat = ChooseSurfaceFormat(formats);
    VkPresentModeKHR presentMode = ChoosePresentMode(presentModes);
    VkExtent2D extent = ChooseSwapExtent(capabilities, width, height);

    // Количество images (triple buffering)
    u32 imageCount = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount) {
        imageCount = capabilities.maxImageCount;
    }

    Msg("[Vulkan] Creating swapchain: %dx%d, %u images", extent.width, extent.height, imageCount);

    // Создаём swapchain
    VkSwapchainCreateInfoKHR createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = VulkanHW.m_Surface;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

    u32 queueFamilyIndices[] = { VulkanHW.m_GraphicsFamily, VulkanHW.m_PresentFamily };

    if (VulkanHW.m_GraphicsFamily != VulkanHW.m_PresentFamily) {
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilyIndices;
    } else {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        createInfo.queueFamilyIndexCount = 0;
        createInfo.pQueueFamilyIndices = nullptr;
    }

    createInfo.preTransform = capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = VK_NULL_HANDLE;

    VK_CHECK(vkCreateSwapchainKHR(VulkanHW.m_Device, &createInfo, nullptr, &m_Swapchain));

    // Получаем images
    vkGetSwapchainImagesKHR(VulkanHW.m_Device, m_Swapchain, &imageCount, nullptr);
    m_Images.resize(imageCount);
    vkGetSwapchainImagesKHR(VulkanHW.m_Device, m_Swapchain, &imageCount, m_Images.data());

    m_Format = surfaceFormat.format;
    m_Extent = extent;
    m_ImageCount = imageCount;

    // Создаём image views
    m_ImageViews.resize(imageCount);
    for (u32 i = 0; i < imageCount; i++) {
        VkImageViewCreateInfo viewInfo = {};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = m_Images[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = m_Format;
        viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        VK_CHECK(vkCreateImageView(VulkanHW.m_Device, &viewInfo, nullptr, &m_ImageViews[i]));
    }

    // Создаём depth buffer
    CreateDepthResources();

    Msg("[Vulkan] Swapchain created successfully");
}

// Уничтожение swapchain
void CVulkanSwapchain::Destroy()
{
    if (VulkanHW.m_Device == VK_NULL_HANDLE) return;

    // Уничтожаем depth buffer
    DestroyDepthResources();

    // Уничтожаем image views
    for (auto imageView : m_ImageViews) {
        vkDestroyImageView(VulkanHW.m_Device, imageView, nullptr);
    }
    m_ImageViews.clear();

    // Уничтожаем swapchain
    if (m_Swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(VulkanHW.m_Device, m_Swapchain, nullptr);
        m_Swapchain = VK_NULL_HANDLE;
    }

    m_Images.clear();

    Msg("[Vulkan] Swapchain destroyed");
}

// Пересоздание swapchain (при resize)
void CVulkanSwapchain::Recreate(u32 width, u32 height)
{
    Msg("[Vulkan] Recreating swapchain: %dx%d", width, height);

    // Ждём завершения работы
    vkDeviceWaitIdle(VulkanHW.m_Device);

    // Уничтожаем старый swapchain
    Destroy();

    // Создаём новый
    Create(width, height);
}

// Получение следующего image
u32 CVulkanSwapchain::AcquireNextImage(VkSemaphore semaphore, VkFence fence)
{
    u32 imageIndex = 0;
    VkResult result = vkAcquireNextImageKHR(VulkanHW.m_Device, m_Swapchain, UINT64_MAX,
                                            semaphore, fence, &imageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        Msg("[Vulkan] Swapchain out of date during acquire - recreating");
        // Recreate swapchain
        vkDeviceWaitIdle(VulkanHW.m_Device);
        Recreate(m_Extent.width, m_Extent.height);
        // Try acquire again
        result = vkAcquireNextImageKHR(VulkanHW.m_Device, m_Swapchain, UINT64_MAX,
                                        semaphore, fence, &imageIndex);
        if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
            Msg("![Vulkan] Failed to acquire after swapchain recreate: %d", result);
            return UINT32_MAX;
        }
    } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        Msg("![Vulkan] Failed to acquire swapchain image: %d", result);
        return UINT32_MAX;
    }

    // Update current image tracking
    m_CurrentImageIndex = imageIndex;
    m_CurrentImage = m_Images[imageIndex];
    m_CurrentImageView = m_ImageViews[imageIndex];

    return imageIndex;
}

// Present
void CVulkanSwapchain::Present(VkSemaphore waitSemaphore, u32 imageIndex)
{
    VkPresentInfoKHR presentInfo = {};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &waitSemaphore;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &m_Swapchain;
    presentInfo.pImageIndices = &imageIndex;
    presentInfo.pResults = nullptr;

    VkResult result = vkQueuePresentKHR(VulkanHW.m_PresentQueue, &presentInfo);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        Msg("[Vulkan] Swapchain out of date/suboptimal during present (result=%d)", result);
    } else if (result != VK_SUCCESS) {
        Msg("![Vulkan] Failed to present swapchain image: %d", result);
    }
}

// ============================================================================
// Depth Buffer
// ============================================================================

// Поиск поддерживаемого формата
VkFormat CVulkanSwapchain::FindSupportedFormat(const std::vector<VkFormat>& candidates,
                                                VkImageTiling tiling,
                                                VkFormatFeatureFlags features)
{
    for (VkFormat format : candidates) {
        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(VulkanHW.m_PhysicalDevice, format, &props);

        if (tiling == VK_IMAGE_TILING_LINEAR && (props.linearTilingFeatures & features) == features) {
            return format;
        } else if (tiling == VK_IMAGE_TILING_OPTIMAL && (props.optimalTilingFeatures & features) == features) {
            return format;
        }
    }

    Msg("!Failed to find supported format");
    return VK_FORMAT_UNDEFINED;
}

// Поиск depth формата
VkFormat CVulkanSwapchain::FindDepthFormat()
{
    // Пробуем форматы в порядке предпочтения
    std::vector<VkFormat> candidates = {
        VK_FORMAT_D32_SFLOAT,           // 32-bit float depth (preferred)
        VK_FORMAT_D32_SFLOAT_S8_UINT,   // 32-bit float depth + 8-bit stencil
        VK_FORMAT_D24_UNORM_S8_UINT     // 24-bit depth + 8-bit stencil
    };

    VkFormat format = FindSupportedFormat(
        candidates,
        VK_IMAGE_TILING_OPTIMAL,
        VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT
    );

    if (format != VK_FORMAT_UNDEFINED) {
        Msg("[Vulkan] Selected depth format: %d", format);
        return format;
    }

    Msg("!Failed to find depth format");
    return VK_FORMAT_D32_SFLOAT;  // Fallback
}

// Создание depth resources
void CVulkanSwapchain::CreateDepthResources()
{
    // Находим подходящий depth format
    m_DepthFormat = FindDepthFormat();

    // Создаём depth image через VMA
    VkImageCreateInfo imageInfo = {};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = m_Extent.width;
    imageInfo.extent.height = m_Extent.height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = m_DepthFormat;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    allocInfo.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;

    VK_CHECK(vmaCreateImage(VulkanHW.m_Allocator, &imageInfo, &allocInfo,
                            &m_DepthImage, &m_DepthAllocation, nullptr));

    // Создаём image view для depth
    VkImageViewCreateInfo viewInfo = {};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_DepthImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = m_DepthFormat;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    VK_CHECK(vkCreateImageView(VulkanHW.m_Device, &viewInfo, nullptr, &m_DepthView));
    m_DepthImageView = m_DepthView;  // Sync alias

    Msg("[Vulkan] Depth buffer created: %dx%d, format %d",
        m_Extent.width, m_Extent.height, m_DepthFormat);
}

// Уничтожение depth resources
void CVulkanSwapchain::DestroyDepthResources()
{
    if (m_DepthView != VK_NULL_HANDLE) {
        vkDestroyImageView(VulkanHW.m_Device, m_DepthView, nullptr);
        m_DepthView = VK_NULL_HANDLE;
        m_DepthImageView = VK_NULL_HANDLE;  // Clear alias
    }

    if (m_DepthImage != VK_NULL_HANDLE) {
        vmaDestroyImage(VulkanHW.m_Allocator, m_DepthImage, m_DepthAllocation);
        m_DepthImage = VK_NULL_HANDLE;
        m_DepthAllocation = VK_NULL_HANDLE;
    }
}
