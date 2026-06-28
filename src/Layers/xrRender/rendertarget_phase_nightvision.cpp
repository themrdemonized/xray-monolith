#include "stdafx.h"
#include "xrRender_console.h"   // scope_debug, scope_objective_lens_offset
#include "FBasicVisual.h"       // dxRender_Visual (PiP draw_scope)
#include "SkeletonX.h"          // CSkeletonX (PiP lens bone skinning matrix)

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

// pip compute the front and second focal-plane world points from the eyepiece and objective lenses
void ffp_sfp()
{
	auto e = Device.m_SecondViewport.eyepiece;
	auto o = Device.m_SecondViewport.objective;

	Fvector p_e = { 0,0,0 }; e.m_W.transform(p_e);
	Fvector p_o = { 0,0,0 }; o.m_W.transform(p_o);

	if (o.radius < EPS)
	{	// We have to have an objective lens or ffp/sfp wont work, so make one up
		float cm = 0.01f;
		float distance = (10 * cm); // Something scopelike
		o.radius = e.radius;
		p_o = { 0,0, distance };
		e.m_W.transform(p_o);
	}

	{	// Not all scopes have eyepiece and objective inline, reproject the objective directly in
		// front of the eyepiece (we sure aren't going to simulate prisms)
		float distance = p_o.distance_to(p_e);
		Fvector dir = { 0,0,1 };
		o.m_W.transform_dir(dir);
		p_o.set(dir.mul(distance).add(p_e));
	}

	Fvector p_d = Fvector(p_o).sub(p_e);
	Fvector p_c1 = Fvector(p_d).mul(0.4f).add(p_e);
	Fvector p_c2 = Fvector(p_d).mul(0.6f).add(p_e);

	Device.m_SecondViewport.w_ffp = Fvector(p_c1).add(p_e).mul(0.5f);
	Device.m_SecondViewport.w_sfp = Fvector(p_c2).add(p_o).mul(0.5f);
}

// pip re-draw each scope HUD mesh through the engine shader element se for the phase the bind lambda sets
// binds the mesh texture to t_reticle and derives the eyepiece and objective from the lens sphere
void CRenderTarget::draw_scope(ref_shader se, std::function<void(R_dsgraph::mapSorted_Node* N)> bind)
{
	auto elem = se ? se->E[0] : nullptr;
	if (!elem)
		return;

	Fmatrix FTold = Device.mFullTransform;
	Device.mFullTransform = Device.mFullTransformHud;
	RCache.set_xform_project(Device.mProjectHud);

	RImplementation.rmNear();

	for (auto N : RImplementation.mapScopeHUDSorted)
	{
		dxRender_Visual* V = N.val.pVisual;

		// per-lens marker (r__gpu_markers), nests under the caller marker, lense = iScopeLense
		PIX_EVENT_F("scope_lens lense=%d V=%p", N.val.se ? (int)N.val.se->flags.iScopeLense : -1, (void*)V);

		auto tex = V->GetTexture();
		if (tex)
			t_reticle->surface_set(tex->surface_get());

		RCache.set_Element(elem);
		RCache.set_xform_world(N.val.Matrix);
		RImplementation.apply_object(N.val.pObject);
		RImplementation.apply_lmaterial();

		auto set_v3 = [](LPCSTR id, Fvector3 v) -> void {
			RCache.set_c(id, v.x, v.y, v.z, 1.0f);
		};

		RCache.set_c("scope_svp", (int)Device.m_SecondViewport.IsSVPActive());
		RCache.set_c("scope_debug", (int)scope_debug);
		set_v3("scope_w_ffp", Device.m_SecondViewport.w_ffp);
		set_v3("scope_w_sfp", Device.m_SecondViewport.w_sfp);
		Fvector pt = { 0,0,0 };
		Device.m_SecondViewport.eyepiece.m_W.transform(pt);
		set_v3("scope_w_eyepiece", pt);

		bind(&N);
		V->Render(0);

		auto p = &Device.m_SecondViewport;

		// Derive the lens geometry ONCE per frame (consumed by svpCamera + the scope shader)
		static u32 dwFrame = 0;
		if (Device.dwFrame > dwFrame)
		{
			dwFrame = Device.dwFrame;

			p->eyepiece.radius = 0.f;
			p->objective.radius = 0.f;

			// a skinned scope lens is positioned by its bone, the captured matrix is only the kinematics
			// root, fold in the lens bone skinning matrix so the eyepiece follows the glass on ADS and sway
			// base off the captured per lens matrix like MT, get_xform_world here is read after V->Render and
			// lands ~15cm off the captured root, that mismatch placed the eyepiece high and blacked the exit pupil
			auto m_W = N.val.Matrix;
			if (CSkeletonX* sk = fast_dynamic_cast<CSkeletonX*>(N.val.pVisual))
			{
				Fmatrix boneR;
				if (sk->SVP_LensBoneXform(boneR))
					m_W.mulB_43(boneR);
			}

			auto& Vd = V->getVisData();
			Fvector c;
			Vd.box.getcenter(c); // AABB center fits a flat lens disc tighter than the bounding sphere center
			m_W.mulB_43(Fmatrix().translate(c));

			p->eyepiece.m_W = m_W;
			p->eyepiece.radius = Vd.sphere.R;

			if (p->eyepiece.radius > EPS)
			{
				// pip capture the true bore before stabilization reduces it (for the eye-box shadow)
				p->svp_bore_fwd.set(p->eyepiece.m_W.k); p->svp_bore_fwd.normalize();

				// pip steady-scope: blend the lens orientation toward aim to reduce magnified sway (off = 1:1)
				if (ps_r__svp_stabilize > EPS)
				{
					const float keep = 1.0f - ps_r__svp_stabilize; // fraction of the real sway retained
					Fvector aim; aim.set(Device.vCameraDirection); aim.normalize();
					Fvector f; f.set(p->eyepiece.m_W.k); f.normalize();
					f.lerp(aim, f, keep); f.normalize(); // blend bone forward toward aim
					Fvector wup = {0.f, 1.f, 0.f}, right, up;
					right.crossproduct(wup, f);
					if (right.magnitude() > EPS_S)
					{
						right.normalize();
						up.crossproduct(f, right); up.normalize();
						p->eyepiece.m_W.i.set(right);
						p->eyepiece.m_W.j.set(up);
						p->eyepiece.m_W.k.set(f);
					}
				}

				// objective: prefer the REAL front lens from the mesh (mapScopeHUDObjective) placed along the
				// stabilized optical axis, else derive it a fixed scope-length forward of the eyepiece (eyepiece radii)
				extern float ps_r__svp_obj_dist, ps_r__svp_obj_size;
				bool have_obj = false;
				for (auto& ON : RImplementation.mapScopeHUDObjective)
				{
					if (!ON.val.pVisual) break;
					Fmatrix oX = ON.val.Matrix;
					if (CSkeletonX* osk = fast_dynamic_cast<CSkeletonX*>(ON.val.pVisual))
					{
						Fmatrix oboneR;
						if (osk->SVP_LensBoneXform(oboneR)) oX.mulB_43(oboneR);
					}
					auto& OV = ON.val.pVisual->getVisData();
					Fvector oc; OV.box.getcenter(oc);
					Fvector ow; oX.transform_tiny(ow, oc);
					if (OV.sphere.R > EPS && ow.distance_to(p->eyepiece.m_W.c) > p->eyepiece.radius * 0.5f) // distinct from the eyepiece
					{
						p->objective.m_W = p->eyepiece.m_W; // stabilized optical axis
						p->objective.m_W.c.set(ow);          // real front-lens world position
						p->objective.radius = OV.sphere.R;
						have_obj = true;
					}
					break;
				}
				if (!have_obj)
				{
					Fvector ofwd; ofwd.set(p->eyepiece.m_W.k); ofwd.normalize();
					p->objective.m_W = p->eyepiece.m_W;
					p->objective.m_W.c.mad(ofwd, p->eyepiece.radius * 14.0f * ps_r__svp_obj_dist);
					p->objective.radius = p->eyepiece.radius * ps_r__svp_obj_size;
				}

				ffp_sfp();
			}
		}
	}

	RImplementation.rmNormal();

	// Restore projection
	Device.mFullTransform = FTold;
	RCache.set_xform_project(Device.mProject);

	RImplementation.o.distortion = FALSE;
}

// pip reflex-sight re-draw, reflex meshes keep their own shader element N.val.se and draw in HUD projection
void CRenderTarget::draw_reflex()
{
	PIX_EVENT(RENDER_REFLEX_SIGHTS);

	Fmatrix FTold = Device.mFullTransform;

	Device.mFullTransform = Device.mFullTransformHud;
	RCache.set_xform_project(Device.mProjectHud);

	// Rendering
	RImplementation.rmNear();
	for (auto N : RImplementation.mapReflexHUDSorted)
	{
		PIX_EVENT_F("reflex lense=%d V=%p", N.val.se ? (int)N.val.se->flags.iScopeLense : -1, (void*)N.val.pVisual);
		RCache.set_Element(N.val.se);

		RCache.set_xform_world(N.val.Matrix);
		RImplementation.apply_object(N.val.pObject);
		RImplementation.apply_lmaterial();

		N.val.pVisual->Render(0);
	}

	RImplementation.rmNormal();

	// Restore projection
	Device.mFullTransform = FTold;
	RCache.set_xform_project(Device.mProject);
}

//  Redotix99: for 3D Shader Based Scopes 		(sorry for using the nightvision phase file)
void CRenderTarget::phase_3DSSReticle()
{
	// pip legacy see-through fallback, runs whenever no real SVP is active
	// the gc64 colour path below needs a magnifications config so route fake optics here
	if (!Device.m_SecondViewport.IsSVPActive())
	{
		// pip legacy see-through reticle, samples rt_Generic_temp a copy of rt_Generic_0
		PIX_EVENT_F("3DSSReticle LEGACY (fake / no real SVP)");
		HW.pContext->CopyResource(rt_Generic_2->pTexture->surface_get(), RImplementation.Target->rt_Position->pTexture->surface_get());
		HW.pContext->CopyResource(rt_Generic_temp->pTexture->surface_get(), rt_Generic_0->pTexture->surface_get());

		u_setrt(RImplementation.Target->rt_Generic_0, RImplementation.Target->rt_Position, 0, HW.pBaseZB);

		RCache.set_CullMode(CULL_CCW);
		RCache.set_Stencil(FALSE);
		RCache.set_ColorWriteEnable();

		// pip draw the reflex at 1x too, otherwise it only draws in the magnified path
		draw_reflex();

		// Legacy render_Reticle / r_dsgraph_render_ScopeSorted (sorted_L1), adapted to the vector
		Fmatrix FTold = Device.mFullTransform;
		Device.mFullTransform = Device.mFullTransformHud;
		RCache.set_xform_project(Device.mProjectHud);
		RImplementation.rmNear();
		RImplementation.o.distortion = RImplementation.o.distortion_enabled;
		for (auto N : RImplementation.mapScopeHUDSorted)
		{
			if (!N.val.se)
				continue;
			PIX_EVENT_F("legacy_reticle lense=%d V=%p", (int)N.val.se->flags.iScopeLense, (void*)N.val.pVisual);
			RCache.set_Element(N.val.se);
			RCache.set_xform_world(N.val.Matrix);
			RImplementation.apply_object(N.val.pObject);
			RImplementation.apply_lmaterial();
			N.val.pVisual->Render(0);
		}
		RImplementation.o.distortion = FALSE;
		RImplementation.rmNormal();
		Device.mFullTransform = FTold;
		RCache.set_xform_project(Device.mProject);
		return;
	}

	PIX_EVENT(PHASE_SCOPE_RETICLE);
	HW.pContext->CopyResource(rt_Generic_2->pTexture->surface_get(), RImplementation.Target->rt_Position->pTexture->surface_get());

	// Fakescope (r__svpscope 0): no SVP pass ran, so capture the MAIN lit scene to sample for the
	// digital-zoom fake (scope_color_write's isSVPActive()==0 branch). True PiP captures in phase_svp_capture
	if (!Device.m_SecondViewport.IsSVPActive())
		HW.pContext->CopyResource(rt_secondVP->pTexture->surface_get(), rt_Generic_0->pTexture->surface_get());

	u_setrt(RImplementation.Target->rt_Generic_0, nullptr, RImplementation.Target->rt_Position, RImplementation.Target->baseZB);

	RCache.set_CullMode(CULL_CCW);
	RCache.set_Stencil(FALSE);
	RCache.set_ColorWriteEnable();

	draw_reflex();

	{	PIX_EVENT(SCOPE_PHASE_JITTERFIX);
		// Write a dark color to the jittered geometry position to ensure no light/sky bleed
		draw_scope(s_scope_color_write, [](auto N) -> void {
			RCache.set_c("scope_phase", SCOPE_PHASE_JITTERFIX);
		});
	}

	{	// Remap gbuffer bindings to where the scope was rendered (SVP target vs main)
		auto f = [&](LPCSTR name, ref_rt& target) -> void {
			ref_texture t;
			t.create(name);
			RCache.override_Texture(t->cName, target->pTexture);
		};

		auto svp = Device.m_SecondViewport.IsSVPActive();

		f(r2_RT_secondVP, svp ? RImplementation.TargetSVP->rt_secondVP : RImplementation.TargetMain->rt_secondVP);
		f(r2_RT_generic2, svp ? RImplementation.TargetSVP->rt_Position : RImplementation.TargetMain->rt_Generic_2);
		f(r2_RT_heat,     svp ? RImplementation.TargetSVP->rt_Heat : RImplementation.TargetMain->rt_Heat);

		u_setrt(RImplementation.TargetMain->rt_Generic_0, nullptr, RImplementation.TargetMain->rt_Position, RImplementation.TargetMain->baseZB);
	}

	{	PIX_EVENT(SCOPE_PHASE_IMAGE);
		draw_scope(s_scope_color_write, [](auto N) -> void {
			RCache.set_c("scope_phase", SCOPE_PHASE_IMAGE);

			{	// Set screen_res to the gbuffer size (SVP square in true PiP, main otherwise)
				auto t = Device.m_SecondViewport.IsSVPActive() ? RImplementation.TargetSVP : RImplementation.TargetMain;
				RCache.set_c("screen_res", Fvector4({ (float)t->Width, (float)t->Height, 1.0f / (float)t->Width, 1.0f / (float)t->Height }));
			}

			{	// Resolution of the view we are rendering into
				auto t = RImplementation.TargetMain;
				RCache.set_c("output_res", Fvector4({ (float)t->Width, (float)t->Height, 1.0f / (float)t->Width, 1.0f / (float)t->Height }));
			}

			auto P = Device.m_SecondViewport;
			Fvector up = { 0,1,0 };
			P.objective.m_W.transform_dir(up);
			Device.mView.transform_dir(up);

			up.z = 0.0;
			up.normalize();
			float angle = acos(up.dotproduct({ 0,1,0 })) * (up.x > 0 ? 1 : -1);

			RCache.set_c("hack_tex_angle", angle);
		});
	}

	// pip lens FX, resample the composited disc through scope_lensfx (CA, barrel, dimming, eye box)
	// two passes swap rt_Generic_0 and rt_Generic_temp so no RT is read while bound for output
	// thermals (3DSS s3ds_image_type 2 or 3, in ps_s3ds_param_3.x) skip it, the feed is an electronic
	// screen with no optical exit pupil so tunnel, dim and eye box make no sense on them
	extern Fvector4 ps_s3ds_param_3;
	extern float ps_r__svp_truepip;
	const bool lens_thermal = ps_s3ds_param_3.x > 1.5f;
	if ((ps_r__svp_lensfx || ps_r__svp_eyebox > 0.f || ps_r__svp_truepip > 0.f) && !lens_thermal && !s_scope_lensfx)
		s_scope_lensfx.create("scope_lensfx"); // lazy + isolated, a bad compile cannot touch the working scope shaders
	if ((ps_r__svp_lensfx || ps_r__svp_eyebox > 0.f || ps_r__svp_truepip > 0.f) && !lens_thermal && s_scope_lensfx)
	{
		auto* M = RImplementation.TargetMain;
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
		draw_scope(s_scope_lensfx, [ep, st, sw, sh, mag](auto N) {
			RCache.set_c("scope_phase", 0); // scope_vertex.vs only jitters hpos under JITTERFIX, keep it clean
			RCache.set_c("screen_res", sw, sh, 1.0f / sw, 1.0f / sh);
			// lens model constants, all live cvars. params: CA, barrel, tunnel floor, exit-pupil
			RCache.set_c("lensfx_params",  ps_r__svp_lens_ca, ps_r__svp_lens_distort, ps_r__svp_lens_floor, ep);
			RCache.set_c("lensfx_params2", mag, ps_r__svp_lens_vigk, ps_r__svp_lens_refmag, st);
			extern float ps_r__svp_dof; extern float ps_r__svp_dof_onset;
			RCache.set_c("lensfx_params3", ps_r__svp_lens_blur, ps_r__svp_dof, ps_r__svp_dof_onset, 0.0f);
			extern float ps_r__svp_glass_dirt; extern float ps_r__svp_glass_rim;
			RCache.set_c("lensfx_glass", ps_r__svp_glass_dirt, ps_r__svp_glass_rim, 8.0f, 0.0f);
			extern float ps_r__svp_lens_fringe; extern float ps_r__svp_lens_vignette; extern float ps_r__svp_lens_vignette_r;
			RCache.set_c("lensfx_optics", ps_r__svp_lens_fringe, 0.0f, ps_r__svp_lens_vignette, ps_r__svp_lens_vignette_r);
			// eye-box: truepip canonical (exit-pupil clear zone + eye-relief-scaled offset) or the
			// legacy crescent, svp_eyebox.xy = engine bore-vs-aim drift (tan)
			const Fvector4& eb = Device.m_SecondViewport.svp_eyebox;
			if (ps_r__svp_truepip > 0.f)
			{
				extern float ps_r__svp_optics_gain, ps_r__svp_optics_soft;
				auto& vp = Device.m_SecondViewport;
				const float R = (vp.eyepiece.radius > 1e-5f) ? vp.eyepiece.radius : 1e-5f;
				const float Robj = (vp.objective.radius > 1e-5f) ? vp.objective.radius : (R * 1.4f);
				const float m = (mag > 0.1f) ? mag : 0.1f;
				// real per-scope optics from the 3DSS config (s3ds_param_1.y = eye relief cm, .z = exit-pupil /
				// ocular ratio), fall back to scope geometry when a scope has no 3DSS optics entry (.z stays 0)
				extern Fvector4 ps_s3ds_param_1;
				extern float ps_r__svp_optics_real;
				const bool use_real = (ps_r__svp_optics_real > 0.f && ps_s3ds_param_1.z > 1e-4f);
				float xp_ratio;
				if (use_real)
				{
					const float xp_static = ps_s3ds_param_1.z;
					extern int ps_r__svp_optics_zoomvig; extern float ps_r__svp_optics_zoomvig_blend;
					if (ps_r__svp_optics_zoomvig != 0)
					{
						const float xp_zoom = xp_static / m; // exit pupil tightens as magnification rises
						float b = ps_r__svp_optics_zoomvig_blend; b = (b < 0.f) ? 0.f : ((b > 1.f) ? 1.f : b);
						xp_ratio = xp_static + (xp_zoom - xp_static) * b;
					}
					else xp_ratio = xp_static;
				}
				else xp_ratio = (Robj / m) / R; // geometry fallback (exit-pupil radius / ocular radius)
				const float L = use_real ? (ps_s3ds_param_1.y * 0.01f) : vp.eyepiece.m_W.c.distance_to(Device.vCameraPosition);
				float innerR = 0.30f * xp_ratio + 0.30f; // bigger exit pupil -> bigger clear zone
				if (innerR < 0.28f) innerR = 0.28f; else if (innerR > 0.62f) innerR = 0.62f;
				const float K = L / (xp_ratio * R); // eye offset per unit drift
				const float k = K * (use_real ? 0.80f : 0.10f) * ps_r__svp_optics_gain;
				extern float ps_r__svp_eyebox_aspect, ps_r__svp_eyebox_relief, ps_r__svp_eyebox_relief_size, ps_r__svp_eyebox_relief_soft;
				RCache.set_c("lensfx_eyebox", eb.x * k, eb.y * k, innerR, ps_r__svp_optics_soft);
				RCache.set_c("lensfx_eyebox2", ps_r__svp_eyebox_relief_size, ps_r__svp_eyebox_relief_soft, ps_r__svp_eyebox_aspect, ps_r__svp_eyebox_relief);
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
		draw_scope(s_scope_lensfx, [sw, sh](auto N) {
			RCache.set_c("scope_phase", 0); // keep hpos un-jittered for the copy-back too
			RCache.set_c("screen_res", sw, sh, 1.0f / sw, 1.0f / sh);
			RCache.set_c("lensfx_ctrl", 1.0f, 0.0f, 0.0f, 0.0f);
		});
	}

	RImplementation.TargetMain->SetActive(true); // Force set main target to fully invalidate rcache
	u_setrt(RImplementation.TargetMain->rt_Generic_0, nullptr, RImplementation.TargetMain->rt_Position, RImplementation.TargetMain->baseZB);

	{	PIX_EVENT(SCOPE_PHASE_RETICLE);
		draw_scope(s_scope_color_write, [](auto N) -> void {
			RCache.set_c("scope_phase", SCOPE_PHASE_RETICLE);
		});
	}

	{	PIX_EVENT(SCOPE_PHASE_SHADOW);
		draw_scope(s_scope_color_write, [](auto N) -> void {
			RCache.set_c("scope_phase", SCOPE_PHASE_SHADOW);
		});
	}

	{	PIX_EVENT(SCOPE_PHASE_LENS);
		draw_scope(s_scope_color_write, [](auto N) -> void {
			RCache.set_c("scope_phase", SCOPE_PHASE_LENS);
		});
	}

	{	PIX_EVENT(SCOPE_PHASE_CUSTOM_DEPTH);
		// Allow a custom shader to override the depth for DOF calculations
		u_setrt(RImplementation.Target->rt_Position, nullptr, nullptr, nullptr, RImplementation.Target->baseZB);
		draw_scope(s_scope_depth_write, [](auto _) -> void {
			RCache.set_c("scope_phase", SCOPE_PHASE_DEPTHWRITE | SCOPE_PHASE_CUSTOM_DEPTH);
			RCache.set_c("scope_depth_value", 1);
		});
	}

	u_setrt(RImplementation.Target->rt_Generic_0, RImplementation.Target->rt_Position, RImplementation.Target->baseZB);
};

/** Run scope preprocess on the current frame and store in the SVP rt.
  * (Handle anything that needs to read from the g-buffer here.)
  */
void CRenderTarget::phase_svp_capture()
{
	PIX_EVENT(PHASE_SCOPE_SVP_CAPTURE);
	CRenderTarget* svp = RImplementation.TargetSVP;
	if (ps_r__svp_dlss != 0)
	{
		// pip DLSS seam, assemble the eval inputs from the SVP target + the cached constants + the
		// read-cleared reset flag, then run EvalSVP_DLSS (bilinear stub for now, the SL eval replaces it)
		SvpDlssInputs in;
		auto& vp = Device.m_SecondViewport;
		in.viewport_id = 1; // stable SVP handle for DLSS history (main = 0), NOT the per-frame dwViewport
		in.color_srv = svp->rt_Generic_0->pTexture->get_SRView();
		in.render_extent = { (u32)svp->Width, (u32)svp->Height };
		in.depth_srv = svp->rt_baseZB ? svp->rt_baseZB->pTexture->get_SRView() : nullptr;
		in.mvec_srv = svp->rt_ssfx_motion_vectors->pTexture->get_SRView();
		in.out_rtv = svp->rt_secondVP->pRT;
		in.out_uav = svp->rt_secondVP->pUAView;
		in.display_extent = { (u32)svp->Width, (u32)svp->Height }; // stub rt_secondVP follows the render extent, Ascii makes it display-res
		in.view = Device.matrices[1].mView;
		in.proj = Device.matrices[1].mProject;
		in.view_proj.mul(Device.matrices[1].mProject, Device.matrices[1].mView);
		in.prev_view = Device.matrices_previous[1].mView;
		in.prev_proj = Device.matrices_previous[1].mProject;
		in.prev_view_proj.mul(Device.matrices_previous[1].mProject, Device.matrices_previous[1].mView);
		in.jitter_px = vp.svp_jitter_px;
		in.near_plane = vp.svp_near; in.far_plane = vp.svp_far; in.fov = vp.svp_fov; in.aspect = vp.svp_aspect;
		in.cam_pos = vp.svp_cam_pos; in.up = vp.svp_up; in.right = vp.svp_right; in.fwd = vp.svp_fwd;
		in.reset = vp.dlss_reset_next; vp.dlss_reset_next = false; // read + clear, single-threaded so no atomic exchange
		svp->EvalSVP_DLSS(in);
		return;
	}
	// This copy is not necessary, remove in the future and read directly from rt_Generic_0
	HW.pContext->CopyResource(svp->rt_secondVP->pSurface, svp->rt_Generic_0->pSurface);
}

// pip DLSS-SR eval, CURRENT body is a bilinear passthrough stub. TODO: Ascii replaces this whole body
// with the Streamline SL eval, the SvpDlssInputs signature and the seam call are frozen, no sl::/NGX
// symbols here. equal extents (render_scale 1.0) = exact copy, scaled = a soft upscale blit
void CRenderTarget::EvalSVP_DLSS(const SvpDlssInputs& in)
{
	// post-eval state restore (for the future SL eval, the stub needs none, no CS work): after a real eval
	// call RImplementation.Target->SetActive(true) (RCache.Invalidate + full RT/ZB/SRV rebind, mirrors
	// phase_3DSSReticle) then explicitly unbind CS SRV/UAV/shader, RCache does not track the CS stage
	// (UNCONFIRMED, Ascii to verify), RCache is render-thread-local so the restore is single-threaded
	// CURRENT body is a 1:1 CopyResource (rt_secondVP follows the render extent), the SL eval replaces it
	HW.pContext->CopyResource(rt_secondVP->pSurface, rt_Generic_0->pSurface);
}
#endif
