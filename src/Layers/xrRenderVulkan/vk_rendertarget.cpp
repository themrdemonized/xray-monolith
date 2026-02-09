// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// SPDX-License-Identifier: MIT

#include "stdafx.h"
#include "vk_rendertarget.h"
#include "HW_Vulkan.h"
#include "vk_swapchain.h"
#include "vk_shaders.h"

// Глобальный экземпляр
VK::CRenderTarget* RTarget = nullptr;

namespace VK
{

// Constructor
CRenderTarget::CRenderTarget()
{
    Msg("[Vulkan] CRenderTarget::CRenderTarget()");
}

// Destructor
CRenderTarget::~CRenderTarget()
{
    Msg("[Vulkan] CRenderTarget::~CRenderTarget()");
    Destroy();
}

// Создание G-Buffer
void CRenderTarget::Create(u32 width, u32 height)
{
    if (m_bCreated) {
        Msg("![Vulkan] CRenderTarget already created, call Destroy first");
        return;
    }

    if (width == 0 || height == 0) {
        Msg("![Vulkan] Invalid G-Buffer dimensions: %dx%d", width, height);
        return;
    }

    m_Width = width;
    m_Height = height;
    m_DisplayWidth = width;
    m_DisplayHeight = height;

    Msg("[Vulkan] Creating G-Buffer: %dx%d", width, height);

    // ========================================================================
    // G-Buffer Render Targets
    // ========================================================================

    // rt_Position: Eye-space position (R32G32B32A32_SFLOAT)
    Msg("[Vulkan]   Creating rt_Position...");
    rt_Position.Create(
        VK_FORMAT_R32G32B32A32_SFLOAT,
        width, height,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        false
    );

    // rt_Normal: Eye-space normal + hemi (R32G32B32A32_SFLOAT)
    Msg("[Vulkan]   Creating rt_Normal...");
    rt_Normal.Create(
        VK_FORMAT_R32G32B32A32_SFLOAT,
        width, height,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        false
    );

    // rt_Color: Albedo - sRGB для правильного PBR (R8G8B8A8_SRGB)
    Msg("[Vulkan]   Creating rt_Color...");
    rt_Color.Create(
        VK_FORMAT_R8G8B8A8_SRGB,
        width, height,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        false
    );

    // rt_Material: PBR properties - Metallic/Roughness/SSS/AO (R8G8B8A8_UNORM)
    Msg("[Vulkan]   Creating rt_Material...");
    rt_Material.Create(
        VK_FORMAT_R8G8B8A8_UNORM,
        width, height,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        false
    );

    // rt_MotionVector: Screen-space motion vectors (R16G16_SFLOAT) for DLSS/TAA
    Msg("[Vulkan]   Creating rt_MotionVector...");
    rt_MotionVector.Create(
        VK_FORMAT_R16G16_SFLOAT,
        width, height,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        false
    );

    // rt_Accumulator: Accumulated light (R16G16B16A16_SFLOAT)
    Msg("[Vulkan]   Creating rt_Accumulator...");
    rt_Accumulator.Create(
        VK_FORMAT_R16G16B16A16_SFLOAT,
        width, height,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        false
    );

    // ========================================================================
    // Generic Render Targets (для post-process эффектов)
    // ========================================================================

    Msg("[Vulkan]   Creating rt_Generic_0...");
    rt_Generic_0.Create(
        VK_FORMAT_R16G16B16A16_SFLOAT,
        width, height,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        false
    );

    Msg("[Vulkan]   Creating rt_Generic_1...");
    rt_Generic_1.Create(
        VK_FORMAT_R16G16B16A16_SFLOAT,
        width, height,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        false
    );

    // rt_HDR: HDR intermediate render target (for future DLSS)
    // All post-combine scene rendering goes here; tonemap reads it → swapchain
    Msg("[Vulkan]   Creating rt_HDR...");
    rt_HDR.Create(
        VK_FORMAT_R16G16B16A16_SFLOAT,
        width, height,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        false
    );
    Msg("[Vulkan]   rt_HDR created: %dx%d R16G16B16A16_SFLOAT", width, height);

    // rt_Distortion: Distortion map (R8G8B8A8_UNORM)
    // R/B encode UV offset (127 = neutral), A = blur amount
    // Note: TRANSFER_DST_BIT needed for vkCmdClearColorImage in phase_distortion()
    Msg("[Vulkan]   Creating rt_Distortion...");
    rt_Distortion.Create(
        VK_FORMAT_R8G8B8A8_UNORM,
        width, height,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        false
    );

    // ========================================================================
    // Water SSR Render Targets
    // ========================================================================
    Msg("[Vulkan]   Creating rt_ssfx_temp...");
    rt_ssfx_temp.Create(
        VK_FORMAT_R16G16B16A16_SFLOAT,
        width, height,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        false
    );

    Msg("[Vulkan]   Creating rt_ssfx_temp2...");
    rt_ssfx_temp2.Create(
        VK_FORMAT_R16G16B16A16_SFLOAT,
        width, height,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        false
    );

    Msg("[Vulkan]   Creating rt_ssfx_water...");
    rt_ssfx_water.Create(
        VK_FORMAT_R16G16B16A16_SFLOAT,
        width, height,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        false
    );

    Msg("[Vulkan]   Creating rt_ssfx_water_waves...");
    rt_ssfx_water_waves.Create(
        VK_FORMAT_R8G8B8A8_UNORM,
        512, 512,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        false
    );

    // ========================================================================
    // Shadow Maps
    // ========================================================================
    CreateShadowMaps(m_ShadowMapSize);

    // ========================================================================
    // Shadow Atlas для Spot Lights (Phase 2.17.1)
    // ========================================================================
    InitShadowAtlas();

    // ========================================================================
    // Descriptor Sets для Deferred Shading
    // ========================================================================
    m_GBufferDescSet = CreateGBufferDescriptorSet();
    m_SunDescSet = CreateSunDescriptorSet();

    if (m_GBufferDescSet == VK_NULL_HANDLE || m_SunDescSet == VK_NULL_HANDLE) {
        Msg("![Vulkan] Failed to create descriptor sets for deferred shading");
    } else {
        Msg("[Vulkan] Deferred shading descriptor sets created");
    }

    // ========================================================================
    // Light Volume Geometry (Phase 2.16.4)
    // ========================================================================
    CreatePointVolumeGeometry();

    // ========================================================================
    // Spot Light Cone Geometry (Phase 2.17.4)
    // ========================================================================
    CreateSpotVolumeGeometry();

    // ========================================================================
    // Auto-Exposure Compute Resources
    // ========================================================================
    CreateExposureResources();

    m_bCreated = true;

    Msg("[Vulkan] G-Buffer created successfully");
    Msg("[Vulkan]   - rt_Position:    %dx%d, R32G32B32A32_SFLOAT", width, height);
    Msg("[Vulkan]   - rt_Normal:      %dx%d, R32G32B32A32_SFLOAT", width, height);
    Msg("[Vulkan]   - rt_Color:       %dx%d, R8G8B8A8_SRGB", width, height);
    Msg("[Vulkan]   - rt_Material:    %dx%d, R8G8B8A8_UNORM (PBR)", width, height);
    Msg("[Vulkan]   - rt_MotionVector:%dx%d, R16G16_SFLOAT", width, height);
    Msg("[Vulkan]   - rt_Accumulator: %dx%d, R16G16B16A16_SFLOAT", width, height);

    // Подсчёт VRAM
    u64 vramUsage = 0;
    vramUsage += width * height * 16; // rt_Position (4 * float)
    vramUsage += width * height * 16; // rt_Normal (4 * float)
    vramUsage += width * height * 4;  // rt_Color (4 * byte)
    vramUsage += width * height * 4;  // rt_Material (4 * byte)
    vramUsage += width * height * 4;  // rt_MotionVector (2 * half)
    vramUsage += width * height * 8;  // rt_Accumulator (4 * half)
    vramUsage += width * height * 8;  // rt_Generic_0
    vramUsage += width * height * 8;  // rt_Generic_1
    vramUsage += width * height * 8;  // rt_HDR (R16G16B16A16_SFLOAT)

    float vramMB = vramUsage / (1024.0f * 1024.0f);
    Msg("[Vulkan] G-Buffer VRAM usage: %.2f MB", vramMB);
}

// Уничтожение G-Buffer
void CRenderTarget::Destroy()
{
    if (!m_bCreated) {
        return;
    }

    Msg("[Vulkan] Destroying G-Buffer...");

    // Destroy render targets
    rt_Position.Destroy();
    rt_Normal.Destroy();
    rt_Color.Destroy();
    rt_Material.Destroy();
    rt_MotionVector.Destroy();
    rt_Accumulator.Destroy();

    rt_Generic_0.Destroy();
    rt_Generic_1.Destroy();
    rt_HDR.Destroy();
    rt_DlssOutput.Destroy();
    rt_Distortion.Destroy();

    // Water SSR render targets
    rt_ssfx_temp.Destroy();
    rt_ssfx_temp2.Destroy();
    rt_ssfx_water.Destroy();
    rt_ssfx_water_waves.Destroy();

    // Destroy shadow maps
    rt_smap_depth.Destroy();
    rt_smap_depth_minmax.Destroy();
    rt_smap_cube.Destroy();

    // Destroy light volume geometry
    DestroyPointVolumeGeometry();
    DestroySpotVolumeGeometry();

    // Destroy auto-exposure resources
    DestroyExposureResources();

    // Destroy sky resources
    DestroySkyResources();

    // Destroy cloud resources
    DestroyCloudResources();

    m_bCreated = false;
    m_Width = 0;
    m_Height = 0;

    Msg("[Vulkan] G-Buffer destroyed");
}

// Пересоздание при resize
void CRenderTarget::OnResize(u32 width, u32 height)
{
    if (width == m_Width && height == m_Height) {
        // Размер не изменился
        return;
    }

    Msg("[Vulkan] G-Buffer resize: %dx%d -> %dx%d", m_Width, m_Height, width, height);

    // Пересоздаём G-Buffer
    Destroy();
    Create(width, height);
}

// ============================================================================
// Shadow Maps
// ============================================================================

void CRenderTarget::CreateShadowMaps(u32 size)
{
    Msg("[Vulkan] Creating shadow maps: %dx%d", size, size);

    // ========================================================================
    // Directional light shadow map (2048x2048, D32_SFLOAT)
    // ========================================================================
    Msg("[Vulkan]   Creating rt_smap_depth...");
    rt_smap_depth.Create(
        VK_FORMAT_D32_SFLOAT,  // 32-bit depth
        size, size,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        true  // isDepth = true
    );

    // ========================================================================
    // MinMax optimization для PCF (512x512, R32G32_SFLOAT)
    // ========================================================================
    // Используется для ускорения PCF filtering
    // Хранит min/max depth в каждом блоке 4x4
    Msg("[Vulkan]   Creating rt_smap_depth_minmax...");
    rt_smap_depth_minmax.Create(
        VK_FORMAT_R32G32_SFLOAT,  // min/max depth
        size / 4, size / 4,       // 1/4 resolution (512x512 for 2048x2048 smap)
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        false  // not a depth texture
    );

    // ========================================================================
    // Point light shadow cubemap (512x512x6, D32_SFLOAT)
    // ========================================================================
    // Phase 2.16: Omnidirectional shadows для point lights
    // 6 faces: +X, -X, +Y, -Y, +Z, -Z
    u32 cubeSize = size / 4;  // 512x512 для point lights (smaller than directional)
    Msg("[Vulkan]   Creating rt_smap_cube...");
    rt_smap_cube.CreateCube(
        VK_FORMAT_D32_SFLOAT,  // 32-bit depth
        cubeSize, cubeSize,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        true  // isDepth = true
    );

    Msg("[Vulkan] Shadow maps created successfully");
    Msg("[Vulkan]   - rt_smap_depth:        %dx%d, D32_SFLOAT", size, size);
    Msg("[Vulkan]   - rt_smap_depth_minmax: %dx%d, R32G32_SFLOAT (minmax)", size/4, size/4);
    Msg("[Vulkan]   - rt_smap_cube:         %dx%dx6, D32_SFLOAT (cubemap)", cubeSize, cubeSize);

    // VRAM usage
    u64 vramUsage = 0;
    vramUsage += size * size * 4;         // rt_smap_depth (4 bytes per pixel)
    vramUsage += (size/4) * (size/4) * 8; // rt_smap_depth_minmax (8 bytes per pixel)
    vramUsage += cubeSize * cubeSize * 6 * 4; // rt_smap_cube (6 faces * 4 bytes)

    float vramMB = vramUsage / (1024.0f * 1024.0f);
    Msg("[Vulkan] Shadow maps VRAM usage: %.2f MB", vramMB);
}

// Render phases implemented in separate files:
// phase_accumulator()  → vk_rendertarget_phase_accumulator.cpp
// phase_combine()      → vk_rendertarget_phase_combine.cpp
// phase_gbuffer()      → vk_rendertarget_phase_gbuffer.cpp
// phase_forward()      → vk_rendertarget_phase_forward.cpp
// phase_smap_point()   → vk_rendertarget_phase_smap.cpp
// phase_smap_spot()    → vk_rendertarget_phase_smap.cpp
// phase_exposure()     → vk_rendertarget_phase_exposure.cpp

// ============================================================================
// DLSS Output Render Target
// ============================================================================

void CRenderTarget::CreateDlssOutput(u32 displayW, u32 displayH)
{
    DestroyDlssOutput();
    Msg("[Vulkan] Creating rt_DlssOutput: %dx%d R16G16B16A16_SFLOAT", displayW, displayH);
    rt_DlssOutput.Create(
        VK_FORMAT_R16G16B16A16_SFLOAT,
        displayW, displayH,
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        false
    );
    m_DisplayWidth = displayW;
    m_DisplayHeight = displayH;
}

void CRenderTarget::DestroyDlssOutput()
{
    if (rt_DlssOutput.m_Image != VK_NULL_HANDLE)
    {
        rt_DlssOutput.Destroy();
        Msg("[Vulkan] rt_DlssOutput destroyed");
    }
}

// ============================================================================
// Auto-Exposure Resources
// ============================================================================

void CRenderTarget::CreateExposureResources()
{
    Msg("[Vulkan] Creating auto-exposure resources...");

    // ========================================================================
    // 1. Create 1x1 R32F exposure image (STORAGE | SAMPLED | TRANSFER_DST)
    // ========================================================================
    VkImageCreateInfo imageInfo = {};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R32_SFLOAT;
    imageInfo.extent = { 1, 1, 1 };
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

    VkResult res = vmaCreateImage(VulkanHW.m_Allocator, &imageInfo, &allocInfo,
        &m_ExposureImage, &m_ExposureAlloc, nullptr);
    if (res != VK_SUCCESS)
    {
        Msg("![Vulkan] Failed to create exposure image: %d", res);
        return;
    }

    // Image view
    VkImageViewCreateInfo viewInfo = {};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_ExposureImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R32_SFLOAT;
    viewInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    VK_CHECK(vkCreateImageView(VulkanHW.m_Device, &viewInfo, nullptr, &m_ExposureView));

    // Sampler (nearest, clamp — 1x1 image, filtering doesn't matter)
    VkSamplerCreateInfo sampInfo = {};
    sampInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampInfo.magFilter = VK_FILTER_NEAREST;
    sampInfo.minFilter = VK_FILTER_NEAREST;
    sampInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    VK_CHECK(vkCreateSampler(VulkanHW.m_Device, &sampInfo, nullptr, &m_ExposureSampler));

    // ========================================================================
    // 2. Create 256 x uint SSBO for histogram (STORAGE_BUFFER | TRANSFER_DST)
    // ========================================================================
    VkBufferCreateInfo bufInfo = {};
    bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufInfo.size = 256 * sizeof(u32);
    bufInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo bufAllocInfo = {};
    bufAllocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

    res = vmaCreateBuffer(VulkanHW.m_Allocator, &bufInfo, &bufAllocInfo,
        &m_HistogramBuffer, &m_HistogramAlloc, nullptr);
    if (res != VK_SUCCESS)
    {
        Msg("![Vulkan] Failed to create histogram buffer: %d", res);
        return;
    }

    // ========================================================================
    // 3. Initial clear: exposure image to 2.2 (default), SSBO to 0
    // ========================================================================
    VkCommandBuffer uploadCmd = VulkanHW.BeginSingleTimeCommands();
    if (uploadCmd != VK_NULL_HANDLE)
    {
        // Transition exposure image to TRANSFER_DST
        VkImageMemoryBarrier bar = {};
        bar.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        bar.srcAccessMask = 0;
        bar.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        bar.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        bar.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        bar.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bar.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bar.image = m_ExposureImage;
        bar.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        vkCmdPipelineBarrier(uploadCmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &bar);

        // Clear exposure to default 2.2
        VkClearColorValue clearColor = {};
        clearColor.float32[0] = 2.2f;
        VkImageSubresourceRange range = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        vkCmdClearColorImage(uploadCmd, m_ExposureImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            &clearColor, 1, &range);

        // Transition to GENERAL (compute writes, fragment reads via sampler — both valid in GENERAL)
        bar.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        bar.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        bar.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        bar.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        vkCmdPipelineBarrier(uploadCmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &bar);

        // Clear histogram SSBO to 0
        vkCmdFillBuffer(uploadCmd, m_HistogramBuffer, 0, 256 * sizeof(u32), 0);

        VulkanHW.EndSingleTimeCommands(uploadCmd);
    }

    // ========================================================================
    // 4. Load compute shaders
    // ========================================================================
    VkShaderModule histShader = g_ShaderManager->Load("exposure_histogram.comp.spv");
    VkShaderModule avgShader  = g_ShaderManager->Load("exposure_average.comp.spv");

    if (histShader == VK_NULL_HANDLE || avgShader == VK_NULL_HANDLE)
    {
        Msg("![Vulkan] Failed to load exposure compute shaders");
        return;
    }

    // ========================================================================
    // 5. Descriptor set layouts
    // ========================================================================

    // Histogram layout: {0: combined_image_sampler (HDR), 1: storage_buffer (histogram)}
    {
        VkDescriptorSetLayoutBinding bindings[2] = {};
        bindings[0].binding = 0;
        bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        bindings[1].binding = 1;
        bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        VkDescriptorSetLayoutCreateInfo layoutInfo = {};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = 2;
        layoutInfo.pBindings = bindings;
        VK_CHECK(vkCreateDescriptorSetLayout(VulkanHW.m_Device, &layoutInfo, nullptr, &m_ExposureHistDescLayout));
    }

    // Average layout: {0: storage_buffer (histogram), 1: storage_image (exposure)}
    {
        VkDescriptorSetLayoutBinding bindings[2] = {};
        bindings[0].binding = 0;
        bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        bindings[1].binding = 1;
        bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        VkDescriptorSetLayoutCreateInfo layoutInfo = {};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = 2;
        layoutInfo.pBindings = bindings;
        VK_CHECK(vkCreateDescriptorSetLayout(VulkanHW.m_Device, &layoutInfo, nullptr, &m_ExposureAvgDescLayout));
    }

    // ========================================================================
    // 6. Pipeline layouts with push constant ranges
    // ========================================================================

    // Histogram: 16 bytes push constants (minLogLum, logLumRange, width, height)
    {
        VkPushConstantRange pushRange = {};
        pushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        pushRange.offset = 0;
        pushRange.size = 16;  // 2 floats + 2 uints

        VkPipelineLayoutCreateInfo layoutInfo = {};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &m_ExposureHistDescLayout;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushRange;
        VK_CHECK(vkCreatePipelineLayout(VulkanHW.m_Device, &layoutInfo, nullptr, &m_ExposureHistPipeLayout));
    }

    // Average: 20 bytes push constants (minLogLum, logLumRange, totalPixels, adaptSpeed, keyValue)
    {
        VkPushConstantRange pushRange = {};
        pushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        pushRange.offset = 0;
        pushRange.size = 20;  // 2 floats + 1 uint + 2 floats

        VkPipelineLayoutCreateInfo layoutInfo = {};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &m_ExposureAvgDescLayout;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushRange;
        VK_CHECK(vkCreatePipelineLayout(VulkanHW.m_Device, &layoutInfo, nullptr, &m_ExposureAvgPipeLayout));
    }

    // ========================================================================
    // 7. Descriptor pool (1 combined_image_sampler + 2 storage_buffer + 1 storage_image, 2 sets max)
    // ========================================================================
    {
        VkDescriptorPoolSize poolSizes[3] = {};
        poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        poolSizes[0].descriptorCount = 1;
        poolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        poolSizes[1].descriptorCount = 2;
        poolSizes[2].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        poolSizes[2].descriptorCount = 1;

        VkDescriptorPoolCreateInfo poolInfo = {};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.maxSets = 2;
        poolInfo.poolSizeCount = 3;
        poolInfo.pPoolSizes = poolSizes;
        VK_CHECK(vkCreateDescriptorPool(VulkanHW.m_Device, &poolInfo, nullptr, &m_ExposureDescPool));
    }

    // ========================================================================
    // 8. Allocate + write descriptor sets
    // ========================================================================

    // Histogram descriptor set
    {
        VkDescriptorSetAllocateInfo dsAlloc = {};
        dsAlloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        dsAlloc.descriptorPool = m_ExposureDescPool;
        dsAlloc.descriptorSetCount = 1;
        dsAlloc.pSetLayouts = &m_ExposureHistDescLayout;
        VK_CHECK(vkAllocateDescriptorSets(VulkanHW.m_Device, &dsAlloc, &m_ExposureHistDescSet));

        // Write: binding 0 = HDR image, binding 1 = histogram SSBO
        VkDescriptorImageInfo hdrImageInfo = {};
        hdrImageInfo.sampler = rt_HDR.GetSampler();
        hdrImageInfo.imageView = rt_HDR.m_ImageView;
        hdrImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkDescriptorBufferInfo histBufInfo = {};
        histBufInfo.buffer = m_HistogramBuffer;
        histBufInfo.offset = 0;
        histBufInfo.range = 256 * sizeof(u32);

        VkWriteDescriptorSet writes[2] = {};
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = m_ExposureHistDescSet;
        writes[0].dstBinding = 0;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[0].descriptorCount = 1;
        writes[0].pImageInfo = &hdrImageInfo;

        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = m_ExposureHistDescSet;
        writes[1].dstBinding = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[1].descriptorCount = 1;
        writes[1].pBufferInfo = &histBufInfo;

        vkUpdateDescriptorSets(VulkanHW.m_Device, 2, writes, 0, nullptr);
    }

    // Average descriptor set
    {
        VkDescriptorSetAllocateInfo dsAlloc = {};
        dsAlloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        dsAlloc.descriptorPool = m_ExposureDescPool;
        dsAlloc.descriptorSetCount = 1;
        dsAlloc.pSetLayouts = &m_ExposureAvgDescLayout;
        VK_CHECK(vkAllocateDescriptorSets(VulkanHW.m_Device, &dsAlloc, &m_ExposureAvgDescSet));

        // Write: binding 0 = histogram SSBO, binding 1 = exposure storage image
        VkDescriptorBufferInfo histBufInfo = {};
        histBufInfo.buffer = m_HistogramBuffer;
        histBufInfo.offset = 0;
        histBufInfo.range = 256 * sizeof(u32);

        VkDescriptorImageInfo expImageInfo = {};
        expImageInfo.imageView = m_ExposureView;
        expImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        VkWriteDescriptorSet writes[2] = {};
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = m_ExposureAvgDescSet;
        writes[0].dstBinding = 0;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[0].descriptorCount = 1;
        writes[0].pBufferInfo = &histBufInfo;

        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = m_ExposureAvgDescSet;
        writes[1].dstBinding = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[1].descriptorCount = 1;
        writes[1].pImageInfo = &expImageInfo;

        vkUpdateDescriptorSets(VulkanHW.m_Device, 2, writes, 0, nullptr);
    }

    // ========================================================================
    // 9. Create compute pipelines
    // ========================================================================
    m_ExposureHistPipeline.Create(histShader, m_ExposureHistPipeLayout);
    m_ExposureAvgPipeline.Create(avgShader, m_ExposureAvgPipeLayout);

    m_bExposureReady = true;
    Msg("[Vulkan] Auto-exposure resources created (1x1 R32F + 256-bin histogram)");
}

void CRenderTarget::DestroyExposureResources()
{
    m_bExposureReady = false;

    m_ExposureHistPipeline.Destroy();
    m_ExposureAvgPipeline.Destroy();

    if (m_ExposureDescPool != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorPool(VulkanHW.m_Device, m_ExposureDescPool, nullptr);
        m_ExposureDescPool = VK_NULL_HANDLE;
        m_ExposureHistDescSet = VK_NULL_HANDLE;
        m_ExposureAvgDescSet = VK_NULL_HANDLE;
    }

    if (m_ExposureHistPipeLayout != VK_NULL_HANDLE)
    {
        vkDestroyPipelineLayout(VulkanHW.m_Device, m_ExposureHistPipeLayout, nullptr);
        m_ExposureHistPipeLayout = VK_NULL_HANDLE;
    }
    if (m_ExposureAvgPipeLayout != VK_NULL_HANDLE)
    {
        vkDestroyPipelineLayout(VulkanHW.m_Device, m_ExposureAvgPipeLayout, nullptr);
        m_ExposureAvgPipeLayout = VK_NULL_HANDLE;
    }

    if (m_ExposureHistDescLayout != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorSetLayout(VulkanHW.m_Device, m_ExposureHistDescLayout, nullptr);
        m_ExposureHistDescLayout = VK_NULL_HANDLE;
    }
    if (m_ExposureAvgDescLayout != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorSetLayout(VulkanHW.m_Device, m_ExposureAvgDescLayout, nullptr);
        m_ExposureAvgDescLayout = VK_NULL_HANDLE;
    }

    if (m_ExposureView != VK_NULL_HANDLE)
    {
        vkDestroyImageView(VulkanHW.m_Device, m_ExposureView, nullptr);
        m_ExposureView = VK_NULL_HANDLE;
    }
    if (m_ExposureSampler != VK_NULL_HANDLE)
    {
        vkDestroySampler(VulkanHW.m_Device, m_ExposureSampler, nullptr);
        m_ExposureSampler = VK_NULL_HANDLE;
    }
    if (m_ExposureImage != VK_NULL_HANDLE && m_ExposureAlloc != VK_NULL_HANDLE)
    {
        vmaDestroyImage(VulkanHW.m_Allocator, m_ExposureImage, m_ExposureAlloc);
        m_ExposureImage = VK_NULL_HANDLE;
        m_ExposureAlloc = VK_NULL_HANDLE;
    }

    if (m_HistogramBuffer != VK_NULL_HANDLE && m_HistogramAlloc != VK_NULL_HANDLE)
    {
        vmaDestroyBuffer(VulkanHW.m_Allocator, m_HistogramBuffer, m_HistogramAlloc);
        m_HistogramBuffer = VK_NULL_HANDLE;
        m_HistogramAlloc = VK_NULL_HANDLE;
    }
}

} // namespace VK
