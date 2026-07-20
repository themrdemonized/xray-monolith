#include "stdafx.h"
#pragma hdrstop

#include "ResourceManager.h"
#include "blenders\Blender_Recorder.h"
#include "blenders\Blender.h"

#include "../../xrEngine/igame_persistent.h"
#include "../../xrEngine/environment.h"

#include "dxRenderDeviceRender.h"

#include "svp_constants.h"

// pip intent flags for the patched 3DSS shaders, x/y/z = strip parallax shadow / chromatism /
// nvg blur under true PiP, the per-image-type judgment lives in the shader
static class svp_control_binder : public R_constant_setup
{
	virtual void setup(R_constant* C)
	{
		extern int ps_r__svp_clean_optics;
		extern int ps_r__svp_thermal_sim;
		extern int ps_r__svp_chroma;
		const float k = (ps_r__svp_clean_optics && Device.true_pip_on) ? 1.f : 0.f;
		// chromatism kept when r__svp_chroma is on so real per-scope CA survives clean optics
		const float ky = (ps_r__svp_clean_optics && !ps_r__svp_chroma && Device.true_pip_on) ? 1.f : 0.f;
		const float ts = (ps_r__svp_thermal_sim && Device.true_pip_on) ? 1.f : 0.f;
		RCache.set_c(C, k, ky, k, ts);
	}
} binder_svp_control;

// glass optics tunables, x = illuminated reticle washout, y = field curvature edge softness, z = ACOG fiber sun mode
static class svp_glass_binder : public R_constant_setup
{
	virtual void setup(R_constant* C)
	{
		extern float ps_r__svp_reticle_washout;
		extern float ps_r__svp_field_curve;
		extern int ps_r__svp_acog_fiber;
		extern int ps_r__svp_autoflip_reticle;
		if (Device.true_pip_on)
			RCache.set_c(C, ps_r__svp_reticle_washout, ps_r__svp_field_curve, ps_r__svp_acog_fiber ? 1.f : 0.f, ps_r__svp_autoflip_reticle ? 1.f : 0.f);
		else
			RCache.set_c(C, 0.f, 0.f, 0.f, 0.f);
	}
} binder_svp_glass;

// near-blur composite selector, x = scatter mode (0 gather default, 1 scatter accumulator)
static class svp_nearblur_binder : public R_constant_setup
{
	virtual void setup(R_constant* C)
	{
		extern int ps_r__svp_nearblur_scatter;
		RCache.set_c(C, ps_r__svp_nearblur_scatter ? 1.f : 0.f, 0.f, 0.f, 0.f);
	}
} binder_svp_nearblur;

// glass environment data, x = veiling glare (sun near the scope boresight scatters off the coatings), y = rain
static class svp_env_binder : public R_constant_setup
{
	virtual void setup(R_constant* C)
	{
		extern float ps_r__svp_veiling_glare;
		if (!Device.true_pip_on)
		{
			RCache.set_c(C, 0.f, 0.f, 0.f, 0.f);
			return;
		}
		CEnvDescriptor& desc = *g_pGamePersistent->Environment().CurrentEnv;
		const Fvector& fwd = Device.m_SecondViewport.svp_fwd;
		// sun_dir points FROM the sun, so -dot is how much the scope points TOWARD it
		float align = -desc.sun_dir.dotproduct(fwd);
		float sun_luma = 0.299f * desc.sun_color.x + 0.587f * desc.sun_color.y + 0.114f * desc.sun_color.z;
		float glare = 0.f;
		if (align > 0.f && ps_r__svp_veiling_glare > 0.f)
		{
			extern int ps_r__svp_glare_model;
			float fall;
			if (ps_r__svp_glare_model)
			{
				// Stiles-Holladay disability glare, veil falls as 1/theta^2 off axis, the floor is
				// the scope's own half fov where the sun enters the image and stops being veil
				const float t0 = _max(rad2deg(Device.m_SecondViewport.svp_fov) * 0.5f, 2.f);
				const float th = _max(rad2deg(acosf(_min(align, 1.f))), t0);
				fall = (t0 / th) * (t0 / th);
			}
			else
				fall = powf(align, 6.f);
			glare = fall * (sun_luma > 1.f ? 1.f : sun_luma) * ps_r__svp_veiling_glare;
		}
		extern float ps_r__svp_rain_optic;
		extern float ps_r__svp_rain_debug;
		extern int ps_r__svp_uv_debug;
		extern int ps_r__svp_autoflip;
		float rain = (ps_r__svp_rain_optic > 0.f) ? desc.rain_density * ps_r__svp_rain_optic : 0.f;
		// debug forces the droplets in any weather, the value stands in for the density product
		if (ps_r__svp_rain_debug > 0.f)
			rain = ps_r__svp_rain_debug;
		RCache.set_c(C, glare, rain, ps_r__svp_uv_debug ? 1.f : 0.f, ps_r__svp_autoflip ? 1.f : 0.f);
	}
} binder_svp_env;

// scope magnification (curMag/minMag/maxMag/fov), pip overrides with the engine range, else 3DSS Lua passthrough
extern Fvector4 ps_shader_scope_params;
extern float g_pip_scope_magnification;
extern float g_pip_scope_min_mag;
extern float g_pip_scope_max_mag;
extern float g_pip_scope_ratio;
static class shader_scope_params : public R_constant_setup
{
	virtual void setup(R_constant* C)
	{
#if 1 // pip drive curMag/minMag so the reticle current_zoom lands on mag/min (matches the 2D reticle)
		if (Device.true_pip_on && g_pip_scope_magnification > 0.01f)
		{
			// curMag = ratio*scope bounds the exit-pupil shadow, minMag solved so current_zoom = mag/min
			const float r = g_pip_scope_ratio;
			const float mn_eng = (g_pip_scope_min_mag > 0.01f) ? g_pip_scope_min_mag : g_pip_scope_magnification;
			const float zoom_ratio = g_pip_scope_magnification / mn_eng; // 1.0 at min zoom, scope zoom ratio at max
			const float cur = r * g_pip_scope_magnification;
			float mn = cur - (zoom_ratio - 1.0f) * 2.5f; // (cur - mn) * 0.4 + 1 == zoom_ratio
			if (mn < 0.01f) mn = 0.01f; // floor above zero, the shader divides by minMag
			const float mx = r * ((g_pip_scope_max_mag > 0.01f) ? g_pip_scope_max_mag : g_pip_scope_magnification);
			// w = -2 is the true-PiP sentinel, the legacy Lua writes -1 so patched shaders gate on
			// w < -1.5 and stay inert at svpscope 0
			RCache.set_c(C, cur, mn, mx, Device.m_SecondViewport.IsSVPActive() ? -2.f : ps_shader_scope_params.w);
		}
		else
#endif
		if (ps_shader_scope_params.y > 0.f)
			RCache.set_c(C, ps_shader_scope_params.x, ps_shader_scope_params.y, ps_shader_scope_params.z, ps_shader_scope_params.w);
		else if (Device.true_pip_on)
		{
			// engine fallback for a scope with no Lua data, only meaningful while PiP drives the mags
			const float cur = g_pip_scope_magnification;
			const float mn = (g_pip_scope_min_mag > 0.f) ? g_pip_scope_min_mag : cur;
			const float mx = (g_pip_scope_max_mag > 0.f) ? g_pip_scope_max_mag : cur;
			RCache.set_c(C, cur, mn, mx, 0.0f);
		}
		else
			// svpscope 0 passes the authored vector through untouched
			RCache.set_c(C, ps_shader_scope_params.x, ps_shader_scope_params.y, ps_shader_scope_params.z, ps_shader_scope_params.w);
	}
}    shader_scope_params;

static class ssfx_issvp : public R_constant_setup
{
	virtual void setup(R_constant* C)
	{
		// pip force_water_reflect and force_svp_sss make this read 0 so the water and sun take their non SVP path
		const bool force0 = Device.m_SecondViewport.force_water_reflect || Device.m_SecondViewport.force_svp_sss;
		const float issvp = force0 ? 0.f : float(Device.m_SecondViewport.IsSVPFrame());
		RCache.set_c(C, issvp, 0, 0, 0);
	}
}    ssfx_issvp;

// one table drives both registration and the startup guard so the two never drift
static const struct { const char* name; R_constant_setup* setup; } s_svp_binders[] = {
	{ "shader_scope_params", &shader_scope_params }, // scope magnification
	{ "svp_control", &binder_svp_control }, // pip clean-optics intent flags
	{ "svp_glass", &binder_svp_glass }, // pip glass optics tunables
	{ "svp_env", &binder_svp_env }, // pip glass environment data (glare, rain)
	{ "ssfx_issvp", &ssfx_issvp },
	{ "svp_nearblur_mode", &binder_svp_nearblur }, // pip near-blur composite selector
};

void RegisterSvpConstants(CBlender_Compile& dst)
{
	static bool s_validated = false;
	if (!s_validated) { s_validated = true; ValidateSvpConstants(); }
	for (const auto& b : s_svp_binders)
		dst.r_Constant(b.name, b.setup);
}

// name-matched binders fail silently, so log the table health once at first use
void ValidateSvpConstants()
{
	const int n = (int)(sizeof(s_svp_binders) / sizeof(s_svp_binders[0]));
	int bad = 0;
	for (int i = 0; i < n; ++i)
	{
		if (!s_svp_binders[i].setup)
		{
			Msg("! [SVP-GUARD] MISSING %s (null binder)", s_svp_binders[i].name);
			++bad;
			continue;
		}
		for (int j = 0; j < i; ++j)
			if (0 == xr_strcmp(s_svp_binders[j].name, s_svp_binders[i].name))
			{
				Msg("! [SVP-GUARD] MISSING %s (duplicate registration)", s_svp_binders[i].name);
				++bad;
			}
	}
	Msg("[SVP-GUARD] binders ok (%d)", n - bad);
}
