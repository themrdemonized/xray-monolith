#include	"stdafx.h"
#pragma		hdrstop

#include	"xrRender_console.h"
#include	"dxRenderDeviceRender.h"

#include "../../build_config_defines.h"

u32 ps_Preset = 2;
xr_token qpreset_token [ ] = {
	{"Minimum", 0},
	{"Low", 1},
	{"Default", 2},
	{"High", 3},
	{"Extreme", 4},
	{0, 0}
};

u32 ps_r_ssao_mode = 2;
xr_token qssao_mode_token [ ] = {
	{"disabled", 0},
	{"default", 1},
	{"hdao", 2},
	{"hbao", 3},
	{0, 0}
};

u32 ps_r_sun_shafts = 2;
xr_token qsun_shafts_token [ ] = {
	{"st_opt_low", 1},
	{"st_opt_medium", 2},
	{"st_opt_high", 3},
	{0, 0}
};

u32 ps_sunshafts_mode = 2;
xr_token sunshafts_mode_token [ ] = {
	{"off", 0},
	{"volumetric", 1},
	{"screen_space", 2},
	{"combined", 3},
	{0, 0}
};

u32 ps_smaa_quality = 3;
xr_token smaa_quality_token[] = {
	{ "off", 0 },
	{ "low", 1 },
	{ "medium", 2 },
	{ "high", 3 },
	{ "ultra", 4 },
	{ 0, 0 }
};

u32 ps_r_ssao = 3;
xr_token qssao_token [ ] = {
	{"st_opt_off", 0},
	{"st_opt_low", 1},
	{"st_opt_medium", 2},
	{"st_opt_high", 3},
#if defined(USE_DX10) || defined(USE_DX11)
	{"st_opt_ultra", 4},
#endif
	{0, 0}
};

u32 ps_r_sun_quality = 1; //	=	0;
xr_token qsun_quality_token [ ] = {
	{"st_opt_low", 0},
	{"st_opt_medium", 1},
	{"st_opt_high", 2},
#if defined(USE_DX10) || defined(USE_DX11)
	{"st_opt_ultra", 3},
	{"st_opt_extreme", 4},
#endif	//	USE_DX10
	{0, 0}
};

u32 ps_r3_msaa = 0; //	=	0;
xr_token qmsaa_token [ ] = {
	{"st_opt_off", 0},
	{"2x", 1},
	{"4x", 2},
	{"8x", 3},
	{0, 0}
};

u32 ps_r3_msaa_atest = 0; //	=	0;
xr_token qmsaa__atest_token [ ] = {
	{"st_opt_off", 0},
	{"st_opt_atest_msaa_dx10_0", 1},
	{"st_opt_atest_msaa_dx10_1", 2},
	{0, 0}
};

u32 ps_r3_minmax_sm = 3; //	=	0;
xr_token qminmax_sm_token [ ] = {
	{"off", 0},
	{"on", 1},
	{"auto", 2},
	{"autodetect", 3},
	{0, 0}
};

u32 ps_r_screenshot_token = 0;
xr_token screenshot_mode_token [ ] = {
	{ "jpg", 0 },
	{ "png", 1 },
	{ "tga", 2 },
	{ 0, 0 }
};

//	УOffФ
//	УDX10.0 style [Standard]Ф
//	УDX10.1 style [Higher quality]Ф

// Common
extern int psSkeletonUpdate;
extern float r__dtex_range;

Flags32 ps_r__common_flags = {/*RFLAG_NO_RAM_TEXTURES*/ }; // All renders

//int		ps_r__Supersample			= 1		;
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
float ps_r__Tree_SBC = 1.5f; // scale bias correct

float ps_r__WallmarkTTL = 50.f;
float ps_r__WallmarkSHIFT = 0.0001f;
float ps_r__WallmarkSHIFT_V = 0.0001f;

float ps_r__GLOD_ssa_start = 256.f;
float ps_r__GLOD_ssa_end = 64.f;
float ps_r__LOD = 0.75f;
//. float		ps_r__LOD_Power				=  1.5f	;
float ps_r__ssaDISCARD = 3.5f; //RO
float ps_r__ssaDONTSORT = 32.f; //RO
float ps_r__ssaHZBvsTEX = 96.f; //RO

int ps_r__tf_Anisotropic = 8;
float ps_r__tf_Mipbias = 0.0f;
// R1
float ps_r1_ssaLOD_A = 64.f;
float ps_r1_ssaLOD_B = 48.f;
Flags32 ps_r1_flags = {R1FLAG_DLIGHTS}; // r1-only
float ps_r1_lmodel_lerp = 0.1f;
float ps_r1_dlights_clip = 40.f;
float ps_r1_pps_u = 0.f;
float ps_r1_pps_v = 0.f;

// R1-specific
int ps_r1_GlowsPerFrame = 16; // r1-only
float ps_r1_fog_luminance = 1.1f; // r1-only
int ps_r1_SoftwareSkinning = 0; // r1-only

// R2
float ps_r2_ssaLOD_A = 64.f;
float ps_r2_ssaLOD_B = 48.f;

// R2-specific
Flags32 ps_r2_ls_flags = {
	R2FLAG_SUN
	//| R2FLAG_SUN_IGNORE_PORTALS
	| R2FLAG_EXP_DONT_TEST_UNSHADOWED
	| R2FLAG_USE_NVSTENCIL | R2FLAG_EXP_SPLIT_SCENE
	| R2FLAG_EXP_MT_CALC | R3FLAG_DYN_WET_SURF
	| R3FLAG_VOLUMETRIC_SMOKE
	//| R3FLAG_MSAA 
	//| R3FLAG_MSAA_OPT

	| R2FLAG_DETAIL_BUMP
	| R2FLAG_DOF
	| R2FLAG_SOFT_PARTICLES
	| R2FLAG_SOFT_WATER
	| R2FLAG_STEEP_PARALLAX
	| R2FLAG_SUN_FOCUS
	| R2FLAG_SUN_TSM
	| R2FLAG_TONEMAP
	| R2FLAG_VOLUMETRIC_LIGHTS
}; // r2-only

Flags32 ps_r2_ls_flags_ext = {
	/*R2FLAGEXT_SSAO_OPT_DATA |*/ R2FLAGEXT_SSAO_HALF_DATA
	| R2FLAGEXT_ENABLE_TESSELLATION
};


Flags32 ps_r2_anomaly_flags = {
	R2_AN_FLAG_WATER_REFLECTIONS
	| R2_AN_FLAG_MBLUR
	| R2_AN_FLAG_FLARES
};

float ps_r2_df_parallax_h = 0.02f;
float ps_r2_df_parallax_range = 75.f;
float ps_r2_tonemap_middlegray = 1.f; // r2-only
float ps_r2_tonemap_adaptation = 1.f; // r2-only
float ps_r2_tonemap_low_lum = .4f; // r2-only
float ps_r2_tonemap_amount = 0.7f; // r2-only
float ps_r2_ls_bloom_kernel_g = 3.f; // r2-only
float ps_r2_ls_bloom_kernel_b = .7f; // r2-only
float ps_r2_ls_bloom_speed = 100.f; // r2-only
float ps_r2_ls_bloom_kernel_scale = .7f; // r2-only	// gauss
float ps_r2_ls_dsm_kernel = .7f; // r2-only
float ps_r2_ls_psm_kernel = .7f; // r2-only
float ps_r2_ls_ssm_kernel = .7f; // r2-only
float ps_r2_ls_bloom_threshold = 1.f; // r2-only
Fvector ps_r2_aa_barier = {.8f, .1f, 0}; // r2-only
Fvector ps_r2_aa_weight = {.25f, .25f, 0}; // r2-only
float ps_r2_aa_kernel = .5f; // r2-only
float ps_r2_mblur = .0f; // .5f
int ps_r2_GI_depth = 1; // 1..5
int ps_r2_GI_photons = 16; // 8..64
float ps_r2_GI_clip = EPS_L; // EPS
float ps_r2_GI_refl = .9f; // .9f
float ps_r2_ls_depth_scale = 1.00001f; // 1.00001f
float ps_r2_ls_depth_bias = -0.001f; // -0.0001f
float ps_r2_ls_squality = 1.0f; // 1.00f
float ps_r2_sun_tsm_projection = 0.3f; // 0.18f
float ps_r2_sun_tsm_bias = -0.01f; // 
float ps_r2_sun_near = 20.f; // 12.0f

extern float OLES_SUN_LIMIT_27_01_07; //	actually sun_far

float ps_r2_sun_near_border = 0.75f; // 1.0f
float ps_r2_sun_depth_far_scale = 1.00000f; // 1.00001f
float ps_r2_sun_depth_far_bias = -0.00002f; // -0.0000f
float ps_r2_sun_depth_near_scale = 1.0000f; // 1.00001f
float ps_r2_sun_depth_near_bias = 0.00001f; // -0.00005f
float ps_r2_sun_lumscale = 1.0f; // 1.0f
float ps_r2_sun_lumscale_hemi = 1.0f; // 1.0f
float ps_r2_sun_lumscale_amb = 1.0f;
float ps_r2_gmaterial = 2.2f; // 
float ps_r2_zfill = 0.25f; // .1f

float ps_r2_dhemi_sky_scale = 0.08f; // 1.5f
float ps_r2_dhemi_light_scale = 0.2f;
float ps_r2_dhemi_light_flow = 0.1f;
int ps_r2_dhemi_count = 5; // 5
int ps_r2_wait_sleep = 0;
int ps_r2_qsync = 0;

float ps_r2_lt_smooth = 1.f; // 1.f
float ps_r2_slight_fade = 0.5f; // 1.f
///////lvutner
Fvector4 ps_r2_mask_control = {.0f, .0f, .0f, .0f}; // r2-only
Fvector ps_r2_drops_control = {.0f, 1.15f, .0f}; // r2-only

int ps_r2_nightvision = 0;

//--DSR-- SilencerOverheat_start
float sil_glow_max_temp = 0.15f;				// Max possible weapon temperature
float sil_glow_shot_temp = 0.004f;				// Temperature added on 1 successful shot
float sil_glow_cool_temp_rate = 0.01f;			// Temperature removed after 1 second
Fvector sil_glow_color = { 1.f, .0f, .0f };	// Color of max temperature
//--DSR-- SilencerOverheat_end

Fvector dsr_test = { 0.f, 0.f, 0.f };
Fvector dsr_test1 = { 0.f, 0.f, 0.f };
Fvector dsr_test2 = { 0.f, 0.f, 0.f };

//--DSR-- HeatVision_start
int ps_r2_heatvision = 0;			// heatvision shader ON/OFF
float heat_vision_mode = 0.0f;		// heatvision mode - rgb/greyscale
int heat_vision_cooldown = 1;		// heatvision corpse cooling down ON/OFF
float heat_vision_cooldown_time = 20000.f;	// heatvision corpse cooling down time (in ms)
Fvector4 heat_vision_steps = { 0.45f, 0.65f, 0.76f, .0f };
Fvector4 heat_vision_blurring = { 15.f, 4.f, 60.f, .0f };
Fvector4 heat_vision_args_1 = { .0f, .0f, .0f, .0f };
Fvector4 heat_vision_args_2 = { .0f, .0f, .0f, .0f };
//--DSR-- HeatVision_end
//crookr
int scope_fake_enabled = 1;
//string32 scope_fake_texture = "wpn\\wpn_crosshair_pso1";

float ps_r2_ss_sunshafts_length = 1.f;
float ps_r2_ss_sunshafts_radius = 1.f;

float ps_r2_tnmp_a = .205f; // r2-only
float ps_r2_tnmp_b = .35f; // r2-only
float ps_r2_tnmp_c = .55f; // r2-only
float ps_r2_tnmp_d = .20f; // r2-only
float ps_r2_tnmp_e = .02f; // r2-only
float ps_r2_tnmp_f = .15f; // r2-only
float ps_r2_tnmp_w = 7.5f; // r2-only
float ps_r2_tnmp_exposure = 7.0f; // r2-only
float ps_r2_tnmp_gamma = .25f; // r2-only
float ps_r2_tnmp_onoff = .0f; // r2-only

float ps_r2_img_exposure = 1.0f; // r2-only
float ps_r2_img_gamma = 1.0f; // r2-only
float ps_r2_img_saturation = 1.0f; // r2-only
Fvector ps_r2_img_cg = {.0f, .0f, .0f}; // r2-only

Fvector4 ps_pp_bloom_thresh = { .7, .8f, .9f, .0f };
Fvector4 ps_pp_bloom_weight = { .33f, .33f, .33f, .0f };

//debug
Fvector4 ps_dev_param_1 = { .0f, .0f, .0f, .0f };
Fvector4 ps_dev_param_2 = { .0f, .0f, .0f, .0f };
Fvector4 ps_dev_param_3 = { .0f, .0f, .0f, .0f };
Fvector4 ps_dev_param_4 = { .0f, .0f, .0f, .0f };
Fvector4 ps_dev_param_5 = { .0f, .0f, .0f, .0f };
Fvector4 ps_dev_param_6 = { .0f, .0f, .0f, .0f };
Fvector4 ps_dev_param_7 = { .0f, .0f, .0f, .0f };
Fvector4 ps_dev_param_8 = { .0f, .0f, .0f, .0f };

float ps_particle_update_coeff = 1.f;

/////////////////////////////////

// Mark Switch
int ps_markswitch_current = 0;
int ps_markswitch_count = 0;
Fvector4 ps_markswitch_color = { 0, 0, 0, 0 };

// Screen Space Shaders Stuff
float ps_ssfx_hud_hemi = 0.15f; // HUD Hemi Offset

int ps_ssfx_il_quality = 32; // IL Samples
Fvector4 ps_ssfx_il = { 6.66f, 1.0f, 1.0f, 5.0f }; // Res, Int, Vibrance, Blur
Fvector4 ps_ssfx_il_setup1 = { 150.0f, 1.0f, 0.5f, 0.0f }; // Distance, HUD, Flora, -

int ps_ssfx_ao_quality = 4; // AO Samples
Fvector4 ps_ssfx_ao = { 1.0f, 5.0f, 1.0f, 2.5f }; // Res, AO int, Blur, Radius
Fvector4 ps_ssfx_ao_setup1 = { 150.0, 1.0, 1.0, 0.0 }; // Distance, HUD, Flora, Max OCC

Fvector4 ps_ssfx_water = { 1.0f, 0.8f, 1.0f, 0.0f }; // Res, Blur, Blur Perlin, -
Fvector3 ps_ssfx_water_quality = { 1.0, 2.0, 0.0 }; // SSR Quality, Parallax Quality, -
Fvector4 ps_ssfx_water_setup1 = { 0.6f, 3.0f, 0.3f, 0.05f }; // Distortion, Turbidity, Softborder, Parallax Height
Fvector4 ps_ssfx_water_setup2 = { 0.8f, 6.0f, 0.3f, 0.5f }; // Reflection, Specular, Caustics, Ripples

int ps_ssfx_ssr_quality = 0; // Quality
Fvector4 ps_ssfx_ssr = { 1.0f, 0.2f, 0.0f, 0.0f }; // Res, Blur, Temp, Noise
Fvector4 ps_ssfx_ssr_2 = { 0.0f, 1.3f, 1.0f, 0.015f }; // Quality, Fade, Int, Wpn Int

Fvector4 ps_ssfx_terrain_quality = { 6, 0, 0, 0 };
Fvector4 ps_ssfx_terrain_offset = { 0, 0, 0, 0 };

Fvector3 ps_ssfx_shadows = { 256, 1536, 0.0f };
Fvector4 ps_ssfx_volumetric = { 0, 1.0f, 3.0f, 8.0f };

Fvector3 ps_ssfx_shadow_bias = { 0.4f, 0.03f, 0.0f };

Fvector4 ps_ssfx_lut = { 0.0f, 0.0f, 0.0f, 0.0f };

Fvector4 ps_ssfx_wind_grass = { 9.5f, 1.4f, 1.5f, 0.4f };
Fvector4 ps_ssfx_wind_trees = { 11.0f, 0.15f, 0.5f, 0.15f };

Fvector4 ps_ssfx_florafixes_1 = { 0.3f, 0.21f, 0.3f, 0.21f }; // Flora fixes 1
Fvector4 ps_ssfx_florafixes_2 = { 2.0f, 1.0f, 0.0f, 0.0f }; // Flora fixes 2

Fvector4 ps_ssfx_wetsurfaces_1 = { 1.0f, 1.0f, 1.0f, 1.0f }; // Wet surfaces 1
Fvector4 ps_ssfx_wetsurfaces_2 = { 1.0f, 1.0f, 1.0f, 1.0f }; // Wet surfaces 2

int ps_ssfx_is_underground = 0;
int ps_ssfx_gloss_method = 0;
float ps_ssfx_gloss_factor = 0.5f;
Fvector3 ps_ssfx_gloss_minmax = { 0.0f,0.92f,0.0f }; // Gloss

Fvector4 ps_ssfx_lightsetup_1 = { 0.35f, 0.5f, 0.0f, 0.0f }; // Spec intensity

Fvector4 ps_ssfx_hud_drops_1 = { 1.0f, 1.0f, 1.0f, 1.0f }; // Anim Speed, Int, Reflection, Refraction
Fvector4 ps_ssfx_hud_drops_2 = { 1.5f, 0.85f, 0.0f, 2.0f }; // Density, Size, Extra Gloss, Gloss

Fvector4 ps_ssfx_blood_decals = { 0.6f, 0.6f, 0.f, 0.f };
Fvector4 ps_ssfx_rain_1 = { 2.0f, 0.1f, 0.6f, 2.f }; // Len, Width, Speed, Quality
Fvector4 ps_ssfx_rain_2 = { 0.5f, 0.1f, 1.0f, 0.5f }; // Alpha, Brigthness, Refraction, Reflection
Fvector4 ps_ssfx_rain_3 = { 0.5f, 1.0f, 0.0f, 0.0f }; // Alpha, Refraction ( Splashes )

Fvector3 ps_ssfx_shadow_cascades = { 20, 40, 160 };
Fvector4 ps_ssfx_grass_shadows = { .0f, .35f, 30.0f, .0f };

Fvector4 ps_ssfx_grass_interactive = { .0f, .0f, 2000.0f, 1.0f };
Fvector4 ps_ssfx_int_grass_params_1 = { 1.0f, 1.0f, 1.0f, 25.0f };
Fvector4 ps_ssfx_int_grass_params_2 = { 1.0f, 5.0f, 1.0f, 1.0f };

Fvector4 ps_ssfx_wpn_dof_1 = { .0f, .0f, .0f, .0f };
float ps_ssfx_wpn_dof_2 = 1.0f;

//	x - min (0), y - focus (1.4), z - max (100)
Fvector3 ps_r2_dof = { -1.25f, 0.f, 600.f };
float ps_r2_dof_sky = 30; //	distance to sky
float ps_r2_dof_kernel_size = 0.25f; //	7.0f

float ps_r3_dyn_wet_surf_near = 10.f; // 10.0f
float ps_r3_dyn_wet_surf_far = 30.f; // 30.0f
int ps_r3_dyn_wet_surf_sm_res = 256; // 256
Flags32 psDeviceFlags2 = { 0 };
Flags32 ps_actor_shadow_flags = {0}; //Swartz: actor shadow

//AVO: detail draw radius
Flags32 ps_common_flags = {0}; // r1-only
u32 ps_steep_parallax = 0;
int ps_r__detail_radius = 49;
#ifdef DETAIL_RADIUS // управление радиусом отрисовки травы
u32 dm_size = 24;
u32 dm_cache1_line = 12; //dm_size*2/dm_cache1_count
u32 dm_cache_line = 49; //dm_size+1+dm_size
u32 dm_cache_size = 2401; //dm_cache_line*dm_cache_line
float dm_fade = 47.5; //float(2*dm_size)-.5f;
u32 dm_current_size = 24;
u32 dm_current_cache1_line = 12; //dm_current_size*2/dm_cache1_count
u32 dm_current_cache_line = 49; //dm_current_size+1+dm_current_size
u32 dm_current_cache_size = 2401; //dm_current_cache_line*dm_current_cache_line
float dm_current_fade = 47.5; //float(2*dm_current_size)-.5f;
#endif
float ps_current_detail_density = 0.6f;
float ps_current_detail_height = 1.0f;
xr_token ext_quality_token[] = {
	{"qt_off", 0},
	{"qt_low", 1},
	{"qt_medium", 2},
	{"qt_high", 3},
	{"qt_extreme", 4},
	{0, 0}
};
//-AVO

//- Mad Max
float ps_r2_gloss_factor = 4.0f;
float ps_r2_gloss_min = 0.0f;
//- Mad Max

int opt_static = 2;
int opt_dynamic = 2;

#ifndef _EDITOR
#include	"../../xrEngine/xr_ioconsole.h"
#include	"../../xrEngine/xr_ioc_cmd.h"

#if defined(USE_DX10) || defined(USE_DX11)
#include "../xrRenderDX10/StateManager/dx10SamplerStateCache.h"
#endif	//	USE_DX10

class CCC_ssfx_cascades : public CCC_Vector3
{
public:
	void apply()
	{
#if defined(USE_DX10) || defined(USE_DX11)
		RImplementation.init_cacades();
#endif
	}

	CCC_ssfx_cascades(LPCSTR N, Fvector3* V, const Fvector3 _min, const Fvector3 _max) : CCC_Vector3(N, V, _min, _max)
	{
	};

	virtual void Execute(LPCSTR args)
	{
		CCC_Vector3::Execute(args);
		apply();
	}

	virtual void Status(TStatus& S)
	{
		CCC_Vector3::Status(S);
		apply();
	}
};

//-----------------------------------------------------------------------
//AVO: detail draw radius
#ifdef DETAIL_RADIUS
class CCC_detail_radius : public CCC_Integer
{
public:
	void apply()
	{
		dm_current_size = iFloor((float)ps_r__detail_radius / 4) * 2;
		dm_current_cache1_line = dm_current_size * 2 / 4; // assuming cache1_count = 4
		dm_current_cache_line = dm_current_size + 1 + dm_current_size;
		dm_current_cache_size = dm_current_cache_line * dm_current_cache_line;
		dm_current_fade = float(2 * dm_current_size) - .5f;
	}

	CCC_detail_radius(LPCSTR N, int* V, int _min = 0, int _max = 999) : CCC_Integer(N, V, _min, _max)
	{
	};

	virtual void Execute(LPCSTR args)
	{
		CCC_Integer::Execute(args);
		apply();
	}

	virtual void Status(TStatus& S)
	{
		CCC_Integer::Status(S);
	}
};

//-AVO
#endif

class CCC_tf_Aniso : public CCC_Integer
{
public:
	void apply()
	{
		if (0 == HW.pDevice) return;
		int val = *value;
		clamp(val, 1, 16);
#if defined(USE_DX10) || defined(USE_DX11)
		SSManager.SetMaxAnisotropy(val);
#else	//	USE_DX10
		for (u32 i = 0; i < HW.Caps.raster.dwStages; i++)
			CHK_DX(HW.pDevice->SetSamplerState( i, D3DSAMP_MAXANISOTROPY, val ));
#endif	//	USE_DX10
	}

	CCC_tf_Aniso(LPCSTR N, int* v) : CCC_Integer(N, v, 1, 16)
	{
	};

	virtual void Execute(LPCSTR args)
	{
		CCC_Integer::Execute(args);
		apply();
	}

	virtual void Status(TStatus& S)
	{
		CCC_Integer::Status(S);
		apply();
	}
};

class CCC_tf_MipBias : public CCC_Float
{
public:
	void apply()
	{
		if (0 == HW.pDevice) return;

#if defined(USE_DX10) || defined(USE_DX11)
		//	TODO: DX10: Implement mip bias control
		//VERIFY(!"apply not implmemented.");
		//Done. Thanks for reminding me.
		SSManager.SetMipLODBias(*value);
#else	//	USE_DX10
		for (u32 i = 0; i < HW.Caps.raster.dwStages; i++)
			CHK_DX(HW.pDevice->SetSamplerState( i, D3DSAMP_MIPMAPLODBIAS, *((LPDWORD) value)));
#endif	//	USE_DX10
	}

	CCC_tf_MipBias(LPCSTR N, float* v) : CCC_Float(N, v, -3.0f, +3.0f)
	{
	};

	virtual void Execute(LPCSTR args)
	{
		CCC_Float::Execute(args);
		apply();
	}

	virtual void Status(TStatus& S)
	{
		CCC_Float::Status(S);
		apply();
	}
};

class CCC_R2GM : public CCC_Float
{
public:
	CCC_R2GM(LPCSTR N, float* v) : CCC_Float(N, v, 0.f, 4.f) { *v = 0; };

	virtual void Execute(LPCSTR args)
	{
		if (0 == xr_strcmp(args, "on"))
		{
			ps_r2_ls_flags.set(R2FLAG_GLOBALMATERIAL,TRUE);
		}
		else if (0 == xr_strcmp(args, "off"))
		{
			ps_r2_ls_flags.set(R2FLAG_GLOBALMATERIAL,FALSE);
		}
		else
		{
			CCC_Float::Execute(args);
			if (ps_r2_ls_flags.test(R2FLAG_GLOBALMATERIAL))
			{
				static LPCSTR name[4] = {"oren", "blin", "phong", "metal"};
				float mid = *value;
				int m0 = iFloor(mid) % 4;
				int m1 = (m0 + 1) % 4;
				float frc = mid - float(iFloor(mid));
				Msg("* material set to [%s]-[%s], with lerp of [%f]", name[m0], name[m1], frc);
			}
		}
	}
};

class CCC_Screenshot : public IConsole_Command
{
public:
	CCC_Screenshot(LPCSTR N) : IConsole_Command(N)
	{
	};

	virtual void Execute(LPCSTR args)
	{
		if (g_dedicated_server)
			return;

		string_path name;
		name[0] = 0;
		sscanf(args, "%s", name);
		LPCSTR image = xr_strlen(name) ? name : 0;
		::Render->Screenshot(IRender_interface::SM_NORMAL, image);
	}
};

class CCC_RestoreQuadIBData : public IConsole_Command
{
public:
	CCC_RestoreQuadIBData(LPCSTR N) : IConsole_Command(N)
	{
	};

	virtual void Execute(LPCSTR args)
	{
		RCache.RestoreQuadIBData();
	}
};

class CCC_ModelPoolStat : public IConsole_Command
{
public:
	CCC_ModelPoolStat(LPCSTR N) : IConsole_Command(N) { bEmptyArgsHandled = TRUE; };

	virtual void Execute(LPCSTR args)
	{
		RImplementation.Models->dump();
	}
};

class CCC_SSAO_Mode : public CCC_Token
{
public:
	CCC_SSAO_Mode(LPCSTR N, u32* V, xr_token* T) : CCC_Token(N, V, T)
	{
	} ;

	virtual void Execute(LPCSTR args)
	{
		CCC_Token::Execute(args);

		switch (*value)
		{
		case 0:
			{
				ps_r_ssao = 0;
				ps_r2_ls_flags_ext.set(R2FLAGEXT_SSAO_HBAO, 0);
				ps_r2_ls_flags_ext.set(R2FLAGEXT_SSAO_HDAO, 0);
				break;
			}
		case 1:
			{
				if (ps_r_ssao == 0)
				{
					ps_r_ssao = 1;
				}
				ps_r2_ls_flags_ext.set(R2FLAGEXT_SSAO_HBAO, 0);
				ps_r2_ls_flags_ext.set(R2FLAGEXT_SSAO_HDAO, 0);
				ps_r2_ls_flags_ext.set(R2FLAGEXT_SSAO_HALF_DATA, 0);
				break;
			}
		case 2:
			{
				if (ps_r_ssao == 0)
				{
					ps_r_ssao = 1;
				}
				ps_r2_ls_flags_ext.set(R2FLAGEXT_SSAO_HBAO, 0);
				ps_r2_ls_flags_ext.set(R2FLAGEXT_SSAO_HDAO, 1);
				ps_r2_ls_flags_ext.set(R2FLAGEXT_SSAO_OPT_DATA, 0);
				ps_r2_ls_flags_ext.set(R2FLAGEXT_SSAO_HALF_DATA, 0);
				break;
			}
		case 3:
			{
				if (ps_r_ssao == 0)
				{
					ps_r_ssao = 1;
				}
				ps_r2_ls_flags_ext.set(R2FLAGEXT_SSAO_HBAO, 1);
				ps_r2_ls_flags_ext.set(R2FLAGEXT_SSAO_HDAO, 0);
				ps_r2_ls_flags_ext.set(R2FLAGEXT_SSAO_OPT_DATA, 1);
				break;
			}
		}
	}
};

//-----------------------------------------------------------------------
class CCC_Preset : public CCC_Token
{
public:
	CCC_Preset(LPCSTR N, u32* V, xr_token* T) : CCC_Token(N, V, T)
	{
	} ;

	virtual void Execute(LPCSTR args)
	{
		CCC_Token::Execute(args);
		string_path _cfg;
		string_path cmd;

		switch (*value)
		{
		case 0: xr_strcpy(_cfg, "rspec_minimum.ltx");
			break;
		case 1: xr_strcpy(_cfg, "rspec_low.ltx");
			break;
		case 2: xr_strcpy(_cfg, "rspec_default.ltx");
			break;
		case 3: xr_strcpy(_cfg, "rspec_high.ltx");
			break;
		case 4: xr_strcpy(_cfg, "rspec_extreme.ltx");
			break;
		}
		FS.update_path(_cfg, "$game_config$", _cfg);
		strconcat(sizeof(cmd), cmd, "cfg_load", " ", _cfg);
		Console->Execute(cmd);
	}
};


class CCC_memory_stats : public IConsole_Command
{
protected :

public :

	CCC_memory_stats(LPCSTR N) : IConsole_Command(N) { bEmptyArgsHandled = true; };

	virtual void Execute(LPCSTR args)
	{
		u32 m_base = 0;
		u32 c_base = 0;
		u32 m_lmaps = 0;
		u32 c_lmaps = 0;

		dxRenderDeviceRender::Instance().ResourcesGetMemoryUsage(m_base, c_base, m_lmaps, c_lmaps);

		Msg("memory usage  mb \t \t video    \t managed      \t system \n");

		float vb_video = (float)HW.stats_manager.memory_usage_summary[enum_stats_buffer_type_vertex][D3DPOOL_DEFAULT] /
			1024 / 1024;
		float vb_managed = (float)HW.stats_manager.memory_usage_summary[enum_stats_buffer_type_vertex][D3DPOOL_MANAGED]
			/ 1024 / 1024;
		float vb_system = (float)HW.stats_manager.memory_usage_summary[enum_stats_buffer_type_vertex][D3DPOOL_SYSTEMMEM]
			/ 1024 / 1024;
		Msg("vertex buffer      \t \t %f \t %f \t %f ", vb_video, vb_managed, vb_system);

		float ib_video = (float)HW.stats_manager.memory_usage_summary[enum_stats_buffer_type_index][D3DPOOL_DEFAULT] /
			1024 / 1024;
		float ib_managed = (float)HW.stats_manager.memory_usage_summary[enum_stats_buffer_type_index][D3DPOOL_MANAGED] /
			1024 / 1024;
		float ib_system = (float)HW.stats_manager.memory_usage_summary[enum_stats_buffer_type_index][D3DPOOL_SYSTEMMEM]
			/ 1024 / 1024;
		Msg("index buffer      \t \t %f \t %f \t %f ", ib_video, ib_managed, ib_system);

		float textures_managed = (float)(m_base + m_lmaps) / 1024 / 1024;
		Msg("textures          \t \t %f \t %f \t %f ", 0.f, textures_managed, 0.f);

		float rt_video = (float)HW.stats_manager.memory_usage_summary[enum_stats_buffer_type_rtarget][D3DPOOL_DEFAULT] /
			1024 / 1024;
		float rt_managed = (float)HW.stats_manager.memory_usage_summary[enum_stats_buffer_type_rtarget][D3DPOOL_MANAGED]
			/ 1024 / 1024;
		float rt_system = (float)HW.stats_manager.memory_usage_summary[enum_stats_buffer_type_rtarget][D3DPOOL_SYSTEMMEM
		] / 1024 / 1024;
		Msg("R-Targets         \t \t %f \t %f \t %f ", rt_video, rt_managed, rt_system);

		Msg("\nTotal             \t \t %f \t %f \t %f ", vb_video + ib_video + rt_video,
		    textures_managed + vb_managed + ib_managed + rt_managed,
		    vb_system + ib_system + rt_system);
	}
};


#if RENDER!=R_R1
#include "r__pixel_calculator.h"

class CCC_BuildSSA : public IConsole_Command
{
public:
	CCC_BuildSSA(LPCSTR N) : IConsole_Command(N) { bEmptyArgsHandled = TRUE; };

	virtual void Execute(LPCSTR args)
	{
#if !defined(USE_DX10) && !defined(USE_DX11)
		//	TODO: DX10: Implement pixel calculator
		r_pixel_calculator c;
		c.run();
#endif	//	USE_DX10
	}
};
#endif

class CCC_DofFar : public CCC_Float
{
public:
	CCC_DofFar(LPCSTR N, float* V, float _min = 0.0f, float _max = 10000.0f)
		: CCC_Float(N, V, _min, _max)
	{
	}

	virtual void Execute(LPCSTR args)
	{
		float v = float(atof(args));

		if (v < ps_r2_dof.y + 0.1f)
		{
			char pBuf[256];
			_snprintf(pBuf, sizeof(pBuf) / sizeof(pBuf[0]), "float value greater or equal to r2_dof_focus+0.1");
			Msg("~ Invalid syntax in call to '%s'", cName);
			Msg("~ Valid arguments: %s", pBuf);
			Console->Execute("r2_dof_focus");
		}
		else
		{
			CCC_Float::Execute(args);
			if (g_pGamePersistent)
				g_pGamePersistent->SetBaseDof(ps_r2_dof);
		}
	}

	//	CCC_Dof should save all data as well as load from config
	virtual void Save(IWriter* F) { ; }
};

class CCC_DofNear : public CCC_Float
{
public:
	CCC_DofNear(LPCSTR N, float* V, float _min = 0.0f, float _max = 10000.0f)
		: CCC_Float(N, V, _min, _max)
	{
	}

	virtual void Execute(LPCSTR args)
	{
		float v = float(atof(args));

		if (v > ps_r2_dof.y - 0.1f)
		{
			char pBuf[256];
			_snprintf(pBuf, sizeof(pBuf) / sizeof(pBuf[0]), "float value less or equal to r2_dof_focus-0.1");
			Msg("~ Invalid syntax in call to '%s'", cName);
			Msg("~ Valid arguments: %s", pBuf);
			Console->Execute("r2_dof_focus");
		}
		else
		{
			CCC_Float::Execute(args);
			if (g_pGamePersistent)
				g_pGamePersistent->SetBaseDof(ps_r2_dof);
		}
	}

	//	CCC_Dof should save all data as well as load from config
	virtual void Save(IWriter* F) { ; }
};

class CCC_DofFocus : public CCC_Float
{
public:
	CCC_DofFocus(LPCSTR N, float* V, float _min = 0.0f, float _max = 10000.0f)
		: CCC_Float(N, V, _min, _max)
	{
	}

	virtual void Execute(LPCSTR args)
	{
		float v = float(atof(args));

		if (v > ps_r2_dof.z - 0.1f)
		{
			char pBuf[256];
			_snprintf(pBuf, sizeof(pBuf) / sizeof(pBuf[0]), "float value less or equal to r2_dof_far-0.1");
			Msg("~ Invalid syntax in call to '%s'", cName);
			Msg("~ Valid arguments: %s", pBuf);
			Console->Execute("r2_dof_far");
		}
		else if (v < ps_r2_dof.x + 0.1f)
		{
			char pBuf[256];
			_snprintf(pBuf, sizeof(pBuf) / sizeof(pBuf[0]), "float value greater or equal to r2_dof_far-0.1");
			Msg("~ Invalid syntax in call to '%s'", cName);
			Msg("~ Valid arguments: %s", pBuf);
			Console->Execute("r2_dof_near");
		}
		else
		{
			CCC_Float::Execute(args);
			if (g_pGamePersistent)
				g_pGamePersistent->SetBaseDof(ps_r2_dof);
		}
	}

	//	CCC_Dof should save all data as well as load from config
	virtual void Save(IWriter* F) { ; }
};

class CCC_Dof : public CCC_Vector3
{
public:
	CCC_Dof(LPCSTR N, Fvector* V, const Fvector _min, const Fvector _max) :
		CCC_Vector3(N, V, _min, _max) { ; }

	virtual void Execute(LPCSTR args)
	{
		Fvector v;
		if (3 != sscanf(args, "%f,%f,%f", &v.x, &v.y, &v.z))
			InvalidSyntax();
		else if ((v.x > v.y - 0.1f) || (v.z < v.y + 0.1f))
		{
			InvalidSyntax();
			Msg("x <= y - 0.1");
			Msg("y <= z - 0.1");
		}
		else
		{
			CCC_Vector3::Execute(args);
			if (g_pGamePersistent)
				g_pGamePersistent->SetBaseDof(ps_r2_dof);
		}
	}

	virtual void Status(TStatus& S)
	{
		xr_sprintf(S, "%f,%f,%f", value->x, value->y, value->z);
	}

	virtual void Info(TInfo& I)
	{
		xr_sprintf(I, "vector3 in range [%f,%f,%f]-[%f,%f,%f]", min.x, min.y, min.z, max.x, max.y, max.z);
	}
};

class CCC_DumpResources : public IConsole_Command
{
public:
	CCC_DumpResources(LPCSTR N) : IConsole_Command(N) { bEmptyArgsHandled = TRUE; };

	virtual void Execute(LPCSTR args)
	{
		RImplementation.Models->dump();
		dxRenderDeviceRender::Instance().Resources->Dump(false);
	}
};

//	Allow real-time fog config reload
#if	(RENDER == R_R3) || (RENDER == R_R4)
#ifdef	DEBUG

#include "../xrRenderDX10/3DFluid/dx103DFluidManager.h"

class CCC_Fog_Reload : public IConsole_Command
{
public:
	CCC_Fog_Reload(LPCSTR N) : IConsole_Command(N) { bEmptyArgsHandled = TRUE; };
	virtual void Execute(LPCSTR args) 
	{
		FluidManager.UpdateProfiles();
	}
};
#endif	//	DEBUG
#endif	//	(RENDER == R_R3) || (RENDER == R_R4)

//-----------------------------------------------------------------------
void xrRender_initconsole()
{
	CMD3(CCC_Preset, "_preset", &ps_Preset, qpreset_token);

	CMD4(CCC_Integer, "rs_skeleton_update", &psSkeletonUpdate, 2, 128);
#ifdef	DEBUG
	CMD1(CCC_DumpResources,		"dump_resources");
#endif	//	 DEBUG

	CMD4(CCC_Float, "r__dtex_range", &r__dtex_range, 5, 175);

	// Common
	CMD1(CCC_Screenshot, "screenshot");

	//	Igor: just to test bug with rain/particles corruption
	CMD1(CCC_RestoreQuadIBData, "r_restore_quad_ib_data");
#ifdef DEBUG
#if RENDER!=R_R1
	CMD1(CCC_BuildSSA,	"build_ssa"				);
#endif
	CMD4(CCC_Integer,	"r__lsleep_frames",		&ps_r__LightSleepFrames,	4,		30		);
	CMD4(CCC_Float,		"r__ssa_glod_start",	&ps_r__GLOD_ssa_start,		128,	512		);
	CMD4(CCC_Float,		"r__ssa_glod_end",		&ps_r__GLOD_ssa_end,		16,		96		);
	CMD4(CCC_Float,		"r__wallmark_shift_pp",	&ps_r__WallmarkSHIFT,		0.0f,	1.f		);
	CMD4(CCC_Float,		"r__wallmark_shift_v",	&ps_r__WallmarkSHIFT_V,		0.0f,	1.f		);
	CMD1(CCC_ModelPoolStat,"stat_models"		);
#endif // DEBUG
	CMD4(CCC_Float, "r__wallmark_ttl", &ps_r__WallmarkTTL, 1.0f, 10.f*60.f);

	CMD4(CCC_Integer, "r__supersample", &ps_r__Supersample, 1, 8);

	Fvector tw_min, tw_max;

	CMD4(CCC_Float, "r__geometry_lod", &ps_r__LOD, 0.1f, 1.5f);
	//.	CMD4(CCC_Float,		"r__geometry_lod_pow",	&ps_r__LOD_Power,			0,		2		);

	//.	CMD4(CCC_Float,		"r__detail_density",	&ps_r__Detail_density,		.05f,	0.99f	);
	CMD4(CCC_Float, "r__detail_density", &ps_current_detail_density/*&ps_r__Detail_density*/, 0.04f/*.2f*/, 1.f);
	//AVO: extended from 0.2 to 0.04 and replaced variable
	CMD4(CCC_Float, "r__detail_height", &ps_current_detail_height, 0.5f, 2.0f);

#ifdef DEBUG
	CMD4(CCC_Float,		"r__detail_l_ambient",	&ps_r__Detail_l_ambient,	.5f,	.95f	);
	CMD4(CCC_Float,		"r__detail_l_aniso",	&ps_r__Detail_l_aniso,		.1f,	.5f		);

	CMD4(CCC_Float,		"r__d_tree_w_amp",		&ps_r__Tree_w_amp,			.001f,	1.f		);
	CMD4(CCC_Float,		"r__d_tree_w_rot",		&ps_r__Tree_w_rot,			.01f,	100.f	);
	CMD4(CCC_Float,		"r__d_tree_w_speed",	&ps_r__Tree_w_speed,		1.0f,	10.f	);

	tw_min.set			(EPS,EPS,EPS);
	tw_max.set			(2,2,2);
	CMD4(CCC_Vector3,	"r__d_tree_wave",		&ps_r__Tree_Wave,			tw_min, tw_max	);
#endif // DEBUG

	//no ram textures should be enabled by default on r3/r4
	if (RENDER == R_R3 || RENDER == R_R4) ps_r__common_flags.set(RFLAG_NO_RAM_TEXTURES, TRUE);

	CMD3(CCC_Mask, "r__no_ram_textures", &ps_r__common_flags, RFLAG_NO_RAM_TEXTURES);
	CMD2(CCC_tf_Aniso, "r__tf_aniso", &ps_r__tf_Anisotropic); //	{1..16}
	CMD2(CCC_tf_MipBias, "r__tf_mipbias", &ps_r__tf_Mipbias); // {-3 +3}

	// R1
	CMD4(CCC_Float, "r1_ssa_lod_a", &ps_r1_ssaLOD_A, 16, 96);
	CMD4(CCC_Float, "r1_ssa_lod_b", &ps_r1_ssaLOD_B, 16, 64);
	CMD4(CCC_Float, "r1_lmodel_lerp", &ps_r1_lmodel_lerp, 0, 0.333f);
	CMD3(CCC_Mask, "r1_dlights", &ps_r1_flags, R1FLAG_DLIGHTS);
	CMD4(CCC_Float, "r1_dlights_clip", &ps_r1_dlights_clip, 10.f, 150.f);
	CMD4(CCC_Float, "r1_pps_u", &ps_r1_pps_u, -1.f, +1.f);
	CMD4(CCC_Float, "r1_pps_v", &ps_r1_pps_v, -1.f, +1.f);


	// R1-specific
	CMD4(CCC_Integer, "r1_glows_per_frame", &ps_r1_GlowsPerFrame, 2, 32);
	CMD3(CCC_Mask, "r1_detail_textures", &ps_r2_ls_flags, R1FLAG_DETAIL_TEXTURES);

	CMD4(CCC_Float, "r1_fog_luminance", &ps_r1_fog_luminance, 0.2f, 5.f);

	// Software Skinning
	// 0 - disabled (renderer can override)
	// 1 - enabled
	// 2 - forced hardware skinning (renderer can not override)
	CMD4(CCC_Integer, "r1_software_skinning", &ps_r1_SoftwareSkinning, 0, 2);

	// R2
	CMD4(CCC_Float, "r2_ssa_lod_a", &ps_r2_ssaLOD_A, 16, 96);
	CMD4(CCC_Float, "r2_ssa_lod_b", &ps_r2_ssaLOD_B, 32, 96);

	// R2-specific
	CMD2(CCC_R2GM, "r2em", &ps_r2_gmaterial);
	CMD3(CCC_Mask, "r2_tonemap", &ps_r2_ls_flags, R2FLAG_TONEMAP);
	CMD4(CCC_Float, "r2_tonemap_middlegray", &ps_r2_tonemap_middlegray, 0.0f, 2.0f);
	CMD4(CCC_Float, "r2_tonemap_adaptation", &ps_r2_tonemap_adaptation, 0.01f, 10.0f);
	CMD4(CCC_Float, "r2_tonemap_lowlum", &ps_r2_tonemap_low_lum, 0.0001f, 1.0f);
	CMD4(CCC_Float, "r2_tonemap_amount", &ps_r2_tonemap_amount, 0.0000f, 1.0f);
	CMD4(CCC_Float, "r2_ls_bloom_kernel_scale", &ps_r2_ls_bloom_kernel_scale, 0.05f, 2.f);
	CMD4(CCC_Float, "r2_ls_bloom_kernel_g", &ps_r2_ls_bloom_kernel_g, 1.f, 7.f);
	CMD4(CCC_Float, "r2_ls_bloom_kernel_b", &ps_r2_ls_bloom_kernel_b, 0.01f, 1.f);
	CMD4(CCC_Float, "r2_ls_bloom_threshold", &ps_r2_ls_bloom_threshold, 0.f, 1.f);
	CMD4(CCC_Float, "r2_ls_bloom_speed", &ps_r2_ls_bloom_speed, 0.f, 100.f);
	CMD3(CCC_Mask, "r2_ls_bloom_fast", &ps_r2_ls_flags, R2FLAG_FASTBLOOM);
	CMD4(CCC_Float, "r2_ls_dsm_kernel", &ps_r2_ls_dsm_kernel, .1f, 3.f);
	CMD4(CCC_Float, "r2_ls_psm_kernel", &ps_r2_ls_psm_kernel, .1f, 3.f);
	CMD4(CCC_Float, "r2_ls_ssm_kernel", &ps_r2_ls_ssm_kernel, .1f, 3.f);
	CMD4(CCC_Float, "r2_ls_squality", &ps_r2_ls_squality, .5f, 3.f);

	CMD3(CCC_Mask, "r2_zfill", &ps_r2_ls_flags, R2FLAG_ZFILL);
	CMD4(CCC_Float, "r2_zfill_depth", &ps_r2_zfill, .001f, .5f);
	CMD3(CCC_Mask, "r2_allow_r1_lights", &ps_r2_ls_flags, R2FLAG_R1LIGHTS);

	CMD3(CCC_Mask, "r__actor_shadow", &ps_actor_shadow_flags, RFLAG_ACTOR_SHADOW); //Swartz: actor shadow

	//- Mad Max
	CMD4(CCC_Float, "r2_gloss_factor", &ps_r2_gloss_factor, .001f, 10.f);
	CMD4(CCC_Float, "r2_gloss_min", &ps_r2_gloss_min, .001f, 1.0f);
	//- Mad Max

	CMD3(CCC_Token, "r_screenshot_mode", &ps_r_screenshot_token, screenshot_mode_token);

#ifdef DEBUG
	CMD3(CCC_Mask,		"r2_use_nvdbt",			&ps_r2_ls_flags,			R2FLAG_USE_NVDBT);
	CMD3(CCC_Mask,		"r2_mt",				&ps_r2_ls_flags,			R2FLAG_EXP_MT_CALC);
#endif // DEBUG

	CMD3(CCC_Mask, "r2_sun", &ps_r2_ls_flags, R2FLAG_SUN);
	CMD3(CCC_Mask, "r2_sun_details", &ps_r2_ls_flags, R2FLAG_SUN_DETAILS);
	CMD3(CCC_Mask, "r2_sun_focus", &ps_r2_ls_flags, R2FLAG_SUN_FOCUS);
	//	CMD3(CCC_Mask,		"r2_sun_static",		&ps_r2_ls_flags,			R2FLAG_SUN_STATIC);
	//	CMD3(CCC_Mask,		"r2_exp_splitscene",	&ps_r2_ls_flags,			R2FLAG_EXP_SPLIT_SCENE);
	//	CMD3(CCC_Mask,		"r2_exp_donttest_uns",	&ps_r2_ls_flags,			R2FLAG_EXP_DONT_TEST_UNSHADOWED);
	CMD3(CCC_Mask, "r2_exp_donttest_shad", &ps_r2_ls_flags, R2FLAG_EXP_DONT_TEST_SHADOWED);

	CMD3(CCC_Mask, "r2_sun_tsm", &ps_r2_ls_flags, R2FLAG_SUN_TSM);
	CMD4(CCC_Float, "r2_sun_tsm_proj", &ps_r2_sun_tsm_projection, .001f, 0.8f);
	CMD4(CCC_Float, "r2_sun_tsm_bias", &ps_r2_sun_tsm_bias, -0.5, +0.5);
	CMD4(CCC_Float, "r2_sun_near", &ps_r2_sun_near, 1.f, /*50.f*/150.f); //AVO: extended from 50 to 150
#if RENDER!=R_R1
	CMD4(CCC_Float, "r2_sun_far", &OLES_SUN_LIMIT_27_01_07, 51.f, 180.f);
#endif
	CMD4(CCC_Float, "r2_sun_near_border", &ps_r2_sun_near_border, .5f, 1.0f);
	CMD4(CCC_Float, "r2_sun_depth_far_scale", &ps_r2_sun_depth_far_scale, 0.5, 1.5);
	CMD4(CCC_Float, "r2_sun_depth_far_bias", &ps_r2_sun_depth_far_bias, -0.5, +0.5);
	CMD4(CCC_Float, "r2_sun_depth_near_scale", &ps_r2_sun_depth_near_scale, 0.5, 1.5);
	CMD4(CCC_Float, "r2_sun_depth_near_bias", &ps_r2_sun_depth_near_bias, -0.5, +0.5);
	CMD4(CCC_Float, "r2_sun_lumscale", &ps_r2_sun_lumscale, -1.0, +3.0);
	CMD4(CCC_Float, "r2_sun_lumscale_hemi", &ps_r2_sun_lumscale_hemi, 0.0, +3.0);
	CMD4(CCC_Float, "r2_sun_lumscale_amb", &ps_r2_sun_lumscale_amb, 0.0, +3.0);

	CMD3(CCC_Mask, "r2_aa", &ps_r2_ls_flags, R2FLAG_AA);
	CMD4(CCC_Float, "r2_aa_kernel", &ps_r2_aa_kernel, 0.3f, 0.7f);
	CMD4(CCC_Float, "r2_mblur", &ps_r2_mblur, 0.0f, 1.0f);

	///////lvutner
	CMD4(CCC_Vector4, "r2_mask_control", &ps_r2_mask_control, Fvector4().set(0,0,0,0), Fvector4().set(10,3,1,1));

	tw_min.set(0, 0, 0);
	tw_max.set(1, 2, 1);
	CMD4(CCC_Vector3, "r2_drops_control", &ps_r2_drops_control, tw_min, tw_max);
	CMD3(CCC_Token, "r2_sunshafts_mode", &ps_sunshafts_mode, sunshafts_mode_token);
	CMD4(CCC_Float, "r2_ss_sunshafts_length", &ps_r2_ss_sunshafts_length, .2f, 1.5f);
	CMD4(CCC_Float, "r2_ss_sunshafts_radius", &ps_r2_ss_sunshafts_radius, .5f, 2.f);

	CMD4(CCC_Float, "r2_tnmp_a", &ps_r2_tnmp_a, 0.0f, 20.0f);
	CMD4(CCC_Float, "r2_tnmp_b", &ps_r2_tnmp_b, 0.0f, 20.0f);
	CMD4(CCC_Float, "r2_tnmp_c", &ps_r2_tnmp_c, 0.0f, 20.0f);
	CMD4(CCC_Float, "r2_tnmp_d", &ps_r2_tnmp_d, 0.0f, 20.0f);
	CMD4(CCC_Float, "r2_tnmp_e", &ps_r2_tnmp_e, 0.0f, 20.0f);
	CMD4(CCC_Float, "r2_tnmp_f", &ps_r2_tnmp_f, 0.0f, 20.0f);
	CMD4(CCC_Float, "r2_tnmp_w", &ps_r2_tnmp_w, 0.0f, 20.0f);
	CMD4(CCC_Float, "r2_tnmp_exposure", &ps_r2_tnmp_exposure, 0.0f, 20.0f);
	CMD4(CCC_Float, "r2_tnmp_gamma", &ps_r2_tnmp_gamma, 0.0f, 20.0f);
	CMD4(CCC_Float, "r2_tnmp_onoff", &ps_r2_tnmp_onoff, 0.0f, 1.0f);


	CMD4(CCC_Float, "r__exposure", &ps_r2_img_exposure, 0.5f, 4.0f);
	CMD4(CCC_Float, "r__gamma", &ps_r2_img_gamma, 0.5f, 2.2f);
	CMD4(CCC_Float, "r__saturation", &ps_r2_img_saturation, 0.0f, 2.0f);

	tw_min.set(0, 0, 0);
	tw_max.set(1, 1, 1);
	CMD4(CCC_Vector3, "r__color_grading", &ps_r2_img_cg, tw_min, tw_max);

	//Refactor
	Fvector4 twb_min = { 0.f, 0.f, 0.f, 0.f };
	Fvector4 twb_max = { 1.f, 1.f, 1.f, 1.f };
	CMD4(CCC_Vector4, "r__bloom_weight", &ps_pp_bloom_weight, twb_min, twb_max);
	CMD4(CCC_Vector4, "r__bloom_thresh", &ps_pp_bloom_thresh, twb_min, twb_max);
	CMD4(CCC_Integer, "r__nightvision", &ps_r2_nightvision, 0, 3); //For beef's nightvision shader or other stuff

	CMD4(CCC_Integer, "r__fakescope", &scope_fake_enabled, 0, 1); //crookr for fake scope

	CMD4(CCC_Integer, "r__heatvision", &ps_r2_heatvision, 0, 1); //--DSR-- HeatVision
	CMD3(CCC_Mask, "r2_terrain_z_prepass", &ps_r2_ls_flags, R2FLAG_TERRAIN_PREPASS); //Terrain Z Prepass @Zagolski
	
	//////////other
	CMD3(CCC_Mask, "r2_water_reflections", &ps_r2_anomaly_flags, R2_AN_FLAG_WATER_REFLECTIONS);
	CMD3(CCC_Mask, "r2_mblur_enabled", &ps_r2_anomaly_flags, R2_AN_FLAG_MBLUR);
	CMD3(CCC_Mask, "r__lens_flares", &ps_r2_anomaly_flags, R2_AN_FLAG_FLARES);
	CMD3(CCC_Token, "r2_smaa", &ps_smaa_quality, smaa_quality_token);
	CMD3(CCC_Mask,		"r2_gi",				&ps_r2_ls_flags,			R2FLAG_GI);
	CMD4(CCC_Float,		"r2_gi_clip",			&ps_r2_GI_clip,				EPS,	0.1f	);
	CMD4(CCC_Integer,	"r2_gi_depth",			&ps_r2_GI_depth,			1,		5		);
	CMD4(CCC_Integer,	"r2_gi_photons",		&ps_r2_GI_photons,			8,		256		);
	CMD4(CCC_Float,		"r2_gi_refl",			&ps_r2_GI_refl,				EPS_L,	0.99f	);
	
	//Shader param stuff
	Fvector4 tw2_min = { -100.f, -100.f, -100.f, -100.f };
	Fvector4 tw2_max = { 100.f, 100.f, 100.f, 100.f };
	CMD4(CCC_Vector4, "shader_param_1", &ps_dev_param_1, tw2_min, tw2_max);
	CMD4(CCC_Vector4, "shader_param_2", &ps_dev_param_2, tw2_min, tw2_max);
	CMD4(CCC_Vector4, "shader_param_3", &ps_dev_param_3, tw2_min, tw2_max);
	CMD4(CCC_Vector4, "shader_param_4", &ps_dev_param_4, tw2_min, tw2_max);
	CMD4(CCC_Vector4, "shader_param_5", &ps_dev_param_5, tw2_min, tw2_max);
	CMD4(CCC_Vector4, "shader_param_6", &ps_dev_param_6, tw2_min, tw2_max);
	CMD4(CCC_Vector4, "shader_param_7", &ps_dev_param_7, tw2_min, tw2_max);
	CMD4(CCC_Vector4, "shader_param_8", &ps_dev_param_8, tw2_min, tw2_max);
	
	// Mark Switch
	CMD4(CCC_Integer, "markswitch_current", &ps_markswitch_current, 0, 32);
	CMD4(CCC_Integer, "markswitch_count", &ps_markswitch_count, 0, 32);
	CMD4(CCC_Vector4, "markswitch_color", &ps_markswitch_color, Fvector4().set(0.0, 0.0, 0.0, 0.0), Fvector4().set(1.0, 1.0, 1.0, 1.0));
	
	// Screen Space Shaders
	CMD4(CCC_Float, "ssfx_hud_hemi", &ps_ssfx_hud_hemi, 0.0f, 1.0f);

	CMD4(CCC_Integer, "ssfx_il_quality", &ps_ssfx_il_quality, 16, 64);
	CMD4(CCC_Vector4, "ssfx_il", &ps_ssfx_il, Fvector4().set(0, 0, 0, 0), Fvector4().set(8, 10, 3, 6));
	CMD4(CCC_Vector4, "ssfx_il_setup1", &ps_ssfx_il_setup1, Fvector4().set(0, 0, 0, 0), Fvector4().set(300, 1, 1, 1));

	CMD4(CCC_Integer, "ssfx_ao_quality", &ps_ssfx_ao_quality, 2, 8);
	CMD4(CCC_Vector4, "ssfx_ao", &ps_ssfx_ao, Fvector4().set(0, 0, 0, 0), Fvector4().set(8, 10, 1, 10));
	CMD4(CCC_Vector4, "ssfx_ao_setup1", &ps_ssfx_ao_setup1, Fvector4().set(0, 0, 0, 0), Fvector4().set(300, 1, 1, 1));

	CMD4(CCC_Vector4, "ssfx_water", &ps_ssfx_water, Fvector4().set(1, 0, 0, 0), Fvector4().set(8, 1, 1, 0));
	CMD4(CCC_Vector3, "ssfx_water_quality", &ps_ssfx_water_quality, Fvector3().set(0, 0, 0), Fvector3().set(4, 3, 0));
	CMD4(CCC_Vector4, "ssfx_water_setup1", &ps_ssfx_water_setup1, Fvector4().set(0, 0, 0, 0), Fvector4().set(2, 10, 1, 0.1));
	CMD4(CCC_Vector4, "ssfx_water_setup2", &ps_ssfx_water_setup2, Fvector4().set(0, 0, 0, 0), Fvector4().set(1, 10, 1, 1));

	CMD4(CCC_Integer, "ssfx_ssr_quality", &ps_ssfx_ssr_quality, 0, 5);
	CMD4(CCC_Vector4, "ssfx_ssr", &ps_ssfx_ssr, Fvector4().set(1, 0, 0, 0), Fvector4().set(2, 1, 1, 1));
	CMD4(CCC_Vector4, "ssfx_ssr_2", &ps_ssfx_ssr_2, Fvector4().set(0, 0, 0, 0), Fvector4().set(2, 2, 2, 2));
	
	CMD4(CCC_Vector4, "ssfx_terrain_quality", &ps_ssfx_terrain_quality, Fvector4().set(0, 0, 0, 0), Fvector4().set(12, 0, 0, 0));
	CMD4(CCC_Vector4, "ssfx_terrain_offset", &ps_ssfx_terrain_offset, Fvector4().set(-1, -1, -1, -1), Fvector4().set(1, 1, 1, 1));

	CMD4(CCC_Vector3, "ssfx_shadows", &ps_ssfx_shadows, Fvector3().set(128, 1536, 0), Fvector3().set(1536, 4096, 0));
	CMD4(CCC_Vector4, "ssfx_volumetric", &ps_ssfx_volumetric, Fvector4().set(0, 0, 1.0, 1.0), Fvector4().set(1.0, 5.0, 5.0, 16.0));

	CMD4(CCC_Vector3, "ssfx_shadow_bias", &ps_ssfx_shadow_bias, Fvector3().set(0, 0, 0), Fvector3().set(1.0, 1.0, 1.0));

	CMD4(CCC_Vector4, "ssfx_lut", &ps_ssfx_lut, Fvector4().set(0.0, 0.0, 0.0, 0.0), tw2_max);

	CMD4(CCC_Vector4, "ssfx_wind_grass", &ps_ssfx_wind_grass, Fvector4().set(0.0, 0.0, 0.0, 0.0), Fvector4().set(20.0, 5.0, 5.0, 5.0));
	CMD4(CCC_Vector4, "ssfx_wind_trees", &ps_ssfx_wind_trees, Fvector4().set(0.0, 0.0, 0.0, 0.0), Fvector4().set(20.0, 5.0, 5.0, 1.0));

	CMD4(CCC_Vector4, "ssfx_florafixes_1", &ps_ssfx_florafixes_1, Fvector4().set(0.0, 0.0, 0.0, 0.0), Fvector4().set(1.0, 1.0, 1.0, 1.0));
	CMD4(CCC_Vector4, "ssfx_florafixes_2", &ps_ssfx_florafixes_2, Fvector4().set(0.0, 0.0, 0.0, 0.0), Fvector4().set(10.0, 1.0, 1.0, 1.0));

	CMD4(CCC_Vector4, "ssfx_wetsurfaces_1", &ps_ssfx_wetsurfaces_1, Fvector4().set(0.01, 0.01, 0.01, 0.01), Fvector4().set(2.0, 2.0, 2.0, 2.0));
	CMD4(CCC_Vector4, "ssfx_wetsurfaces_2", &ps_ssfx_wetsurfaces_2, Fvector4().set(0.01, 0.01, 0.01, 0.01), Fvector4().set(2.0, 2.0, 2.0, 2.0));

	CMD4(CCC_Integer, "ssfx_is_underground", &ps_ssfx_is_underground, 0, 1);
	CMD4(CCC_Integer, "ssfx_gloss_method", &ps_ssfx_gloss_method, 0, 1);
	CMD4(CCC_Vector3, "ssfx_gloss_minmax", &ps_ssfx_gloss_minmax, Fvector3().set(0, 0, 0), Fvector3().set(1.0, 1.0, 1.0));
	CMD4(CCC_Float, "ssfx_gloss_factor", &ps_ssfx_gloss_factor, 0.0f, 1.0f);

	CMD4(CCC_Vector4, "ssfx_lightsetup_1", &ps_ssfx_lightsetup_1, Fvector4().set(0, 0, 0, 0), Fvector4().set(1.0, 1.0, 1.0, 1.0));

	CMD4(CCC_Vector4, "ssfx_hud_drops_1", &ps_ssfx_hud_drops_1, Fvector4().set(0, 0, 0, 0), Fvector4().set(100000, 100, 100, 100));
	CMD4(CCC_Vector4, "ssfx_hud_drops_2", &ps_ssfx_hud_drops_2, Fvector4().set(0, 0, 0, 0), tw2_max);

	CMD4(CCC_Vector4, "ssfx_blood_decals", &ps_ssfx_blood_decals, Fvector4().set(0, 0, 0, 0), Fvector4().set(5, 5, 0, 0));

	CMD4(CCC_Vector4, "ssfx_rain_1", &ps_ssfx_rain_1, Fvector4().set(0, 0, 0, 0), Fvector4().set(10, 5, 5, 2));
	CMD4(CCC_Vector4, "ssfx_rain_2", &ps_ssfx_rain_2, Fvector4().set(0, 0, 0, 0), Fvector4().set(1, 10, 10, 10));
	CMD4(CCC_Vector4, "ssfx_rain_3", &ps_ssfx_rain_3, Fvector4().set(0, 0, 0, 0), Fvector4().set(1, 10, 10, 10));

	CMD4(CCC_Vector4, "ssfx_grass_shadows", &ps_ssfx_grass_shadows, Fvector4().set(0, 0, 0, 0), Fvector4().set(3, 1, 100, 100));
	CMD4(CCC_ssfx_cascades, "ssfx_shadow_cascades", &ps_ssfx_shadow_cascades, Fvector3().set(1.0f, 1.0f, 1.0f), Fvector3().set(300, 300, 300));
	
	CMD4(CCC_Vector4, "ssfx_grass_interactive", &ps_ssfx_grass_interactive, Fvector4().set(0, 0, 0, 0), Fvector4().set(1, 15, 5000, 1));
	CMD4(CCC_Vector4, "ssfx_int_grass_params_1", &ps_ssfx_int_grass_params_1, Fvector4().set(0, 0, 0, 0), Fvector4().set(5, 5, 5, 60));
	CMD4(CCC_Vector4, "ssfx_int_grass_params_2", &ps_ssfx_int_grass_params_2, Fvector4().set(0, 0, 0, 0), Fvector4().set(5, 20, 1, 5));
	
	CMD4(CCC_Vector4, "ssfx_wpn_dof_1", &ps_ssfx_wpn_dof_1, tw2_min, tw2_max);
	CMD4(CCC_Float, "ssfx_wpn_dof_2", &ps_ssfx_wpn_dof_2, 0, 1);

	//--DSR-- SilencerOverheat_start
	CMD4(CCC_Float, "sil_glow_max_temp", &sil_glow_max_temp, 0.f, 1.f);
	CMD4(CCC_Float, "sil_glow_shot_temp", &sil_glow_shot_temp, 0.f, 1.f);
	CMD4(CCC_Float, "sil_glow_cool_temp_rate", &sil_glow_cool_temp_rate, 0.f, 1.f);
	CMD4(CCC_Vector3, "sil_glow_color", &sil_glow_color, Fvector3().set(0, 0, 0), Fvector3().set(1.0, 1.0, 1.0));
	//--DRS-- SilencerOverheat_end

	CMD4(CCC_Vector3, "dsr_test", &dsr_test, Fvector3().set(-100.f, -100.f, -100.f), Fvector3().set(100.f, 100.f, 100.f));
	CMD4(CCC_Vector3, "dsr_test1", &dsr_test1, Fvector3().set(-100.f, -100.f, -100.f), Fvector3().set(100.f, 100.f, 100.f));
	CMD4(CCC_Vector3, "dsr_test2", &dsr_test2, Fvector3().set(-100.f, -100.f, -100.f), Fvector3().set(100.f, 100.f, 100.f));

	//--DSR-- HeatVision_start
	CMD4(CCC_Integer, "heat_vision_cooldown",	&heat_vision_cooldown, 0, 1);
	CMD4(CCC_Float, "heat_vision_cooldown_time", &heat_vision_cooldown_time, 0, 300000.f);
	CMD2(CCC_Float,   "heat_vision_mode",		&heat_vision_mode);
	CMD4(CCC_Vector4, "heat_vision_steps",		&heat_vision_steps, tw2_min, tw2_max);
	CMD4(CCC_Vector4, "heat_vision_blurring",	&heat_vision_blurring, tw2_min, tw2_max);
	CMD4(CCC_Vector4, "heat_vision_args_1",		&heat_vision_args_1, tw2_min, tw2_max);
	CMD4(CCC_Vector4, "heat_vision_args_2",		&heat_vision_args_2, tw2_min, tw2_max);
	//--DSR-- HeatVision_end

	CMD4(CCC_Float, "particle_update_mod", &ps_particle_update_coeff, 0.04f, 10.f);

	// Geometry optimization
	CMD4(CCC_Integer, "r__optimize_static_geom", &opt_static, 0, 4);
	CMD4(CCC_Integer, "r__optimize_dynamic_geom", &opt_dynamic, 0, 4);
	psDeviceFlags2.set(rsOptShadowGeom, TRUE);
	CMD3(CCC_Mask, "r__optimize_shadow_geom", &psDeviceFlags2, rsOptShadowGeom);

	CMD4(CCC_Integer, "r2_wait_sleep", &ps_r2_wait_sleep, 0, 1);
	CMD4(CCC_Integer, "r2_qsync", &ps_r2_qsync, 0, 1);

#ifndef MASTER_GOLD
	CMD4(CCC_Integer,	"r2_dhemi_count",		&ps_r2_dhemi_count,			4,		25		);
	CMD4(CCC_Float,		"r2_dhemi_sky_scale",	&ps_r2_dhemi_sky_scale,		0.0f,	100.f	);
	CMD4(CCC_Float,		"r2_dhemi_light_scale",	&ps_r2_dhemi_light_scale,	0,		100.f	);
	CMD4(CCC_Float,		"r2_dhemi_light_flow",	&ps_r2_dhemi_light_flow,	0,		1.f	);
	CMD4(CCC_Float,		"r2_dhemi_smooth",		&ps_r2_lt_smooth,			0.f,	10.f	);
	CMD3(CCC_Mask,		"rs_hom_depth_draw",	&ps_r2_ls_flags_ext,		R_FLAGEXT_HOM_DEPTH_DRAW);
	CMD3(CCC_Mask,		"r2_shadow_cascede_zcul",&ps_r2_ls_flags_ext,		R2FLAGEXT_SUN_ZCULLING);

#endif // DEBUG

	CMD3(CCC_Mask,		"r2_shadow_cascede_old", &ps_r2_ls_flags_ext,		R2FLAGEXT_SUN_OLD);
	
	CMD4(CCC_Float, "r2_ls_depth_scale", &ps_r2_ls_depth_scale, 0.5, 1.5);
	CMD4(CCC_Float, "r2_ls_depth_bias", &ps_r2_ls_depth_bias, -0.5, +0.5);

	CMD4(CCC_Float, "r2_parallax_h", &ps_r2_df_parallax_h, .0f, .5f);
	//	CMD4(CCC_Float,		"r2_parallax_range",	&ps_r2_df_parallax_range,	5.0f,	175.0f	);

	CMD4(CCC_Float, "r2_slight_fade", &ps_r2_slight_fade, .2f, 1.f);

	tw_min.set(0, 0, 0);
	tw_max.set(1, 1, 1);
	CMD4(CCC_Vector3, "r2_aa_break", &ps_r2_aa_barier, tw_min, tw_max);

	tw_min.set(0, 0, 0);
	tw_max.set(1, 1, 1);
	CMD4(CCC_Vector3, "r2_aa_weight", &ps_r2_aa_weight, tw_min, tw_max);

	//	Igor: Depth of field
	tw_min.set(-10000, -10000, 0);
	tw_max.set(10000, 10000, 10000);
	CMD4(CCC_Dof, "r2_dof", &ps_r2_dof, tw_min, tw_max);
	CMD4(CCC_DofNear, "r2_dof_near", &ps_r2_dof.x, tw_min.x, tw_max.x);
	CMD4(CCC_DofFocus, "r2_dof_focus", &ps_r2_dof.y, tw_min.y, tw_max.y);
	CMD4(CCC_DofFar, "r2_dof_far", &ps_r2_dof.z, tw_min.z, tw_max.z);

	CMD4(CCC_Float, "r2_dof_radius", &ps_r2_dof_kernel_size, .05f, 1.f);
	CMD4(CCC_Float, "r2_dof_sky", &ps_r2_dof_sky, -10000.f, 10000.f);

	ps_r2_ls_flags.set(R2FLAG_DOF, FALSE);
	CMD3(CCC_Mask, "r2_dof_enable", &ps_r2_ls_flags, R2FLAG_DOF);

	//	float		ps_r2_dof_near			= 0.f;					// 0.f
	//	float		ps_r2_dof_focus			= 1.4f;					// 1.4f

	CMD3(CCC_Mask, "r2_volumetric_lights", &ps_r2_ls_flags, R2FLAG_VOLUMETRIC_LIGHTS);
	//	CMD3(CCC_Mask,		"r2_sun_shafts",				&ps_r2_ls_flags,			R2FLAG_SUN_SHAFTS);
	CMD3(CCC_Token, "r2_sunshafts_quality", &ps_r_sun_shafts, qsun_shafts_token);
	CMD3(CCC_SSAO_Mode, "r2_ssao_mode", &ps_r_ssao_mode, qssao_mode_token);
	CMD3(CCC_Token, "r2_ssao", &ps_r_ssao, qssao_token);
	CMD3(CCC_Mask, "r2_ssao_blur", &ps_r2_ls_flags_ext, R2FLAGEXT_SSAO_BLUR); //Need restart
	CMD3(CCC_Mask, "r2_ssao_opt_data", &ps_r2_ls_flags_ext, R2FLAGEXT_SSAO_OPT_DATA); //Need restart
	CMD3(CCC_Mask, "r2_ssao_half_data", &ps_r2_ls_flags_ext, R2FLAGEXT_SSAO_HALF_DATA); //Need restart
	CMD3(CCC_Mask, "r2_ssao_hbao", &ps_r2_ls_flags_ext, R2FLAGEXT_SSAO_HBAO); //Need restart
	CMD3(CCC_Mask, "r2_ssao_hdao", &ps_r2_ls_flags_ext, R2FLAGEXT_SSAO_HDAO); //Need restart
	CMD3(CCC_Mask, "r4_enable_tessellation", &ps_r2_ls_flags_ext, R2FLAGEXT_ENABLE_TESSELLATION); //Need restart
	CMD3(CCC_Mask, "r4_wireframe", &ps_r2_ls_flags_ext, R2FLAGEXT_WIREFRAME); //Need restart
	CMD3(CCC_Mask, "r2_steep_parallax", &ps_r2_ls_flags, R2FLAG_STEEP_PARALLAX);
	CMD3(CCC_Mask, "r2_detail_bump", &ps_r2_ls_flags, R2FLAG_DETAIL_BUMP);

	CMD3(CCC_Token, "r2_sun_quality", &ps_r_sun_quality, qsun_quality_token);

	//	Igor: need restart
	CMD3(CCC_Mask, "r2_soft_water", &ps_r2_ls_flags, R2FLAG_SOFT_WATER);
	CMD3(CCC_Mask, "r2_soft_particles", &ps_r2_ls_flags, R2FLAG_SOFT_PARTICLES);

	//CMD3(CCC_Mask,		"r3_msaa",						&ps_r2_ls_flags,			R3FLAG_MSAA);
	CMD3(CCC_Token, "r3_msaa", &ps_r3_msaa, qmsaa_token);
	//CMD3(CCC_Mask,		"r3_msaa_hybrid",				&ps_r2_ls_flags,			R3FLAG_MSAA_HYBRID);
	//CMD3(CCC_Mask,		"r3_msaa_opt",					&ps_r2_ls_flags,			R3FLAG_MSAA_OPT);

	CMD3(CCC_Mask, "r3_use_dx10_1", &ps_r2_ls_flags, (u32)R3FLAG_USE_DX10_1);
	//CMD3(CCC_Mask,		"r3_msaa_alphatest",			&ps_r2_ls_flags,			(u32)R3FLAG_MSAA_ALPHATEST);
	CMD3(CCC_Token, "r3_msaa_alphatest", &ps_r3_msaa_atest, qmsaa__atest_token);
	CMD3(CCC_Token, "r3_minmax_sm", &ps_r3_minmax_sm, qminmax_sm_token);

#ifdef DETAIL_RADIUS
	CMD4(CCC_detail_radius, "r__detail_radius", &ps_r__detail_radius, 0, 250);
	CMD3(CCC_Mask, "r__clear_models_on_unload", &psDeviceFlags2, rsClearModels); //Alundaio
	CMD3(CCC_Mask, "r__use_precompiled_shaders", &psDeviceFlags2, rsPrecompiledShaders); //Alundaio
	CMD3(CCC_Mask, "r__enable_grass_shadow", &psDeviceFlags2, rsGrassShadow); //Alundaio
	CMD3(CCC_Mask, "r__no_scale_on_fade", &psDeviceFlags2, rsNoScale); //Alundaio
#endif

	//	Allow real-time fog config reload
#if	(RENDER == R_R3) || (RENDER == R_R4)
#ifdef	DEBUG
	CMD1(CCC_Fog_Reload,"r3_fog_reload");
#endif	//	DEBUG
#endif	//	(RENDER == R_R3) || (RENDER == R_R4)

	CMD3(CCC_Mask, "r3_dynamic_wet_surfaces", &ps_r2_ls_flags, R3FLAG_DYN_WET_SURF);
	CMD4(CCC_Float, "r3_dynamic_wet_surfaces_near", &ps_r3_dyn_wet_surf_near, 10, 70);
	CMD4(CCC_Float, "r3_dynamic_wet_surfaces_far", &ps_r3_dyn_wet_surf_far, 30, 100);
	CMD4(CCC_Integer, "r3_dynamic_wet_surfaces_sm_res", &ps_r3_dyn_wet_surf_sm_res, 64, 2048);

	CMD3(CCC_Mask, "r3_volumetric_smoke", &ps_r2_ls_flags, R3FLAG_VOLUMETRIC_SMOKE);
	CMD1(CCC_memory_stats, "render_memory_stats");


	//	CMD3(CCC_Mask,		"r2_sun_ignore_portals",		&ps_r2_ls_flags,			R2FLAG_SUN_IGNORE_PORTALS);
}
#endif
