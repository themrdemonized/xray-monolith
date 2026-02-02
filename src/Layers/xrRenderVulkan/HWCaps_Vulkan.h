// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// SPDX-License-Identifier: MIT

#pragma once
#include "stdafx.h"

// Структура для хранения capabilities физического устройства
struct VulkanCaps
{
    // ========================================================================
    // Basic Info (Device Identification)
    // ========================================================================
    char deviceName[256] = {};
    u32 vendorID = 0;
    u32 deviceID = 0;
    u32 driverVersion = 0;
    u32 apiVersion = 0;

    // ========================================================================
    // Vulkan 1.3 Core Features
    // ========================================================================
    bool dynamicRendering = false;
    bool synchronization2 = false;
    bool maintenance4 = false;

    // ========================================================================
    // Geometry Capabilities (Vertex Processing)
    // ========================================================================
    struct GeometryCaps
    {
        u32 maxVertexInputAttributes = 0;       // Usually 16-32
        u32 maxVertexInputBindings = 0;         // Usually 16-32
        u32 maxVertexOutputComponents = 0;      // Usually 64-128
        u32 maxClipPlanes = 0;                  // User clip planes (usually 8)
        bool bVTF = false;                      // Vertex Texture Fetch support
        bool bPointSprites = false;             // Point sprites (always true in modern)
        bool bGeometryShader = false;           // Geometry shader support
        bool bTessellation = false;             // Tessellation support
    } geometry;

    // ========================================================================
    // Raster Capabilities (Fragment Processing)
    // ========================================================================
    struct RasterCaps
    {
        u32 maxFragmentInputComponents = 0;     // Usually 64-128
        u32 maxFragmentOutputAttachments = 0;   // MRT count (usually 4-8)
        u32 maxFragmentCombinedOutputResources = 0;
        u32 maxDescriptorSetSamplers = 0;       // Texture stages (usually 16+)
        bool bMRT_mixdepth = false;             // MRT with different depth formats
        bool bNonPow2Textures = false;          // Non-power-of-2 textures
        bool bCubemaps = false;                 // Cubemap support
        bool bTextureCompression_BC = false;    // BC1-BC7 (DXT)
        bool bTextureCompression_ETC2 = false;  // ETC2/EAC
        bool bTextureCompression_ASTC = false;  // ASTC
    } raster;

    // ========================================================================
    // Multi-GPU Support (SLI/CrossFire/Multi-Adapter)
    // ========================================================================
    u32 iGPUNum = 1;                            // Number of GPUs in SLI/CrossFire
    bool bDeviceGroup = false;                  // VK_KHR_device_group support

    // ========================================================================
    // MSAA / Anti-Aliasing
    // ========================================================================
    VkSampleCountFlags msaaSamples = VK_SAMPLE_COUNT_1_BIT;
    bool bMSAA_2x = false;
    bool bMSAA_4x = false;
    bool bMSAA_8x = false;
    u32 maxMSAASamples = 1;                     // Maximum MSAA sample count

    // ========================================================================
    // Depth/Stencil Capabilities
    // ========================================================================
    bool bStencil = false;                      // Stencil buffer support
    bool bDepthBoundsTest = false;              // Depth bounds test
    bool bDepthClamp = false;                   // Depth clamp (disable clipping)
    u32 dwMaxStencilValue = 255;                // Max stencil value (usually 255)

    // ========================================================================
    // Advanced Rendering Features
    // ========================================================================
    bool bConservativeRasterization = false;    // Conservative raster
    bool bVariableRateShading = false;          // VRS support
    bool bRayTracing = false;                   // Ray tracing support
    bool bMeshShader = false;                   // Mesh shader support

    // ========================================================================
    // Memory and Limits
    // ========================================================================
    VkDeviceSize totalDeviceMemory = 0;
    u32 maxColorAttachments = 0;
    float maxSamplerAnisotropy = 0.0f;
    u32 maxTextureSize = 0;                     // Max 2D texture dimension
    u32 maxCubemapSize = 0;                     // Max cubemap dimension
    u32 maxViewports = 0;                       // Multi-viewport rendering

    // ========================================================================
    // Format Support (checked at runtime)
    // ========================================================================
    bool bFormatD24S8 = false;                  // D24_UNORM_S8_UINT
    bool bFormatD32F = false;                   // D32_SFLOAT
    bool bFormatBC1 = false;                    // BC1_UNORM (DXT1)
    bool bFormatBC3 = false;                    // BC3_UNORM (DXT5)
    bool bFormatBC5 = false;                    // BC5_UNORM (normal maps)

    // ========================================================================
    // Methods
    // ========================================================================

    // Update capabilities from physical device (called during initialization)
    void Update(VkPhysicalDevice device, const VkPhysicalDeviceProperties& props,
                const VkPhysicalDeviceFeatures& features,
                const VkPhysicalDeviceVulkan13Features& features13);

    // Check minimum requirements
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

    // Print full capabilities report
    void LogInfo() const
    {
        Msg("=================================================================");
        Msg("[Vulkan] GPU Capabilities Report");
        Msg("=================================================================");

        // Device Info
        Msg("[Vulkan] Device: %s", deviceName);
        Msg("[Vulkan] Vendor: 0x%X (%s)", vendorID, GetVendorName(vendorID));
        Msg("[Vulkan] API Version: %d.%d.%d",
            VK_API_VERSION_MAJOR(apiVersion),
            VK_API_VERSION_MINOR(apiVersion),
            VK_API_VERSION_PATCH(apiVersion));
        Msg("[Vulkan] Driver Version: %d", driverVersion);
        Msg("[Vulkan] Total VRAM: %llu MB", totalDeviceMemory / (1024 * 1024));

        // Multi-GPU
        if (iGPUNum > 1) {
            Msg("[Vulkan] Multi-GPU: %d-Way (%s)",
                iGPUNum, bDeviceGroup ? "Device Group" : "Independent");
        }

        // Geometry Caps
        Msg("[Vulkan] Geometry Capabilities:");
        Msg("  - Max Vertex Attributes: %u", geometry.maxVertexInputAttributes);
        Msg("  - Max Clip Planes: %u", geometry.maxClipPlanes);
        Msg("  - Vertex Texture Fetch: %s", geometry.bVTF ? "YES" : "NO");
        Msg("  - Geometry Shader: %s", geometry.bGeometryShader ? "YES" : "NO");
        Msg("  - Tessellation: %s", geometry.bTessellation ? "YES" : "NO");

        // Raster Caps
        Msg("[Vulkan] Raster Capabilities:");
        Msg("  - Max Fragment Outputs (MRT): %u", raster.maxFragmentOutputAttachments);
        Msg("  - Max Texture Stages: %u", raster.maxDescriptorSetSamplers);
        Msg("  - Non-Power-of-2 Textures: %s", raster.bNonPow2Textures ? "YES" : "NO");
        Msg("  - Cubemaps: %s", raster.bCubemaps ? "YES" : "NO");
        Msg("  - Texture Compression BC: %s", raster.bTextureCompression_BC ? "YES" : "NO");
        Msg("  - Texture Compression ETC2: %s", raster.bTextureCompression_ETC2 ? "YES" : "NO");

        // MSAA
        Msg("[Vulkan] Anti-Aliasing:");
        Msg("  - Max MSAA Samples: %ux", maxMSAASamples);
        Msg("  - MSAA 2x: %s", bMSAA_2x ? "YES" : "NO");
        Msg("  - MSAA 4x: %s", bMSAA_4x ? "YES" : "NO");
        Msg("  - MSAA 8x: %s", bMSAA_8x ? "YES" : "NO");

        // Depth/Stencil
        Msg("[Vulkan] Depth/Stencil:");
        Msg("  - Stencil Buffer: %s", bStencil ? "YES" : "NO");
        Msg("  - Depth Bounds Test: %s", bDepthBoundsTest ? "YES" : "NO");
        Msg("  - Max Stencil Value: %u", dwMaxStencilValue);

        // Advanced Features
        Msg("[Vulkan] Advanced Features:");
        Msg("  - Conservative Rasterization: %s", bConservativeRasterization ? "YES" : "NO");
        Msg("  - Variable Rate Shading: %s", bVariableRateShading ? "YES" : "NO");
        Msg("  - Ray Tracing: %s", bRayTracing ? "YES" : "NO");
        Msg("  - Mesh Shaders: %s", bMeshShader ? "YES" : "NO");

        // Limits
        Msg("[Vulkan] Resource Limits:");
        Msg("  - Max Texture Size: %u x %u", maxTextureSize, maxTextureSize);
        Msg("  - Max Cubemap Size: %u", maxCubemapSize);
        Msg("  - Max Anisotropy: %.1f", maxSamplerAnisotropy);
        Msg("  - Max Viewports: %u", maxViewports);

        Msg("=================================================================");
    }

private:
    // Helper to get vendor name from ID
    const char* GetVendorName(u32 id) const
    {
        switch (id) {
        case 0x10DE: return "NVIDIA";
        case 0x1002: return "AMD";
        case 0x8086: return "Intel";
        case 0x13B5: return "ARM";
        case 0x5143: return "Qualcomm";
        case 0x1010: return "ImgTec";
        default: return "Unknown";
        }
    }
};
