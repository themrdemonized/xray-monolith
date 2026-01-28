// Vulkan Render Console Variables
// All render console variables required by xrGame

#include "stdafx.h"
#pragma hdrstop

#include "../xrRender/xrRender_console.h"
#include "../../xrEngine/XR_IOConsole.h"
#include "../../xrEngine/xr_ioc_cmd.h"

// VULKAN_DIAG: Static init diagnostics
static void VulkanDiagWriteConsole(const char* msg) {
	HANDLE h = CreateFileA("D:\\anomaly\\appdata\\logs\\vulkan_diag.txt",
		FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS,
		FILE_ATTRIBUTE_NORMAL, NULL);
	if (h != INVALID_HANDLE_VALUE) {
		DWORD written;
		WriteFile(h, msg, (DWORD)strlen(msg), &written, NULL);
		WriteFile(h, "\r\n", 2, &written, NULL);
		FlushFileBuffers(h);
		CloseHandle(h);
	}
}
static struct DiagConsole1 { DiagConsole1() { VulkanDiagWriteConsole("[DIAG] vk_console.cpp: static init START"); } } g_diagConsole1;

// ============================================================================
// Common render variables
// ============================================================================
u32 ps_Preset = 2;
xr_token qpreset_token[] = {
    {"Minimum", 0}, {"Low", 1}, {"Default", 2}, {"High", 3}, {"Extreme", 4}, {0, 0}
};

u32 ps_r_ssao_mode = 2;
xr_token qssao_mode_token[] = {
    {"disabled", 0}, {"default", 1}, {"hdao", 2}, {"hbao", 3}, {0, 0}
};

u32 ps_r_sun_shafts = 2;
xr_token qsun_shafts_token[] = {
    {"st_opt_low", 1}, {"st_opt_medium", 2}, {"st_opt_high", 3}, {0, 0}
};

u32 ps_sunshafts_mode = 2;
xr_token sunshafts_mode_token[] = {
    {"off", 0}, {"volumetric", 1}, {"screen_space", 2}, {"combined", 3}, {0, 0}
};

u32 ps_smaa_quality = 3;
xr_token smaa_quality_token[] = {
    {"off", 0}, {"low", 1}, {"medium", 2}, {"high", 3}, {"ultra", 4}, {0, 0}
};

u32 ps_r_ssao = 3;
xr_token qssao_token[] = {
    {"st_opt_off", 0}, {"st_opt_low", 1}, {"st_opt_medium", 2}, {"st_opt_high", 3}, {"st_opt_ultra", 4}, {0, 0}
};

u32 ps_r_sun_quality = 1;
xr_token qsun_quality_token[] = {
    {"st_opt_low", 0}, {"st_opt_medium", 1}, {"st_opt_high", 2}, {"st_opt_ultra", 3}, {"st_opt_extreme", 4}, {0, 0}
};

u32 ps_r3_msaa = 0;
xr_token qmsaa_token[] = {
    {"st_opt_off", 0}, {"2x", 1}, {"4x", 2}, {"8x", 3}, {0, 0}
};

u32 ps_r3_msaa_atest = 0;
xr_token qmsaa__atest_token[] = {
    {"st_opt_off", 0}, {"st_opt_atest_msaa_dx10_0", 1}, {"st_opt_atest_msaa_dx10_1", 2}, {0, 0}
};

u32 ps_r3_minmax_sm = 3;
xr_token qminmax_sm_token[] = {
    {"off", 0}, {"on", 1}, {"auto", 2}, {"autodetect", 3}, {0, 0}
};

u32 ps_r_screenshot_token = 0;
xr_token screenshot_mode_token[] = {
    {"jpg", 0}, {"png", 1}, {"tga", 2}, {0, 0}
};

// ============================================================================
// Common flags and settings
// ============================================================================
Flags32 ps_r__common_flags = {0};
int ps_r__LightSleepFrames = 10;

float ps_r__Detail_l_ambient = 0.9f;
float ps_r__Detail_l_aniso = 0.25f;
float ps_r__Detail_density = 0.3f;
float ps_r__Detail_height = 1.0f;
float ps_r__Detail_rainbow_hemi = 0.75f;

float ps_r__Tree_w_rot = 10.0f;
float ps_r__Tree_w_speed = 1.00f;
float ps_r__Tree_w_amp = 0.005f;
Fvector ps_r__Tree_Wave = {.1f, .01f, .11f};
float ps_r__Tree_SBC = 1.5f;

float ps_r__WallmarkTTL = 50.f;
float ps_r__WallmarkSHIFT = 0.0001f;
float ps_r__WallmarkSHIFT_V = 0.0001f;

float ps_r__GLOD_ssa_start = 256.f;
float ps_r__GLOD_ssa_end = 64.f;
float ps_r__LOD = 0.75f;
float ps_r__ssaDISCARD = 3.5f;
float ps_r__ssaDONTSORT = 32.f;
float ps_r__ssaHZBvsTEX = 96.f;

int ps_r__tf_Anisotropic = 8;
float ps_r__tf_Mipbias = 0.0f;

// ============================================================================
// R1 variables
// ============================================================================
float ps_r1_ssaLOD_A = 64.f;
float ps_r1_ssaLOD_B = 48.f;
Flags32 ps_r1_flags = {R1FLAG_DLIGHTS};
float ps_r1_lmodel_lerp = 0.1f;
float ps_r1_dlights_clip = 40.f;
float ps_r1_pps_u = 0.f;
float ps_r1_pps_v = 0.f;
int ps_r1_GlowsPerFrame = 16;
float ps_r1_fog_luminance = 1.1f;
int ps_r1_SoftwareSkinning = 0;

// ============================================================================
// R2 variables
// ============================================================================
float ps_r2_ssaLOD_A = 64.f;
float ps_r2_ssaLOD_B = 48.f;

Flags32 ps_r2_ls_flags = {
    R2FLAG_SUN | R2FLAG_EXP_DONT_TEST_UNSHADOWED | R2FLAG_USE_NVSTENCIL | R2FLAG_EXP_SPLIT_SCENE |
    R2FLAG_EXP_MT_CALC | R3FLAG_DYN_WET_SURF | R3FLAG_VOLUMETRIC_SMOKE | R2FLAG_DETAIL_BUMP |
    R2FLAG_DOF | R2FLAG_SOFT_PARTICLES | R2FLAG_SOFT_WATER | R2FLAG_STEEP_PARALLAX |
    R2FLAG_SUN_FOCUS | R2FLAG_SUN_TSM | R2FLAG_TONEMAP | R2FLAG_VOLUMETRIC_LIGHTS
};

Flags32 ps_r2_ls_flags_ext = {R2FLAGEXT_SSAO_HALF_DATA | R2FLAGEXT_ENABLE_TESSELLATION};
Flags32 ps_r2_anomaly_flags = {R2_AN_FLAG_WATER_REFLECTIONS | R2_AN_FLAG_MBLUR | R2_AN_FLAG_FLARES};

float ps_r2_df_parallax_h = 0.02f;
float ps_r2_df_parallax_range = 75.f;
float ps_r2_tonemap_middlegray = 1.f;
float ps_r2_tonemap_adaptation = 1.f;
float ps_r2_tonemap_low_lum = .4f;
float ps_r2_tonemap_amount = 0.7f;
float ps_r2_ls_bloom_kernel_g = 3.f;
float ps_r2_ls_bloom_kernel_b = .7f;
float ps_r2_ls_bloom_speed = 100.f;
float ps_r2_ls_bloom_kernel_scale = .7f;
float ps_r2_ls_dsm_kernel = .7f;
float ps_r2_ls_psm_kernel = .7f;
float ps_r2_ls_ssm_kernel = .7f;
float ps_r2_ls_bloom_threshold = 1.f;
Fvector ps_r2_aa_barier = {.8f, .1f, 0};
Fvector ps_r2_aa_weight = {.25f, .25f, 0};
float ps_r2_aa_kernel = .5f;
float ps_r2_mblur = .0f;
int ps_r2_GI_depth = 1;
int ps_r2_GI_photons = 16;
float ps_r2_GI_clip = EPS_L;
float ps_r2_GI_refl = .9f;
float ps_r2_ls_depth_scale = 1.00001f;
float ps_r2_ls_depth_bias = -0.001f;
float ps_r2_ls_squality = 1.0f;
float ps_r2_sun_tsm_projection = 0.3f;
float ps_r2_sun_tsm_bias = -0.01f;
float ps_r2_sun_near = 20.f;
float ps_r2_sun_near_border = 0.75f;
float ps_r2_sun_depth_far_scale = 1.00000f;
float ps_r2_sun_depth_far_bias = -0.00002f;
float ps_r2_sun_depth_near_scale = 1.0000f;
float ps_r2_sun_depth_near_bias = 0.00001f;
float ps_r2_sun_lumscale = 1.0f;
float ps_r2_sun_lumscale_hemi = 1.0f;
float ps_r2_sun_lumscale_amb = 1.0f;
float ps_r2_gmaterial = 2.2f;
float ps_r2_zfill = 0.25f;

float ps_r2_dhemi_sky_scale = 0.08f;
float ps_r2_dhemi_light_scale = 0.2f;
float ps_r2_dhemi_light_flow = 0.1f;
int ps_r2_dhemi_count = 5;
int ps_r2_wait_sleep = 0;
int ps_r2_qsync = 0;

float ps_r2_lt_smooth = 1.f;
float ps_r2_slight_fade = 0.5f;

Fvector4 ps_r2_mask_control = {.0f, .0f, .0f, .0f};
Fvector ps_r2_drops_control = {.0f, 1.15f, .0f};

int ps_r2_nightvision = 0;

// Silencer overheat
float sil_glow_max_temp = 0.15f;
float sil_glow_shot_temp = 0.004f;
float sil_glow_cool_temp_rate = 0.01f;
Fvector sil_glow_color = {1.f, .0f, .0f};

Fvector dsr_test = {0.f, 0.f, 0.f};
Fvector dsr_test1 = {0.f, 0.f, 0.f};
Fvector dsr_test2 = {0.f, 0.f, 0.f};

// Heat vision
int ps_r2_heatvision = 0;
float heat_vision_mode = 0.0f;
int heat_vision_cooldown = 1;
float heat_vision_cooldown_time = 20000.f;
int heat_vision_zombie_cold = 0;
Fvector4 heat_vision_steps = {0.45f, 0.65f, 0.76f, .0f};
Fvector4 heat_vision_blurring = {15.f, 4.f, 60.f, .0f};
Fvector4 heat_vision_args_1 = {.0f, .0f, .0f, .0f};
Fvector4 heat_vision_args_2 = {.0f, .0f, .0f, .0f};

int scope_fake_enabled = 1;
int scope_3D_fake_enabled = 0;

float ps_r2_ss_sunshafts_length = 1.f;
float ps_r2_ss_sunshafts_radius = 1.f;

// Tonemapping
float ps_r2_tnmp_a = .205f;
float ps_r2_tnmp_b = .35f;
float ps_r2_tnmp_c = .55f;
float ps_r2_tnmp_d = .20f;
float ps_r2_tnmp_e = .02f;
float ps_r2_tnmp_f = .15f;
float ps_r2_tnmp_w = 7.5f;
float ps_r2_tnmp_exposure = 7.0f;
float ps_r2_tnmp_gamma = .25f;
float ps_r2_tnmp_onoff = .0f;

// ============================================================================
// R4/HDR10 variables
// ============================================================================
float ps_r4_hdr10_whitepoint_nits = 400.0f;
float ps_r4_hdr10_ui_nits = 400.0f;
float ps_r4_hdr10_pda_intensity = 1.0f;
int ps_r4_hdr10_pda = 0;
int ps_r4_hdr10_on = 0;
int ps_r4_hdr10_colorspace = 2;

int ps_r4_hdr10_tonemapper = 0;
int ps_r4_hdr10_tonemap_mode = 1;
float ps_r4_hdr10_exposure = 0.8f;
float ps_r4_hdr10_contrast = 0.0f;
float ps_r4_hdr10_contrast_middle_gray = 0.5f;
float ps_r4_hdr10_saturation = 0.1f;
float ps_r4_hdr10_brightness = 0.0f;
float ps_r4_hdr10_gamma = 1.1f;
float ps_r4_hdr10_ui_saturation = 0.5f;

int ps_r4_hdr10_bloom_on = 0;
int ps_r4_hdr10_bloom_blur_passes = 20;
float ps_r4_hdr10_bloom_blur_scale = 1.0f;
float ps_r4_hdr10_bloom_intensity = 0.06f;

int ps_r4_hdr10_flare_on = 0;
float ps_r4_hdr10_flare_threshold = 0.0f;
float ps_r4_hdr10_flare_power = 0.04f;
int ps_r4_hdr10_flare_ghosts = 1;
float ps_r4_hdr10_flare_ghost_dispersal = 0.6f;
float ps_r4_hdr10_flare_center_falloff = 1.1f;
float ps_r4_hdr10_flare_halo_scale = 0.47f;
float ps_r4_hdr10_flare_halo_ca = 10.0f;
float ps_r4_hdr10_flare_ghost_ca = 3.0f;
int ps_r4_hdr10_flare_blur_passes = 12;
float ps_r4_hdr10_flare_blur_scale = 1.0f;
float ps_r4_hdr10_flare_ghost_intensity = 0.04f;
float ps_r4_hdr10_flare_halo_intensity = 0.04f;
Fvector3 ps_r4_hdr10_flare_lens_color = {1.0f, 0.7f, 1.0f};

int ps_r4_hdr10_sun_on = 0;
float ps_r4_hdr10_sun_intensity = 80.0f;
float ps_r4_hdr10_sun_inner_radius = 0.20f;
float ps_r4_hdr10_sun_outer_radius = 0.40f;
float ps_r4_hdr10_sun_dawn_begin = 4.5f;
float ps_r4_hdr10_sun_dawn_end = 6.0f;
float ps_r4_hdr10_sun_dusk_begin = 18.5f;
float ps_r4_hdr10_sun_dusk_end = 21.0f;

// Image settings
float ps_r2_img_exposure = 1.0f;
float ps_r2_img_gamma = 1.0f;
float ps_r2_img_saturation = 1.0f;
Fvector ps_r2_img_cg = {.0f, .0f, .0f};

// Debug params
Fvector4 ps_pp_bloom_thresh = {.7, .8f, .9f, .0f};
Fvector4 ps_pp_bloom_weight = {.33f, .33f, .33f, .0f};
Fvector4 ps_dev_param_1 = {.0f, .0f, .0f, .0f};
Fvector4 ps_dev_param_2 = {.0f, .0f, .0f, .0f};
Fvector4 ps_dev_param_3 = {.0f, .0f, .0f, .0f};
Fvector4 ps_dev_param_4 = {.0f, .0f, .0f, .0f};
Fvector4 ps_dev_param_5 = {.0f, .0f, .0f, .0f};
Fvector4 ps_dev_param_6 = {.0f, .0f, .0f, .0f};
Fvector4 ps_dev_param_7 = {.0f, .0f, .0f, .0f};
Fvector4 ps_dev_param_8 = {.0f, .0f, .0f, .0f};
Fvector4 ps_vignette_control = {.0f, .0f, .0f, .0f};
float ps_particle_update_coeff = 1.f;

// Mark switch
int ps_markswitch_current = 0;
int ps_markswitch_count = 0;
Fvector4 ps_markswitch_color = {0, 0, 0, 0};

// Shader 3D Scopes
Fvector4 ps_s3ds_param_1 = {0, 0, 0, 0};
Fvector4 ps_s3ds_param_2 = {0, 0, 0, 0};
Fvector4 ps_s3ds_param_3 = {0, 0, 0, 0};
Fvector4 ps_s3ds_param_4 = {0, 0, 0, 0};

float hud_fov_aim_factor = 0;

// ============================================================================
// SSFX variables
// ============================================================================
Fvector4 ps_ssfx_floravariation = {0.025, 0.1, 0.025, 0.05};
Fvector4 ps_ssfx_motionblur = {6, 0, 0, 0};
Fvector4 ps_ssfx_taa = {1, 0.5f, 0.6f, 0};
Fvector4 ps_ssfx_fog = {8, 1.3f, 0.1f, 0};
float ps_ssfx_fog_scattering = 0.6f;

int ps_ssfx_pom_refine = 0;
Fvector4 ps_ssfx_pom = {16, 12, 0.035f, 0.4f};

int ps_ssfx_terrain_grass_align = 0;
float ps_ssfx_terrain_grass_slope = 1.0f;
Fvector4 ps_ssfx_terrain_pom = {12, 20, 0.04f, 1.0f};
int ps_ssfx_terrain_pom_refine = 0;

int ps_ssfx_bloom_use_presets = 0;
Fvector4 ps_ssfx_bloom_1 = {3.5f, 3.0f, 0.0f, 0.6f};
Fvector4 ps_ssfx_bloom_2 = {3.0f, 1.5f, 1.5f, 1.0f};

Fvector4 ps_ssfx_sss_quality = {12.0f, 4.0f, 1.0f, 1.0f};
Fvector4 ps_ssfx_sss = {1.0f, 1.0f, 1.0f, 0.0f};

float ps_ssfx_hud_hemi = 0.15f;

int ps_ssfx_il_quality = 32;
Fvector4 ps_ssfx_il = {6.66f, 1.0f, 1.0f, 5.0f};
Fvector4 ps_ssfx_il_setup1 = {150.0f, 1.0f, 0.5f, 0.0f};

int ps_ssfx_ao_quality = 4;
Fvector4 ps_ssfx_ao = {1.0f, 5.0f, 1.0f, 2.5f};
Fvector4 ps_ssfx_ao_setup1 = {150.0, 1.0, 1.0, 0.0};

Fvector4 ps_ssfx_water = {1.0f, 0.8f, 1.0f, 0.0f};
Fvector3 ps_ssfx_water_quality = {1.0, 2.0, 0.0};
Fvector4 ps_ssfx_water_setup1 = {0.6f, 3.0f, 0.3f, 0.05f};
Fvector4 ps_ssfx_water_setup2 = {0.8f, 6.0f, 0.3f, 0.5f};

int ps_ssfx_ssr_quality = 0;
Fvector4 ps_ssfx_ssr = {1.0f, 0.2f, 0.0f, 0.0f};
Fvector4 ps_ssfx_ssr_2 = {0.0f, 1.3f, 1.0f, 0.015f};

Fvector4 ps_ssfx_terrain_quality = {8, 0, 0, 0};
Fvector4 ps_ssfx_terrain_offset = {0, 0, 0, 0};

Fvector3 ps_ssfx_shadows = {256, 1536, 0.0f};
Fvector4 ps_ssfx_volumetric = {1.0f, 1.0f, 3.0f, 1.0f};

Fvector3 ps_ssfx_shadow_bias = {0.4f, 0.03f, 0.0f};
Fvector4 ps_ssfx_lut = {0.0f, 0.0f, 0.0f, 0.0f};

Fvector4 ps_ssfx_wind_grass = {9.5f, 1.4f, 1.5f, 0.4f};
Fvector4 ps_ssfx_wind_trees = {11.0f, 0.15f, 0.5f, 0.15f};

Fvector4 ps_ssfx_florafixes_1 = {0.3f, 0.21f, 0.3f, 0.21f};
Fvector4 ps_ssfx_florafixes_2 = {2.0f, 1.0f, 0.0f, 0.0f};

Fvector4 ps_ssfx_wetsurfaces_1 = {1.0f, 1.0f, 1.0f, 1.0f};
Fvector4 ps_ssfx_wetsurfaces_2 = {1.0f, 1.0f, 1.0f, 1.0f};

int ps_ssfx_is_underground = 0;
int ps_ssfx_gloss_method = 0;
float ps_ssfx_gloss_factor = 0.5f;
Fvector3 ps_ssfx_gloss_minmax = {0.0f, 0.92f, 0.0f};

Fvector4 ps_ssfx_lightsetup_1 = {0.35f, 0.5f, 0.0f, 0.0f};

Fvector4 ps_ssfx_hud_drops_1 = {1.0f, 1.0f, 1.0f, 1.0f};
Fvector4 ps_ssfx_hud_drops_2 = {1.5f, 0.85f, 0.0f, 2.0f};

Fvector4 ps_ssfx_blood_decals = {0.6f, 0.6f, 0.f, 0.f};
Fvector4 ps_ssfx_rain_1 = {2.0f, 0.1f, 0.6f, 2.f};
Fvector4 ps_ssfx_rain_2 = {0.5f, 0.1f, 1.0f, 0.5f};
Fvector4 ps_ssfx_rain_3 = {0.5f, 1.0f, 0.0f, 0.0f};
Fvector4 ps_ssfx_rain_drops_setup = {2500, 15, 0.0f, 0.0f};

Fvector3 ps_ssfx_shadow_cascades = {20, 40, 160};
Fvector4 ps_ssfx_grass_shadows = {.0f, .35f, 30.0f, .0f};

Fvector4 ps_ssfx_grass_interactive = {.0f, .0f, 2000.0f, 1.0f};
Fvector4 ps_ssfx_int_grass_params_1 = {1.0f, 1.0f, 1.0f, 25.0f};
Fvector4 ps_ssfx_int_grass_params_2 = {1.0f, 5.0f, 1.0f, 1.0f};

Fvector4 ps_ssfx_wpn_dof_1 = {.0f, .0f, .0f, .0f};
float ps_ssfx_wpn_dof_2 = 1.0f;

// DOF
Fvector3 ps_r2_dof = {-1.25f, 0.f, 600.f};
float ps_r2_dof_sky = 30;
float ps_r2_dof_kernel_size = 0.25f;

// Wet surfaces
float ps_r3_dyn_wet_surf_near = 10.f;
float ps_r3_dyn_wet_surf_far = 30.f;
int ps_r3_dyn_wet_surf_sm_res = 256;

// Flags
Flags32 psDeviceFlags2 = {0};
Flags32 ps_actor_shadow_flags = {0};
Flags32 ps_common_flags = {0};

// Detail radius
u32 ps_steep_parallax = 0;
int ps_r__detail_radius = 49;

#ifdef DETAIL_RADIUS
u32 dm_size = 24;
u32 dm_cache1_line = 12;
u32 dm_cache_line = 49;
u32 dm_cache_size = 2401;
float dm_fade = 47.5;
u32 dm_current_size = 24;
u32 dm_current_cache1_line = 12;
u32 dm_current_cache_line = 49;
u32 dm_current_cache_size = 2401;
float dm_current_fade = 47.5;
#endif

float ps_current_detail_density = 0.6f;
float ps_current_detail_height = 1.0f;

xr_token ext_quality_token[] = {
    {"qt_off", 0}, {"qt_low", 1}, {"qt_medium", 2}, {"qt_high", 3}, {"qt_extreme", 4}, {0, 0}
};

// Gloss
float ps_r2_gloss_factor = 4.0f;
float ps_r2_gloss_min = 0.0f;

// Optimization
int opt_static = 2;
int opt_dynamic = 2;

// ============================================================================
// Additional symbols required by xrGame
// ============================================================================

// From SkeletonRigid.cpp
BOOL r_optimize_calculate_bones = TRUE;

// From WallmarksEngine.cpp
float wallmark_range_static = 100.f;
float wallmark_range_skeleton = 50.f;

// ============================================================================
// Console initialization (stub for Vulkan)
// ============================================================================
void xrRender_initconsole()
{
    Msg("* [Vulkan] xrRender_initconsole: Registering render console commands");

    // DOF commands - required by CGamePersistent constructor
    Fvector tw_min, tw_max;
    tw_min.set(-10000, -10000, 0);
    tw_max.set(10000, 10000, 10000);
    CMD4(CCC_Vector3, "r2_dof", &ps_r2_dof, tw_min, tw_max);
    CMD4(CCC_Float, "r2_dof_radius", &ps_r2_dof_kernel_size, .05f, 1.f);
    CMD4(CCC_Float, "r2_dof_sky", &ps_r2_dof_sky, -10000.f, 10000.f);
}

BOOL xrRender_test_hw()
{
    // Vulkan is always supported when this renderer is loaded
    return TRUE;
}
