#include "stdafx.h"
#pragma hdrstop

#pragma warning(push)
#pragma warning(disable:4995)
#include <d3dx9.h>
#pragma warning(pop)

#include "ResourceManager.h"
#include "blenders\Blender_Recorder.h"
#include "blenders\Blender.h"

#include "../../xrEngine/igame_persistent.h"
#include "../../xrEngine/environment.h"

#include "dxRenderDeviceRender.h"

// matrices
#define	BIND_DECLARE(xf)	\
class cl_xform_##xf	: public R_constant_setup {	virtual void setup (R_constant* C) { RCache.xforms.set_c_##xf (C); } }; \
	static cl_xform_##xf	binder_##xf
BIND_DECLARE(w);
BIND_DECLARE(invw);
BIND_DECLARE(v);
BIND_DECLARE(p);
BIND_DECLARE(wv);
BIND_DECLARE(vp);
BIND_DECLARE(wvp);

#define DECLARE_TREE_BIND(c)	\
	class cl_tree_##c: public R_constant_setup	{virtual void setup(R_constant* C) {RCache.tree.set_c_##c(C);} };	\
	static cl_tree_##c	tree_binder_##c

DECLARE_TREE_BIND(m_xform_v);
DECLARE_TREE_BIND(m_xform);
DECLARE_TREE_BIND(consts);
DECLARE_TREE_BIND(wave);
DECLARE_TREE_BIND(wind);
DECLARE_TREE_BIND(c_scale);
DECLARE_TREE_BIND(c_bias);
DECLARE_TREE_BIND(c_sun);

class cl_hemi_cube_pos_faces : public R_constant_setup
{
	virtual void setup(R_constant* C) { RCache.hemi.set_c_pos_faces(C); }
};

static cl_hemi_cube_pos_faces binder_hemi_cube_pos_faces;

class cl_hemi_cube_neg_faces : public R_constant_setup
{
	virtual void setup(R_constant* C) { RCache.hemi.set_c_neg_faces(C); }
};

static cl_hemi_cube_neg_faces binder_hemi_cube_neg_faces;

class cl_material : public R_constant_setup
{
	virtual void setup(R_constant* C) { RCache.hemi.set_c_material(C); }
};

static cl_material binder_material;

class cl_texgen : public R_constant_setup
{
	virtual void setup(R_constant* C)
	{
		Fmatrix mTexgen;

#if defined(USE_DX10) || defined(USE_DX11)
		Fmatrix mTexelAdjust =
		{
			0.5f, 0.0f, 0.0f, 0.0f,
			0.0f, -0.5f, 0.0f, 0.0f,
			0.0f, 0.0f, 1.0f, 0.0f,
			0.5f, 0.5f, 0.0f, 1.0f
		};
#else	//	USE_DX10
		float _w = float(RDEVICE.dwWidth);
		float _h = float(RDEVICE.dwHeight);
		float o_w = (.5f / _w);
		float o_h = (.5f / _h);
		Fmatrix mTexelAdjust =
		{
			0.5f, 0.0f, 0.0f, 0.0f,
			0.0f, -0.5f, 0.0f, 0.0f,
			0.0f, 0.0f, 1.0f, 0.0f,
			0.5f + o_w, 0.5f + o_h, 0.0f, 1.0f
		};
#endif	//	USE_DX10

		mTexgen.mul(mTexelAdjust, RCache.xforms.m_wvp);

		RCache.set_c(C, mTexgen);
	}
};

static cl_texgen binder_texgen;

class cl_VPtexgen : public R_constant_setup
{
	virtual void setup(R_constant* C)
	{
		Fmatrix mTexgen;

#if defined(USE_DX10) || defined(USE_DX11)
		Fmatrix mTexelAdjust =
		{
			0.5f, 0.0f, 0.0f, 0.0f,
			0.0f, -0.5f, 0.0f, 0.0f,
			0.0f, 0.0f, 1.0f, 0.0f,
			0.5f, 0.5f, 0.0f, 1.0f
		};
#else	//	USE_DX10
		float _w = float(RDEVICE.dwWidth);
		float _h = float(RDEVICE.dwHeight);
		float o_w = (.5f / _w);
		float o_h = (.5f / _h);
		Fmatrix mTexelAdjust =
		{
			0.5f, 0.0f, 0.0f, 0.0f,
			0.0f, -0.5f, 0.0f, 0.0f,
			0.0f, 0.0f, 1.0f, 0.0f,
			0.5f + o_w, 0.5f + o_h, 0.0f, 1.0f
		};
#endif	//	USE_DX10

		mTexgen.mul(mTexelAdjust, RCache.xforms.m_vp);

		RCache.set_c(C, mTexgen);
	}
};

static cl_VPtexgen binder_VPtexgen;

// fog
#ifndef _EDITOR
class cl_fog_plane : public R_constant_setup
{
	u32 marker;
	Fvector4 result;

	virtual void setup(R_constant* C)
	{
		if (marker != Device.dwFrame)
		{
			// Plane
			Fvector4 plane;
			Fmatrix& M = Device.mFullTransform;
			plane.x = -(M._14 + M._13);
			plane.y = -(M._24 + M._23);
			plane.z = -(M._34 + M._33);
			plane.w = -(M._44 + M._43);
			float denom = -1.0f / _sqrt(_sqr(plane.x) + _sqr(plane.y) + _sqr(plane.z));
			plane.mul(denom);

			// Near/Far
			float A = g_pGamePersistent->Environment().CurrentEnv->fog_near;
			float B = 1 / (g_pGamePersistent->Environment().CurrentEnv->fog_far - A);
			result.set(-plane.x * B, -plane.y * B, -plane.z * B, 1 - (plane.w - A) * B); // view-plane
		}
		RCache.set_c(C, result);
	}
};

static cl_fog_plane binder_fog_plane;

// fog-params
class cl_fog_params : public R_constant_setup
{
	u32 marker;
	Fvector4 result;

	virtual void setup(R_constant* C)
	{
		if (marker != Device.dwFrame)
		{
			// Near/Far
			float n = g_pGamePersistent->Environment().CurrentEnv->fog_near;
			float f = g_pGamePersistent->Environment().CurrentEnv->fog_far;
			float r = 1 / (f - n);
			result.set(-n * r, n, f, r);
		}
		RCache.set_c(C, result);
	}
};

static cl_fog_params binder_fog_params;

// fog-color
class cl_fog_color : public R_constant_setup
{
	u32 marker;
	Fvector4 result;

	virtual void setup(R_constant* C)
	{
		if (marker != Device.dwFrame)
		{
			CEnvDescriptor& desc = *g_pGamePersistent->Environment().CurrentEnv;
			result.set(desc.fog_color.x, desc.fog_color.y, desc.fog_color.z, desc.fog_density);
		}
		RCache.set_c(C, result);
	}
};

static cl_fog_color binder_fog_color;

static class cl_wind_params : public R_constant_setup
{
	u32 marker;
	Fvector4 result;

	virtual void setup(R_constant* C)
	{
		if (marker != Device.dwFrame)
		{
			CEnvDescriptor& E = *g_pGamePersistent->Environment().CurrentEnv;
			result.set(E.wind_direction, E.wind_velocity, 0.0f, 0.0f);
		}
		RCache.set_c(C, result);
	}
} binder_wind_params;

#endif


// times
class cl_times : public R_constant_setup
{
	virtual void setup(R_constant* C)
	{
		float t = RDEVICE.fTimeGlobal;
		RCache.set_c(C, t, t * 10, t / 10, _sin(t));
	}
};

static cl_times binder_times;

// game time
class cl_game_times : public R_constant_setup
{
	virtual void setup(R_constant* C)
	{
		float t = g_pGamePersistent->Environment().GetGameTime();
		RCache.set_c(C, t, t / DAY_LENGTH, t / (DAY_LENGTH / 24), floor(t / (DAY_LENGTH / 24)));
	}
};

static cl_game_times binder_game_times;

// eye-params
class cl_eye_P : public R_constant_setup
{
	virtual void setup(R_constant* C)
	{
		Fvector& V = RDEVICE.vCameraPosition;
		RCache.set_c(C, V.x, V.y, V.z, 1);
	}
};
static cl_eye_P binder_eye_P;

// interpolated eye position (crookr scope parallax)
// We can improve this by clamping the magnitude of the travel here instead of in-shader. 
// it would fix the issue with the fog "sticking" when moving too far off center
extern float scope_fog_interp;
extern float scope_fog_travel;
class cl_eye_PL : public R_constant_setup
{
	Fvector tV;
	virtual void setup(R_constant* C)
	{
		Fvector& V = RDEVICE.vCameraPosition;
		tV = tV.lerp(tV, V, scope_fog_interp);

		RCache.set_c(C, tV.x, tV.y, tV.z, 1);
	}
};
static cl_eye_PL binder_eye_PL;

// eye-params
class cl_eye_D : public R_constant_setup
{
	virtual void setup(R_constant* C)
	{
		Fvector& V = RDEVICE.vCameraDirection;
		RCache.set_c(C, V.x, V.y, V.z, 0);
	}
};
static cl_eye_D binder_eye_D;

// interpolated eye direction (crookr scope parallax)
class cl_eye_DL : public R_constant_setup
{
	Fvector tV;
	virtual void setup(R_constant* C)
	{
		Fvector& V = RDEVICE.vCameraDirection;
		tV = tV.lerp(tV, V, scope_fog_interp);

		RCache.set_c(C, tV.x, tV.y, tV.z, 0);
	}
};
static cl_eye_DL binder_eye_DL;

// eye-params
class cl_eye_N : public R_constant_setup
{
	virtual void setup(R_constant* C)
	{
		Fvector& V = RDEVICE.vCameraTop;
		RCache.set_c(C, V.x, V.y, V.z, 0);
	}
};

static cl_eye_N binder_eye_N;


// fake scope params (crookr)
extern float scope_outerblur;
extern float scope_innerblur;
extern float scope_scrollpower;
extern float scope_brightness;
class cl_fakescope_params : public R_constant_setup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, scope_scrollpower, scope_innerblur, scope_outerblur, scope_brightness);
	}
};
static cl_fakescope_params binder_fakescope_params;

extern float scope_ca;
extern float scope_fog_attack;
extern float scope_fog_mattack;
//extern float scope_fog_travel;
class cl_fakescope_ca : public R_constant_setup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, scope_ca, scope_fog_attack, scope_fog_mattack, scope_fog_travel);
	}
};
static cl_fakescope_ca binder_fakescope_ca;

extern float scope_radius;
extern float scope_fog_radius;
extern float scope_fog_sharp;
//extern float scope_drift_amount;
class cl_fakescope_params3 : public R_constant_setup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, scope_radius, scope_fog_radius, scope_fog_sharp, 0.0f);
	}
};
static cl_fakescope_params3 binder_fakescope_params3;

// Mark Switch
extern int ps_markswitch_current;
extern int ps_markswitch_count;
extern Fvector4 ps_markswitch_color;

static class markswitch_current : public R_constant_setup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, ps_markswitch_current, 0, 0, 0);
	}
}    markswitch_current;

static class markswitch_count : public R_constant_setup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, ps_markswitch_count, 0, 0, 0);
	}
}    markswitch_count;

static class markswitch_color : public R_constant_setup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, ps_markswitch_color.x, ps_markswitch_color.y, ps_markswitch_color.z, ps_markswitch_color.w);
	}
}    markswitch_color;

//--DSR-- SilencerOverheat_start
static class cl_silencer_glowing : public R_constant_setup
{
	virtual void setup(R_constant* C)
	{
		RCache.hemi.set_c_glowing(C);
	}
} binder_silencer_glowing;
//--DSR-- SilencerOverheat_end

//--DSR-- HeatVision_start
extern float heat_vision_mode;
extern Fvector4 heat_vision_steps;
extern Fvector4 heat_vision_blurring;
extern Fvector4 heat_vision_args_1;
extern Fvector4 heat_vision_args_2;

static class cl_heatvision_hotness : public R_constant_setup
{
	virtual void setup(R_constant* C)
	{
		RCache.hemi.set_c_hotness(C);
	}
} binder_heatvision_hotness;

static class cl_heatvision_steps : public R_constant_setup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, ps_r2_heatvision, heat_vision_steps.x, heat_vision_steps.y, heat_vision_steps.z);
	}
} binder_heatvision_params1;

static class cl_heatvision_blurring : public R_constant_setup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, heat_vision_blurring.x, heat_vision_blurring.y, heat_vision_blurring.z, heat_vision_mode);
	}
} binder_heatvision_params2;

static class cl_heatvision_args1 : public R_constant_setup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, heat_vision_args_1.x, heat_vision_args_1.y, heat_vision_args_1.z, heat_vision_args_1.w);
	}
} binder_heatvision_args1;

static class cl_heatvision_args2 : public R_constant_setup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, heat_vision_args_2.x, heat_vision_args_2.y, heat_vision_args_2.z, heat_vision_args_2.w);
	}
} binder_heatvision_args2;

//--DSR-- HeatVision_end


#ifndef _EDITOR
// D-Light0
class cl_sun0_color : public R_constant_setup
{
	u32 marker;
	Fvector4 result;

	virtual void setup(R_constant* C)
	{
		if (marker != Device.dwFrame)
		{
			CEnvDescriptor& desc = *g_pGamePersistent->Environment().CurrentEnv;
			#if RENDER==R_R1 //Lumscale control for R1
				result.set(desc.sun_color.x * ps_r2_sun_lumscale, desc.sun_color.y * ps_r2_sun_lumscale, desc.sun_color.z * ps_r2_sun_lumscale, 0);
			#else
				result.set(desc.sun_color.x, desc.sun_color.y, desc.sun_color.z, 0);
			#endif
		}
		RCache.set_c(C, result);
	}
};

static cl_sun0_color binder_sun0_color;

class cl_sun0_dir_w : public R_constant_setup
{
	u32 marker;
	Fvector4 result;

	virtual void setup(R_constant* C)
	{
		if (marker != Device.dwFrame)
		{
			CEnvDescriptor& desc = *g_pGamePersistent->Environment().CurrentEnv;
			result.set(desc.sun_dir.x, desc.sun_dir.y, desc.sun_dir.z, 0);
		}
		RCache.set_c(C, result);
	}
};

static cl_sun0_dir_w binder_sun0_dir_w;

class cl_sun0_dir_e : public R_constant_setup
{
	u32 marker;
	Fvector4 result;

	virtual void setup(R_constant* C)
	{
		if (marker != Device.dwFrame)
		{
			Fvector D;
			CEnvDescriptor& desc = *g_pGamePersistent->Environment().CurrentEnv;
			Device.mView.transform_dir(D, desc.sun_dir);
			D.normalize();
			result.set(D.x, D.y, D.z, 0);
		}
		RCache.set_c(C, result);
	}
};

static cl_sun0_dir_e binder_sun0_dir_e;

//
class cl_amb_color : public R_constant_setup
{
	u32 marker;
	Fvector4 result;

	virtual void setup(R_constant* C)
	{
		if (marker != Device.dwFrame)
		{
			CEnvDescriptorMixer& desc = *g_pGamePersistent->Environment().CurrentEnv;
			result.set(desc.ambient.x, desc.ambient.y, desc.ambient.z, desc.weight);
		}
		RCache.set_c(C, result);
	}
};

static cl_amb_color binder_amb_color;

class cl_hemi_color : public R_constant_setup
{
	u32 marker;
	Fvector4 result;

	virtual void setup(R_constant* C)
	{
		if (marker != Device.dwFrame)
		{
			CEnvDescriptor& desc = *g_pGamePersistent->Environment().CurrentEnv;
			result.set(desc.hemi_color.x, desc.hemi_color.y, desc.hemi_color.z, desc.hemi_color.w);
		}
		RCache.set_c(C, result);
	}
};

static cl_hemi_color binder_hemi_color;

class cl_sky_color : public R_constant_setup
{
	u32 marker;
	Fvector4 result;

	virtual void setup(R_constant* C)
	{
		if (marker != Device.dwFrame)
		{
			CEnvDescriptor& desc = *g_pGamePersistent->Environment().CurrentEnv;
			result.set(desc.sky_color.x, desc.sky_color.y, desc.sky_color.z, desc.sky_rotation);
		}
		RCache.set_c(C, result);
	}
};
static cl_sky_color binder_sky_color;
#endif

static class cl_screen_res : public R_constant_setup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, (float)RDEVICE.dwWidth, (float)RDEVICE.dwHeight, 1.0f / (float)RDEVICE.dwWidth,
		             1.0f / (float)RDEVICE.dwHeight);
	}
} binder_screen_res;

static class cl_screen_params : public R_constant_setup
{
	Fvector4 result;

	virtual void setup(R_constant* C)
	{
		float fov = float(Device.fFOV);
		float aspect = float(Device.fASPECT);
		result.set(fov, aspect, tan(deg2rad(fov) / 2), g_pGamePersistent->Environment().CurrentEnv->far_plane * 0.75f);
		RCache.set_c(C, result);
	}
};

static cl_screen_params binder_screen_params;

static class cl_hud_params : public R_constant_setup //--#SM+#--
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, g_pGamePersistent->m_pGShaderConstants->hud_params);
	}
}	binder_hud_params;

static class cl_script_params : public R_constant_setup //--#SM+#--
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, g_pGamePersistent->m_pGShaderConstants->m_script_params);
	}
}	binder_script_params;

static class cl_blend_mode : public R_constant_setup //--#SM+#--
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, g_pGamePersistent->m_pGShaderConstants->m_blender_mode);
	}
}	binder_blend_mode;

static class cl_rain_params : public R_constant_setup
{
	u32 marker;
	Fvector4 result;

	virtual void setup(R_constant* C)
	{
		float rainDensity = g_pGamePersistent->Environment().CurrentEnv->rain_density;
		float rainWetness = g_pGamePersistent->Environment().wetness_factor;

		RCache.set_c(C, rainDensity, rainWetness, 0.0f, 0.0f);
	}
} binder_rain_params;

static class cl_actor_params : public R_constant_setup
{
	u32 marker;
	Fvector4 result;

	virtual void setup(R_constant* C)
	{
		float actorHealth = g_pGamePersistent->actor_data.health;
		float actorStamina = g_pGamePersistent->actor_data.stamina;
		float actorBleeding = g_pGamePersistent->actor_data.bleeding;
		int actorHelmet = g_pGamePersistent->actor_data.helmet;

		RCache.set_c(C, actorHealth, actorStamina, actorBleeding, actorHelmet);
	}
} binder_actor_data;

static class cl_inv_v : public R_constant_setup
{
	u32	marker;
	Fmatrix	result;

	virtual void setup(R_constant* C)
	{
		result.invert(Device.mView);

		RCache.set_c(C, result);
	}
} binder_inv_v;

static class pp_image_corrections : public R_constant_setup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, ps_r2_img_exposure, ps_r2_img_gamma, ps_r2_img_saturation, 1);
	}
} pp_image_corrections;

static class pp_color_grading : public R_constant_setup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, ps_r2_img_cg.x, ps_r2_img_cg.y, ps_r2_img_cg.z, 1);
	}
} pp_color_grading;

static class cl_pda_params : public R_constant_setup
{
	u32 marker;
	Fvector4 result;

	virtual void setup(R_constant* C)
	{
		float pda_factor = g_pGamePersistent->pda_shader_data.pda_display_factor;
		float pda_psy_factor = g_pGamePersistent->pda_shader_data.pda_psy_influence;
		float pda_display_brightness = g_pGamePersistent->pda_shader_data.pda_displaybrightness;
		RCache.set_c(C, pda_factor, pda_psy_factor, pda_display_brightness, 0.0f);
	}

} binder_pda_params;

static class cl_near_far_plane : public R_constant_setup
{
	Fvector4 result;

	virtual void setup(R_constant* C)
	{
		float nearPlane = float(VIEWPORT_NEAR);
		float farPlane = g_pGamePersistent->Environment().CurrentEnv->far_plane;
		result.set(nearPlane, farPlane, 0, 0);
		RCache.set_c(C, result);
	}
} binder_near_far_plane;

// Screen Space Shaders Stuff
extern float ps_ssfx_hud_hemi;
extern Fvector4 ps_ssfx_il;
extern Fvector4 ps_ssfx_il_setup1;
extern Fvector4 ps_ssfx_ao;
extern Fvector4 ps_ssfx_ao_setup1;
extern Fvector4 ps_ssfx_water;
extern Fvector4 ps_ssfx_water_setup1;
extern Fvector4 ps_ssfx_water_setup2;

extern Fvector4 ps_ssfx_volumetric;
extern Fvector4 ps_ssfx_ssr_2;
extern Fvector4 ps_ssfx_terrain_offset;

extern Fvector3 ps_ssfx_shadow_bias;
extern Fvector4 ps_ssfx_lut;
extern Fvector4 ps_ssfx_wind_grass;
extern Fvector4 ps_ssfx_wind_trees;

extern Fvector4 ps_ssfx_florafixes_1;
extern Fvector4 ps_ssfx_florafixes_2;

extern float ps_ssfx_gloss_factor;
extern Fvector3 ps_ssfx_gloss_minmax;

extern Fvector4 ps_ssfx_wetsurfaces_1;
extern Fvector4 ps_ssfx_wetsurfaces_2;

extern int ps_ssfx_is_underground;
extern Fvector4 ps_ssfx_lightsetup_1;
extern Fvector4 ps_ssfx_hud_drops_1;
extern Fvector4 ps_ssfx_hud_drops_2;
extern Fvector4 ps_ssfx_blood_decals;
extern Fvector4 ps_ssfx_wpn_dof_1;
extern float ps_ssfx_wpn_dof_2;

//Sneaky debug stuff
extern Fvector4 ps_dev_param_1;
extern Fvector4 ps_dev_param_2;
extern Fvector4 ps_dev_param_3;
extern Fvector4 ps_dev_param_4;
extern Fvector4 ps_dev_param_5;
extern Fvector4 ps_dev_param_6;
extern Fvector4 ps_dev_param_7;
extern Fvector4 ps_dev_param_8;

static class dev_param_1 : public R_constant_setup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, ps_dev_param_1.x, ps_dev_param_1.y, ps_dev_param_1.z, ps_dev_param_1.w);
	}
}    dev_param_1;

static class dev_param_2 : public R_constant_setup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, ps_dev_param_2.x, ps_dev_param_2.y, ps_dev_param_2.z, ps_dev_param_2.w);
	}
}    dev_param_2;

static class dev_param_3 : public R_constant_setup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, ps_dev_param_3.x, ps_dev_param_3.y, ps_dev_param_3.z, ps_dev_param_3.w);
	}
}    dev_param_3;

static class dev_param_4 : public R_constant_setup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, ps_dev_param_4.x, ps_dev_param_4.y, ps_dev_param_4.z, ps_dev_param_4.w);
	}
}    dev_param_4;

static class dev_param_5 : public R_constant_setup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, ps_dev_param_5.x, ps_dev_param_5.y, ps_dev_param_5.z, ps_dev_param_5.w);
	}
}    dev_param_5;

static class dev_param_6 : public R_constant_setup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, ps_dev_param_6.x, ps_dev_param_6.y, ps_dev_param_6.z, ps_dev_param_6.w);
	}
}    dev_param_6;

static class dev_param_7 : public R_constant_setup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, ps_dev_param_7.x, ps_dev_param_7.y, ps_dev_param_7.z, ps_dev_param_7.w);
	}
}    dev_param_7;

static class dev_param_8 : public R_constant_setup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, ps_dev_param_8.x, ps_dev_param_8.y, ps_dev_param_8.z, ps_dev_param_8.w);
	}
}    dev_param_8;

static class ssfx_wpn_dof_1 : public R_constant_setup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, ps_ssfx_wpn_dof_1.x, ps_ssfx_wpn_dof_1.y, ps_ssfx_wpn_dof_1.z, ps_ssfx_wpn_dof_1.w);
	}
}    ssfx_wpn_dof_1;

static class ssfx_wpn_dof_2 : public R_constant_setup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, ps_ssfx_wpn_dof_2, 0, 0, 0);
	}
}    ssfx_wpn_dof_2;

static class ssfx_blood_decals : public R_constant_setup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, ps_ssfx_blood_decals);
	}
}    ssfx_blood_decals;

static class ssfx_hud_drops_1 : public R_constant_setup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, ps_ssfx_hud_drops_1);
	}
}    ssfx_hud_drops_1;

static class ssfx_hud_drops_2 : public R_constant_setup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, ps_ssfx_hud_drops_2);
	}
}    ssfx_hud_drops_2;

static class ssfx_lightsetup_1 : public R_constant_setup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, ps_ssfx_lightsetup_1);
	}
}    ssfx_lightsetup_1;

static class ssfx_is_underground : public R_constant_setup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, ps_ssfx_is_underground, 0, 0, 0);
	}
}    ssfx_is_underground;

static class ssfx_wetsurfaces_1 : public R_constant_setup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, ps_ssfx_wetsurfaces_1);
	}
}    ssfx_wetsurfaces_1;

static class ssfx_wetsurfaces_2 : public R_constant_setup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, ps_ssfx_wetsurfaces_2);
	}
}    ssfx_wetsurfaces_2;

static class ssfx_gloss : public R_constant_setup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, ps_ssfx_gloss_minmax.x, ps_ssfx_gloss_minmax.y, ps_ssfx_gloss_factor, 0);
	}
}    ssfx_gloss;

static class ssfx_florafixes_1 : public R_constant_setup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, ps_ssfx_florafixes_1);
	}
}    ssfx_florafixes_1;

static class ssfx_florafixes_2 : public R_constant_setup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, ps_ssfx_florafixes_2);
	}
}    ssfx_florafixes_2;

static class ssfx_wind_grass : public R_constant_setup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, ps_ssfx_wind_grass);
	}
}    ssfx_wind_grass;

static class ssfx_wind_trees : public R_constant_setup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, ps_ssfx_wind_trees);
	}
}    ssfx_wind_trees;

static class ssfx_wind_anim : public R_constant_setup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, g_pGamePersistent->Environment().wind_anim);
	}
}    ssfx_wind_anim;

static class ssfx_lut : public R_constant_setup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, ps_ssfx_lut);
	}
}    ssfx_lut;

static class ssfx_shadow_bias : public R_constant_setup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, ps_ssfx_shadow_bias.x, ps_ssfx_shadow_bias.y, 0, 0);
	}
}    ssfx_shadow_bias;

static class ssfx_terrain_offset : public R_constant_setup
{
	virtual void setup(R_constant * C)
	{
		RCache.set_c(C, ps_ssfx_terrain_offset);
	}
}    ssfx_terrain_offset;

static class ssfx_ssr_2 : public R_constant_setup
{
	virtual void setup(R_constant * C)
	{
		RCache.set_c(C, ps_ssfx_ssr_2);
	}
}    ssfx_ssr_2;

static class ssfx_volumetric : public R_constant_setup
{
	virtual void setup(R_constant * C)
	{
		RCache.set_c(C, ps_ssfx_volumetric);
	}
}    ssfx_volumetric;

static class ssfx_water : public R_constant_setup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, ps_ssfx_water);
	}
}    ssfx_water;

static class ssfx_water_setup1 : public R_constant_setup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, ps_ssfx_water_setup1);
	}
}    ssfx_water_setup1;

static class ssfx_water_setup2 : public R_constant_setup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, ps_ssfx_water_setup2);
	}
}    ssfx_water_setup2;

static class ssfx_ao : public R_constant_setup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, ps_ssfx_ao);
	}
}    ssfx_ao;

static class ssfx_ao_setup1 : public R_constant_setup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, ps_ssfx_ao_setup1);
	}
}    ssfx_ao_setup1;

static class ssfx_il : public R_constant_setup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, ps_ssfx_il);
	}
}    ssfx_il;

static class ssfx_il_setup1 : public R_constant_setup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, ps_ssfx_il_setup1);
	}
}    ssfx_il_setup1;

static class ssfx_hud_hemi : public R_constant_setup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, ps_ssfx_hud_hemi, 0, 0, 0);
	}
}    ssfx_hud_hemi;

static class ssfx_issvp : public R_constant_setup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, Device.m_SecondViewport.IsSVPFrame(), 0, 0, 0);
	}
}    ssfx_issvp;

// Standart constant-binding
void CBlender_Compile::SetMapping()
{
	// matrices
	r_Constant("m_W", &binder_w);
	r_Constant("m_invW", &binder_invw);
	r_Constant("m_V", &binder_v);
	r_Constant("m_P", &binder_p);
	r_Constant("m_WV", &binder_wv);
	r_Constant("m_VP", &binder_vp);
	r_Constant("m_WVP", &binder_wvp);
	r_Constant("m_inv_V", &binder_inv_v);

	r_Constant("m_xform_v", &tree_binder_m_xform_v);
	r_Constant("m_xform", &tree_binder_m_xform);
	r_Constant("consts", &tree_binder_consts);
	r_Constant("wave", &tree_binder_wave);
	r_Constant("wind", &tree_binder_wind);
	r_Constant("c_scale", &tree_binder_c_scale);
	r_Constant("c_bias", &tree_binder_c_bias);
	r_Constant("c_sun", &tree_binder_c_sun);

	//hemi cube
	r_Constant("L_material", &binder_material);
	r_Constant("hemi_cube_pos_faces", &binder_hemi_cube_pos_faces);
	r_Constant("hemi_cube_neg_faces", &binder_hemi_cube_neg_faces);

	//	Igor	temp solution for the texgen functionality in the shader
	r_Constant("m_texgen", &binder_texgen);
	r_Constant("mVPTexgen", &binder_VPtexgen);

#ifndef _EDITOR
	// fog-params
	r_Constant("fog_plane", &binder_fog_plane);
	r_Constant("fog_params", &binder_fog_params);
	r_Constant("fog_color", &binder_fog_color);
	r_Constant("wind_params", &binder_wind_params);
#endif
	// time
	r_Constant("timers", &binder_times);
	r_Constant("timers_game", &binder_game_times);

	// eye-params
	r_Constant("eye_position", &binder_eye_P);
	r_Constant("eye_position_lerp", &binder_eye_PL); // crookr
	r_Constant("eye_direction", &binder_eye_D);
	r_Constant("eye_direction_lerp", &binder_eye_DL); // crookr
	r_Constant("eye_normal", &binder_eye_N);

#ifndef _EDITOR
	// global-lighting (env params)
	r_Constant("L_sun_color", &binder_sun0_color);
	r_Constant("L_sun_dir_w", &binder_sun0_dir_w);
	r_Constant("L_sun_dir_e", &binder_sun0_dir_e);
	//	r_Constant				("L_lmap_color",	&binder_lm_color);
	r_Constant("L_hemi_color", &binder_hemi_color);
	r_Constant("L_ambient", &binder_amb_color);
#endif

	r_Constant("screen_res", &binder_screen_res);
	r_Constant("ogse_c_screen", &binder_screen_params);
	r_Constant("near_far_plane", &binder_near_far_plane);
	// misc
	r_Constant("m_hud_params", &binder_hud_params);	//--#SM+#--
	r_Constant("m_script_params", &binder_script_params); //--#SM+#--
	r_Constant("m_blender_mode", &binder_blend_mode);	//--#SM+#--
	
	// Rain
	r_Constant("rain_params", &binder_rain_params);
	//Actor data
	r_Constant("actor_data", &binder_actor_data);
	//Image corrections
	r_Constant("pp_img_corrections", &pp_image_corrections);
	//Image corrections
	r_Constant("pp_img_cg", &pp_color_grading);
	
	// detail
	//if (bDetail	&& detail_scaler)
	//	Igor: bDetail can be overridden by no_detail_texture option.
	//	But shader can be deatiled implicitly, so try to set this parameter
	//	anyway.
	if (detail_scaler)
		r_Constant("dt_params", detail_scaler);

	// PDA
	r_Constant("pda_params", &binder_pda_params);

	// Screen Space Shaders
	r_Constant("ssfx_issvp", &ssfx_issvp);
	r_Constant("ssfx_hud_hemi", &ssfx_hud_hemi);
	r_Constant("ssfx_il_setup", &ssfx_il);
	r_Constant("ssfx_il_setup2", &ssfx_il_setup1);
	r_Constant("ssfx_ao_setup", &ssfx_ao);
	r_Constant("ssfx_ao_setup2", &ssfx_ao_setup1);
	r_Constant("ssfx_water", &ssfx_water);
	r_Constant("ssfx_water_setup1", &ssfx_water_setup1);
	r_Constant("ssfx_water_setup2", &ssfx_water_setup2);

	r_Constant("ssfx_volumetric", &ssfx_volumetric);
	r_Constant("ssfx_ssr_2", &ssfx_ssr_2);
	r_Constant("ssfx_terrain_offset", &ssfx_terrain_offset);
	r_Constant("ssfx_shadow_bias", &ssfx_shadow_bias);
	r_Constant("ssfx_wind_anim", &ssfx_wind_anim);
	r_Constant("sky_color", &binder_sky_color);
	r_Constant("ssfx_wpn_dof_1", &ssfx_wpn_dof_1);
	r_Constant("ssfx_wpn_dof_2", &ssfx_wpn_dof_2);
	r_Constant("ssfx_blood_decals", &ssfx_blood_decals);
	r_Constant("ssfx_hud_drops_1", &ssfx_hud_drops_1);
	r_Constant("ssfx_hud_drops_2", &ssfx_hud_drops_2);
	r_Constant("ssfx_lightsetup_1", &ssfx_lightsetup_1);
	r_Constant("ssfx_is_underground", &ssfx_is_underground);
	r_Constant("ssfx_wetsurfaces_1", &ssfx_wetsurfaces_1);
	r_Constant("ssfx_wetsurfaces_2", &ssfx_wetsurfaces_2);
	r_Constant("ssfx_gloss", &ssfx_gloss);
	r_Constant("ssfx_florafixes_1", &ssfx_florafixes_1);
	r_Constant("ssfx_florafixes_2", &ssfx_florafixes_2);
	r_Constant("ssfx_wsetup_grass", &ssfx_wind_grass);
	r_Constant("ssfx_wsetup_trees", &ssfx_wind_trees);
	r_Constant("ssfx_lut", &ssfx_lut);

	// Shader stuff
	r_Constant("shader_param_1", &dev_param_1);
	r_Constant("shader_param_2", &dev_param_2);
	r_Constant("shader_param_3", &dev_param_3);
	r_Constant("shader_param_4", &dev_param_4);
	r_Constant("shader_param_5", &dev_param_5);
	r_Constant("shader_param_6", &dev_param_6);
	r_Constant("shader_param_7", &dev_param_7);
	r_Constant("shader_param_8", &dev_param_8);
	
	// Mark Switch
	r_Constant("markswitch_current", &markswitch_current);
	r_Constant("markswitch_count", &markswitch_count);
	r_Constant("markswitch_color", &markswitch_color);

	// crookr
	r_Constant("fakescope_params1", &binder_fakescope_params);
	r_Constant("fakescope_params2", &binder_fakescope_ca);
	r_Constant("fakescope_params3", &binder_fakescope_params3);

	// other common
	for (u32 it = 0; it < DEV->v_constant_setup.size(); it++)
	{
		std::pair<shared_str, R_constant_setup*> cs = DEV->v_constant_setup[it];
		r_Constant(*cs.first, cs.second);
	}


	r_Constant("L_glowing", &binder_silencer_glowing);		//--DSR-- SilencerOverheat
	//--DSR-- HeatVision_start
	r_Constant("L_hotness", &binder_heatvision_hotness);
	r_Constant("heatvision_params1", &binder_heatvision_params1);
	r_Constant("heatvision_params2", &binder_heatvision_params2);
	r_Constant("heatvision_params3", &binder_heatvision_args1);
	r_Constant("heatvision_params4", &binder_heatvision_args2);
	//--DSR-- HeatVision_end
}
