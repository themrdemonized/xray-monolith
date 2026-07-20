#include "stdafx.h"
#include "../xrRender/FBasicVisual.h" // pip dxRender_Visual (GetTexture/Render) for draw_scope
#include "../xrRender/SkeletonX.h" // pip lens bone latch compensation for the skinned lens draws
#include "../../xrEngine/igame_persistent.h" // pip env-driven eye pupil for the exit-pupil twilight dimming
#include "../../xrEngine/environment.h"
#if defined(USE_DX11)
#include "../../../gamedata/shaders/r3/scope_defines.h" // SCOPE_PHASE_* (kept in sync with the shader)
#endif

#if defined(USE_DX11)

// per-scope objective diameter in mm, resolved by the optics bus (spec cvar then authored w), 0 = none
static float svp_objective_mm()
{
	return Device.m_SecondViewport.svp_opt_obj_mm;
}

// pip electronic overlay actually on screen, NV shows only with its overlay on (markswitch 0),
// thermal shows until the overlay is dropped (markswitch < 2)
static bool svp_overlay_active(float param3x, int markswitch)
{
	return (param3x >= 0.5f) && ((param3x < 1.5f) ? (markswitch == 0) : (markswitch < 2));
}
// pip thermal-typed optic with its overlay on, near-blur skips these so they keep full DoF
static bool svp_thermal_active(float param3x, int markswitch)
{
	return (param3x >= 1.5f) && svp_overlay_active(param3x, markswitch);
}

// pip near-field defocus pass, true when it dispatched else the caller copies
bool CRenderTarget::svp_nearblur_pass()
{
	// pip near-field defocus replaces the plain copy in the realism mode, thermal sensors keep
	// full depth of field so they take the plain copy
	extern float ps_r__svp_near_blur;
	extern Fvector4 ps_s3ds_param_3;
	extern int ps_markswitch_current;
	// thermal keeps full DoF only while its overlay is on, markswitch >= 2 falls back to a plain
	// relayed image that should defocus like any optic
	const bool thermal_active = svp_thermal_active(ps_s3ds_param_3.x, ps_markswitch_current);
	if (ps_r__svp_near_blur > 0.01f && scope_svp_enabled >= 2 && !thermal_active
		&& rt_Position && rt_secondVP->pRT)
	{
		EnsureScopeShaders();
		if (s_svp_nearblur)
		{
			// pip prefilter the svp color into a mip chain so each sparse ring area-averages its cell
			if (m_svp_nb_mip_surf)
			{
				HW.pContext->CopySubresourceRegion(m_svp_nb_mip_surf, 0, 0, 0, 0, rt_Generic_0->pSurface, 0, nullptr);
				HW.pContext->GenerateMips(m_svp_nb_mip_tex->get_SRView());
			}
			{
				ref_texture t; t.create("$user$svp_nearblur_src");
				t->surface_set(m_svp_nb_mip_surf ? m_svp_nb_mip_surf : rt_Generic_0->pSurface);
			}
			{
				ref_texture t; t.create("$user$svp_nearblur_pos");
				t->surface_set(rt_Position->pSurface);
			}
			u_setrt(Width, Height, rt_secondVP->pRT, nullptr, nullptr, nullptr);
			const float w = float(Width), h = float(Height);
			u32 Offset = 0;
			const float d_Z = EPS_S, d_W = 1.f;
			const u32 C = color_rgba(255, 255, 255, 255);
			FVF::TL* pv = (FVF::TL*)RCache.Vertex.Lock(3, g_combine->vb_stride, Offset);
			pv->set(0, h * 2, d_Z, d_W, C, 0.f, 2.f); pv++;
			pv->set(0, 0, d_Z, d_W, C, 0.f, 0.f); pv++;
			pv->set(w * 2, 0, d_Z, d_W, C, 2.f, 0.f); pv++;
			RCache.Vertex.Unlock(3, g_combine->vb_stride);
			RCache.set_Geometry(g_combine);
			RCache.set_Element(s_svp_nearblur->E[1]);
			// set_c is stateful, screen_res must be the SVP dims for the SV_Position uv
			RCache.set_c("screen_res", w, h, 1.f / w, 1.f / h);
			// near-field defocus by the real thin-lens CoC of the objective aperture
			// y = k in px*m (CoC px = k * (1/z - 1/focus))
			{
				extern float ps_r__svp_focus_m;
				const float omm = svp_objective_mm();
				const float A = (omm > 0.01f) ? omm * 0.001f : 0.024f; // typical 24mm when no per-scope data
				const float vfov = (Device.m_SecondViewport.svp_fov > 0.01f) ? Device.m_SecondViewport.svp_fov : 0.35f;
				const float k = A * h / vfov * _min(ps_r__svp_near_blur, 3.f);
				const float focus = _max(ps_r__svp_focus_m, 1.f);
				// the near field ends at the real muzzle, per weapon, clamped to the old design bound
				float md = 1.5f;
				if (Device.m_SecondViewport.muzzle_pos.square_magnitude() > EPS)
					md = clampr(Device.m_SecondViewport.muzzle_pos.distance_to(Device.vCameraPosition), 0.5f, 3.f);
				// max radius is the physical coc at the svp near plane capped at the inscribed disc
				const float z_near = Device.m_SecondViewport.svp_near;
				const float coc_near = k * _max(1.f / _max(z_near, 0.05f) - 1.f / focus, 0.f);
				const float maxR = _min(coc_near, 0.5f * _min(w, h));
				RCache.set_c("svp_nearblur_params", focus, k, maxR, md);
				// pip one-shot proof that the pass ran, throttled while r__svp_diag is on
				extern int ps_r__svp_diag;
				extern int ps_r__svp_nearblur_scatter;
				static u32 s_nb_ms = 0;
				if (ps_r__svp_diag && Device.dwTimeGlobal - s_nb_ms > 1000)
				{
					s_nb_ms = Device.dwTimeGlobal;
					const float g_dbg = powf(_max(maxR, 1.f), 1.f / 150.f) - 1.f; // 150 mirrors the shader NB_TAP_BUDGET
					u32 mips = 0;
					if (m_svp_nb_mip_surf) { D3D_TEXTURE2D_DESC d; m_svp_nb_mip_surf->GetDesc(&d); mips = d.MipLevels; }
					PipMsg("[SVP-NB] maxR=%.0fpx g=%.4f lodk=%.4f scatter=%d mips=%u", maxR, g_dbg, g_dbg, ps_r__svp_nearblur_scatter, mips);
				}
			}
			RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 3, 0, 1);
			return true;
		}
	}
	return false;
}
#endif
