#include "stdafx.h"
#include "FBasicVisual.h" // pip dxRender_Visual (GetTexture/Render) for draw_scope
#if defined(USE_DX11)
#include "../../../gamedata/shaders/r3/scope_defines.h" // SCOPE_PHASE_* (kept in sync with the shader)
#endif

void CRenderTarget::phase_nightvision()
{
	//Constants
	u32 Offset = 0;
	u32 C = color_rgba(0, 0, 0, 255);

	float d_Z = EPS_S;
	float d_W = 1.0f;
	float w = float(Device.dwWidth);
	float h = float(Device.dwHeight);

	Fvector2 p0, p1;
#if defined(USE_DX10) || defined(USE_DX11)	
	p0.set(0.0f, 0.0f);
	p1.set(1.0f, 1.0f);
#else
	p0.set(0.5f / w, 0.5f / h);
	p1.set((w + 0.5f) / w, (h + 0.5f) / h);
#endif
	
	//////////////////////////////////////////////////////////////////////////
	//Set MSAA/NonMSAA rendertarget
#if defined(USE_DX10) || defined(USE_DX11)
	ref_rt& dest_rt = RImplementation.o.dx10_msaa ? rt_Generic : rt_Color;
	u_setrt(dest_rt, nullptr, nullptr, nullptr);
#else
	u_setrt(rt_Generic_0, nullptr, nullptr, nullptr);
#endif		

	RCache.set_CullMode(CULL_NONE);
	RCache.set_Stencil(FALSE);

	//Fill vertex buffer
	FVF::TL* pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
	pv->set(0, float(h), d_Z, d_W, C, p0.x, p1.y); pv++;
	pv->set(0, 0, d_Z, d_W, C, p0.x, p0.y); pv++;
	pv->set(float(w), float(h), d_Z, d_W, C, p1.x, p1.y); pv++;
	pv->set(float(w), 0, d_Z, d_W, C, p1.x, p0.y); pv++;
	RCache.Vertex.Unlock(4, g_combine->vb_stride);

	//Set pass
	RCache.set_Element(s_nightvision->E[ps_r2_nightvision]);

	//Set geometry
	RCache.set_Geometry(g_combine);
	RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);
	
#if defined(USE_DX10) || defined(USE_DX11)
	HW.pContext->CopyResource(rt_Generic_0->pTexture->surface_get(), dest_rt->pTexture->surface_get());
#endif
};


//crookr
void CRenderTarget::phase_fakescope()
{
	//Constants
	u32 Offset = 0;
	u32 C = color_rgba(0, 0, 0, 255);

	float d_Z = EPS_S;
	float d_W = 1.0f;
	float w = float(Device.dwWidth);
	float h = float(Device.dwHeight);

	Fvector2 p0, p1;
#if defined(USE_DX10) || defined(USE_DX11)	
	p0.set(0.0f, 0.0f);
	p1.set(1.0f, 1.0f);
#else
	p0.set(0.5f / w, 0.5f / h);
	p1.set((w + 0.5f) / w, (h + 0.5f) / h);
#endif

	//////////////////////////////////////////////////////////////////////////
	//Set MSAA/NonMSAA rendertarget
#if defined(USE_DX10) || defined(USE_DX11)
	ref_rt& dest_rt = RImplementation.o.dx10_msaa ? rt_Generic : rt_Color;
	u_setrt(dest_rt, nullptr, nullptr, nullptr);

	RCache.set_CullMode(CULL_NONE);
	RCache.set_Stencil(FALSE);

	//Fill vertex buffer
	FVF::TL* pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
	pv->set(0, float(h), d_Z, d_W, C, p0.x, p1.y); pv++;
	pv->set(0, 0, d_Z, d_W, C, p0.x, p0.y); pv++;
	pv->set(float(w), float(h), d_Z, d_W, C, p1.x, p1.y); pv++;
	pv->set(float(w), 0, d_Z, d_W, C, p1.x, p0.y); pv++;
	RCache.Vertex.Unlock(4, g_combine->vb_stride);

	//Set pass
	RCache.set_Element(s_fakescope->E[ps_r2_nightvision]);

	//Set geometry
	RCache.set_Geometry(g_combine);
	RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);

	HW.pContext->CopyResource(rt_Generic_0->pTexture->surface_get(), dest_rt->pTexture->surface_get());
#else
	//Main pass (we avoid write-read from the same buffer)
	u_setrt(rt_Generic_PingPong, nullptr, nullptr, nullptr);

	RCache.set_CullMode(CULL_NONE);
	RCache.set_Stencil(FALSE);

	//Fill vertex buffer
	FVF::TL* pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
	pv->set(0, float(h), d_Z, d_W, C, p0.x, p1.y); pv++;
	pv->set(0, 0, d_Z, d_W, C, p0.x, p0.y); pv++;
	pv->set(float(w), float(h), d_Z, d_W, C, p1.x, p1.y); pv++;
	pv->set(float(w), 0, d_Z, d_W, C, p1.x, p0.y); pv++;
	RCache.Vertex.Unlock(4, g_combine->vb_stride);

	//Set pass
	RCache.set_Element(s_fakescope->E[0]);

	//Set geometry
	RCache.set_Geometry(g_combine);
	RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);

	//Draw to rt_Generic_0
	u_setrt(rt_Generic_0, nullptr, nullptr, nullptr);

	RCache.set_CullMode(CULL_NONE);
	RCache.set_Stencil(FALSE);

	//Fill vertex buffer
	pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
	pv->set(0, float(h), d_Z, d_W, C, p0.x, p1.y); pv++;
	pv->set(0, 0, d_Z, d_W, C, p0.x, p0.y); pv++;
	pv->set(float(w), float(h), d_Z, d_W, C, p1.x, p1.y); pv++;
	pv->set(float(w), 0, d_Z, d_W, C, p1.x, p0.y); pv++;
	RCache.Vertex.Unlock(4, g_combine->vb_stride);

	//Set pass
	RCache.set_Element(s_fakescope->E[1]);

	//Set geometry
	RCache.set_Geometry(g_combine);
	RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);
#endif
};

//--DSR-- HeatVision_start
void CRenderTarget::phase_heatvision()
{
	//Constants
	u32 Offset = 0;
	u32 C = color_rgba(0, 0, 0, 255);

	float d_Z = EPS_S;
	float d_W = 1.0f;
	float w = float(Device.dwWidth);
	float h = float(Device.dwHeight);

	Fvector2 p0, p1;
#if defined(USE_DX10) || defined(USE_DX11)	
	p0.set(0.0f, 0.0f);
	p1.set(1.0f, 1.0f);
#else
	p0.set(0.5f / w, 0.5f / h);
	p1.set((w + 0.5f) / w, (h + 0.5f) / h);
#endif

	//////////////////////////////////////////////////////////////////////////
	//Set MSAA/NonMSAA rendertarget
#if defined(USE_DX10) || defined(USE_DX11)
	ref_rt& dest_rt = RImplementation.o.dx10_msaa ? rt_Generic : rt_Color;
	u_setrt(dest_rt, nullptr, nullptr, nullptr);
#else
	u_setrt(rt_Generic_0, nullptr, nullptr, nullptr);
#endif		

	RCache.set_CullMode(CULL_NONE);
	RCache.set_Stencil(FALSE);

	//Fill vertex buffer
	FVF::TL* pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
	pv->set(0, float(h), d_Z, d_W, C, p0.x, p1.y); pv++;
	pv->set(0, 0, d_Z, d_W, C, p0.x, p0.y); pv++;
	pv->set(float(w), float(h), d_Z, d_W, C, p1.x, p1.y); pv++;
	pv->set(float(w), 0, d_Z, d_W, C, p1.x, p0.y); pv++;
	RCache.Vertex.Unlock(4, g_combine->vb_stride);

	//Set pass
	RCache.set_Element(s_heatvision->E[ps_r2_heatvision]);

	//Set geometry
	RCache.set_Geometry(g_combine);
	RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);

#if defined(USE_DX10) || defined(USE_DX11)
	HW.pContext->CopyResource(rt_Generic_0->pTexture->surface_get(), dest_rt->pTexture->surface_get());
#endif
};
//--DSR-- HeatVision_start

#if defined(USE_DX11)	//  Redotix99: for 3D Shader Based Scopes 		(sorry for using the nightvision phase file)
// pip load the scope glue shaders lazily on first PiP use, they ship in the PiP mod (gamedata/shaders/r3)
void CRenderTarget::EnsureScopeShaders()
{
	if (m_scope_shaders_ready)
		return;
	s_scope_color_write.create("scope_color_write");
	s_scope_depth_write.create("scope_depth_write");
	s_scope_debug.create("scope_debug");
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
	auto bind = [](LPCSTR name, ref_rt& rt)
	{
		if (!rt)
			return;
		ref_texture t;
		t.create(name);
		t->surface_set(rt->pTexture->surface_get());
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
	HW.pContext->CopyResource(rt_secondVP->pSurface, rt_Generic_0->pSurface);
}

// pip DLSS-SR eval, CURRENT body is a passthrough stub. TODO: Ascii replaces this whole body with the
// Streamline SL eval, the SvpDlssInputs signature and the seam call are frozen, no sl::/NGX symbols here.
// the stub keeps rt_secondVP at the SVP render extent so a straight copy works and the scope lens upscales
// it to the on-screen lens (sharp at render_scale 1.0, soft below). Ascii makes rt_secondVP display-res +
// UAV (out_rtv) and his eval reconstructs the display image into it
void CRenderTarget::EvalSVP_DLSS(const SvpDlssInputs& in)
{
	// post-eval state restore (for the future SL eval, the stub needs none, no CS work): after a real eval
	// call RImplementation.Target->SetActive(true) (RCache.Invalidate + full RT/ZB/SRV rebind, mirrors
	// phase_3DSSReticle) then explicitly unbind CS SRV/UAV/shader, RCache does not track the CS stage
	// (UNCONFIRMED, Ascii to verify), RCache is render-thread-local so the restore is single-threaded
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

	for (auto& N : RImplementation.GMBase.RGraph.mapScopeHUDSorted)
	{
		dxRender_Visual* V = N.pVisual;
		if (!V || !N.pMatrix)
			continue;

		CTexture* tex = V->GetTexture();
		// per-lens marker, names the reticle source texture so a capture shows which texture each
		// scope_color_write draw samples as s_reticle (gated on r__gpu_markers)
		PIX_EVENT_F("scope_lens tex=%s", tex ? tex->cName.c_str() : "none");
		if (tex)
			t_reticle->surface_set(tex->surface_get());

		RCache.set_Element(elem);
		RCache.set_xform_world(*N.pMatrix);
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

		bind();
		V->Render(0);
	}

	RImplementation.rmNormal();
	Device.mFullTransform = FTold;
	RCache.set_xform_project(Device.mProject);
}

// pip render reflex-sight lenses (iScopeLense==10) with their own shaders, no-op for an eyepiece-only scope
void CRenderTarget::draw_reflex()
{
	PIX_EVENT_F("RENDER_REFLEX_SIGHTS x%u", (u32)RImplementation.GMBase.RGraph.mapReflexHUDSorted.size());

	Fmatrix FTold = Device.mFullTransform;
	Device.mFullTransform = Device.mFullTransformHud;
	RCache.set_xform_project(Device.mProjectHud);
	RImplementation.rmNear();

	for (auto& N : RImplementation.GMBase.RGraph.mapReflexHUDSorted)
	{
		if (!N.pVisual || !N.pSE || !N.pMatrix)
			continue;
		RCache.set_Element(N.pSE);
		RCache.set_xform_world(*N.pMatrix);
		RImplementation.apply_object(N.pObject);
		RImplementation.apply_lmaterial();
		N.pVisual->Render(0);
	}

	RImplementation.rmNormal();
	Device.mFullTransform = FTold;
	RCache.set_xform_project(Device.mProject);
}

void CRenderTarget::phase_3DSSReticle()
{
	PIX_EVENT(PHASE_SCOPE_RETICLE);

	// pip reticle pipeline, draw_reflex (the red dot) runs at 1x and magnified, the eyepiece lens
	// composite only runs when the magnifier is engaged (IsSVPActive), true_pip off uses the legacy path
	// take the PiP path only when the active optic can drive the SVP (active SVP or a real captured
	// ocular lens), a reflex thermal or alt optic that captures nothing falls through to render_Reticle
	// below so it renders the stock way instead of see through
	const bool svp = Device.m_SecondViewport.IsSVPActive() && RImplementation.TargetSVP;
	const bool has_lens = !RImplementation.GMBase.RGraph.mapScopeHUDSorted.empty() && Device.m_SecondViewport.eyepiece.radius > EPS;
	if (Device.true_pip_on && (svp || has_lens))
	{
		EnsureScopeShaders(); // glue shaders (lazy)

		auto M = RImplementation.TargetMain;
		auto S = RImplementation.TargetSVP;

		// the scope shader reads generic2 as the gbuffer position for the holepunch/depth
		HW.pContext->CopyResource(rt_Generic_2->pTexture->surface_get(), RImplementation.Target->rt_Position->pTexture->surface_get());

		u_setrt(RImplementation.Target->rt_Generic_0, nullptr, RImplementation.Target->rt_Position, RImplementation.Target->baseZB);
		RCache.set_CullMode(CULL_CCW);
		RCache.set_Stencil(FALSE);
		RCache.set_ColorWriteEnable();

		draw_reflex(); // reflex / red dot, both 1x and magnifier

		// composite the eyepiece lens when magnified or a 1x eyepiece was captured, magnified samples
		// the SVP image, 1x / fake-PiP samples a main-frame copy, a pure reflex optic captures no ==3
		if (svp || (!RImplementation.GMBase.RGraph.mapScopeHUDSorted.empty() && Device.m_SecondViewport.eyepiece.radius > EPS))
		{
			// fake-PiP, off-SVP the lens reads a copy of the finished main frame (magnified already filled rt_secondVP)
			if (!svp)
				HW.pContext->CopyResource(M->rt_secondVP->pSurface, M->rt_Generic_0->pSurface);

			// JITTERFIX, cancel the TAA jitter in the VS so the lens edge has no ring
			{ PIX_EVENT(SCOPE_PHASE_JITTERFIX); draw_scope(s_scope_color_write, []() { RCache.set_c("scope_phase", SCOPE_PHASE_JITTERFIX); }); }

			// point the stock-named textures at this viewport's RTs, the SVP image when magnified else
			// the main-frame copy + the main gbuffer (rt_Generic_2 already holds the copied position)
			auto remap = [](LPCSTR name, ref_rt& target) {
				ref_texture t;
				t.create(name);
				t->surface_set(target->pTexture->surface_get());
			};
			remap(r2_RT_secondVP, svp ? S->rt_secondVP : M->rt_secondVP);
			remap(r2_RT_generic2, svp ? S->rt_Position : M->rt_Generic_2);
			remap(r2_RT_heat,     svp ? S->rt_Heat : M->rt_Heat);
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

			// pip lens FX, resample the composited disc through scope_lensfx (CA, barrel, dimming, eye box)
			// two passes swap rt_Generic_0 and rt_Generic_temp so no RT is read while bound for output
			// thermals (3DSS s3ds_image_type 2 or 3, in ps_s3ds_param_3.x) skip it, the feed is an electronic
			// screen with no optical exit pupil so tunnel, dim and eye box make no sense on them
			extern Fvector4 ps_s3ds_param_3;
			const bool lens_thermal = ps_s3ds_param_3.x > 1.5f;
			if ((ps_r__svp_lensfx || ps_r__svp_eyebox > 0.f || ps_r__svp_truepip > 0.f) && !lens_thermal && !s_scope_lensfx)
				s_scope_lensfx.create("scope_lensfx"); // lazy + isolated, a bad compile cannot touch the working scope shaders
			if ((ps_r__svp_lensfx || ps_r__svp_eyebox > 0.f || ps_r__svp_truepip > 0.f) && !lens_thermal && s_scope_lensfx)
			{
				extern float g_pip_scope_magnification;
				const float mag = g_pip_scope_magnification;
				const float st = ps_r__svp_lensfx ? ps_r__svp_lensfx_strength : 0.f; // lens FX off but eye-box on -> no tunnel/blur
				// exit-pupil dim: brightness ~ (REF/mag)^2, floored, scaled by strength
				const float REF = 4.0f;
				float ep = (REF * REF) / (mag * mag);
				if (ep > 1.0f) ep = 1.0f; else if (ep < 0.40f) ep = 0.40f;
				ep = 1.0f + (ep - 1.0f) * st;
				// reset screen_res to the main target res (the IMAGE pass left it at SVP res)
				const float sw = (float)M->Width, sh = (float)M->Height;
				ref_texture src;
				src.create("$user$pip_lensfx_src");

				// pass 1, FX the disc into the scratch RT sampling the live composited frame
				src->surface_set(M->rt_Generic_0->pTexture->surface_get());
				RCache.Invalidate();
				u_setrt(M->rt_Generic_temp, nullptr, nullptr, M->baseZB);
				RCache.set_CullMode(CULL_CCW);
				RCache.set_Stencil(FALSE);
				RCache.set_ColorWriteEnable();
				draw_scope(s_scope_lensfx, [ep, st, sw, sh, mag]() {
					RCache.set_c("scope_phase", 0); // scope_vertex.vs only jitters hpos under JITTERFIX, keep it clean
					RCache.set_c("screen_res", sw, sh, 1.0f / sw, 1.0f / sh);
					// lens model constants, all live cvars. params: CA, barrel, tunnel floor, exit-pupil
					RCache.set_c("lensfx_params",  ps_r__svp_lens_ca, ps_r__svp_lens_distort, ps_r__svp_lens_floor, ep);
					RCache.set_c("lensfx_params2", mag, ps_r__svp_lens_vigk, ps_r__svp_lens_refmag, st);
					RCache.set_c("lensfx_params3", ps_r__svp_lens_blur, 0.0f, 0.0f, 0.0f);
					// eye-box: truepip canonical (exit-pupil clear zone + eye-relief-scaled offset) or the
					// legacy crescent. svp_eyebox.xy = engine bore-vs-aim drift (tan)
					const Fvector4& eb = Device.m_SecondViewport.svp_eyebox;
					if (ps_r__svp_truepip > 0.f)
					{
						// fully-auto physical eye-box from REAL geometry: eye relief = main-camera -> eyepiece
						// distance, objective from the scope geometry, exit pupil = objective / live mag. the
						// clear zone (innerR) grows with the exit pupil (low mag forgiving, high mag fussy) and
						// the eye offset is the real bore-vs-aim drift scaled into exit-pupil radii. inputs are
						// real geometry, only optics_gain (feel) + optics_soft (falloff width) are tunables
						auto& vp = Device.m_SecondViewport;
						const float R = (vp.eyepiece.radius > 1e-5f) ? vp.eyepiece.radius : 1e-5f;
						const float Robj = (vp.objective.radius > 1e-5f) ? vp.objective.radius : (R * 1.4f);
						const float L = vp.eyepiece.m_W.c.distance_to(Device.vCameraPosition); // eye relief (world)
						const float m = (mag > 0.1f) ? mag : 0.1f;
						const float xp_ratio = (Robj / m) / R;             // exit-pupil radius / eyepiece radius
						float innerR = 0.30f * xp_ratio + 0.30f;           // bigger exit pupil -> bigger clear zone
						if (innerR < 0.28f) innerR = 0.28f; else if (innerR > 0.62f) innerR = 0.62f;
						const float outerR = innerR + ps_r__svp_optics_soft;
						const float K = (L * m) / Robj;                    // eye offset per unit drift (exit-pupil radii)
						const float k = K * 0.10f * ps_r__svp_optics_gain; // -> lens-local bright-circle shift
						RCache.set_c("lensfx_eyebox", eb.x * k, eb.y * k, innerR, outerR);
						RCache.set_c("lensfx_ctrl", 0.0f, 1.0f, ps_r__svp_truepip, 0.0f);
					}
					else
					{
						RCache.set_c("lensfx_eyebox", eb.x, eb.y, ps_r__svp_eyebox, ps_r__svp_eyebox_shift);
						RCache.set_c("lensfx_ctrl", 0.0f, 0.0f, 0.0f, 0.0f);
					}
				});

				// pass 2, copy the FX'd disc back into the main frame sampling the scratch RT
				src->surface_set(M->rt_Generic_temp->pTexture->surface_get());
				RCache.Invalidate();
				u_setrt(M->rt_Generic_0, nullptr, nullptr, M->baseZB);
				RCache.set_CullMode(CULL_CCW);
				RCache.set_Stencil(FALSE);
				RCache.set_ColorWriteEnable();
				draw_scope(s_scope_lensfx, [sw, sh]() {
					RCache.set_c("scope_phase", 0); // keep hpos un-jittered for the copy-back too
					RCache.set_c("screen_res", sw, sh, 1.0f / sw, 1.0f / sh);
					RCache.set_c("lensfx_ctrl", 1.0f, 0.0f, 0.0f, 0.0f);
				});
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

			// re-draw the reflex on top of the composited lens, the IMAGE pass painted over the first
			// draw_reflex and the magnified image carries no reticle of its own, drawn at the main-view
			// position so it lands centered in the lens
			u_setrt(M->rt_Generic_0, nullptr, M->rt_Position, M->baseZB);
			RCache.set_CullMode(CULL_CCW);
			RCache.set_Stencil(FALSE);
			RCache.set_ColorWriteEnable();
			draw_reflex();
		}

		// clear the capture maps, nothing else clears them in the true_pip path and a stale entry would
		// leave the lens floating after a weapon-model swap (deriveScopeLens already read them this frame)
		RImplementation.GMBase.RGraph.mapScopeHUDSorted.clear();
		RImplementation.GMBase.RGraph.mapScopeHUDObjective.clear();
		RImplementation.GMBase.RGraph.mapReflexHUDSorted.clear();

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

	// pip red dot reflexes are captured into mapReflexHUDSorted under true_pip (and removed from the scope
	// bucket render_Reticle draws), so draw them here for the fallback optics on top of the thermal, does
	// nothing when true_pip is off and the map is empty so the stock path is unchanged
	u_setrt(RImplementation.Target->rt_Generic_0, RImplementation.Target->rt_Position, 0, HW.pBaseZB);
	RCache.set_CullMode(CULL_CCW);
	RCache.set_Stencil(FALSE);
	RCache.set_ColorWriteEnable();
	draw_reflex();

	// pip clear the PiP capture maps on the fallback too, under true_pip they may be populated and the
	// PiP path (skipped here) is the only other place that clears them, avoids a floating lens after a swap
	RImplementation.GMBase.RGraph.mapScopeHUDSorted.clear();
	RImplementation.GMBase.RGraph.mapScopeHUDObjective.clear();
	RImplementation.GMBase.RGraph.mapReflexHUDSorted.clear();
};
#endif
