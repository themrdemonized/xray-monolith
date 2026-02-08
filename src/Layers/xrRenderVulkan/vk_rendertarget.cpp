// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// SPDX-License-Identifier: MIT

#include "stdafx.h"
#include "vk_rendertarget.h"
#include "HW_Vulkan.h"
#include "vk_swapchain.h"

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

    m_bCreated = true;

    Msg("[Vulkan] G-Buffer created successfully");
    Msg("[Vulkan]   - rt_Position:    %dx%d, R32G32B32A32_SFLOAT", width, height);
    Msg("[Vulkan]   - rt_Normal:      %dx%d, R32G32B32A32_SFLOAT", width, height);
    Msg("[Vulkan]   - rt_Color:       %dx%d, R8G8B8A8_SRGB", width, height);
    Msg("[Vulkan]   - rt_Material:    %dx%d, R8G8B8A8_UNORM (PBR)", width, height);
    Msg("[Vulkan]   - rt_Accumulator: %dx%d, R16G16B16A16_SFLOAT", width, height);

    // Подсчёт VRAM
    u64 vramUsage = 0;
    vramUsage += width * height * 16; // rt_Position (4 * float)
    vramUsage += width * height * 16; // rt_Normal (4 * float)
    vramUsage += width * height * 4;  // rt_Color (4 * byte)
    vramUsage += width * height * 4;  // rt_Material (4 * byte)
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
    rt_Accumulator.Destroy();

    rt_Generic_0.Destroy();
    rt_Generic_1.Destroy();
    rt_HDR.Destroy();
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

} // namespace VK
