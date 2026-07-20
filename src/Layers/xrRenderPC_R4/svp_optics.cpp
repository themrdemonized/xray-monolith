#include "stdafx.h"
#include "../xrRender/FBasicVisual.h" // pip dxRender_Visual (GetTexture/Render) for draw_scope
#include "../xrRender/SkeletonX.h" // pip lens bone latch compensation for the skinned lens draws
#include "../../xrEngine/igame_persistent.h" // pip env-driven eye pupil for the exit-pupil twilight dimming
#include "../../xrEngine/environment.h"
#if defined(USE_DX11)
#include "../../../gamedata/shaders/r3/scope_defines.h" // SCOPE_PHASE_* (kept in sync with the shader)
#endif

#if defined(USE_DX11)	//  Redotix99: for 3D Shader Based Scopes 		(sorry for using the nightvision phase file)
// pip load the scope glue shaders lazily on first PiP use, they ship in the PiP mod (gamedata/shaders/r3)
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

void CRenderTarget::EnsureScopeShaders()
{
	if (m_scope_shaders_ready)
		return;
	s_scope_color_write.create("scope_color_write");
	s_scope_depth_write.create("scope_depth_write");
	s_scope_debug.create("scope_debug");
	s_svp_nearblur.create("svp_nearblur");
	s_svp_distort_stamp.create("svp_distort_stamp");
	s_svp_taa_stamp.create("svp_taa_stamp");
	m_scope_shaders_ready = true;
}

// pip r__scope_debug overlay, a top-left grid of the main + SVP views, their ssfx buffers (prev-frame,
// prev-pos, motion vectors) and the shadow map, main viewport only, binds each $main/$svp RT by name
void CRenderTarget::phase_scope_debug()
{
	if (!scope_debug || Device.m_SecondViewport.IsSVPFrame())
		return;

	EnsureScopeShaders();
	if (!s_scope_debug)
		return;

	// snapshot the finished main view so the overlay can sample it without reading the RT it draws into
	HW.pContext->CopyResource(rt_secondVP->pSurface, rt_Generic_0->pSurface);

	auto M = RImplementation.TargetMain;
	auto S = RImplementation.TargetSVP;
	auto bind = [M](LPCSTR name, ref_rt& rt)
	{
		// fall back to a valid RT when the source is absent (no SSS = no ssfx buffers) so the debug
		// technique bind always resolves to a real texture instead of an unregistered name
		ref_rt& src = rt ? rt : M->rt_Generic_0;
		if (!src)
			return;
		ref_texture t;
		t.create(name);
		// raw pSurface, surface_get would AddRef a reference nobody releases (per-frame leak)
		t->surface_set(src->pSurface);
	};
	bind("$user$viewport2$main", M->rt_secondVP);
	bind("$user$ssfx_prev_p$main", M->rt_Position); // no MT prev-pos buffer, show the gbuffer position
	bind("$user$ssfx_motion_vectors$main", M->rt_ssfx_motion_vectors);
	bind("$user$ssfx_prev_frame$main", M->rt_ssfx_prev_frame);
	bind("$user$smap_depth", M->rt_smap_depth);
	if (S)
	{
		bind("$user$viewport2$svp", S->rt_secondVP);
		bind("$user$ssfx_prev_p$svp", S->rt_Position);
		bind("$user$ssfx_motion_vectors$svp", S->rt_ssfx_motion_vectors);
		bind("$user$ssfx_prev_frame$svp", S->rt_ssfx_prev_frame);
	}
	// the bind cache keys on CTexture identity so the remaps above are invisible to it
	RCache.Invalidate();

	// draw onto the CURRENT target phase_combine left bound (the final LDR image), not a fresh RT or the
	// following HUD/UI passes would render offscreen
	RCache.set_CullMode(CULL_NONE);
	RCache.set_Stencil(FALSE);

	u32 Offset = 0;
	u32 C = color_rgba(0, 0, 0, 255);
	float d_Z = EPS_S;
	float d_W = 1.0f;
	float w = float(Device.dwWidth);
	float h = float(Device.dwHeight);

	// fullscreen triangle, the shader discards everything outside the top-left quarter grid
	FVF::TL* pv = (FVF::TL*)RCache.Vertex.Lock(3, g_combine->vb_stride, Offset);
	pv->set(0, h * 2, d_Z, d_W, C, 0.f, 2.f); pv++;
	pv->set(0, 0, d_Z, d_W, C, 0.f, 0.f); pv++;
	pv->set(w * 2, 0, d_Z, d_W, C, 2.f, 0.f); pv++;
	RCache.Vertex.Unlock(3, g_combine->vb_stride);

	RCache.set_Geometry(g_combine);
	RCache.set_Element(s_scope_debug->E[1]);
	RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 3, 0, 1);
}

// pip zero the geometry stencil in the four dead corners outside the inscribed eyepiece disc so
// every downstream >=1 test (lights, combine, wallmarks) skips them, no shader or accum edits
void CRenderTarget::stamp_svp_corner_mask()
{
	// square rt only, a flat panel fills the whole rect and has no dead corner
	extern int ps_r__svp_flat_window;
	extern Fvector4 ps_s3ds_param_3;
	if (Width != Height || (ps_r__svp_flat_window && (int)ps_s3ds_param_3.y == 8))
		return;

	// corner-triangle leg, a quarter of the side stays clear of the inscribed disc edge
	const float side = float(Width);
	const float leg = side * 0.25f;
	const float z = EPS_S;
	const u32 C = color_rgba(255, 255, 255, 255);

	// each corner is one triangle packed as a quad, the duplicated vertex collapses the second tri
	u32 Offset;
	FVF::TL* pv = (FVF::TL*)RCache.Vertex.Lock(16, g_combine->vb_stride, Offset);
	auto corner = [&](float cx, float cy, float ax, float ay, float bx, float by)
	{
		pv->set(cx, cy, z, 1.f, C, 0, 0); pv++;
		pv->set(ax, ay, z, 1.f, C, 0, 0); pv++;
		pv->set(bx, by, z, 1.f, C, 0, 0); pv++;
		pv->set(ax, ay, z, 1.f, C, 0, 0); pv++;
	};
	corner(0.f,  0.f,  leg,        0.f,  0.f,   leg);
	corner(side, 0.f,  side - leg, 0.f,  side,  leg);
	corner(0.f,  side, leg,        side, 0.f,   side - leg);
	corner(side, side, side - leg, side, side,  side - leg);
	RCache.Vertex.Unlock(16, g_combine->vb_stride);

	u_setrt(NULL, NULL, NULL, baseZB); // only the svp depth-stencil, no color target
	RCache.set_Element(s_occq->E[1]);
	RCache.set_Geometry(g_combine);
	// replace the whole stencil byte with 0 in the corners, clearing the bit0 geometry marker
	StateManager.SetStencil(TRUE, D3DCMP_ALWAYS, 0x00, 0xff, 0xff,
		D3DSTENCILOP_KEEP, D3DSTENCILOP_REPLACE, D3DSTENCILOP_KEEP);
	StateManager.SetColorWriteEnable(0);
	StateManager.SetDepthFunc(D3DCMP_ALWAYS);
	StateManager.SetDepthEnable(FALSE);
	StateManager.SetCullMode(D3DCULL_NONE);
	RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 16, 0, 8);
	StateManager.SetColorWriteEnable(D3D_COLOR_WRITE_ENABLE_ALL);
}

// pip stash the SVP combined color in rt_secondVP so the scope lens can sample it
void CRenderTarget::phase_svp_capture()
{
	PIX_EVENT(PHASE_SCOPE_SVP_CAPTURE);
	if (ps_r__svp_dlss != 0)
	{
		// pip DLSS seam: assemble SvpDlssInputs from the SVP target + cached consts, then EvalSVP_DLSS (stub for now)
		SvpDlssInputs in;
		auto& vp = Device.m_SecondViewport;
		in.viewport_id = 1; // stable SVP handle for DLSS history (main = 0), NOT the per-frame dwViewport
		in.color_srv = rt_Generic_0->pTexture->get_SRView();
		in.render_extent = { (u32)Width, (u32)Height };
		in.depth_srv = rt_baseZB ? rt_baseZB->pTexture->get_SRView() : nullptr;
		in.mvec_srv = rt_ssfx_motion_vectors->pTexture->get_SRView();
		in.out_rtv = rt_secondVP->pRT;
		in.out_uav = rt_secondVP->pUAView;
		in.display_extent = { (u32)Width, (u32)Height }; // stub rt_secondVP follows the render extent, Ascii makes it display-res
		in.view = Device.matrices[1].mView;
		in.proj = Device.matrices[1].mProject;
		in.view_proj.mul(Device.matrices[1].mProject, Device.matrices[1].mView);
		in.prev_view = Device.matrices_previous[1].mView;
		in.prev_proj = Device.matrices_previous[1].mProject;
		in.prev_view_proj.mul(Device.matrices_previous[1].mProject, Device.matrices_previous[1].mView);
		in.jitter_px = vp.svp_jitter_px;
		in.near_plane = vp.svp_near; in.far_plane = vp.svp_far; in.fov = vp.svp_fov; in.aspect = vp.svp_aspect;
		in.cam_pos = vp.svp_cam_pos; in.up = vp.svp_up; in.right = vp.svp_right; in.fwd = vp.svp_fwd;
		in.reset = vp.dlss_reset_next.exchange(false);
		EvalSVP_DLSS(in);
		return;
	}
	// pip near-field defocus, thermal falls through to the plain copy
	if (svp_nearblur_pass()) return;
	// rt_secondVP alpha is garbage (nothing writes it) and must stay UNREAD, the scope shaders sample
	// .rgb only and the lens composite blends srcalpha with its OWN forced o.a, never the source alpha
	HW.pContext->CopyResource(rt_secondVP->pSurface, rt_Generic_0->pSurface);
}

// pip DLSS-SR eval, a passthrough stub. TODO Ascii replaces the body with the Streamline eval,
// the SvpDlssInputs signature and the seam call are frozen, no sl::/NGX symbols here
void CRenderTarget::EvalSVP_DLSS(const SvpDlssInputs& in)
{
	// a real eval restores via Target->SetActive(true) then unbinds the CS stage, the copy needs none
	HW.pContext->CopyResource(rt_secondVP->pSurface, rt_Generic_0->pSurface);
}

// pip render the captured lens meshes with shader se, the bind callback sets the scope_phase
// (IMAGE/RETICLE/SHADOW/LENS) that scope_color_write composites into the lens
void CRenderTarget::draw_scope(ref_shader se, std::function<void()> bind)
{
	auto elem = se ? se->E[0] : nullptr;
	if (!elem)
		return;

	Fmatrix FTold = Device.mFullTransform;
	Device.mFullTransform = Device.mFullTransformHud;
	RCache.set_xform_project(Device.mProjectHud);
	RImplementation.rmNear();

	// per-lens dump, which texture each lens draw samples as s_reticle + the optic type/geometry
	extern int ps_r__svp_diag;
	static u32 s_lens_ms = 0;
	const bool lens_diag = ps_r__svp_diag && Device.dwTimeGlobal - s_lens_ms > 1000;
	if (lens_diag)
		s_lens_ms = Device.dwTimeGlobal;

	u32 lens_idx = 0;
	for (auto& N : RImplementation.GMBase.RGraph.mapScopeHUDSorted)
	{
		dxRender_Visual* V = N.pVisual;
		if (!V || !N.pMatrix)
			continue;

		CTexture* tex = V->GetTexture();
		// per-lens marker, names the reticle source texture so a capture shows which texture each
		// scope_color_write draw samples as s_reticle (gated on r__gpu_markers)
		PIX_EVENT_F("scope_lens tex=%s", tex ? tex->cName.c_str() : "none");
		if (lens_diag)
		{
			extern Fvector4 ps_s3ds_param_3;
			extern float g_pip_scope_magnification, g_pip_scope_min_mag, g_pip_scope_ratio;
			PipMsg("[SVP-LENS] %u tex %s rtype %d itype %d eyep_r %.4f mag %.2f min %.2f ratio %.2f",
				lens_idx, tex ? tex->cName.c_str() : "none", (int)ps_s3ds_param_3.y, (int)ps_s3ds_param_3.x,
				Device.m_SecondViewport.eyepiece.radius, g_pip_scope_magnification, g_pip_scope_min_mag, g_pip_scope_ratio);
		}
		++lens_idx;
		if (tex)
		{
			// surface_get AddRefs (and services staging), release the ref once the alias holds its own
			ID3DBaseTexture* s = tex->surface_get();
			t_reticle->surface_set(s);
			_RELEASE(s);
		}

		RCache.set_Element(elem);
		// the housing pose from the main hud pass, the live matrix may hold a newer logic write by now
		Fmatrix lensW = *RImplementation.GMBase.svp_pose_of(N.pMatrix);
		// the quad skins from the live bone palette, folding latched bone x live inverse into the
		// world cancels any bone step since the housing draw (the lens glass rides one bone)
		{
			Fmatrix bL, bNow;
			CSkeletonX* sk = fast_dynamic_cast<CSkeletonX*>(V);
			if (sk && RImplementation.GMBase.svp_lens_bone_of(V, bL) && sk->SVP_LensBoneXform(bNow))
			{
				Fmatrix inv;
				inv.invert(bNow);
				lensW.mulB_43(bL);
				lensW.mulB_43(inv);
			}
		}
		RCache.set_xform_world(lensW);
		RImplementation.apply_object(N.pObject);
		RImplementation.apply_lmaterial();

		RCache.set_c("scope_svp", (int)Device.m_SecondViewport.IsSVPActive());
		RCache.set_c("scope_debug", (int)scope_debug);
		Fvector pt = {0, 0, 0};
		Device.m_SecondViewport.eyepiece.m_W.transform(pt);
		RCache.set_c("scope_w_eyepiece", pt.x, pt.y, pt.z, 1.0f);
		const Fvector& w_ffp = Device.m_SecondViewport.w_ffp;
		const Fvector& w_sfp = Device.m_SecondViewport.w_sfp;
		RCache.set_c("scope_w_ffp", w_ffp.x, w_ffp.y, w_ffp.z, 1.0f);
		RCache.set_c("scope_w_sfp", w_sfp.x, w_sfp.y, w_sfp.z, 1.0f);
		// pip reticle collimator geometry 2*ocular_radius/eye_distance, the shader rebuilds the
		// authored reticle magnification as a centered field under true PiP
		{
			Fvector ed; ed.sub(Device.m_SecondViewport.eyepiece.m_W.c, Device.vCameraPosition);
			const float dist = _max(ed.magnitude(), 0.02f);
			float kg = 2.f * Device.m_SecondViewport.eyepiece.radius / dist;
			clamp(kg, 0.02f, 3.f);
			// y = true-scale parallax, the real reticle shift is ~0.15 mrad at full eye deflection
			extern float ps_r__svp_parallax;
			extern float g_pip_scope_magnification;
			extern float g_pip_scope_ratio;
			float par = 0.f;
			if (ps_r__svp_parallax > 0.f && g_pip_scope_magnification > 0.01f)
			{
				const float eff_mag = _max(g_pip_scope_ratio * g_pip_scope_magnification, 1.f);
				const float hfov = deg2rad(_max(Device.fFOV, 1.f));
				par = ps_r__svp_parallax * 0.00075f * kg * eff_mag / hfov;
			}
			// z dead lane, no shader reads it. w = eyepiece-fit ratio for the shader optical mag
			RCache.set_c("svp_optics", kg, par, 0.f, _max(g_pip_scope_ratio, 1.f));
		}
		// pip scope-local exposure, x = 0 off else 2^bias
		{
			extern int ps_r__svp_local_exposure;
			extern float ps_r__svp_exposure_bias;
			// y = exit-pupil twilight dimming, exit pupil (ocular*ratio shrunk by zoom) vs the
			// env-adapted eye pupil squared, electronic sights exempt
			extern float ps_r__svp_twilight;
			extern Fvector4 ps_s3ds_param_1;
			extern Fvector4 ps_s3ds_param_3;
			extern float g_pip_scope_magnification;
			extern float g_pip_scope_min_mag;
			// nvg on = the tube gain owns the scope brightness, our exposure lifts stand down
			extern Fvector4 ps_dev_param_8;
			const bool nvg_on = ps_dev_param_8.x >= 1.f;
			// env-adapted eye pupil (mm), shared by the twilight dimming and the eyebox
			float envb = 0.f, pupil_mm = 0.f;
			extern int ps_r__svp_photo_model;
			if (g_pGamePersistent)
			{
				CEnvDescriptor& E = *g_pGamePersistent->Environment().CurrentEnv;
				envb = 0.299f * E.sun_color.x + 0.587f * E.sun_color.y + 0.114f * E.sun_color.z
					+ 0.5f * (0.299f * E.hemi_color.x + 0.587f * E.hemi_color.y + 0.114f * E.hemi_color.z);
				if (ps_r__svp_photo_model)
				{
					// Moon-Spencer pupil response, luminance anchored so envb 1 reads as an
					// overcast day (~2500 cd/m2) and envb 0.01 as moonlight (~0.25)
					const float L = 2500.f * envb * envb;
					pupil_mm = 4.9f - 3.f * tanhf(0.4f * log10f(_max(L, 1e-4f)));
					clamp(pupil_mm, 2.f, 8.f);
				}
				else
					pupil_mm = 6.f - 3.5f * _min(envb / 0.25f, 1.f);
			}
			float dim = 0.f;
			extern int ps_markswitch_current;
			// twilight dims passive optics, NV/thermal are exempt only while their overlay is actually
			// active (markswitch 0 for NV, < 2 for thermal), else the scope shows a plain image
			const bool overlay_active = svp_overlay_active(ps_s3ds_param_3.x, ps_markswitch_current);
			if (ps_r__svp_twilight > 0.f && !overlay_active && g_pip_scope_magnification > 0.01f && pupil_mm > EPS)
			{
				const float mn = (g_pip_scope_min_mag > 0.01f) ? g_pip_scope_min_mag : g_pip_scope_magnification;
				// exit pupil = objective diameter / magnification, real per-scope objective when
				// known, else the ocular-ratio proxy
				const float omm = svp_objective_mm();
				float ep_mm;
				if (omm > 0.01f)
					ep_mm = omm / g_pip_scope_magnification;
				else
				{
					const float xp = (ps_s3ds_param_1.z > 0.01f) ? ps_s3ds_param_1.z : 0.5f;
					ep_mm = xp * Device.m_SecondViewport.eyepiece.radius * 2000.f * (mn / g_pip_scope_magnification);
				}
				// relative brightness is the pupil ratio squared, the legacy model used it linearly
				const float dr = _min(ep_mm / pupil_mm, 1.f);
				const float d = ps_r__svp_photo_model ? dr * dr : dr;
				dim = 1.f + (_max(d, 0.6f) - 1.f) * _min(ps_r__svp_twilight, 1.f);
				extern int ps_r__svp_diag;
				if (ps_r__svp_diag)
				{
					static u32 s_twl_ms = 0;
					if (Device.dwTimeGlobal - s_twl_ms > 1000)
					{
						s_twl_ms = Device.dwTimeGlobal;
						PipMsg("[SVP-TWL] ep %.1fmm pupil %.1fmm env %.2f dim %.2f", ep_mm, pupil_mm, envb, dim);
					}
				}
			}
			// pip eyebox half angle from the real exit pupil, the sight anchor bound reads it
			{
				extern float ps_r__svp_eyebox;
				extern int ps_r__svp_authored_optics;
				extern float g_pip_scope_magnification;
				extern int ps_r__svp_diag;
				const float eb_omm = svp_objective_mm();
				if (ps_r__svp_eyebox > 0.f && ps_r__svp_authored_optics && eb_omm > 0.01f && g_pip_scope_magnification > 0.01f && pupil_mm > EPS)
				{
					const float ep_r = eb_omm * 0.0005f / g_pip_scope_magnification;
					const float p_r = pupil_mm * 0.0005f;
					// full-blackout half angle for the sight anchor bound, arm = authored eye relief
					const float eb_arm = _max(ps_s3ds_param_1.y * 0.01f, 0.05f);
					Device.m_SecondViewport.svp_eyebox_rad = atanf((ep_r + p_r) / eb_arm);
				}
				else
				{
					// no authored bound on this optic, drop the previous scope's so the anchor
					// falls back to its own default instead of a stale narrow eyebox
					Device.m_SecondViewport.svp_eyebox_rad = 0.f;
					if (ps_r__svp_eyebox > 0.f && ps_r__svp_diag)
					{
						static u32 s_ebg_ms = 0;
						if (Device.dwTimeGlobal - s_ebg_ms > 1000)
						{
							s_ebg_ms = Device.dwTimeGlobal;
							PipMsg("[SVP-EYEBOX] gated off, authored %d obj_w %.3f obj_mm %.1f mag %.2f pupil %.1f",
								ps_r__svp_authored_optics, scope_objective_lens_offset.w, ps_s3ds_objective_mm,
								g_pip_scope_magnification, pupil_mm);
						}
					}
				}
			}
			// crescent drive, exposure zw carries the latched swing side, the tangent offset is
			// scaled by pupil over eye relief so the bite depth reads the same on every scope
			float st = 0.f;
			float sw_k = 0.f;
			const float shadow_g = Device.m_SecondViewport.svp_shadow_gain;
			{
				extern float g_pip_scope_magnification;
				extern Fvector4 ps_s3ds_param_1;
				st = (g_pip_scope_magnification > 0.01f) ? (g_pip_scope_magnification - 1.f) / 7.f : 0.f;
				clamp(st, 0.f, 1.f);
				Device.m_SecondViewport.svp_mag = _max(g_pip_scope_magnification, 0.f);
				const float snug = 1.3f + (0.55f - 1.3f) * st;
				float gp = shadow_g * 4.f;
				clamp(gp, 0.f, 1.f);
				float z_eff = ps_s3ds_param_1.z * (3.0f + (snug - 3.0f) * gp);
				if (z_eff < 0.08f) z_eff = 0.08f;
				sw_k = 0.4f * z_eff / _max(ps_s3ds_param_1.y, 0.05f);
			}
			RCache.set_c("svp_exposure", (ps_r__svp_local_exposure && !nvg_on) ? powf(2.f, ps_r__svp_exposure_bias) : 0.f,
				nvg_on ? 0.f : dim,
				Device.m_SecondViewport.svp_swing_x * shadow_g * sw_k,
				Device.m_SecondViewport.svp_swing_y * shadow_g * sw_k);
			// pip glass2: x = lens coating strength, y = heat mirage (sun elevation + magnification)
			{
				extern float ps_r__svp_coating;
				extern float ps_r__svp_mirage;
				extern float g_pip_scope_magnification;
				float mirage = 0.f;
				if (ps_r__svp_mirage > 0.f && g_pip_scope_magnification > 0.01f && g_pGamePersistent)
				{
					CEnvDescriptor& Em = *g_pGamePersistent->Environment().CurrentEnv;
					const float heat = _max(-Em.sun_dir.y, 0.f);       // high overhead sun heats the ground
					const float clear = 1.f - _min(Em.rain_density, 1.f);
					const float magf = _min(_max((g_pip_scope_magnification - 2.f) / 6.f, 0.f), 1.f);
					mirage = ps_r__svp_mirage * heat * clear * magf;
					extern int ps_r__svp_diag;
					if (ps_r__svp_diag)
					{
						static u32 s_mir_ms = 0;
						if (Device.dwTimeGlobal - s_mir_ms > 1000)
						{
							s_mir_ms = Device.dwTimeGlobal;
							PipMsg("[SVP-MIRAGE] heat %.2f clear %.2f magf %.2f -> %.3f", heat, clear, magf, mirage);
						}
					}
				}
				// w = flat-panel V-crop: (svp W/H) / panel aspect. 1 when the SVP already matches the
				// panel (non-square path, no crop needed); 1/aspect when the SVP is square (fallback)
				float vcrop = 1.f;
				if (RImplementation.TargetSVP && RImplementation.TargetSVP->Height > 0
					&& Device.m_SecondViewport.svp_panel_aspect > 0.01f)
					vcrop = ((float)RImplementation.TargetSVP->Width / (float)RImplementation.TargetSVP->Height)
						/ Device.m_SecondViewport.svp_panel_aspect;
				RCache.set_c("svp_glass2", ps_r__svp_coating, mirage, 0.f, vcrop);
				Device.m_SecondViewport.svp_panel_vcrop = vcrop; // pip binocular bracket mapping reads it
				// pip glass3: x = sharpen amount, y = field-stop onset, z = sharpen radial falloff, w = sharpen inner crisp radius
				extern float ps_r__svp_sharpen, ps_r__svp_sharpen_falloff, ps_r__svp_sharpen_inner;
				// pip field-stop onset, the stop sits at the field edge and the exit pupil
				// blurs it by a penumbra whose inner half shows inside the field, 1 = off
				float fs_onset = 1.f;
				{
					extern float g_pip_scope_magnification;
					extern Fvector4 ps_s3ds_param_1;
					const float fs_omm = svp_objective_mm();
					const float fs_fov = Device.m_SecondViewport.svp_fov;
					if (fs_omm > 0.01f && g_pip_scope_magnification > 0.01f && fs_fov > 0.01f)
					{
						const float ep_r = fs_omm * 0.0005f / g_pip_scope_magnification;
						const float er = _max(ps_s3ds_param_1.y * 0.01f, 0.05f);
						const float app_half = g_pip_scope_magnification * fs_fov * 0.5f;
						const float penumbra = _min(atanf(ep_r / er) / app_half, 1.f);
						fs_onset = 1.f - 0.5f * penumbra;
					}
				}
				RCache.set_c("svp_glass3", ps_r__svp_sharpen, fs_onset, ps_r__svp_sharpen_falloff, ps_r__svp_sharpen_inner);
				// pip glass4: x = nvg bleach roll-off, y = nvg auto-gain, w = shadow swing envelope
				extern float ps_r__svp_nvg_bleach, ps_r__svp_nvg_sensitivity;
				RCache.set_c("svp_glass4", ps_r__svp_nvg_bleach, ps_r__svp_nvg_sensitivity, 0.f,
					shadow_g);
			}
		}

		bind();
		V->Render(0);
	}

	RImplementation.rmNormal();
	Device.mFullTransform = FTold;
	RCache.set_xform_project(Device.mProject);
}

// pip render reflex-sight lenses (iScopeLense==10) with their own shaders, no-op for an eyepiece-only scope
// svp draws them through the entrance-pupil camera so a hybrid holo dot magnifies and tracks with the world
void CRenderTarget::draw_reflex(bool svp)
{
	PIX_EVENT_F("RENDER_REFLEX_SIGHTS x%u", (u32)RImplementation.GMBase.RGraph.mapReflexHUDSorted.size());

	Fmatrix FTold = Device.mFullTransform;
	if (svp)
	{
		// the svp camera is already active, keep it so the dot lands in the magnified scene
		Device.mFullTransform.mul(Device.matrices[1].mProject, Device.matrices[1].mView);
		RCache.set_xform_view(Device.matrices[1].mView);
		RCache.set_xform_project(Device.matrices[1].mProject);
	}
	else
	{
		Device.mFullTransform = Device.mFullTransformHud;
		RCache.set_xform_project(Device.mProjectHud);
	}
	RImplementation.rmNear();

	for (auto& N : RImplementation.GMBase.RGraph.mapReflexHUDSorted)
	{
		if (!N.pVisual || !N.pSE || !N.pMatrix)
			continue;
		RCache.set_Element(N.pSE);
		Fmatrix refW = *RImplementation.GMBase.svp_pose_of(N.pMatrix);
		// same bone step cancel as draw_scope, the reflex mesh rides one bone too
		{
			Fmatrix bL, bNow;
			CSkeletonX* sk = fast_dynamic_cast<CSkeletonX*>(N.pVisual);
			if (sk && RImplementation.GMBase.svp_lens_bone_of(N.pVisual, bL) && sk->SVP_LensBoneXform(bNow))
			{
				Fmatrix inv;
				inv.invert(bNow);
				refW.mulB_43(bL);
				refW.mulB_43(inv);
			}
		}
		RCache.set_xform_world(refW);
		RImplementation.apply_object(N.pObject);
		RImplementation.apply_lmaterial();
		N.pVisual->Render(0);
	}

	RImplementation.rmNormal();
	Device.mFullTransform = FTold;
	if (svp)
		RCache.set_xform_view(Device.mView);
	RCache.set_xform_project(Device.mProject);
}

// pip collimated reflex proxy, place a scaled copy of the reflex mesh at a virtual distance on the
// boresight and draw it into the captured svp image so the magnifier carries it, sharp and tracking
bool CRenderTarget::draw_reflex_proxy()
{
	extern Fvector4 ps_s3ds_param_3;
	extern int ps_markswitch_current;
	// thermal image is pixelated and carries its own reticle, keep the proxy out of it
	if (svp_thermal_active(ps_s3ds_param_3.x, ps_markswitch_current))
		return false;

	auto& G = RImplementation.GMBase.RGraph;
	auto& vp = Device.m_SecondViewport;

	// bone-folded world pose of a reflex node, same bone step cancel draw_reflex uses so the proxy
	// matches the legacy 1x dot
	auto pose_of = [&](auto& N) -> Fmatrix {
		Fmatrix W = *RImplementation.GMBase.svp_pose_of(N.pMatrix);
		Fmatrix bL, bNow;
		CSkeletonX* sk = fast_dynamic_cast<CSkeletonX*>(N.pVisual);
		if (sk && RImplementation.GMBase.svp_lens_bone_of(N.pVisual, bL) && sk->SVP_LensBoneXform(bNow))
		{
			Fmatrix inv; inv.invert(bNow);
			W.mulB_43(bL);
			W.mulB_43(inv);
		}
		return W;
	};

	// primary node, measure boresight + subtense from the first valid reflex mesh
	bool have_prim = false;
	Fmatrix refW0; Fvector C0 = {}; float r0 = 0.f;
	for (auto& N : G.mapReflexHUDSorted)
	{
		if (!N.pVisual || !N.pSE || !N.pMatrix)
			continue;
		refW0 = pose_of(N);
		auto& V = N.pVisual->getVisData();
		Fvector lc; V.box.getcenter(lc);
		refW0.transform_tiny(C0, lc);
		r0 = V.sphere.R;
		have_prim = true;
		break;
	}
	if (!have_prim)
		return false;

	// classify by aperture overlap, a coaxial hybrid (magnifier + holo) sits close enough that the
	// reflex glass overlaps the eyepiece aperture, a stacked secondary (rmr) sits beyond both
	{
		auto& ep = Device.m_SecondViewport.eyepiece;
		bool coax = ep.radius > EPS;
		float lateral = 0.f;
		if (coax)
		{
			Fvector eax; eax.set(ep.m_W.k); eax.normalize();
			Fvector rel; rel.sub(C0, ep.m_W.c);
			Fvector proj; proj.mad(ep.m_W.c, eax, rel.dotproduct(eax));
			lateral = C0.distance_to(proj);
			// r0 is the measured reflex glass radius, ep.radius the eyepiece, overlap when the
			// lateral gap is under their radii summed
			if (lateral > ep.radius + r0)
				coax = false;
		}
		const char* cls = !(ep.radius > EPS) ? "legacy" : (coax ? "coax" : "stacked");
		extern int ps_r__svp_diag;
		static u32 s_cls_ms = 0;
		if (ps_r__svp_diag && Device.dwTimeGlobal - s_cls_ms > 1000)
		{
			s_cls_ms = Device.dwTimeGlobal;
			PipMsg("[SVP-RET] proxy class=%s lat=%.2fcm eye_r=%.2fcm", cls, lateral * 100.f, ep.radius * 100.f);
		}
		if (!coax)
			return false; // stacked or no eyepiece, the real top sight draws via the 1x overlay
	}

	const float eye_dist = C0.distance_to(Device.vCameraPosition);
	if (!(eye_dist > 0.02f)) // false for NaN too
		return false;

	// boresight, the reflex mesh normal is the real collimated axis, the stable sight line is fallback
	Fvector b; b.set(refW0.k);
	if (b.magnitude() < EPS)
	{
		if (vp.svp_sight_ok && (Device.dwFrame - vp.svp_sight_frame) < 8)
			b.set(vp.svp_sight_dir);
		else
			return false;
	}
	b.normalize_safe();
	if (b.magnitude() < 0.5f)
		return false;

	const float D = 10.0f; // virtual distance past the svp near plane, the projected size is D invariant
	// the scale draws the reticle at the reflex glass angular size r0/eye_dist, the physical aperture
	// bound, a collimated reticle cannot appear larger than its own window so no extra cap is applied
	const float s = D / eye_dist;
	Fvector target0; target0.mad(vp.svp_cam_pos, b, D);

	// in-front + in-frame test against the svp camera, a wildly off-axis reflex is degenerate
	Fmatrix FT; FT.mul(Device.matrices[1].mProject, Device.matrices[1].mView);
	const float cx = target0.x*FT._11 + target0.y*FT._21 + target0.z*FT._31 + FT._41;
	const float cy = target0.x*FT._12 + target0.y*FT._22 + target0.z*FT._32 + FT._42;
	const float cw = target0.x*FT._14 + target0.y*FT._24 + target0.z*FT._34 + FT._44;
	const bool front_ok = b.dotproduct(vp.svp_fwd) > 0.f;
	const bool inframe = (cw > 0.f) && (_abs(cx) < 2.f*cw) && (_abs(cy) < 2.f*cw);
	if (!front_ok || !inframe)
		return false;

	// bind the captured svp color with no depth so the additive dot always lands on top
	u_setrt(Width, Height, rt_secondVP->pRT, nullptr, nullptr, nullptr);
	RCache.set_CullMode(CULL_CCW);
	RCache.set_Stencil(FALSE);
	RCache.set_ColorWriteEnable();

	Fmatrix FTold = Device.mFullTransform;
	Device.mFullTransform.mul(Device.matrices[1].mProject, Device.matrices[1].mView);
	RCache.set_xform_view(Device.matrices[1].mView);
	RCache.set_xform_project(Device.matrices[1].mProject);
	RImplementation.rmNormal();

	u32 drawn = 0;
	for (auto& N : G.mapReflexHUDSorted)
	{
		if (!N.pVisual || !N.pSE || !N.pMatrix)
			continue;
		Fmatrix refW = pose_of(N);
		auto& V = N.pVisual->getVisData();
		Fvector lc; V.box.getcenter(lc);
		Fvector Cn; refW.transform_tiny(Cn, lc);

		// scale the mesh uniformly and anchor its box center on the boresight, extra meshes keep
		// their scaled offset from the primary so a multi-element reticle stays rigid
		Fmatrix proxyW = refW;
		proxyW.i.mul(s); proxyW.j.mul(s); proxyW.k.mul(s);
		Fvector off; proxyW.transform_dir(off, lc);
		Fvector rel; rel.sub(Cn, C0); rel.mul(s);
		proxyW.c.add(target0, rel);
		proxyW.c.sub(off);

		RCache.set_Element(N.pSE);
		RCache.set_xform_world(proxyW);
		RImplementation.apply_object(N.pObject);
		RImplementation.apply_lmaterial();
		N.pVisual->Render(0);
		drawn++;
	}

	RImplementation.rmNormal();
	Device.mFullTransform = FTold;
	RCache.set_xform_view(Device.mView);
	RCache.set_xform_project(Device.mProject);

	{
		extern int ps_r__svp_diag;
		static u32 s_prox_ms = 0;
		if (ps_r__svp_diag && Device.dwTimeGlobal - s_prox_ms > 1000)
		{
			s_prox_ms = Device.dwTimeGlobal;
			PipMsg("[SVP-RET] proxy drew=%u eye=%.1fcm theta=%.2fmrad s=%.2f front=%d in=%d",
				drawn, eye_dist * 100.f, (r0 / eye_dist) * 1000.f, s, (int)front_ok, (int)inframe);
		}
	}

	if (drawn > 0) { if (ps_r__svp_stats) ++svp_stats_reflex_proxy; svp_ledger_reflex_proxy = 1; } // overlay + ledger proof the collimated proxy drew
	return drawn > 0;
}

void CRenderTarget::phase_3DSSReticle()
{
	PIX_EVENT(PHASE_SCOPE_RETICLE);

	// pip take the PiP path only when the active optic can drive the SVP, an optic that
	// captures nothing falls through to the stock render_Reticle
	const bool svp = Device.m_SecondViewport.IsSVPActive() && RImplementation.TargetSVP;
	const bool has_lens = !RImplementation.GMBase.RGraph.mapScopeHUDSorted.empty() && Device.m_SecondViewport.eyepiece.radius > EPS;

	// pip [SVP-RET] reticle pipeline diag, dumps the capture maps before the draws consume them
	{
		extern int ps_r__svp_diag;
		static u32 s_ret_ms = 0;
		if (ps_r__svp_diag && Device.dwTimeGlobal - s_ret_ms > 1000)
		{
			s_ret_ms = Device.dwTimeGlobal;
			auto& G = RImplementation.GMBase.RGraph;
			extern Fvector4 ps_s3ds_param_1;
			extern Fvector4 ps_s3ds_param_3;
			extern Fvector4 ps_shader_scope_params;
			PipMsg("[SVP-RET] path=%s svp=%d lens=%d scope=%u reflex=%u obj=%u rsize=%.2f rtype=%.0f mag=%.2f/%.2f/%.2f w=%.1f hudy=%.1f",
				(Device.true_pip_on && (svp || has_lens)) ? "pip" : "stock",
				(int)svp, (int)has_lens,
				(u32)G.mapScopeHUDSorted.size(), (u32)G.mapReflexHUDSorted.size(), (u32)G.mapScopeHUDObjective.size(),
				ps_s3ds_param_1.x, ps_s3ds_param_3.y,
				ps_shader_scope_params.x, ps_shader_scope_params.y, ps_shader_scope_params.z, ps_shader_scope_params.w,
				g_pGamePersistent ? g_pGamePersistent->m_pGShaderConstants->hud_params.y : 0.f);
			auto dump = [&](const char* tag, auto& map) {
				u32 i = 0;
				for (auto& N : map)
				{
					if (!N.pVisual || !N.pMatrix) { i++; continue; }
					Fmatrix W = *RImplementation.GMBase.svp_pose_of(N.pMatrix);
					Fvector c; N.pVisual->getVisData().box.getcenter(c);
					Fvector wc; W.transform_tiny(wc, c);
					auto tx = N.pVisual->GetTexture();
					PipMsg("[SVP-RET]  %s[%u] tex=%s pos=(%.2f %.2f %.2f) d=%.1fcm r=%.1fcm",
						tag, i, tx ? tx->cName.c_str() : "?", wc.x, wc.y, wc.z,
						wc.distance_to(Device.vCameraPosition) * 100.f,
						N.pVisual->getVisData().sphere.R * 100.f);
					i++;
				}
			};
			dump("scope", G.mapScopeHUDSorted);
			dump("reflex", G.mapReflexHUDSorted);
			dump("obj", G.mapScopeHUDObjective);
		}
	}

	if (Device.true_pip_on && (svp || has_lens))
	{
		EnsureScopeShaders(); // glue shaders (lazy)

		auto M = RImplementation.TargetMain;
		auto S = RImplementation.TargetSVP;

		// a hybrid magnifier drew the holo dot inside the svp already, skip the 1x main-view overlay
		extern int ps_r__svp_reflex_capture;
		// suppress the 1x overlay only when the collimated proxy actually drew this frame, never blank the dot
		const bool reflex_in_svp = ps_r__svp_reflex_capture && svp
			&& Device.m_SecondViewport.svp_reflex_proxy_ok;

		// the scope shader reads generic2 as the gbuffer position for the holepunch/depth
		HW.pContext->CopyResource(rt_Generic_2->pTexture->surface_get(), RImplementation.Target->rt_Position->pTexture->surface_get());

		u_setrt(RImplementation.Target->rt_Generic_0, nullptr, RImplementation.Target->rt_Position, RImplementation.Target->baseZB);
		RCache.set_CullMode(CULL_CCW);
		RCache.set_Stencil(FALSE);
		RCache.set_ColorWriteEnable();

		if (!reflex_in_svp)
			draw_reflex(); // reflex / red dot, both 1x and magnifier

		// composite the eyepiece lens when magnified or a 1x eyepiece was captured, magnified samples
		// the SVP image, 1x / fake-PiP samples a main-frame copy, a pure reflex optic captures no ==3
		if (svp || (!RImplementation.GMBase.RGraph.mapScopeHUDSorted.empty() && Device.m_SecondViewport.eyepiece.radius > EPS))
		{
			// fake-PiP, off-SVP the lens reads a copy of the finished main frame (magnified already filled rt_secondVP)
			if (!svp)
				HW.pContext->CopyResource(M->rt_secondVP->pSurface, M->rt_Generic_0->pSurface);

			// JITTERFIX, cancel the TAA jitter in the VS so the lens edge has no ring, cvar 0 skips for the a/b
			extern int ps_r__svp_jitterfix;
			if (ps_r__svp_jitterfix)
			{ PIX_EVENT(SCOPE_PHASE_JITTERFIX); draw_scope(s_scope_color_write, []() { RCache.set_c("scope_phase", SCOPE_PHASE_JITTERFIX); }); }

			// point the stock-named textures at this viewport's RTs, the SVP image when magnified else
			// the main-frame copy + the main gbuffer (rt_Generic_2 already holds the copied position)
			auto remap = [](LPCSTR name, ref_rt& target) {
				ref_texture t;
				t.create(name);
				// raw pSurface, surface_get would AddRef a reference nobody releases (per-frame leak)
				t->surface_set(target->pSurface);
			};
			remap(r2_RT_secondVP, svp ? S->rt_secondVP : M->rt_secondVP);
			remap(r2_RT_generic2, svp ? S->rt_Position : M->rt_Generic_2);
			remap(r2_RT_heat,     svp ? S->rt_Heat : M->rt_Heat);
			// pip the scope's own measured exposure for the local-exposure image grade
			{
				ref_texture t;
				t.create("$user$svp_tonemap");
				ID3DBaseTexture* s = (svp ? S : M)->t_LUM_dest->surface_get();
				t->surface_set(s);
				_RELEASE(s);
			}
			// invalidate so the IMAGE pass picks up the remapped surfaces (the bind cache keys on CTexture identity)
			RCache.Invalidate();

			u_setrt(M->rt_Generic_0, nullptr, M->rt_Position, M->baseZB);
			RCache.set_CullMode(CULL_CCW);
			RCache.set_Stencil(FALSE);
			RCache.set_ColorWriteEnable();

			// IMAGE, the magnified SVP scene (or the main frame under fake-PiP)
			{ PIX_EVENT(SCOPE_PHASE_IMAGE);
			draw_scope(s_scope_color_write, [svp]() {
				RCache.set_c("scope_phase", SCOPE_PHASE_IMAGE);
				auto ts = svp ? RImplementation.TargetSVP : RImplementation.TargetMain;
				Fvector4 sr; sr.set((float)ts->Width, (float)ts->Height, 1.0f / (float)ts->Width, 1.0f / (float)ts->Height);
				RCache.set_c("screen_res", sr);
				auto tm = RImplementation.TargetMain;
				Fvector4 outr; outr.set((float)tm->Width, (float)tm->Height, 1.0f / (float)tm->Width, 1.0f / (float)tm->Height);
				RCache.set_c("output_res", outr);
				// lens roll, project the objective up-vector to screen so the reticle stays upright
				Fvector up = {0, 1, 0};
				Device.m_SecondViewport.objective.m_W.transform_dir(up);
				Device.mView.transform_dir(up);
				up.z = 0.0f;
				up.normalize();
				float angle = acosf(up.dotproduct({0, 1, 0})) * (up.x > 0 ? 1.0f : -1.0f);
				RCache.set_c("hack_tex_angle", angle);
			});
			}


				// latch the on-screen eyepiece disc px for adaptive SVP resolution, learn only the
				// settled aimed disc so a raise transient or quick peek never freezes a partial value
				if (svp && RImplementation.TargetSVP && Device.m_SecondViewport.eyepiece.radius > EPS)
				{
					auto& vpd = Device.m_SecondViewport;
					const Fmatrix& MH = Device.mFullTransformHud;
					auto toPx = [&](const Fvector& wp, float& sx, float& sy) {
						const float x  = wp.x*MH._11 + wp.y*MH._21 + wp.z*MH._31 + MH._41;
						const float y  = wp.x*MH._12 + wp.y*MH._22 + wp.z*MH._32 + MH._42;
						const float w  = wp.x*MH._14 + wp.y*MH._24 + wp.z*MH._34 + MH._44;
						const float iw = (fabsf(w) > 1e-6f) ? 1.0f/w : 0.0f;
						sx = (x*iw*0.5f + 0.5f) * (float)M->Width;
						sy = (1.0f - (y*iw*0.5f + 0.5f)) * (float)M->Height;
					};
					Fvector ei, li; li.set(vpd.eyepiece.radius, 0.f, 0.f);
					vpd.eyepiece.m_W.transform_tiny(ei, li);
					float cx, cy, ix, iy; toPx(vpd.eyepiece.m_W.c, cx, cy); toPx(ei, ix, iy);
					const float disc = 2.0f * sqrtf((ix-cx)*(ix-cx) + (iy-cy)*(iy-cy)); // on-screen disc diameter px
					// weapon raise factor, 1 only when fully aimed, gates learning to the settled pose
					const float rot = (g_pGamePersistent && g_pGamePersistent->m_pGShaderConstants)
						? g_pGamePersistent->m_pGShaderConstants->hud_params.x : 0.f;
					// subscribe to the optic epoch, a swap under the jump threshold still re-learns the
					// new optic's disc, the epoch is consumed only after a settled re-learn
					static u32 s_disc_px_epoch = 0;
					const bool disc_epoch_swap = (vpd.svp_optic_epoch != s_disc_px_epoch);
					if (disc > 1.f && disc == disc && rot > 0.999f) // settled, ignore NaN / degenerate
					{
						float& latched = vpd.svp_disc_px;
						const float prev_latched = latched;
						if (latched <= 0.f || disc > latched + 24.f || disc_epoch_swap) // first settle, a jump, or an optic swap
							latched = disc;
						else if (disc < latched * 0.85f)               // big drop, swapped to a smaller optic
							latched = disc;
						if (latched != prev_latched) { if (ps_r__svp_stats) ++svp_stats_disc_latch; svp_ledger_disc_latch = 1; } // overlay + ledger proof the latch moved
						s_disc_px_epoch = vpd.svp_optic_epoch;
					}
					extern int ps_r__svp_diag;
					static u32 s_svpres_t = 0;
					if (ps_r__svp_diag && disc > 1.f && Device.dwTimeGlobal - s_svpres_t > 700)
					{
						s_svpres_t = Device.dwTimeGlobal;
						const u32 sres = RImplementation.TargetSVP->Width;
						extern float g_pip_scope_magnification; extern float ps_r__svp_adaptive_res;
						const float lin = (float)sres / disc;
						PipMsg("[SVP-RES] mag=%.1f svp=%ux%u disc=%.0fpx latch=%.0f adapt=%.2f overrender=%.2fx_linear %.2fx_area",
							g_pip_scope_magnification, sres, sres, disc, vpd.svp_disc_px, ps_r__svp_adaptive_res, lin, lin*lin);
					}
				}


			// restore the stock textures for the reticle/shadow/lens draws
			M->SetActive(true);
			u_setrt(M->rt_Generic_0, nullptr, M->rt_Position, M->baseZB);
			RCache.set_CullMode(CULL_CCW);
			RCache.set_Stencil(FALSE);
			RCache.set_ColorWriteEnable();

			{ PIX_EVENT(SCOPE_PHASE_RETICLE); draw_scope(s_scope_color_write, []() { RCache.set_c("scope_phase", SCOPE_PHASE_RETICLE); }); }
			{ PIX_EVENT(SCOPE_PHASE_SHADOW);  draw_scope(s_scope_color_write, []() { RCache.set_c("scope_phase", SCOPE_PHASE_SHADOW); }); }
			{ PIX_EVENT(SCOPE_PHASE_LENS);    draw_scope(s_scope_color_write, []() { RCache.set_c("scope_phase", SCOPE_PHASE_LENS); }); }

			// CUSTOM_DEPTH, let the scope override depth so DOF focuses on the lens image
			{ PIX_EVENT(SCOPE_PHASE_CUSTOM_DEPTH);
			u_setrt(RImplementation.Target->rt_Position, 0, 0, 0, RImplementation.Target->baseZB);
			draw_scope(s_scope_depth_write, []() {
				RCache.set_c("scope_phase", SCOPE_PHASE_DEPTHWRITE | SCOPE_PHASE_CUSTOM_DEPTH);
				RCache.set_c("scope_depth_value", 1.0f);
			});
			}

			// re-draw the reflex over the composited lens at the main-view position, the hybrid
			// magnifier already has it in the svp image so skip the 1x overlay there
			if (!reflex_in_svp)
			{
				u_setrt(M->rt_Generic_0, nullptr, M->rt_Position, M->baseZB);
				RCache.set_CullMode(CULL_CCW);
				RCache.set_Stencil(FALSE);
				RCache.set_ColorWriteEnable();
				draw_reflex();
			}
		}

		// the capture maps clear at the main-pass combine tail now, past the nvg split and taa mask
		// stamp that read them (deriveScopeLens already read them this frame)
		u_setrt(RImplementation.Target->rt_Generic_0, RImplementation.Target->rt_Position, 0, HW.pBaseZB);
		return;
	}

	// legacy 3D-fake / fake-SVP reticle, the stock path when true_pip is off
	HW.pContext->CopyResource(rt_Generic_2->pTexture->surface_get(), RImplementation.Target->rt_Position->pTexture->surface_get());

	HW.pContext->CopyResource(rt_Generic_temp->pTexture->surface_get(), rt_Generic_0->pTexture->surface_get());

	u_setrt(RImplementation.Target->rt_Generic_0, RImplementation.Target->rt_Position, 0, HW.pBaseZB);

	RCache.set_CullMode(CULL_CCW);
	RCache.set_Stencil(FALSE);
	RCache.set_ColorWriteEnable();

	RImplementation.render_Reticle();

	// pip reflexes captured into mapReflexHUDSorted draw here for the fallback optics,
	// the map is empty when true_pip is off
	u_setrt(RImplementation.Target->rt_Generic_0, RImplementation.Target->rt_Position, 0, HW.pBaseZB);
	RCache.set_CullMode(CULL_CCW);
	RCache.set_Stencil(FALSE);
	RCache.set_ColorWriteEnable();
	draw_reflex();

	// the capture maps clear at the main-pass combine tail now, past every consumer, the frame-start
	// clear + per-frame rebuild cover staleness on the fallback path too
};
#endif
