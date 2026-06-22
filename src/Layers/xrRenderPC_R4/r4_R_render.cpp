#include "stdafx.h"
#include "../../xrEngine/igame_persistent.h"
#include "../xrRender/FBasicVisual.h"
#include "../../xrEngine/customhud.h"
#include "../../xrEngine/xr_object.h"

#include "../xrRender/QueryHelper.h"
#include "../../Include/xrAPI/xrAPI.h"          // pip: DRender (debug-line backend for the scope_debug overlays)
#include "../../Include/xrRender/DebugRender.h" // pip: IDebugRender::add_lines
#if defined(USE_DX11)
#include "../../../gamedata/shaders/r3/scope_defines.h" // SCOPE_PHASE_* (kept in sync with the shader)
#endif

// pip SVP combine widens the NVG tube radius via shader_param_7.y, declared locally
extern Fvector4 ps_dev_param_7;

IC bool pred_sp_sort(ISpatial* _1, ISpatial* _2)
{
	float d1 = _1->spatial.sphere.P.distance_to_sqr(Device.vCameraPosition);
	float d2 = _2->spatial.sphere.P.distance_to_sqr(Device.vCameraPosition);
	return d1 < d2;
}

void CRender::render_main(Fmatrix& m_ViewProjection, bool _fportals)
{
	PIX_EVENT(render_main);
	//	Msg						("---begin");
	marker ++;

	// Calculate sector(s) and their objects
	if (pLastSector)
	{
		//!!!
		//!!! BECAUSE OF PARALLEL HOM RENDERING TRY TO DELAY ACCESS TO HOM AS MUCH AS POSSIBLE
		//!!!
		{
			// Traverse object database
			g_SpatialSpace->q_frustum
			(
				lstRenderables,
				ISpatial_DB::O_ORDERED,
				STYPE_RENDERABLE + STYPE_LIGHTSOURCE,
				ViewBase
			);

			// (almost) Exact sorting order (front-to-back)
			std::sort(lstRenderables.begin(), lstRenderables.end(), pred_sp_sort);

			// Determine visibility for dynamic part of scene
			set_Object(0);
			u32 uID_LTRACK = 0xffffffff;
			if (phase == PHASE_NORMAL)
			{
				uLastLTRACK ++;
				if (lstRenderables.size()) uID_LTRACK = uLastLTRACK % lstRenderables.size();

				// update light-vis for current entity / actor
				CObject* O = g_pGameLevel->CurrentViewEntity();
				if (O)
				{
					CROS_impl* R = (CROS_impl*)O->ROS();
					if (R) R->update(O);
				}

				// update light-vis for selected entity
				// track lighting environment
				if (lstRenderables.size())
				{
					IRenderable* renderable = lstRenderables[uID_LTRACK]->dcast_Renderable();
					if (renderable)
					{
						CROS_impl* T = (CROS_impl*)renderable->renderable_ROS();
						if (T) T->update(renderable);
					}
				}
			}
		}

		// Traverse sector/portal structure
		PortalTraverser.traverse
		(
			pLastSector,
			ViewBase,
			Device.vCameraPosition,
			m_ViewProjection,
			CPortalTraverser::VQ_HOM + CPortalTraverser::VQ_SSA + CPortalTraverser::VQ_FADE
			//. disabled scissoring (HW.Caps.bScissor?CPortalTraverser::VQ_SCISSOR:0)	// generate scissoring info
		);

		// Determine visibility for static geometry hierrarhy
		for (u32 s_it = 0; s_it < PortalTraverser.r_sectors.size(); s_it++)
		{
			CSector* sector = (CSector*)PortalTraverser.r_sectors[s_it];
			dxRender_Visual* root = sector->root();
			for (u32 v_it = 0; v_it < sector->r_frustums.size(); v_it++)
			{
				set_Frustum(&(sector->r_frustums[v_it]));
				add_Geometry(root);
			}
		}

		// Traverse frustums
		for (u32 o_it = 0; o_it < lstRenderables.size(); o_it++)
		{
			ISpatial* spatial = lstRenderables[o_it];
			spatial->spatial_updatesector();
			CSector* sector = (CSector*)spatial->spatial.sector;
			if (0 == sector) continue; // disassociated from S/P structure

			if (spatial->spatial.type & STYPE_LIGHTSOURCE)
			{
				// lightsource
				light* L = (light*)(spatial->dcast_Light());
				VERIFY(L);
				float lod = L->get_LOD();
				if (lod > EPS_L)
				{
					vis_data& vis = L->get_homdata();
					if (HOM.visible(vis)) Lights.add_light(L);
				}
				continue ;
			}

			if (PortalTraverser.i_marker != sector->r_marker) continue; // inactive (untouched) sector
			for (u32 v_it = 0; v_it < sector->r_frustums.size(); v_it++)
			{
				CFrustum& view = sector->r_frustums[v_it];
				if (!view.testSphere_dirty(spatial->spatial.sphere.P, spatial->spatial.sphere.R)) continue;

				if (spatial->spatial.type & STYPE_RENDERABLE)
				{
					// renderable
					IRenderable* renderable = spatial->dcast_Renderable();
					VERIFY(renderable);

					// Occlusion
					//	casting is faster then using getVis method
					vis_data& v_orig = ((dxRender_Visual*)renderable->renderable.visual)->vis;
					vis_data v_copy = v_orig;
					v_copy.box.xform(renderable->renderable.xform);
					BOOL bVisible = HOM.visible(v_copy);
					v_orig.marker = v_copy.marker;
					v_orig.accept_frame = v_copy.accept_frame;
					v_orig.hom_frame = v_copy.hom_frame;
					v_orig.hom_tested = v_copy.hom_tested;
					if (!bVisible) break; // exit loop on frustums

					// Rendering
					set_Object(renderable);
					renderable->renderable_Render();
					set_Object(0);
				}
				break; // exit loop on frustums
			}
		}
		if (g_pGameLevel && (phase == PHASE_NORMAL))
		{
			Target->bCaptureScopeLens = true;  // pip: capture the scope lens only during the player HUD render
			g_hud->Render_Last(); // HUD
			Target->bCaptureScopeLens = false;
			if (g_hud->RenderActiveItemUIQuery())
				r_dsgraph_render_hud_ui();
			if (g_hud->RenderCamAttachedUIQuery())
				r_dsgraph_render_cam_ui();
		}
	}
	else
	{
		set_Object(0);
		if (g_pGameLevel && (phase == PHASE_NORMAL))
		{
			Target->bCaptureScopeLens = true;  // pip: capture the scope lens only during the player HUD render
			g_hud->Render_Last(); // HUD
			Target->bCaptureScopeLens = false;
			if (g_hud->RenderActiveItemUIQuery())
				r_dsgraph_render_hud_ui();
			if (g_hud->RenderCamAttachedUIQuery())
				r_dsgraph_render_cam_ui();
		}
	}
}

void CRender::render_menu()
{
	PIX_EVENT(render_menu);
	//	Globals
	RCache.set_CullMode(CULL_CCW);
	RCache.set_Stencil(FALSE);
	RCache.set_ColorWriteEnable();

	// Main Render
	{
		Target->u_setrt(Target->rt_Generic_0, 0, 0, HW.pBaseZB); // LDR RT
		g_pGamePersistent->OnRenderPPUI_main(); // PP-UI
	}

	// Distort
	{
		FLOAT ColorRGBA[4] = {127.0f / 255.0f, 127.0f / 255.0f, 0.0f, 127.0f / 255.0f};
		Target->u_setrt(Target->rt_Generic_1, 0, 0, HW.pBaseZB); // Now RT is a distortion mask
		HW.pContext->ClearRenderTargetView(Target->rt_Generic_1->pRT, ColorRGBA);
		g_pGamePersistent->OnRenderPPUI_PP(); // PP-UI
	}

	// Actual Display
	Target->u_setrt(Device.dwWidth, Device.dwHeight, HW.pBaseRT,NULL,NULL, HW.pBaseZB);
	RCache.set_Shader(Target->s_menu);
	RCache.set_Geometry(Target->g_menu);

	Fvector2 p0, p1;
	u32 Offset;
	u32 C = color_rgba(255, 255, 255, 255);
	float _w = float(Device.dwWidth);
	float _h = float(Device.dwHeight);
	float d_Z = EPS_S;
	float d_W = 1.f;
	p0.set(.5f / _w, .5f / _h);
	p1.set((_w + .5f) / _w, (_h + .5f) / _h);

	FVF::TL* pv = (FVF::TL*)RCache.Vertex.Lock(4, Target->g_menu->vb_stride, Offset);
	pv->set(EPS, float(_h + EPS), d_Z, d_W, C, p0.x, p1.y);
	pv++;
	pv->set(EPS, EPS, d_Z, d_W, C, p0.x, p0.y);
	pv++;
	pv->set(float(_w + EPS), float(_h + EPS), d_Z, d_W, C, p1.x, p1.y);
	pv++;
	pv->set(float(_w + EPS), EPS, d_Z, d_W, C, p1.x, p0.y);
	pv++;
	RCache.Vertex.Unlock(4, Target->g_menu->vb_stride);
	RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);
}

extern u32 g_r;

void CRender::Render()
{
	PIX_EVENT(CRender_Render);

	VERIFY(0 == mapDistort.size() + mapHUDDistort.size());

	rmNormal();

	bool _menu_pp = g_pGamePersistent ? g_pGamePersistent->OnRenderPPUI_query() : false;
	if (_menu_pp)
	{
		render_menu();
		return;
	};

	IMainMenu* pMainMenu = g_pGamePersistent ? g_pGamePersistent->m_pMainMenu : 0;
	bool bMenu = pMainMenu ? pMainMenu->CanSkipSceneRendering() : false;

	if (!(g_pGameLevel && g_hud)
		|| bMenu)
	{
		Target->u_setrt(Device.dwWidth, Device.dwHeight, HW.pBaseRT,NULL,NULL, HW.pBaseZB);
		return;
	}

	if (m_bFirstFrameAfterReset)
	{
		m_bFirstFrameAfterReset = false;
		return;
	}

	//.	VERIFY					(g_pGameLevel && g_pGameLevel->pHUD);

	// Configure
	RImplementation.o.distortion = FALSE; // disable distorion
	Fcolor sun_color = ((light*)Lights.sun_adapted._get())->color;
	bSUN = ps_r2_ls_flags.test(R2FLAG_SUN) && (u_diffuse2s(sun_color.r, sun_color.g, sun_color.b)>EPS) && !strstr(Core.Params, "-r4_dev");
	if (o.sunstatic) bSUN = FALSE;
	// Msg						("sstatic: %s, sun: %s",o.sunstatic?;"true":"false", bSUN?"true":"false");

	// HOM
	ViewBase.CreateFromMatrix(Device.mFullTransform, FRUSTUM_P_LRTB + FRUSTUM_P_FAR);
	View = 0;
	if (!ps_r2_ls_flags.test(R2FLAG_EXP_MT_CALC))
	{
		HOM.Enable();
		HOM.Render(ViewBase);
	}

	//******* Z-prefill calc - DEFERRER RENDERER
	if (ps_r2_ls_flags.test(R2FLAG_ZFILL))
	{
		PIX_EVENT(DEFER_Z_FILL);
		Device.Statistic->RenderCALC.Begin();
		float z_distance = ps_r2_zfill;
		Fmatrix m_zfill, m_project;
		m_project.build_projection(
			deg2rad(Device.fFOV/* *Device.fASPECT*/),
			Device.fASPECT, VIEWPORT_NEAR,
			z_distance * g_pGamePersistent->Environment().CurrentEnv->far_plane);
		m_zfill.mul(m_project, Device.mView);
		r_pmask(true, false); // enable priority "0"
		set_Recorder(NULL);
		phase = PHASE_SMAP;
		render_main(m_zfill, false);
		r_pmask(true, false); // disable priority "1"
		Device.Statistic->RenderCALC.End();

		// flush
		Target->phase_scene_prepare();
		RCache.set_ColorWriteEnable(FALSE);
		r_dsgraph_render_graph(0);
		RCache.set_ColorWriteEnable();
	}
	else
	{
		// pip phase_scene_prepare runs per viewport in renderGBuffer so the SVP clears its own depth
	}

	//*******
	// Sync point
	Device.Statistic->RenderDUMP_Wait_S.Begin();
	if (ps_r2_qsync)
	{
		CTimer T;
		T.Start();
		BOOL result = FALSE;
		HRESULT hr = S_FALSE;
		//while	((hr=q_sync_point[q_sync_count]->GetData	(&result,sizeof(result),D3DGETDATA_FLUSH))==S_FALSE) {
		while ((hr = GetData(q_sync_point[q_sync_count], &result, sizeof(result))) == S_FALSE)
		{
			if (!SwitchToThread()) Sleep(ps_r2_wait_sleep);
			if (T.GetElapsed_ms() > 500)
			{
				result = FALSE;
				break;
			}
		}
	}
	Device.Statistic->RenderDUMP_Wait_S.End();
	q_sync_count = (q_sync_count + 1) % HW.Caps.iGPUNum;
	//CHK_DX										(q_sync_point[q_sync_count]->Issue(D3DISSUE_END));
	CHK_DX(EndQuery(q_sync_point[q_sync_count]));


	mapScopeHUDSorted.clear();
	mapReflexHUDSorted.clear();
	Device.m_SecondViewport.eyepiece.radius = 0;
	Device.m_SecondViewport.objective.radius = 0;

	auto mainCameraPos = Device.vCameraPosition;
	TargetMain->SetActive();
	{
		PIX_EVENT(DRAW_MAIN);
		renderGBuffer();
	}

	if (Device.m_SecondViewport.IsSVPActive())
	{
		TargetSVP->SetActive();

		// pip optional cone occlusion. the shared HOM is 64x64 over the main 68deg view so within the ~4.5deg
		// scope cone it spans a few pixels and culls nothing. rebuild it from the cone, restore the main HOM
		// after. conservative and off by default, no help outdoors where there are no occluders
		extern int ps_r__svp_occlude;
		const bool svp_occlude = ps_r__svp_occlude && Device.m_SecondViewport.eyepiece.radius > EPS;
		if (svp_occlude)
		{
			Fmatrix svp_ft; svp_ft.mul(Device.matrices[1].mProject, Device.matrices[1].mView);
			CFrustum svp_hom; svp_hom.CreateFromMatrix(svp_ft, FRUSTUM_P_LRTB + FRUSTUM_P_FAR);
			HOM.Render(svp_hom);
		}

		{
			PIX_EVENT(DRAW_SVP);
			// SVP HACK: use the main frame view position so the SVP traverses the same sector
			Device.vCameraPosition = mainCameraPos;
			renderGBuffer();
		}

		if (svp_occlude)
		{
			Fmatrix main_ft; main_ft.mul(Device.matrices[0].mProject, Device.matrices[0].mView);
			CFrustum main_hom; main_hom.CreateFromMatrix(main_ft, FRUSTUM_P_LRTB + FRUSTUM_P_FAR);
			HOM.Render(main_hom); // restore main occlusion for the sun and light passes that follow
		}
	}

	// Sun shadow cascades build once (main); render_sun_cascades accumulates per viewport
	{
		PIX_EVENT(RENDER_SUN);
		TargetMain->SetActive();
		renderSun();
	}

	// Emissive + bloom-emissive, per viewport
	{
		PIX_EVENT(COMBINE_GBUFFER_CONT);
		TargetMain->SetActive();
		combineLightingAndBloom();
		if (Device.m_SecondViewport.IsSVPActive())
		{
			TargetSVP->SetActive();
			combineLightingAndBloom();
		}
	}

	// Point/spot shadow atlas build once (main)
	{
		PIX_EVENT(DRAW_SHADOWMAPS);
		TargetMain->SetActive();
		renderShadowmaps();
	}

	// Deferred light accumulation + combine, inverted order (svp -> main)
	if (Device.m_SecondViewport.IsSVPActive())
	{
		TargetSVP->SetActive();
		{
			PIX_EVENT(COMBINE_SVP);
			// NVG shader is required but its tube overlay is not, so widen the tube radius to the
			// max that does not break the shader for the SVP combine
			auto nvg_tube_radius = ps_dev_param_7.y;
			ps_dev_param_7.y = 0.99f;
			combineGBuffer();
			ps_dev_param_7.y = nvg_tube_radius;
		}
	}

	TargetMain->SetActive();
	{
		PIX_EVENT(COMBINE_MAIN);
		combineGBuffer();
	}

	Target->phase_scope_debug(); // pip: r__scope_debug texture-inspector grid (main view, after combine)

	if (Details)
		Details->details_clear();

	VERIFY(0 == mapDistort.size() + mapHUDDistort.size());
}

// pip scope_debug 2 overlay, draws the eyepiece, objective and camera lenses in world space
void debug_scope(Fmatrix scope_camera)
{
	auto dbg_line = [](const Fvector& a, const Fvector& b, u32 color, bool bHud) {
		Fvector v[2] = { a, b };
		u16 idx[2] = { 0, 1 };
		DRender->add_lines(v, 2, idx, 1, color, bHud);
	};

	auto draw_circle = [&](Fmatrix m, u32 color, bool bHud) {
		const int n = 100;
		Fvector v0 = { 0, 0, 0 };
		for (int i = 0; i <= n; i++) {
			float angle = float(i) / float(n) * PI * 2.0f;
			Fvector v1 = { cosf(angle), sinf(angle), 0.f };
			m.transform(v1);
			if (i > 0) dbg_line(v0, v1, color, bHud);
			v0 = v1;
		}
	};

	auto draw_lens = [&](CRenderDevice::CSecondVPParams::Lens lens, u32 color) {
		draw_circle(Fmatrix(lens.m_W).mulB_43(Fmatrix().scale(lens.radius, lens.radius, 0.f)), color, true);
		Fvector v0 = { 0, 0, 0 }, v1 = { 0, 0, 100 };
		lens.m_W.transform(v0);
		lens.m_W.transform(v1);
		dbg_line(v0, v1, color, true);
		// draw the lens up-vector as a radial spoke so a roll about the optical axis shows
		Fvector u0 = { 0, 0, 0 }, u1 = { 0, lens.radius, 0 };
		lens.m_W.transform(u0);
		lens.m_W.transform(u1);
		dbg_line(u0, u1, color, true);
	};

	auto draw_camera = [&](u32 color) {
		const float cm = 1.0f / 100.0f;
		draw_circle(Fmatrix(scope_camera).mulB_43(Fmatrix().scale(.25f * cm, .25f * cm, 0.f)), color, true);
	};

	auto p = Device.m_SecondViewport;
	draw_lens(p.eyepiece, 0xff0000ff);   // eyepiece blue
	draw_lens(p.objective, 0xffffff00);  // objective yellow
	draw_camera(0xffffffff);             // scope cam white
}

// pip STUB sub-pixel jitter for the SVP scene projection (DLSS scaffolding), swap for Ascii's
// shared helper. Halton(2,3), 16-sample phase, returns a centered offset in [-0.5,0.5] px
static float svp_halton(u32 i, u32 b)
{
	float f = 1.0f, r = 0.0f;
	while (i > 0) { f /= (float)b; r += f * (float)(i % b); i /= b; }
	return r;
}
static Fvector2 svp_jitter_offset(u32 frame)
{
	const u32 phase = 16; // TODO match Ascii's confirmed sequence length
	u32 i = (frame % phase) + 1; // Halton is 1-based
	Fvector2 o;
	o.set(svp_halton(i, 2) - 0.5f, svp_halton(i, 3) - 0.5f);
	return o;
}
// apply a pixel jitter to a projection by shifting the post-perspective NDC center, the axis/sign/
// remap convention lives here so the eval swap is one line
static void svp_apply_jitter(Fmatrix& proj, Fvector2 px, float w, float h)
{
	proj.m[2][0] += 2.0f * px.x / w;  // NDC x, clip.x gains z * this, the w-divide cancels z
	proj.m[2][1] -= 2.0f * px.y / h;  // NDC y, negated for texture-down
}

// pip build the SVP camera into matrices[1] from the lens and the weapon SVP zoom factor
void svpCamera()
{
	float svp_fov = g_pGamePersistent->m_pGShaderConstants->hud_params.y * 0.75f;
	// pip floor svp_fov, near-0 breaks the camera math
	if (svp_fov < 1.0f) svp_fov = 1.0f;
	float _, fov, fNearPlane, fFarPlane;
	Device.matrices[0].mProject.decompose_projection(fov, _, fNearPlane, fFarPlane);

	// pip zoom smoothing, some recoil mods punch the main FOV per shot which flutters the scope
	// magnification (fov over svp_fov), low pass fov so the scope zoom holds steady through full auto
	extern float ps_r__svp_zoom_smooth;
	if (ps_r__svp_zoom_smooth > EPS)
	{
		static float s_smooth_fov = 0.f;
		if (s_smooth_fov < EPS || _abs(s_smooth_fov - fov) > 0.5f)
			s_smooth_fov = fov; // init, or snap on a real FOV change (vid_restart or fov setting)
		else
		{
			const float tau = 0.50f * ps_r__svp_zoom_smooth; // heavy, zoom shouldn't change mid burst so no downside
			float a = (tau > EPS) ? Device.fTimeDelta / tau : 1.f;
			if (a > 1.f) a = 1.f;
			s_smooth_fov += (fov - s_smooth_fov) * a;
		}
		fov = s_smooth_fov;
	}

	auto mm = Device.matrices[0];
	auto params = Device.m_SecondViewport;

	// Project the eyepiece top/bottom into NDC to find the extra magnification needed to
	// correct the on-screen scope size
	Fvector4 top, bot;
	Fmatrix m_WVP = Fmatrix().mul(mm.mProjectHud, Fmatrix().mul(mm.mView, params.eyepiece.m_W));
	m_WVP.transform(top, { 0, params.eyepiece.radius, 0, 1 });
	m_WVP.transform(bot, { 0, -params.eyepiece.radius, 0, 1 });
	top.div(top.w);
	bot.div(bot.w);
	float scope_height_NDC = abs(top.y - bot.y);
	float screen_height_NDC = 2.0f;
	float ratio_magnification = screen_height_NDC / scope_height_NDC;

	// The magnification of the scope (1X 4X etc)
	float scope_magnification = fov / deg2rad(svp_fov);

	// pip expose the engine magnification as a fallback curMag when no 3DSS config sets the cvar
	extern float g_pip_scope_magnification;
	extern float g_pip_scope_min_mag;
	extern float g_pip_scope_max_mag;
	if (svp_fov > EPS)
	{
		g_pip_scope_magnification = scope_magnification;
		// pip derive a min/max magnification from hud_fov_params so variable reticles get a real range
		// pip on a fixed scope x equals y so min equals max
		const Fvector4& fovp = g_pGamePersistent->m_pGShaderConstants->hud_fov_params;
		g_pip_scope_max_mag = (fovp.x > EPS) ? fov / deg2rad(fovp.x * 0.75f) : scope_magnification;
		g_pip_scope_min_mag = (fovp.y > EPS) ? fov / deg2rad(fovp.y * 0.75f) : scope_magnification;
	}

	// The fov we render at to get the correct zoom
	float vFov = 2.0f * atan(tan(fov * 0.5f) / (ratio_magnification * scope_magnification));

	// The fov for camera placement
	float vFovMagOnly = 2.0f * atan(tan(fov * 0.5f) / scope_magnification);

	auto camera_offset_from_vfov_and_radius = [](float vFov, float radius) -> float {
		return radius / tan(vFov / 2.0f);
	};

	auto near_plane = fNearPlane;
	auto m_W_svpcam = params.eyepiece.m_W; // default: place the camera on the eyepiece
	if (scope_svp_enabled >= 2 && params.objective.radius > EPS) {
		// place the camera for the objective lens
		auto d = camera_offset_from_vfov_and_radius(vFovMagOnly, params.objective.radius);
		m_W_svpcam = Fmatrix().mul(params.objective.m_W, Fmatrix().translate(0, 0, -d));
		near_plane = d;
	}

	// pip force the SVP camera up to world up so a canted scope renders upright (optical axis k is kept)
	{
		Fvector fwd, wup, right, up;
		fwd.set(m_W_svpcam.k.x, m_W_svpcam.k.y, m_W_svpcam.k.z);
		fwd.normalize();
		wup.set(0.f, 1.f, 0.f);
		right.crossproduct(wup, fwd);
		if (right.magnitude() > EPS_S)
		{
			right.normalize();
			up.crossproduct(fwd, right);
			up.normalize();
			m_W_svpcam.i.x = right.x; m_W_svpcam.i.y = right.y; m_W_svpcam.i.z = right.z;
			m_W_svpcam.j.x = up.x;    m_W_svpcam.j.y = up.y;    m_W_svpcam.j.z = up.z;
			m_W_svpcam.k.x = fwd.x;   m_W_svpcam.k.y = fwd.y;   m_W_svpcam.k.z = fwd.z;
		}
	}

	// pip stabilization/recoil-comp run in the eyepiece derive so the camera and disc sampling stay consistent

	auto aspect = RImplementation.TargetSVP->Width / RImplementation.TargetSVP->Height; // u32/u32 == 1 (square)

	float fNearPlane_hud, fFarPlane_hud;
	Device.matrices[0].mProject.decompose_projection(_, _, fNearPlane_hud, fFarPlane_hud);
	auto svp_proj = Fmatrix().build_projection(vFov, aspect, near_plane, fFarPlane);
	auto svp_proj_hud = Fmatrix().build_projection(vFov, aspect, near_plane, fFarPlane_hud);

	// pip DLSS jitter the SVP scene projection (gated), {0,0} otherwise, applied to mProject only
	Device.m_SecondViewport.svp_jitter_px.set(0, 0);
	if (ps_r__svp_dlss != 0)
	{
		const u32 jf = Device.dwFrame; // latch once, single render thread, 1:1 with rendered frames
		Fvector2 jpx = svp_jitter_offset(jf);
		svp_apply_jitter(svp_proj, jpx, (float)RImplementation.TargetSVP->Width, (float)RImplementation.TargetSVP->Height);
		Device.m_SecondViewport.svp_jitter_px = jpx;
	}

	if (scope_debug >= 2 && params.eyepiece.radius > EPS)
		debug_scope(m_W_svpcam); // pip: lens/camera overlay, skip when no real lens so it clears on detach

	Device.matrices[1].mView.invert(m_W_svpcam);
	Device.matrices[1].mProject = svp_proj;
	Device.matrices[1].mProjectHud = svp_proj_hud;

	// pip cache the SVP scene constants for the DLSS eval inputs (single render thread, written then read
	// the same frame, inert at gate 0). svp_fov is radians from the projection, the basis is the camera world
	{
		auto& vp = Device.m_SecondViewport;
		Device.matrices[1].mProject.decompose_projection(vp.svp_fov, vp.svp_aspect, vp.svp_near, vp.svp_far);
		vp.svp_cam_pos = m_W_svpcam.c;
		vp.svp_right = m_W_svpcam.i;
		vp.svp_up = m_W_svpcam.j;
		vp.svp_fwd = m_W_svpcam.k;

		// pip eye-box drift, the true bore vs the aim as a screen-space offset (tan units). project the
		// captured true bore into the main view, perspective-divide -> NDC offset from center (0 on aim)
		Fvector bv; bv.set(vp.svp_bore_fwd);
		Device.matrices[0].mView.transform_dir(bv); // world -> view space
		const float bz = (bv.z > EPS) ? bv.z : EPS;
		vp.svp_eyebox.set(bv.x / bz, bv.y / bz, ps_r__svp_eyebox, 0.f);

		// pip DLSS reset when the lens first becomes valid, render-thread edge state, inert at gate 0
		bool lens_valid = (vp.eyepiece.radius > EPS);
		if (lens_valid && !vp.m_lens_prev_valid)
			vp.dlss_reset_next = true;
		vp.m_lens_prev_valid = lens_valid;
	}
}

void CRender::renderGBuffer()
{
	PIX_EVENT(RENDER_GBUFFER);
	Device.dwViewport++; // pip: per-viewport cache counter

	// pip cull the SVP geometry to the scope cone, the captured graph is main frustum so the SVP would
	// otherwise resubmit the whole world through a cone that sees a fraction
	extern int ps_r__svp_cull, ps_r__svp_skip_grass, ps_r__svp_cull_grass;
	const bool svp_pass = (Target == TargetSVP) && Device.m_SecondViewport.IsSVPActive();
	const bool svp_cull = svp_pass && ps_r__svp_cull;
	const bool svp_cull_grass = svp_pass && ps_r__svp_cull_grass && !ps_r__svp_skip_grass;
	if (svp_cull || svp_cull_grass)
	{
		Fmatrix svp_full;
		svp_full.mul(Device.matrices[1].mProject, Device.matrices[1].mView);
		svp_cull_begin(svp_full, svp_cull);
	}

	//******* Main calc - DEFERRER RENDERER
	// Main calc
	Device.Statistic->RenderCALC.Begin();
	// pip time the scope cull
	const bool svp_calc = Device.m_SecondViewport.IsSVPFrame();
	if (svp_calc) Device.Statistic->RenderCALC_SVP.Begin();
	r_pmask(true, false, true); // enable priority "0",+ capture wmarks
	// pip capture the coarse structure for sun cascades on the main view only
	if (bSUN && Target == TargetMain) set_Recorder(&main_coarse_structure);
	else set_Recorder(NULL);
	phase = PHASE_NORMAL;

	// pip build the cull frustum from the main camera view so the SVP traverses the player sector
	// for the main target this equals Device.mFullTransform so main culling is unchanged
	Fmatrix main_ft = Fmatrix().mul(Device.mProject, Device.matrices[0].mView);
	ViewBase.CreateFromMatrix(main_ft, FRUSTUM_P_LRTB + FRUSTUM_P_FAR);
	View = 0;
	render_main(main_ft, true);
	set_Recorder(NULL);
	r_pmask(true, false); // disable priority "1"
	if (svp_calc) Device.Statistic->RenderCALC_SVP.End();
	Device.Statistic->RenderCALC.End();

	// pip clear and bind this target gbuffer and depth per viewport so the SVP clears its own depth
	Target->phase_scene_prepare();

	/*if (RImplementation.o.ssfx_core) // SSS23: DEPRECATED
	{
		// HUD Masking rendering
		FLOAT ColorRGBA[4] = { 1.0f, 0.0f, 0.0f, 1.0f };
		HW.pContext->ClearRenderTargetView(Target->rt_ssfx_hud->pRT, ColorRGBA);

		Target->u_setrt(Target->rt_ssfx_hud, NULL, NULL, HW.pBaseZB);
		r_dsgraph_render_hud(true);

		// Reset Depth
		HW.pContext->ClearDepthStencilView(HW.pBaseZB, D3D_CLEAR_DEPTH, 1.0f, 0);
	}*/

	if (RImplementation.o.ssfx_motionvectors)
	{
		Target->u_setrt(Device.dwWidth, Device.dwHeight, 0, 0, Target->rt_ssfx_motion_vectors->pRT, 0);

		FLOAT ColorRGBA[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		HW.pContext->ClearRenderTargetView(Target->rt_ssfx_motion_vectors->pRT, ColorRGBA);

		RCache.set_Stencil(FALSE);
		g_pGamePersistent->Environment().RenderSky(true);

		RCache.Index.Flush();
		RCache.Vertex.Flush();

		RCache.set_xform_world(Fidentity);
	}

	if (ps_r2_ls_flags.test(R2FLAG_TERRAIN_PREPASS))
	{
		Target->u_setrt(Device.dwWidth, Device.dwHeight, NULL, NULL, NULL, !RImplementation.o.dx10_msaa ? HW.pBaseZB : Target->rt_MSAADepth->pZRT);
		r_dsgraph_render_landscape(0, false);
	}

	BOOL split_the_scene_to_minimize_wait = FALSE;
	if (ps_r2_ls_flags.test(R2FLAG_EXP_SPLIT_SCENE)) split_the_scene_to_minimize_wait = TRUE;

	// pip no real SVP here so render the HUD late so the legacy lens samples a clean scene
	const bool late_hud = (Target == TargetMain) && !Device.m_SecondViewport.IsSVPActive();

	// pip nonzero means this optic drives a magnified Second Viewport, a fake 1x optic reads 0 and skips the scope prep
	const bool has_svp_zoom = (g_pGamePersistent->m_pGShaderConstants->hud_params.y > 0.005f);

	//******* Main render :: PART-0	-- first
	if (!split_the_scene_to_minimize_wait)
	{
		PIX_EVENT(DEFER_PART0_NO_SPLIT);
		// level, DO NOT SPLIT
		Target->phase_scene_begin();
		r_dsgraph_render_hud();
		r_dsgraph_render_graph(0);
		r_dsgraph_render_lods(true, true);
		// pip r__svp_skip_grass drops the near grass field on the scope pass (mostly off a zoomed cone)
		if (Details && !(svp_pass && ps_r__svp_skip_grass)) Details->Render();
		if (ps_r2_ls_flags.test(R2FLAG_TERRAIN_PREPASS)) r_dsgraph_render_landscape(1, true);
		Target->phase_scene_end();
	}
	else
	{
		PIX_EVENT(DEFER_PART0_SPLIT);
		// level, SPLIT
		Target->phase_scene_begin();

		// pip draw the HUD early so the scope lens depth lands before the scene gbuffer
		{
			PIX_EVENT(RENDER_HUD_EARLY);

			// pip scope gbuffer prep, only for a magnified optic, a fake 1x optic gets none
			if (Target == TargetMain && scope_svp_enabled != 0 && has_svp_zoom)
			{
				{
					PIX_EVENT(SCOPE_WRITE_LENS_DEPTH);
					// pip derive the eyepiece and write the lens depth, a real SVP writes near depth plus
					// stencil 0x3 plus the magnified gbuffer, a fake 1x optic writes far depth only
					Target->draw_scope(Target->s_scope_depth_write, [&](auto N) -> void {
						if (!late_hud)
						{
							RCache.set_Stencil(TRUE, D3DCMP_ALWAYS, 0x3, 0x3, 0x3, D3DSTENCILOP_KEEP, D3DSTENCILOP_REPLACE, D3DSTENCILOP_KEEP);
							RCache.set_c("scope_phase", SCOPE_PHASE_GBUFFER);
							RCache.set_c("scope_depth_value", 0.f);
						}
						else
						{
							RImplementation.rmNormal();
							RCache.set_Stencil(FALSE);
							RCache.set_c("scope_phase", SCOPE_PHASE_DEPTHWRITE);
							RCache.set_c("scope_depth_value", 1.f);
						}
					});
				}

				svpCamera();
			}

			// pip HUD early only for a real SVP, a no-SVP scope draws it late so it can't leak into the lens
			if (!late_hud)
			{
				PIX_EVENT(RENDER_HUD);
				RCache.set_ZFunc(D3DCMP_LESS);
				r_dsgraph_render_hud();
				RCache.set_ZFunc(D3DCMP_LESSEQUAL);
			}

			if (Target == TargetMain && scope_svp_enabled != 0 && has_svp_zoom && !Device.m_SecondViewport.IsSVPActive())
			{
				PIX_EVENT(SCOPE_HOLEPUNCH);
				// pip clear the lens near depth back to far so the scene and skybox fill the opening
				Target->draw_scope(Target->s_scope_depth_write, [&](auto _) -> void {
					RImplementation.rmNormal();
					RCache.set_Stencil(TRUE, D3DCMP_EQUAL, 0x3, 0x3, 0x3, D3DSTENCILOP_KEEP, D3DSTENCILOP_ZERO, D3DSTENCILOP_KEEP);
					RCache.set_c("scope_phase", SCOPE_PHASE_DEPTHWRITE);
					RCache.set_c("scope_depth_value", 1.f);
				});
			}
		}

		Target->phase_scene_begin();
		r_dsgraph_render_graph(0);
		Target->disable_aniso();
	}

	//  Redotix99: for 3D Shader Based Scopes
	// pip copy the scene depth for the legacy lens shader, any no-SVP scope
	if (late_hud && scope_3D_fake_enabled)
	{
		ID3D11Resource* zbuffer_res;
		HW.pBaseZB->GetResource(&zbuffer_res);
		HW.pContext->CopyResource(RImplementation.Target->rt_tempzb->pSurface, zbuffer_res);
	}

	//******* Occlusion testing of volume-limited light-sources
	// pip build the light list once for the main viewport, the SVP gbuffer reuses it
	// gc64 swaps the occq LP_normal/LP_pending split for a vis_update filter, shadowed lights kept when visible
	{
		bool locked = scope_debug == 4;

		if (!locked)
		{
			if (Target == TargetMain)
			{
				auto LP = &Lights.package;
				LP_normal.clear();
				for (auto L : LP->v_shadowed)
				{
					L->vis_update();
					if (L->vis.visible)
						LP_normal.v_shadowed.push_back(L);
					else if (scope_debug >= 3)
					{
						// pip: grey direction/range vector for each culled shadowed light (r__scope_debug 3+)
						Fvector v[2] = { L->position, Fvector(L->direction).mul(L->range).add(L->position) };
						u16 idx[2] = { 0, 1 };
						DRender->add_lines(v, 2, idx, 1, 0xff999999, false);
					}
				}
				for (auto L : LP->v_point) LP_normal.v_point.push_back(L);
				for (auto L : LP->v_spot) LP_normal.v_spot.push_back(L);

				// stats
				stats.l_shadowed = LP_normal.v_shadowed.size();
				stats.l_unshadowed = LP_normal.v_point.size() + LP_normal.v_spot.size();
				stats.l_total = stats.l_shadowed + stats.l_unshadowed;
			}

			{
				PIX_EVENT(DEFER_TEST_LIGHT_VIS);
				Target->phase_occq();

				auto LP = &Lights.package;
				for (auto L : LP->v_shadowed)
					L->vis_prepare();
			}
		}
	}

	//******* Main render :: PART-1 (second)
	if (split_the_scene_to_minimize_wait)
	{
		PIX_EVENT(DEFER_PART1_SPLIT);
		// skybox can be drawn here
		
		if (0)
		{
			if (!RImplementation.o.dx10_msaa)
				Target->u_setrt(Target->rt_Generic_0, Target->rt_Generic_1, 0, HW.pBaseZB);
			else
				Target->u_setrt(Target->rt_Generic_0_r, Target->rt_Generic_1, 0,
				                RImplementation.Target->rt_MSAADepth->pZRT);
			RCache.set_CullMode(CULL_NONE);
			RCache.set_Stencil(FALSE);

			// draw skybox
			RCache.set_ColorWriteEnable();
			//CHK_DX(HW.pDevice->SetRenderState			( D3DRS_ZENABLE,	FALSE				));
			RCache.set_Z(FALSE);
			g_pGamePersistent->Environment().RenderSky();
			//CHK_DX(HW.pDevice->SetRenderState			( D3DRS_ZENABLE,	TRUE				));
			RCache.set_Z(TRUE);
		}

		// level, a real SVP already drew the HUD early for the scope lens depth
		Target->phase_scene_begin();
		// pip a no-SVP scope draws the HUD here after the scene so the world fills the lens before the z-write
		if (late_hud)
			r_dsgraph_render_hud();
		r_dsgraph_render_lods(true, true);
		// pip r__svp_skip_grass drops the near grass field on the scope pass (mostly off a zoomed cone)
		if (Details && !(svp_pass && ps_r__svp_skip_grass)) Details->Render();
		if (ps_r2_ls_flags.test(R2FLAG_TERRAIN_PREPASS)) r_dsgraph_render_landscape(1, true);
		Target->phase_scene_end();
	}

	if (svp_cull || svp_cull_grass)
		svp_cull_end(); // pip end SVP cull, the wallmarks and the shared shadow and light passes run after

	// Wall marks
	if (Wallmarks)
	{
		PIX_EVENT(DEFER_WALLMARKS);
		Target->phase_wallmarks();

		Wallmarks->Render(); // wallmarks has priority as normal geometry
	}

	// pip the incremental svis flush loop is gone, render_lights_shadowmaps uses vis_update/phase_occq

	// full screen pass to mark msaa-edge pixels in highest stencil bit
	if (RImplementation.o.dx10_msaa)
	{
		PIX_EVENT(MARK_MSAA_EDGES);
		Target->mark_msaa_edges();
	}

	//	TODO: DX10: Implement DX10 rain
	// pip rain shadow-map builds on the main view only, rain accumulation re-runs per viewport
	if (ps_r2_ls_flags.test(R3FLAG_DYN_WET_SURF))
	{
		PIX_EVENT(DEFER_RAIN);
		if (!Device.m_SecondViewport.IsSVPFrame())
			shadowmap_rain();
		render_rain();
	}

	{
		// save previous and current matrices, per-viewport history gc64
		{
			static Fmatrix mm_saved_viewproj[2];

			Target->GetPrevious()->Matrix_previous.mul(mm_saved_viewproj[Device.m_SecondViewport.IsSVPFrame()], Device.mInvView);
			Target->GetPrevious()->Matrix_current.set(Device.mProject);
			mm_saved_viewproj[Device.m_SecondViewport.IsSVPFrame()].set(Device.mFullTransform);
		}

		if (RImplementation.o.ssfx_sss) // pip: SSS shadows run per viewport (no SVP guard)
		{
			static bool sss_rendered, sss_extended_rendered;

			// SSS Shadows
			if (ps_ssfx_sss_quality.z > 0)
			{
				Target->phase_ssfx_sss();
				sss_rendered = true;
			}
			else
			{
				if (sss_rendered) // Clear buffer
				{
					sss_rendered = false;
					FLOAT ColorRGBA[4] = { 1,1,1,1 };
					HW.pContext->ClearRenderTargetView(Target->rt_ssfx_sss->pRT, ColorRGBA);
				}
			}

			if (ps_ssfx_sss_quality.w > 0)
			{
				// Extra lights
				Target->phase_ssfx_sss_ext(Lights.package);
				sss_extended_rendered = true;
			}
			else
			{
				if (sss_extended_rendered) // Clear buffer
				{
					sss_extended_rendered = false;
					FLOAT ColorRGBA[4] = { 1,1,1,1 };
					HW.pContext->ClearRenderTargetView(Target->rt_ssfx_sss_tmp->pRT, ColorRGBA);
				}
			}
		}
	}
}

// pip build the sun cascades once, render_sun_cascades builds the shadow map on the main view
// and accumulates and blends the sun per viewport, the legacy R2FLAGEXT_SUN_OLD path is gone
void CRender::renderSun()
{
	// Directional light - fucking sun
	if (bSUN)
	{
		PIX_EVENT(DEFER_SUN);
		RImplementation.stats.l_visible ++;
		render_sun_cascades();
	}
}

// pip emissive self-illumination plus bloom emissive, run per viewport
void CRender::combineLightingAndBloom()
{
	// FIXME (gc64): SVP hack, force SRVSManager to unbind the first slot before each emissive
	// pass. Required until more robust per-target state invalidation is in place
	auto unbind_s_base = []() -> void {
		ID3D11ShaderResourceView* crv[1] = { nullptr };
		HW.pContext->PSSetShaderResources(0, 1, crv);
		SRVSManager.SetPSResource(0, nullptr);
	};

	{
		PIX_EVENT(DEFER_SELF_ILLUM);
		Target->phase_accumulator();
		// Render emissive geometry, stencil - write 0x0 at pixel pos
		RCache.set_xform_project(Device.mProject);
		RCache.set_xform_view(Device.mView);
		// Stencil - write 0x1 at pixel pos - 
		if (!RImplementation.o.dx10_msaa)
			RCache.set_Stencil(TRUE, D3DCMP_ALWAYS, 0x01, 0xff, 0xff, D3DSTENCILOP_KEEP, D3DSTENCILOP_REPLACE,
			                   D3DSTENCILOP_KEEP);
		else
			RCache.set_Stencil(TRUE, D3DCMP_ALWAYS, 0x01, 0xff, 0x7f, D3DSTENCILOP_KEEP, D3DSTENCILOP_REPLACE,
			                   D3DSTENCILOP_KEEP);
		//RCache.set_Stencil				(TRUE,D3DCMP_ALWAYS,0x00,0xff,0xff,D3DSTENCILOP_KEEP,D3DSTENCILOP_REPLACE,D3DSTENCILOP_KEEP);
		RCache.set_CullMode(CULL_CCW);
		RCache.set_ColorWriteEnable();

		unbind_s_base();

		RImplementation.r_dsgraph_render_emissive(RImplementation.o.ssfx_bloom ? false : true);
	}

	if (RImplementation.o.ssfx_bloom)
	{
		// Render Emissive on `rt_ssfx_bloom_emissive`
		FLOAT ColorRGBA[4] = { 0,0,0,0 };
		HW.pContext->ClearRenderTargetView(Target->rt_ssfx_bloom_emissive->pRT, ColorRGBA);
		Target->u_setrt(Target->rt_ssfx_bloom_emissive, NULL, NULL, !RImplementation.o.dx10_msaa ? HW.pBaseZB : Target->rt_MSAADepth->pZRT);

		unbind_s_base();

		RImplementation.r_dsgraph_render_emissive(true, true);
	}
}

// pip build the point/spot shadow-map atlas once for the main viewport, combineGBuffer accumulates per viewport
void CRender::renderShadowmaps()
{
	PIX_EVENT(RENDER_SHADOWMAPS);
	render_lights_shadowmaps(LP_normal);
}

// pip per-viewport deferred light accumulation, volumetric blur and final combine
// runs svp before main, render_lights here is accumulate-only
void CRender::combineGBuffer()
{
	PIX_EVENT(COMBINE_GBUFFER);
	Device.dwViewport++;

	// Lighting, non dependant on OCCQ
	{
		PIX_EVENT(DEFERRED_LIGHTS);
		Target->phase_accumulator();
		HOM.Disable();
		render_lights(LP_normal);
	}

	{
		if (RImplementation.o.ssfx_volumetric)
			Target->phase_ssfx_volumetric_blur();
	}

	// Postprocess
	{
		PIX_EVENT(DEFER_LIGHT_COMBINE);
		Target->phase_combine();
	}
}

void CRender::render_forward()
{
	VERIFY(0 == mapDistort.size() + mapHUDDistort.size());
	RImplementation.o.distortion = RImplementation.o.distortion_enabled; // enable distorion

	//******* Main render - second order geometry (the one, that doesn't support deffering)
	//.todo: should be done inside "combine" with estimation of of luminance, tone-mapping, etc
	{
		// level
		r_pmask(false, true); // enable priority "1"
		phase = PHASE_NORMAL;
		render_main(Device.mFullTransform, false); //
		//	Igor: we don't want to render old lods on next frame
		mapLOD.clear();
		r_dsgraph_render_graph(1); // normal level, secondary priority
		PortalTraverser.fade_render(); // faded-portals
		r_dsgraph_render_sorted(); // strict-sorted geoms
		//g_pGamePersistent->Environment().RenderLast(); // rain/thunder-bolts
	}

	RImplementation.o.distortion = FALSE; // disable distorion
}

// pip render_Reticle removed, the scope lens is painted by CRenderTarget::phase_3DSSReticle

void CRender::RenderToTarget(RRT target)
{
	ref_rt* RT = nullptr;

	switch (target)
	{
	case rtPDA:
		RT = &Target->rt_ui_pda;
		break;
	case rtSVP:
		RT = &Target->rt_secondVP;
		break;
	default:
		Debug.fatal(DEBUG_INFO, "None or wrong Target specified: %i", target);
		break;
	}

	ID3DTexture2D* pBuffer = nullptr;
	HW.m_pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&pBuffer);
	HW.pContext->CopyResource((*RT)->pSurface, pBuffer);
	pBuffer->Release();
}
