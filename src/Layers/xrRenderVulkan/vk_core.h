// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// SPDX-License-Identifier: MIT

#pragma once
#include "stdafx.h"

// Error checking macro
#define VK_CHECK(result) \
    do { \
        VkResult res = (result); \
        if (res != VK_SUCCESS) { \
            Msg("!Vulkan error: %d at %s:%d", res, __FILE__, __LINE__); \
            VERIFY(false); \
        } \
    } while(0)

// Required instance extensions
inline const char* g_InstanceExtensions[] = {
    VK_KHR_SURFACE_EXTENSION_NAME,
    VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
    #ifdef DEBUG
    VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
    #endif
};

inline constexpr u32 g_InstanceExtensionCount = sizeof(g_InstanceExtensions) / sizeof(g_InstanceExtensions[0]);

// Validation layers (Debug only)
#ifdef DEBUG
inline const char* g_ValidationLayers[] = {
    "VK_LAYER_KHRONOS_validation"
};
inline constexpr u32 g_ValidationLayerCount = sizeof(g_ValidationLayers) / sizeof(g_ValidationLayers[0]);
#else
inline constexpr u32 g_ValidationLayerCount = 0;
#endif

// Required device extensions
inline const char* g_DeviceExtensions[] = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
};

inline constexpr u32 g_DeviceExtensionCount = sizeof(g_DeviceExtensions) / sizeof(g_DeviceExtensions[0]);

// Forward declarations
struct VulkanCaps;

// Core functions
bool VK_CreateInstance(VkInstance* outInstance);
void VK_DestroyInstance(VkInstance instance);

// Physical device selection
VkPhysicalDevice VK_SelectPhysicalDevice(VkInstance instance, VulkanCaps* outCaps);
bool VK_CheckDeviceExtensionSupport(VkPhysicalDevice device);

#ifdef DEBUG
// Debug messenger setup
VkResult VK_SetupDebugMessenger(VkInstance instance, VkDebugUtilsMessengerEXT* outMessenger);
void VK_DestroyDebugMessenger(VkInstance instance, VkDebugUtilsMessengerEXT messenger);
#endif
