#ifndef CONSTANTS_H_INCLUDED
#define CONSTANTS_H_INCLUDED

// ============================================================================
// Vulkan Shader Constants - Uniform Buffer Objects
// ============================================================================
//
// This file declares all UBOs for shader constant access.
// Use #include "constants.h" at the beginning of each shader.
//
// ============================================================================

// ============================================================================
// Set 0, Binding 0: GlobalLighting (512 bytes)
// ============================================================================
layout(set = 0, binding = 0) uniform GlobalLighting {
    // ========== Lighting (offset 0-127) ==========
    vec4 L_hemi_color;      // Hemisphere sky color (RGB) + intensity (A)
    vec4 L_ambient;         // Flat ambient color (RGB) + unused (A)
    vec4 L_sun_color;       // Sun color (RGB) + unused (A)
    vec4 L_sun_dir_w;       // Sun direction world-space (XYZ) + unused (W)
    mat4 m_invV;            // Inverse view matrix (eye → world)

    // ========== Critical (offset 128-255) ==========
    vec4 L_sun_dir_e_vec;   // Sun direction eye-space (XYZ) + unused (W)
    vec4 eye_position_vec;  // Camera position (XYZ) + unused (W)
    vec4 eye_direction_vec; // Camera direction (XYZ) + unused (W)
    vec4 eye_normal_vec;    // Camera up vector (XYZ) + unused (W)

    vec4 fog_plane;         // Fog plane equation
    vec4 fog_params;        // (near*range, near, far, range)
    vec4 fog_color_vec;     // Fog color (RGB) + density (A)

    vec4 timers;            // (t, t*10, t/10, sin(t))

    // ========== Important (offset 256-383) ==========
    vec4 screen_res;        // (width, height, 1/width, 1/height)
    vec4 ogse_c_screen;     // (fov, aspect, tan(fov/2), far_plane*0.75)
    vec4 near_far_plane;    // (near, far, unused, unused)

    vec4 wind_params;       // (dir_x, dir_y, velocity, unused)
    vec4 rain_params;       // (density, wetness, unused, unused)
    vec4 sky_color_vec;     // (r, g, b, rotation)

    vec4 timers_game;       // (game_time, day_frac, hour, floor_hour)
    vec4 actor_data;        // (health, stamina, bleeding, helmet)

    // ========== SSFX (offset 384-511) ==========
    vec4 ssfx_floravariation;
    vec4 ssfx_fog;
    vec4 ssfx_motionblur;
    vec4 ssfx_wind_grass;
    vec4 ssfx_wind_trees;
    vec4 ssfx_wind_anim;
    vec4 ssfx_lut;
    vec4 ssfx_shadow_bias;

    // ========== POST-PROCESSING (offset 512-767) ==========
    vec4 ssfx_bloom_1;
    vec4 ssfx_bloom_2;
    vec4 ssfx_ao;
    vec4 ssfx_ao_setup1;
    vec4 ssfx_il;
    vec4 ssfx_il_setup1;
    vec4 ssfx_ssr;
    vec4 ssfx_ssr_2;
    vec4 ssfx_volumetric;
    vec4 ssfx_terrain_offset;
    vec4 ssfx_florafixes_1;
    vec4 ssfx_florafixes_2;
    vec4 ssfx_wetsurfaces_1;
    vec4 ssfx_wetsurfaces_2;
    vec4 ssfx_gloss;
    vec4 ssfx_lightsetup_1;

    // ========== HUD/SCOPE/PDA (offset 768-1023) ==========
    vec4 m_hud_params;
    vec4 m_hud_fov_params;
    vec4 m_script_params;
    vec4 m_blender_mode;
    vec4 pda_params;
    vec4 fakescope_params1;
    vec4 fakescope_params2;
    vec4 fakescope_params3;
    vec4 ssfx_hud_drops_1;
    vec4 ssfx_hud_drops_2;
    vec4 ssfx_blood_decals;
    vec4 ssfx_wpn_dof_1;
    vec4 ssfx_wpn_dof_2;
    vec4 ssfx_hud_hemi;
    vec4 ssfx_issvp;
    vec4 ssfx_fTimeDelta;

    // ========== HEAT VISION/EFFECTS (offset 1024-1279) ==========
    vec4 heatvision_params1;
    vec4 heatvision_params2;
    vec4 heatvision_args1;
    vec4 heatvision_args2;
    vec4 silencer_glowing;
    vec4 silencer_glow_color;
    vec4 vignette_control;
    vec4 pp_img_corrections;
    vec4 pp_img_cg;
    vec4 markswitch_params;
    vec4 markswitch_color;
    vec4 ssfx_jitter;
    vec4 ssfx_taa;
    vec4 ssfx_rain_1;
    vec4 ssfx_rain_2;
    vec4 ssfx_rain_3;

    // ========== DEBUG/DEV PARAMETERS (offset 1280-1535) ==========
    vec4 dev_param_1;
    vec4 dev_param_2;
    vec4 dev_param_3;
    vec4 dev_param_4;
    vec4 dev_param_5;
    vec4 dev_param_6;
    vec4 dev_param_7;
    vec4 dev_param_8;
    vec4 s3ds_param_1;
    vec4 s3ds_param_2;
    vec4 s3ds_param_3;
    vec4 s3ds_param_4;
    vec4 ssfx_grass_interactive;
    vec4 ssfx_int_grass_params_1;
    vec4 ssfx_int_grass_params_2;

    // ========== MOTION VECTORS (offset 1520-1647) ==========
    mat4 m_prevVP;              // offset 1520: Previous frame ViewProjection matrix
    mat4 m_View;                // offset 1584: Current frame View matrix
} uGlobal;

// Convenience accessors (for compatibility with common_functions.h)
#define L_sun_dir_e (uGlobal.L_sun_dir_e_vec.xyz)
#define eye_position (uGlobal.eye_position_vec.xyz)
#define eye_direction (uGlobal.eye_direction_vec.xyz)
#define eye_normal (uGlobal.eye_normal_vec.xyz)
#define fog_color (uGlobal.fog_color_vec.xyz)
#define sky_color (uGlobal.sky_color_vec.xyz)

// ============================================================================
// Set 4, Binding 0: MaterialConstants (512 bytes)
// ============================================================================
layout(set = 4, binding 0) uniform MaterialConstants {
    // ========== MATERIAL (offset 0-159) ==========
    vec4 L_material_vec;        // (hemi_factor, sun_factor, unused, unused)
    vec4 detail_params;         // (scale, scale, scale, 1/range)
    vec4 parallax_params;       // ssfx_pom
    vec4 terrain_pom_params;    // ssfx_terrain_pom
    vec4 water_params;          // ssfx_water
    vec4 water_setup1;          // ssfx_water_setup1
    vec4 water_setup2;          // ssfx_water_setup2
    vec4 hemi_cube_pos_faces_vec;
    vec4 hemi_cube_neg_faces_vec;
    vec4 m_texgen_params;

    // ========== HDR10 PARAMETERS (offset 160-335) ==========
    vec4 hdr10_params1;         // (whitepoint_nits, ui_nits, pda_intensity, pda_on)
    vec4 hdr10_params2;         // (hdr10_on, colorspace, tonemapper, tonemap_mode)
    vec4 hdr10_tonemap1;        // (exposure, contrast, contrast_middle_gray, saturation)
    vec4 hdr10_tonemap2;        // (brightness, gamma, ui_saturation, 0)
    vec4 hdr10_bloom1;          // (bloom_on, blur_passes, blur_scale, intensity)
    vec4 hdr10_flare1;          // (flare_on, threshold, power, ghosts)
    vec4 hdr10_flare2;          // (ghost_dispersal, center_falloff, halo_scale, halo_ca)
    vec4 hdr10_flare3;          // (ghost_ca, blur_passes, blur_scale, ghost_intensity)
    vec4 hdr10_flare4;          // (halo_intensity, lens_color.rgb)
    vec4 hdr10_sun1;            // (sun_on, intensity, inner_radius, outer_radius)
    vec4 hdr10_sun2;            // (dawn_begin, dawn_end, dusk_begin, dusk_end)

    // ========== ADDITIONAL SSFX (offset 336-511) ==========
    vec4 ssfx_rain_drops;       // Rain drops setup
    vec4 ssfx_shadow_cascades;  // Shadow cascade distances
    vec4 ssfx_grass_shadows;    // Grass shadow parameters
    vec4 ssfx_terrain_quality;  // Terrain quality settings
    vec4 ssfx_water_quality;    // Water quality
    vec4 ssfx_sss_quality;      // Subsurface scattering quality
    vec4 ssfx_sss;              // SSS parameters
    vec4 ssfx_is_underground;   // Underground flag
    vec4 ssfx_wind_anim_prev;   // Previous wind animation
    vec4 ssfx_fog_scattering;   // Fog scattering
    vec4 reserved_mat[1];       // Reserved
} uMaterial;

#define L_material (uMaterial.L_material_vec.xy)
#define hemi_cube_pos_faces (uMaterial.hemi_cube_pos_faces_vec.xyz)
#define hemi_cube_neg_faces (uMaterial.hemi_cube_neg_faces_vec.xyz)

#endif // CONSTANTS_H_INCLUDED
