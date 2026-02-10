// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// Licensed under the same terms as X-Ray Engine (see root License.txt)

#include "stdafx.h"
#include "HWCaps_Vulkan.h"

// ============================================================================
// VulkanCaps::Update() - Fill capabilities from physical device
// ============================================================================

void VulkanCaps::Update(VkPhysicalDevice device,
                        const VkPhysicalDeviceProperties& props,
                        const VkPhysicalDeviceFeatures& features,
                        const VkPhysicalDeviceVulkan13Features& features13)
{
    // ========================================================================
    // Basic info (already filled by VK_SelectPhysicalDevice, but update here)
    // ========================================================================
    xr_strcpy(deviceName, props.deviceName);
    vendorID = props.vendorID;
    deviceID = props.deviceID;
    driverVersion = props.driverVersion;
    apiVersion = props.apiVersion;

    // ========================================================================
    // Geometry Capabilities
    // ========================================================================
    geometry.maxVertexInputAttributes = props.limits.maxVertexInputAttributes;
    geometry.maxVertexInputBindings = props.limits.maxVertexInputBindings;
    geometry.maxVertexOutputComponents = props.limits.maxVertexOutputComponents;
    geometry.maxClipPlanes = _min(props.limits.maxClipDistances, 8u);
    geometry.bVTF = features.vertexPipelineStoresAndAtomics;  // Vertex texture fetch
    geometry.bPointSprites = true;  // Always supported in Vulkan
    geometry.bGeometryShader = features.geometryShader;
    geometry.bTessellation = features.tessellationShader;

    // ========================================================================
    // Raster Capabilities
    // ========================================================================
    raster.maxFragmentInputComponents = props.limits.maxFragmentInputComponents;
    raster.maxFragmentOutputAttachments = props.limits.maxFragmentOutputAttachments;
    raster.maxFragmentCombinedOutputResources = props.limits.maxFragmentCombinedOutputResources;
    raster.maxDescriptorSetSamplers = props.limits.maxDescriptorSetSamplers;
    raster.bMRT_mixdepth = true;  // Vulkan supports MRT with different formats
    raster.bNonPow2Textures = true;  // Always supported in Vulkan
    raster.bCubemaps = true;  // Always supported in Vulkan

    // Texture compression support
    raster.bTextureCompression_BC = features.textureCompressionBC;
    raster.bTextureCompression_ETC2 = features.textureCompressionETC2;
    raster.bTextureCompression_ASTC = features.textureCompressionASTC_LDR;

    // ========================================================================
    // Multi-GPU Detection
    // ========================================================================
    // Query device groups for SLI/CrossFire detection
    // For now, assume single GPU (can be extended later with VK_KHR_device_group)
    iGPUNum = 1;
    bDeviceGroup = false;

    // ========================================================================
    // MSAA Support
    // ========================================================================
    VkSampleCountFlags sampleCounts = props.limits.framebufferColorSampleCounts &
                                      props.limits.framebufferDepthSampleCounts;
    msaaSamples = sampleCounts;

    bMSAA_2x = (sampleCounts & VK_SAMPLE_COUNT_2_BIT) != 0;
    bMSAA_4x = (sampleCounts & VK_SAMPLE_COUNT_4_BIT) != 0;
    bMSAA_8x = (sampleCounts & VK_SAMPLE_COUNT_8_BIT) != 0;

    // Calculate max MSAA samples
    if (sampleCounts & VK_SAMPLE_COUNT_64_BIT) maxMSAASamples = 64;
    else if (sampleCounts & VK_SAMPLE_COUNT_32_BIT) maxMSAASamples = 32;
    else if (sampleCounts & VK_SAMPLE_COUNT_16_BIT) maxMSAASamples = 16;
    else if (sampleCounts & VK_SAMPLE_COUNT_8_BIT) maxMSAASamples = 8;
    else if (sampleCounts & VK_SAMPLE_COUNT_4_BIT) maxMSAASamples = 4;
    else if (sampleCounts & VK_SAMPLE_COUNT_2_BIT) maxMSAASamples = 2;
    else maxMSAASamples = 1;

    // ========================================================================
    // Depth/Stencil Capabilities
    // ========================================================================
    bStencil = true;  // Vulkan always has stencil if using D24S8 or D32FS8 format
    bDepthBoundsTest = features.depthBounds;
    bDepthClamp = features.depthClamp;
    dwMaxStencilValue = 255;  // Standard 8-bit stencil

    // ========================================================================
    // Advanced Features (Extension-based)
    // ========================================================================
    // Conservative rasterization (VK_EXT_conservative_rasterization)
    bConservativeRasterization = false;  // Check extension later if needed

    // Variable rate shading (VK_KHR_fragment_shading_rate)
    bVariableRateShading = false;  // Check extension later if needed

    // Ray tracing (VK_KHR_ray_tracing_pipeline)
    bRayTracing = false;  // Check extension later if needed

    // Mesh shaders (VK_EXT_mesh_shader)
    bMeshShader = false;  // Check extension later if needed

    // ========================================================================
    // Memory and Resource Limits
    // ========================================================================
    maxColorAttachments = props.limits.maxColorAttachments;
    maxSamplerAnisotropy = props.limits.maxSamplerAnisotropy;
    maxTextureSize = _min(props.limits.maxImageDimension2D, 16384u);
    maxCubemapSize = _min(props.limits.maxImageDimensionCube, 4096u);
    maxViewports = props.limits.maxViewports;

    // VRAM is calculated separately in VK_SelectPhysicalDevice

    // ========================================================================
    // Format Support (runtime checks)
    // ========================================================================
    // Check common depth formats
    VkFormatProperties formatProps;

    vkGetPhysicalDeviceFormatProperties(device, VK_FORMAT_D24_UNORM_S8_UINT, &formatProps);
    bFormatD24S8 = (formatProps.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0;

    vkGetPhysicalDeviceFormatProperties(device, VK_FORMAT_D32_SFLOAT, &formatProps);
    bFormatD32F = (formatProps.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0;

    // Check BC compression formats
    vkGetPhysicalDeviceFormatProperties(device, VK_FORMAT_BC1_RGBA_UNORM_BLOCK, &formatProps);
    bFormatBC1 = (formatProps.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) != 0;

    vkGetPhysicalDeviceFormatProperties(device, VK_FORMAT_BC3_UNORM_BLOCK, &formatProps);
    bFormatBC3 = (formatProps.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) != 0;

    vkGetPhysicalDeviceFormatProperties(device, VK_FORMAT_BC5_UNORM_BLOCK, &formatProps);
    bFormatBC5 = (formatProps.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) != 0;

    // ========================================================================
    // Vulkan 1.3 features
    // ========================================================================
    dynamicRendering = features13.dynamicRendering;
    synchronization2 = features13.synchronization2;
    maintenance4 = features13.maintenance4;
}
