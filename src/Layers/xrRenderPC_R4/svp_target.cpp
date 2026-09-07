#include "stdafx.h"
#include "r4.h"
#include "svp_target.h"

// pip SVP scene render extent, EXACT svp_height at gate 0 (byte-identical to stock, no scale and no
// even rounding), scaled + even-rounded for the upscaler only once the DLSS scaffolding is on
u32 svp_render_extent()
{
	if (ps_r__svp_dlss == 0)
	{
		// base SVP square = full display-derived svp_height, or (adaptive) just past the on-screen eyepiece
		// disc so we never rasterise pixels the lens cannot show. supersample then scales the chosen base up
		u32 base = Device.svp_height();
		const float disc = Device.m_SecondViewport.svp_disc_applied;
		extern int ps_r__svp_flat_window;
		extern Fvector4 ps_s3ds_param_3;
		extern float ps_r__svp_adaptive_res;
		if (ps_r__svp_flat_window && (int)ps_s3ds_param_3.y == 8 && disc > 1.0f)
		{
			// a flat panel (binocular) fills far more screen than a round ocular disc, size the square SVP
			// up to the panel extent so the wide window is not upscaled, capped at the main height for cost
			u32 want = u32(disc) & ~1u;
			const u32 cap = Device.dwHeight & ~1u;
			if (want > cap) want = cap;
			if (want > base) base = want;
		}
		else if (ps_r__svp_adaptive_res > 0.f)
		{
			if (disc > 1.0f)
			{
				u32 want = u32(disc * ps_r__svp_adaptive_res) & ~1u; // disc * margin, even side
				if (want < 256) want = 256;                       // floor: never degenerate on a tiny/far ocular
				extern int ps_r__svp_adaptive_grow;
				if (ps_r__svp_adaptive_grow)
				{
					// the disc is the requirement in both directions, a big ocular renders 1:1
					// instead of upscaling soft, capped at the display height for cost
					const u32 cap = Device.dwHeight & ~1u;
					base = (want > cap) ? cap : want;
				}
				else if (want < base)
					base = want;                                  // legacy shrink-only under the half-height base
			}
		}
		float ss = ps_r__svp_supersample;
		clamp(ss, 1.0f, 2.0f);
		if (base == Device.svp_height() && ss <= 1.0f)
			return Device.svp_height(); // exact stock, byte-identical (off == unchanged)
		u32 e = u32(base * ss) & ~1u;
		return (e < 2) ? 2 : e;
	}
	float eff = ps_r__svp_render_scale;
	clamp(eff, 0.5f, 1.0f);
	if (eff >= 1.0f)
		return Device.svp_height(); // no downscale, full res so the eval is a pass-through (CopyResource)
	u32 e = u32(Device.svp_height() * eff) & ~1u; // even side for the upscaler
	if (e < 2)
		e = 2;
	return e;
}

// pip round a target side UP to a 32px step so a few-px disc wobble reuses the target instead of
// reallocating the whole SVP rt chain on every ADS, quality only rounds up never below the ask
static inline u32 svp_quant32(u32 v) { return (v + 31u) & ~31u; }

// pip the SVP target extent, one policy for the allocation and the camera projection. a flat panel
// shapes the square down to the lens aspect (aspect is major/minor so always >= 1), else square
void svp_target_wh(u32& svp_w, u32& svp_h)
{
	u32 svp_side = svp_quant32(svp_render_extent());
	if (svp_side < 64)
		svp_side = 64;
	svp_w = svp_h = svp_side;
	extern int ps_r__svp_flat_window;
	extern Fvector4 ps_s3ds_param_3;
	const float pa = Device.m_SecondViewport.svp_panel_aspect;
	if (ps_r__svp_flat_window && (int)ps_s3ds_param_3.y == 8 && pa > 1.01f)
	{
		svp_h = svp_quant32(u32(svp_side / pa));
		if (svp_h < 64)
			svp_h = 64;
	}
}

// pip allocate the square SVP target the first time an SVP pass runs, with PiP off this is never
// called so the SVP RTs cost nothing, the scene render uses svp_render_extent (display-res at gate 0)
void CRender::EnsureTargetSVP()
{
	if (TargetMain)
		TargetMain->EnsureScopeShaders(); // pip load the lens glue shaders on first aim (idempotent)
	u32 svp_w, svp_h;
	svp_target_wh(svp_w, svp_h);
	if (TargetSVP)
	{
		// pip recreate if the DLSS gate flipped (gbuffer + rt_secondVP UAV change at gate != 0) or the
		// supersample / panel-aspect changed the size, so r__svp_supersample applies live (no vid_restart)
		const bool gate_ok = ((ps_r__svp_dlss != 0) == TargetSVP->m_svp_dlss_built);
		const bool size_ok = (TargetSVP->Width == svp_w && TargetSVP->Height == svp_h);
		if (gate_ok && size_ok)
			return;
		xr_delete(TargetSVP);
	}
	TargetSVP = xr_new<CRenderTarget>("svp", svp_w, svp_h);
	// CHK_DX is a no-op so a VRAM-exhausted creation fails silently, never keep a half-built target
	const bool svp_rt_ok = TargetSVP->rt_secondVP && TargetSVP->rt_secondVP->pSurface
		&& TargetSVP->rt_baseRT && TargetSVP->rt_baseRT->pRT
		&& TargetSVP->rt_baseZB && TargetSVP->rt_baseZB->pZRT
		&& TargetSVP->rt_Color && TargetSVP->rt_Color->pRT
		&& TargetSVP->rt_Position && TargetSVP->rt_Position->pRT;
	if (!svp_rt_ok)
	{
		Msg("! [SVP-ALLOC] SVP target creation FAILED (out of VRAM?), scope pass disabled");
		xr_delete(TargetSVP);
		return;
	}
	Device.m_SecondViewport.dlss_reset_next = true; // pip DLSS history reset, SVP surface (re)created incl. res change
	extern int ps_r__svp_diag; extern float ps_r__svp_supersample;
	if (ps_r__svp_diag)
		PipMsg("[SVP-ALLOC] svp=%ux%u (svp_height=%u aspect=%.2f supersample=%.2f dlss=%d)",
			svp_w, svp_h, Device.svp_height(), Device.m_SecondViewport.svp_panel_aspect, ps_r__svp_supersample, ps_r__svp_dlss);
}
