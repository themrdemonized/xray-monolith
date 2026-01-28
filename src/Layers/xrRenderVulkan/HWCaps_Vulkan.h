#pragma once
#include "stdafx.h"

// Структура для хранения capabilities физического устройства
struct VulkanCaps
{
    // Vulkan 1.3 features
    bool dynamicRendering = false;
    bool synchronization2 = false;
    bool maintenance4 = false;

    // Limits
    u32 maxColorAttachments = 0;
    float maxSamplerAnisotropy = 0.0f;
    VkSampleCountFlags msaaSamples = VK_SAMPLE_COUNT_1_BIT;

    // Memory
    VkDeviceSize totalDeviceMemory = 0;

    // Info
    char deviceName[256] = {};
    u32 vendorID = 0;
    u32 deviceID = 0;
    u32 driverVersion = 0;
    u32 apiVersion = 0;

    // Проверка минимальных требований
    bool CheckMinimumRequirements() const
    {
        if (apiVersion < VK_API_VERSION_1_3) {
            Msg("!Vulkan 1.3 not supported (current: %d.%d.%d)",
                VK_API_VERSION_MAJOR(apiVersion),
                VK_API_VERSION_MINOR(apiVersion),
                VK_API_VERSION_PATCH(apiVersion));
            return false;
        }

        if (!dynamicRendering) {
            Msg("!Dynamic Rendering feature not supported");
            return false;
        }

        if (!synchronization2) {
            Msg("!Synchronization2 feature not supported");
            return false;
        }

        return true;
    }

    // Вывод информации
    void LogInfo() const
    {
        Msg("[Vulkan] Device: %s", deviceName);
        Msg("[Vulkan] Vendor ID: 0x%X", vendorID);
        Msg("[Vulkan] API Version: %d.%d.%d",
            VK_API_VERSION_MAJOR(apiVersion),
            VK_API_VERSION_MINOR(apiVersion),
            VK_API_VERSION_PATCH(apiVersion));
        Msg("[Vulkan] Driver Version: %d", driverVersion);
        Msg("[Vulkan] Total VRAM: %llu MB", totalDeviceMemory / (1024 * 1024));
        Msg("[Vulkan] Max Color Attachments: %u", maxColorAttachments);
        Msg("[Vulkan] Max Anisotropy: %.1f", maxSamplerAnisotropy);

        // Features
        Msg("[Vulkan] Features:");
        Msg("  - Dynamic Rendering: %s", dynamicRendering ? "YES" : "NO");
        Msg("  - Synchronization2: %s", synchronization2 ? "YES" : "NO");
        Msg("  - Maintenance4: %s", maintenance4 ? "YES" : "NO");
    }
};
