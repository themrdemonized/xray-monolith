// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// Licensed under the same terms as X-Ray Engine (see root License.txt)

#pragma once
#include "vk_core.h"
#include <unordered_map>
#include <string>

namespace VK
{

// Forward declarations
class CRenderTarget;

/**
 * CVulkanLighting - Управление lighting pipelines и descriptor sets
 *
 * Отвечает за:
 * - Создание pipelines для различных типов освещения
 * - Управление descriptor sets для G-Buffer textures
 * - Binding shaders и resources для lighting passes
 */
class CVulkanLighting
{
public:
    CVulkanLighting();
    ~CVulkanLighting();

    /**
     * Создать lighting resources
     * Вызывается после создания G-Buffer
     */
    void Create();

    /**
     * Уничтожить resources
     */
    void Destroy();

    /**
     * Setup для directional light pass (simple, без теней)
     * @param cmd Command buffer
     * @param rt Render target (для доступа к G-Buffer)
     * @param L_dir Light direction (view space)
     * @param L_color Light color (RGB) + specular (A)
     * @return true если успешно, false если нет шейдеров/pipeline
     */
    bool BindAccumDirectSimple(VkCommandBuffer cmd,
                                CRenderTarget* rt,
                                const Fvector& L_dir,
                                const Fvector& L_color,
                                float L_spec);

    /**
     * Создать descriptor set для G-Buffer textures
     * Вызывается при создании или изменении G-Buffer
     */
    void CreateGBufferDescriptorSet(CRenderTarget* rt);

private:
    /**
     * Загрузить шейдеры для accum_direct_simple
     */
    bool LoadAccumDirectSimpleShaders();

    /**
     * Создать pipeline для accum_direct_simple
     */
    bool CreateAccumDirectSimplePipeline();

    /**
     * Создать descriptor set layout для G-Buffer
     */
    void CreateGBufferDescriptorSetLayout();

private:
    /**
     * Создать sampler для G-Buffer текстур
     */
    void CreateSampler();

private:
    // === Accum Direct Simple (sun без теней) ===
    VkShaderModule m_AccumDirectSimple_VS = VK_NULL_HANDLE;
    VkShaderModule m_AccumDirectSimple_FS = VK_NULL_HANDLE;
    VkPipeline m_AccumDirectSimple_Pipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_AccumDirectSimple_PipelineLayout = VK_NULL_HANDLE;

    // === Descriptor Sets ===
    VkDescriptorSetLayout m_GBufferLayout = VK_NULL_HANDLE;  // Layout для G-Buffer textures
    VkDescriptorSet m_GBufferDescriptorSet = VK_NULL_HANDLE; // Set с G-Buffer textures
    VkDescriptorPool m_DescriptorPool = VK_NULL_HANDLE;      // Pool для lighting descriptor sets

    // === Sampler ===
    VkSampler m_GBufferSampler = VK_NULL_HANDLE;  // Linear sampler для G-Buffer

    // === Push Constants ===
    struct PushConstants {
        Fvector4 Ldynamic_dir;    // Light direction (view space) + unused w
        Fvector4 Ldynamic_color;  // RGB + specular
    };

    // ========================================================================
    // Hemisphere Lighting Support (Phase: Hemisphere Lighting)
    // ========================================================================

    // UBO structure (matches shader layout in common_functions.h)
    struct GlobalLightingUBO {
        // ========== EXISTING (offset 0-127) ==========
        Fvector4 L_hemi_color;   // 0   - Hemisphere sky color (RGB) + intensity (A)
        Fvector4 L_ambient;      // 16  - Flat ambient color (RGB) + unused (A)
        Fvector4 L_sun_color;    // 32  - Sun color (RGB) + unused (A)
        Fvector4 L_sun_dir_w;    // 48  - Sun direction world-space (XYZ) + unused (W)
        Fmatrix  m_invV;         // 64  - Inverse view matrix (eye-space -> world-space) [64 bytes]

        // ========== CRITICAL (offset 128-255) ==========
        Fvector4 L_sun_dir_e;    // 128 - Sun direction (eye space)
        Fvector4 eye_position;   // 144 - Camera world position
        Fvector4 eye_direction;  // 160 - Camera direction
        Fvector4 eye_normal;     // 176 - Camera up vector

        Fvector4 fog_plane;      // 192 - Fog plane equation
        Fvector4 fog_params;     // 208 - Fog near/far/range
        Fvector4 fog_color;      // 224 - Fog color + density

        Fvector4 timers;         // 240 - (t, t*10, t/10, sin(t))

        // ========== IMPORTANT (offset 256-383) ==========
        Fvector4 screen_res;        // 256 - (w, h, 1/w, 1/h)
        Fvector4 ogse_c_screen;     // 272 - (fov, aspect, tan(fov/2), far_plane)
        Fvector4 near_far_plane;    // 288 - (near, far, unused, unused)

        Fvector4 wind_params;       // 304 - (dir_x, dir_y, velocity, unused)
        Fvector4 rain_params;       // 320 - (density, wetness, unused, unused)
        Fvector4 sky_color;         // 336 - (r, g, b, rotation)

        Fvector4 timers_game;       // 352 - Game time (t, t/day, t/24h, hour)
        Fvector4 actor_data;        // 368 - (health, stamina, bleeding, helmet)

        // ========== SSFX (offset 384-511) ==========
        Fvector4 ssfx_floravariation; // 384
        Fvector4 ssfx_fog;            // 400
        Fvector4 ssfx_motionblur;     // 416
        Fvector4 ssfx_wind_grass;     // 432
        Fvector4 ssfx_wind_trees;     // 448
        Fvector4 ssfx_wind_anim;      // 464
        Fvector4 ssfx_lut;            // 480
        Fvector4 ssfx_shadow_bias;    // 496

        // ========== POST-PROCESSING (offset 512-767) ==========
        Fvector4 ssfx_bloom_1;        // 512 - Bloom threshold, exposure, sky intensity
        Fvector4 ssfx_bloom_2;        // 528 - Bloom blur, vibrance, lens, dirt
        Fvector4 ssfx_ao;             // 544 - AO resolution, intensity, blur, radius
        Fvector4 ssfx_ao_setup1;      // 560 - AO distance, HUD, flora, max occlusion
        Fvector4 ssfx_il;             // 576 - Indirect lighting params
        Fvector4 ssfx_il_setup1;      // 592 - IL distance, HUD, flora
        Fvector4 ssfx_ssr;            // 608 - SSR resolution, blur, temporal, noise
        Fvector4 ssfx_ssr_2;          // 624 - SSR quality, fade, intensity
        Fvector4 ssfx_volumetric;     // 640 - Volumetric lighting
        Fvector4 ssfx_terrain_offset; // 656 - Terrain geometry offset
        Fvector4 ssfx_florafixes_1;   // 672 - Flora adjustment 1
        Fvector4 ssfx_florafixes_2;   // 688 - Flora adjustment 2
        Fvector4 ssfx_wetsurfaces_1;  // 704 - Wet surface params 1
        Fvector4 ssfx_wetsurfaces_2;  // 720 - Wet surface params 2
        Fvector4 ssfx_gloss;          // 736 - Gloss (minmax.x, minmax.y, factor, 0)
        Fvector4 ssfx_lightsetup_1;   // 752 - Light setup (specular intensity)

        // ========== HUD/SCOPE/PDA (offset 768-1023) ==========
        Fvector4 m_hud_params;        // 768 - HUD rendering parameters
        Fvector4 m_hud_fov_params;    // 784 - HUD FOV parameters
        Fvector4 m_script_params;     // 800 - Script-controlled parameters
        Fvector4 m_blender_mode;      // 816 - Blending mode
        Fvector4 pda_params;          // 832 - PDA (factor, psy, brightness, 0)
        Fvector4 fakescope_params1;   // 848 - Scope (scroll, inner blur, outer blur, brightness)
        Fvector4 fakescope_params2;   // 864 - Scope (CA, fog attack, fog mattack, fog travel)
        Fvector4 fakescope_params3;   // 880 - Scope (radius, fog radius, fog sharpness, 0)
        Fvector4 ssfx_hud_drops_1;    // 896 - HUD water drops 1
        Fvector4 ssfx_hud_drops_2;    // 912 - HUD water drops 2
        Fvector4 ssfx_blood_decals;   // 928 - Blood decals
        Fvector4 ssfx_wpn_dof_1;      // 944 - Weapon DOF parameters
        Fvector4 ssfx_wpn_dof_2;      // 960 - Weapon DOF (x=value, yzw unused)
        Fvector4 ssfx_hud_hemi;       // 976 - HUD hemisphere offset
        Fvector4 ssfx_issvp;          // 992 - Is second viewport frame
        Fvector4 ssfx_fTimeDelta;     // 1008 - Frame time delta

        // ========== HEAT VISION/EFFECTS (offset 1024-1279) ==========
        Fvector4 heatvision_params1;  // 1024 - (ps_r2_heatvision, steps.x, steps.y, steps.z)
        Fvector4 heatvision_params2;  // 1040 - (blurring.x, blurring.y, blurring.z, mode)
        Fvector4 heatvision_args1;    // 1056 - Heat vision args 1
        Fvector4 heatvision_args2;    // 1072 - Heat vision args 2
        Fvector4 silencer_glowing;    // 1088 - Silencer overheat (max, shot, cool_rate, color.r)
        Fvector4 silencer_glow_color; // 1104 - Silencer color (g, b, 0, 0)
        Fvector4 vignette_control;    // 1120 - Vignette effect
        Fvector4 pp_img_corrections;  // 1136 - (exposure, gamma, saturation, 1)
        Fvector4 pp_img_cg;           // 1152 - Color grading (r, g, b, 1)
        Fvector4 markswitch_params;   // 1168 - (current, count, unused, unused)
        Fvector4 markswitch_color;    // 1184 - Mark color RGBA
        Fvector4 ssfx_jitter;         // 1200 - TAA jitter
        Fvector4 ssfx_taa;            // 1216 - TAA parameters
        Fvector4 ssfx_rain_1;         // 1232 - Rain parameters 1
        Fvector4 ssfx_rain_2;         // 1248 - Rain parameters 2
        Fvector4 ssfx_rain_3;         // 1264 - Rain parameters 3

        // ========== DEBUG/DEV PARAMETERS (offset 1280-1535) ==========
        Fvector4 dev_param_1;         // 1280
        Fvector4 dev_param_2;         // 1296
        Fvector4 dev_param_3;         // 1312
        Fvector4 dev_param_4;         // 1328
        Fvector4 dev_param_5;         // 1344
        Fvector4 dev_param_6;         // 1360
        Fvector4 dev_param_7;         // 1376
        Fvector4 dev_param_8;         // 1392
        Fvector4 s3ds_param_1;        // 1408 - 3D scope params
        Fvector4 s3ds_param_2;        // 1424
        Fvector4 s3ds_param_3;        // 1440
        Fvector4 s3ds_param_4;        // 1456
        Fvector4 ssfx_grass_interactive; // 1472 - Interactive grass
        Fvector4 ssfx_int_grass_params_1; // 1488
        Fvector4 ssfx_int_grass_params_2; // 1504

        // ========== MOTION VECTORS (offset 1520-1647) ==========
        Fmatrix m_prevVP;        // 1520 - Previous frame ViewProjection matrix (64 bytes)
        Fmatrix m_View;          // 1584 - Current frame View matrix (moved from push constants)

        // TOTAL: 1648 bytes
    };

    // Vulkan resources for GlobalLighting UBO
    VkBuffer m_GlobalLightingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_GlobalLightingMemory = VK_NULL_HANDLE;
    void* m_GlobalLightingMapped = nullptr;

    // Descriptor set for GlobalLighting (Set 0)
    VkDescriptorSetLayout m_GlobalLightingLayout = VK_NULL_HANDLE;
    VkDescriptorSet m_GlobalLightingDescriptorSet = VK_NULL_HANDLE;

    // Methods
    void CreateGlobalLightingUBO();
    void DestroyGlobalLightingUBO();
public:
    void UpdateGlobalLightingUBO();
private:

    // Temporary storage for inverse view matrix
    Fmatrix m_invV;

    // ========================================================================
    // Material Constants UBO (Set 4, binding 0) - 256 bytes
    // ========================================================================
    struct MaterialConstantsUBO {
        // ========== MATERIAL (offset 0-159) ==========
        Fvector4 L_material;           // 0   - (hemi_factor, sun_factor, unused, unused)
        Fvector4 detail_params;        // 16  - (scale, scale, scale, 1/range)
        Fvector4 parallax_params;      // 32  - ssfx_pom (min height, offset, rays, quality)
        Fvector4 terrain_pom_params;   // 48  - ssfx_terrain_pom
        Fvector4 water_params;         // 64  - ssfx_water (normals, reflection, transparency, waves)
        Fvector4 water_setup1;         // 80  - ssfx_water_setup1
        Fvector4 water_setup2;         // 96  - ssfx_water_setup2
        Fvector4 hemi_cube_pos_faces;  // 112 - (px, py, pz, unused)
        Fvector4 hemi_cube_neg_faces;  // 128 - (nx, ny, nz, unused)
        Fvector4 m_texgen_params;      // 144 - texgen helper

        // ========== HDR10 PARAMETERS (offset 160-767) ==========
        // Color space and tonemapping
        Fvector4 hdr10_params1;        // 160 - (whitepoint_nits, ui_nits, pda_intensity, pda_on)
        Fvector4 hdr10_params2;        // 176 - (hdr10_on, colorspace, tonemapper, tonemap_mode)
        Fvector4 hdr10_tonemap1;       // 192 - (exposure, contrast, contrast_middle_gray, saturation)
        Fvector4 hdr10_tonemap2;       // 208 - (brightness, gamma, ui_saturation, 0)

        // Bloom
        Fvector4 hdr10_bloom1;         // 224 - (bloom_on, blur_passes, blur_scale, intensity)

        // Lens flare
        Fvector4 hdr10_flare1;         // 240 - (flare_on, threshold, power, ghosts)
        Fvector4 hdr10_flare2;         // 256 - (ghost_dispersal, center_falloff, halo_scale, halo_ca)
        Fvector4 hdr10_flare3;         // 272 - (ghost_ca, blur_passes, blur_scale, ghost_intensity)
        Fvector4 hdr10_flare4;         // 288 - (halo_intensity, lens_color.rgb)

        // Sun glow
        Fvector4 hdr10_sun1;           // 304 - (sun_on, intensity, inner_radius, outer_radius)
        Fvector4 hdr10_sun2;           // 320 - (dawn_begin, dawn_end, dusk_begin, dusk_end)

        // ========== ADDITIONAL SSFX (offset 336-511) ==========
        Fvector4 ssfx_rain_drops;      // 336 - Rain drops setup
        Fvector4 ssfx_shadow_cascades; // 352 - Shadow cascade distances (x, y, z, 0)
        Fvector4 ssfx_grass_shadows;   // 368 - Grass shadow parameters
        Fvector4 ssfx_terrain_quality; // 384 - Terrain quality settings
        Fvector4 ssfx_water_quality;   // 400 - Water quality (x, y, z, 0)
        Fvector4 ssfx_sss_quality;     // 416 - Subsurface scattering quality
        Fvector4 ssfx_sss;             // 432 - SSS parameters
        Fvector4 ssfx_is_underground;  // 448 - Underground flag (x, 0, 0, 0)
        Fvector4 ssfx_wind_anim_prev;  // 464 - Previous wind animation
        Fvector4 ssfx_fog_scattering;  // 480 - Fog scattering (x, 0, 0, 0)
        Fvector4 reserved_mat[1];      // 496-511 - Reserved

        // TOTAL: 512 bytes (32 × vec4)
    };

    // Material Constants UBO resources
    VkBuffer m_MaterialConstantsBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_MaterialConstantsMemory = VK_NULL_HANDLE;
    void* m_MaterialConstantsMapped = nullptr;
    VkDescriptorSetLayout m_MaterialConstantsLayout = VK_NULL_HANDLE;
    VkDescriptorSet m_MaterialConstantsDescriptorSet = VK_NULL_HANDLE;

    // Methods
    void CreateMaterialConstantsUBO();
    void UpdateMaterialConstantsUBO();
    void DestroyMaterialConstantsUBO();

    // ========================================================================
    // Constant Mapping System
    // ========================================================================
    struct ConstantMapping {
        u32 uboIndex;   // 0 = GlobalLighting, 1 = MaterialConstants
        u32 offset;     // Offset in bytes
    };
    std::unordered_map<std::string, ConstantMapping> m_ConstantMap;

    void BuildConstantMap();

public:
    // Set constant API (called from RCache.set_c)
    void SetConstant(LPCSTR name, float x, float y, float z, float w);
    void SetConstant(LPCSTR name, const Fmatrix& M);
    void SetConstant(LPCSTR name, const Fvector4& V);

    // Descriptor set accessors
    VkDescriptorSetLayout GetGlobalLightingLayout() const { return m_GlobalLightingLayout; }
    VkDescriptorSetLayout GetMaterialConstantsLayout() const { return m_MaterialConstantsLayout; }
    VkDescriptorSet GetGlobalLightingDescriptorSet() const { return m_GlobalLightingDescriptorSet; }
    VkDescriptorSet GetMaterialConstantsDescriptorSet() const { return m_MaterialConstantsDescriptorSet; }

private:
    bool m_bCreated = false;
    bool m_bShadersLoaded = false;
    bool m_bPipelineCreated = false;
    bool m_bDescriptorSetUpdated = false;
};

// Глобальный экземпляр
extern CVulkanLighting* g_VulkanLighting;

} // namespace VK
