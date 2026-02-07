// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// SPDX-License-Identifier: MIT

#pragma once
#include "SH_RT_Vulkan.h"
#include "vk_texture.h"
#include "vk_buffer.h"
#include "../../xrEngine/Render.h"

// === ANOMALY LUA WEATHER SYSTEM ===
// Anomaly sets weather params via Lua: weather.set_value_*(name, ...) -> CurrentEnv
// INI configs may have empty/zero values for clouds, colors etc.
// The engine lerps Current[0] + Current[1] into CurrentEnv every frame.
// Lua can overwrite any field in CurrentEnv at any time.
// Renderers should read runtime params from CurrentEnv (the final blended result).
//
// R4 (DX11) architecture for reference:
//   dxEnvDescriptorRender    — loads sky/clouds textures per weather descriptor
//   dxEnvDescriptorMixerRender — lerp() assembles texture pairs (slot0=A, slot1=B)
//   dxEnvironmentRender      — RenderSky()/RenderClouds() bind mixer textures
//   GPU shader               — blends two textures using CurrentEnv->weight
//
// Vulkan consumers per file:
//   vk_lighting.cpp                      — sky_color, sun_color, sun_dir, hemi_color, ambient, rain_density, wind
//   vk_rendertarget_phase_sky.cpp        — sky_texture_name, sky_color, weight
//   vk_rendertarget_phase_clouds.cpp     — clouds_texture_name, clouds_color, wind_direction, weight
//   vk_rendertarget_phase_combine.cpp    — sky_color, ambient
//   vk_DetailManager*.cpp                — wind_direction, wind_strength_factor
//   rvk_sun.cpp                          — far_plane
//   vk_sector.cpp                        — ambient

// Forward declarations
class light;

// Sun cascade sub-phases (map to cascade indices)
#define SE_SUN_NEAR     0  // Near cascade (0..20m)
#define SE_SUN_MIDDLE   1  // Middle cascade (20..40m)
#define SE_SUN_FAR      2  // Far cascade (40..160m)

namespace VK
{

// Forward declaration
class CRenderTarget;

} // namespace VK

// Глобальный экземпляр
extern VK::CRenderTarget* RTarget;

namespace VK
{

// G-Buffer render target manager
class CRenderTarget : public IRender_Target
{
public:
    // G-Buffer render targets
    CRT rt_Position;      // R32G32B32A32_SFLOAT - Eye-space position
    CRT rt_Normal;        // R32G32B32A32_SFLOAT - Eye-space normal + hemi
    CRT rt_Color;         // R8G8B8A8_SRGB       - Albedo (sRGB for PBR)
    CRT rt_Material;      // R8G8B8A8_UNORM      - PBR: Metallic/Roughness/SSS/AO
    CRT rt_Accumulator;   // R16G16B16A16_SFLOAT - Accumulated light

    // Depth (используем swapchain depth, но предоставляем rt_ZBuffer для совместимости)
    CRT rt_ZBuffer;       // Alias for swapchain depth buffer

    // Generic render targets (для post-process)
    CRT rt_Generic_0;
    CRT rt_Generic_1;

    // Distortion map (R8G8B8A8_UNORM - R/B encode UV offset, 127=neutral)
    CRT rt_Distortion;

    // Water SSR render targets
    CRT rt_ssfx_temp;         // R16G16B16A16_SFLOAT - Temporary for water SSR rendering
    CRT rt_ssfx_temp2;        // R16G16B16A16_SFLOAT - Temporary for blur pass
    CRT rt_ssfx_water;        // R16G16B16A16_SFLOAT - Water SSR result
    CRT rt_ssfx_water_waves;  // R8G8B8A8_UNORM, 512x512 - Wave simulation

    // Shadow maps
    CRT rt_smap_depth;        // 2048x2048 - Directional light shadow map (D32_SFLOAT)
    CRT rt_smap_depth_minmax; // 512x512   - MinMax optimization (R32G32_SFLOAT)
    CRT rt_smap_cube;         // 512x512x6 - Point light shadow cubemap (D32_SFLOAT, 6 faces)

    // TODO: Spot light shadow map может использовать тот же rt_smap_depth

    // Размеры
    u32 m_Width  = 0;
    u32 m_Height = 0;
    u32 m_ShadowMapSize = 2048;  // Shadow map resolution

public:
    CRenderTarget();
    ~CRenderTarget();

    // Lifecycle
    void Create(u32 width, u32 height);
    void Destroy();
    void OnResize(u32 width, u32 height);

    // Invalidate cached descriptor sets (call after descriptor pool reset)
    void InvalidateDescriptorSets()
    {
        m_GBufferDescSet = VK_NULL_HANDLE;
        m_SunDescSet     = VK_NULL_HANDLE;
        m_PointDescSet   = VK_NULL_HANDLE;
        m_SpotDescSet    = VK_NULL_HANDLE;
        m_SkyDescSet     = VK_NULL_HANDLE;
        m_CloudDescSet   = VK_NULL_HANDLE;
    }

    // Shadow maps
    void CreateShadowMaps(u32 size);

    // Render phases
    void phase_scene_begin();
    void phase_scene_end();
    void phase_gbuffer();      // Phase 2.21: G-Buffer pass (geometry rendering)
    void phase_accumulator();
    void phase_combine();
    void phase_sky();          // Sky cubemap rendering (after combine, before forward)
    void phase_clouds();       // Cloud hemisphere rendering (after sky, before forward)
    void phase_forward();      // Phase 2.19: Forward pass (transparent objects)
    void phase_postprocess();  // Phase 2.20: Post-processing (bloom, vignette, etc.)
    void phase_distortion();   // Distortion map rendering (for magnifier effect)

    // Water rendering phases
    void phase_water_ssr();    // Water SSR pre-pass (reduced resolution)
    void phase_water_blur();   // SSR blur post-process
    void phase_water_waves();  // Wave simulation
    void phase_water();        // Final water rendering

    // Shadow map rendering
    void phase_smap_direct(light* sun, u32 sub_phase);
    void phase_smap_point(light* L);
    void phase_smap_spot(light* L);

    // Light accumulation
    void accum_direct(u32 sub_phase);
    void accum_direct_simple();     // Simplified version without shadows (for testing)
    void accum_direct_cascades(u32 sub_phase);  // Full version with cascade shadows
    void accum_point(light* L);
    void accum_spot(light* L);

    // Helper methods для lighting
    void draw_volume(light* L);                    // Render sphere/cone volume
    void enable_scissor(light* L);                 // Scissor optimization
    void enable_dbt_bounds(light* L);              // Depth bounds test
    void u_stencil_optimize();                     // NV stencil recompression
    void u_DBT_disable();                          // Disable depth bounds test

    // Utility
    void u_compute_texgen_screen(Fmatrix& m);      // Screen-space texgen
    void u_compute_texgen_jitter(Fmatrix& m);      // Jitter texgen
    float u_diffuse2s(const Fvector& c);           // Compute specular from diffuse

    // Light marker (для stencil masking)
    u32 dwLightMarkerID = 1;
    void increment_light_marker() { dwLightMarkerID += 2; }  // Keep lowest bit = 1

    // Frame tracking
    u32 dwAccumulatorClearMark = 0;                // Для очистки rt_Accumulator раз за кадр

    // Pipeline helpers
    VkPipeline GetShadowPipeline();                // Get or create depth-only pipeline
    VkPipeline GetShadowCubePipeline();            // Get or create cubemap shadow pipeline
    VkPipeline GetGBufferPipeline(u32 stride = 32, u32 tcOffset = 24); // Get or create G-Buffer pipeline
    VkPipeline GetGBufferPipelineSkinned(u32 stride); // Get or create skinned G-Buffer pipeline (GPU skinning)

    // Cubemap shadow rendering (Phase 2.16)
    void render_smap_cube_face(light* L, u32 face_index, const Fmatrix& face_matrix);  // Render one cubemap face
    void setup_cubemap_matrices(light* L, Fmatrix face_matrices[6]);  // Setup 6 view matrices

    // Spot light shadow atlas management (Phase 2.17.2)
    bool AllocateShadowAtlasSlot(light* L);             // Allocate atlas slot for spot light
    void FreeShadowAtlasSlot(light* L);                 // Free atlas slot

    // Descriptor set helpers
    VkDescriptorSet CreateGBufferDescriptorSet();   // Set 1: G-Buffer textures
    VkDescriptorSet CreateSunDescriptorSet();       // Set 3: Shadow map + sun uniforms
    VkDescriptorSet CreatePointDescriptorSet();     // Set 3: Shadow cube + point uniforms
    VkDescriptorSet CreateSpotDescriptorSet();      // Set 3: Shadow map + spot uniforms (Phase 2.17.5)
    void UpdateGBufferDescriptorSet(VkDescriptorSet set);  // Update G-Buffer bindings
    void UpdateSunDescriptorSet(VkDescriptorSet set, u32 cascade_ind);  // Update sun bindings
    void UpdatePointDescriptorSet(VkDescriptorSet set, light* L);  // Update point light bindings
    void UpdateSpotDescriptorSet(VkDescriptorSet set, light* L);   // Update spot light bindings (Phase 2.17.5)

private:
    bool m_bCreated = false;

    // Cached pipelines
    VkPipeline m_ShadowPipeline = VK_NULL_HANDLE;      // Depth-only pipeline for shadow maps
    VkPipeline m_ShadowCubePipeline = VK_NULL_HANDLE;  // Cubemap shadow pipeline
    xr_map<u32, VkPipeline> m_GBufferPipelines;         // G-Buffer pipelines per vertex stride
    xr_map<u32, VkPipeline> m_GBufferPipelinesSkinned;  // Skinned G-Buffer pipelines per stride

    // Water pipelines
    VkPipeline m_WaterSSRPipeline = VK_NULL_HANDLE;   // Water SSR pre-pass
    VkPipeline m_WaterPipeline = VK_NULL_HANDLE;      // Final water rendering
    VkPipeline m_WaterBlurPipeline = VK_NULL_HANDLE;  // SSR blur
    VkPipeline m_WaterWavesPipeline = VK_NULL_HANDLE; // Wave simulation

    // Cached descriptor sets
    VkDescriptorSet m_GBufferDescSet = VK_NULL_HANDLE;  // G-Buffer textures (Set 1)
    VkDescriptorSet m_SunDescSet = VK_NULL_HANDLE;      // Shadow map + sun data (Set 3)
    VkDescriptorSet m_PointDescSet = VK_NULL_HANDLE;    // Shadow cube + point data (Set 3)
    VkDescriptorSet m_SpotDescSet = VK_NULL_HANDLE;     // Shadow map + spot data (Set 3) - Phase 2.17.5

    // Sky rendering resources
    CVulkanTexture* m_FallbackSky = nullptr;              // 1x1x6 solid-color cubemap (created once)
    CVulkanBuffer  m_SkyVB;                               // Half-box vertex buffer (12 verts)
    CVulkanBuffer  m_SkyIB;                               // Half-box index buffer (60 indices)
    VkDescriptorSet m_SkyDescSet = VK_NULL_HANDLE;       // Sky cubemap descriptor set
    bool m_bSkyGeometryCreated = false;

    // Cloud rendering resources
    CVulkanTexture* m_FallbackCloud = nullptr;            // 1x1 white texture (created once)
    CVulkanBuffer  m_CloudVB;                              // Hemisphere vertex buffer (91 verts, host-visible)
    CVulkanBuffer  m_CloudIB;                              // Hemisphere index buffer (480 indices)
    VkDescriptorSet m_CloudDescSet = VK_NULL_HANDLE;      // Cloud texture descriptor set
    bool m_bCloudGeometryCreated = false;

    // Point light volume geometry (Phase 2.16.4)
    VkBuffer m_PointVolumeVB = VK_NULL_HANDLE;          // Sphere vertex buffer
    VmaAllocation m_PointVolumeVBAlloc = VK_NULL_HANDLE;
    VkBuffer m_PointVolumeIB = VK_NULL_HANDLE;          // Sphere index buffer
    VmaAllocation m_PointVolumeIBAlloc = VK_NULL_HANDLE;
    u32 m_PointVolumeIndexCount = 0;                    // Index count

    // Spot light volume geometry (Phase 2.17.4)
    VkBuffer m_SpotVolumeVB = VK_NULL_HANDLE;           // Cone vertex buffer
    VmaAllocation m_SpotVolumeVBAlloc = VK_NULL_HANDLE;
    VkBuffer m_SpotVolumeIB = VK_NULL_HANDLE;           // Cone index buffer
    VmaAllocation m_SpotVolumeIBAlloc = VK_NULL_HANDLE;
    u32 m_SpotVolumeIndexCount = 0;                     // Index count

    // Spot light shadow atlas (Phase 2.17.2)
    struct ShadowAtlasSlot {
        u32 posX, posY;       // Viewport offset in rt_smap_depth
        u32 size;             // Viewport size (1024, 512, 256, etc.)
        bool occupied;        // true если slot используется
        light* owner;         // Pointer to light that owns this slot
    };
    xr_vector<ShadowAtlasSlot> m_SpotShadowAtlas;  // Shadow atlas slots

public:
    // Light volume geometry (Phase 2.16.4)
    void CreatePointVolumeGeometry();
    void DestroyPointVolumeGeometry();

    // Spot light volume geometry (Phase 2.17.4)
    void CreateSpotVolumeGeometry();
    void DestroySpotVolumeGeometry();

    // Shadow atlas initialization (Phase 2.17.2)
    void InitShadowAtlas();

    // Sky rendering helpers
    void CreateSkyGeometry();
    void DestroySkyResources();
    CVulkanTexture* CreateFallbackCubemap(float r, float g, float b);
    CVulkanTexture* GetOrCreateFallbackSky();

    // Cloud rendering helpers
    void CreateCloudGeometry();
    void DestroyCloudResources();
    CVulkanTexture* GetOrCreateFallbackCloud();

    // IRender_Target interface implementation
    void set_blur(float f) override {}
    void set_gray(float f) override {}
    void set_duality_h(float f) override {}
    void set_duality_v(float f) override {}
    void set_noise(float f) override {}
    void set_noise_scale(float f) override {}
    void set_noise_fps(float f) override {}
    void set_color_base(u32 f) override {}
    void set_color_gray(u32 f) override {}
    void set_color_add(const Fvector& f) override {}
    u32  get_width() override { return m_Width; }
    u32  get_height() override { return m_Height; }
    void set_cm_imfluence(float f) override {}
    void set_cm_interpolate(float f) override {}
    void set_cm_textures(const shared_str& tex0, const shared_str& tex1) override {}
};

} // namespace VK
