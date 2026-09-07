#include "stdafx.h"
#include "../../xrEngine/igame_persistent.h"
#include "../xrRender/FBasicVisual.h"
#include "../../xrEngine/customhud.h"
#include "../../xrEngine/xr_object.h"
#include "../xrRender/SkeletonCustom.h"
#include "../../xrParticles/ParticlesAsyncManager.h"

#include "../xrRender/QueryHelper.h"
#include "../../Include/xrAPI/xrAPI.h"          // pip DRender, the debug-line backend for the scope_debug world overlay
#include "../../Include/xrRender/DebugRender.h" // pip IDebugRender::add_lines
#include "../xrRender/SkeletonX.h"              // pip CSkeletonX for the skinned lens bone transform
#include "svp_camera.h"                         // pip svpCamera + the hud geometry snapshot
#include "svp_stats.h"                          // pip per-viewport render-stats overlay

// set all per-viewport camera globals from an explicit view/proj/proj_hud
// called by SetActive, the main path still uses on_idle
void CRender::SetMatrices(Fmatrix view, Fmatrix projection, Fmatrix projection_hud)
{
	Device.mView.set(view);
	Device.mProject.set(projection);
	Device.mProjectHud.set(projection_hud);
	Device.mFullTransform.mul(Device.mProject, Device.mView);
	Device.mFullTransformHud.mul(Device.mProjectHud, Device.mView);

	Device.mInvView.invert(view);
	Device.mInvProject.invert(projection);
	Device.mInvProjectHud.invert(projection_hud);
	D3DXMatrixInverse((D3DXMATRIX*)&Device.mInvFullTransform, 0, (D3DXMATRIX*)&Device.mFullTransform);

	Device.mInvView.transform(Device.vCameraPosition.set(0, 0, 0));
	Device.mInvView.transform_dir(Device.vCameraDirection.set(0, 0, 1));
	Device.mInvView.transform_dir(Device.vCameraTop.set(0, 1, 0));
	Device.mInvView.transform_dir(Device.vCameraRight.set(1, 0, 0));

	Device.vCameraPosition_saved.set(Device.vCameraPosition);
	Device.mView_saved.set(Device.mView);
	Device.mProject_saved.set(Device.mProject);
	Device.mFullTransform_saved.set(Device.mFullTransform);

	float fFov, fAspect, _;
	projection.decompose_projection(fFov, fAspect, _, _);
	Device.fFOV = rad2deg(fFov);
	Device.fASPECT = fAspect;

	Device.m_pRender->SetCacheXform(Device.mView, Device.mProject);
	Device.prepare_matrices();
}


#include	"../xrRender/dxRenderDeviceRender.h"

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
	auto C = color_rgba(255, 255, 255, 255);
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
		for (light* L : v_all_lights)//critical!!!
			L->m_moving_frames = 0;

		m_bFirstFrameAfterReset = false;
		return;
	}

	// pip svp render-stats frame boundary, self-gates to nothing when r__svp_stats is 0
	svp_stats::frame_begin();
	svp_stats::section_begin(svp_stats::SEC_FRAME);

	if ((Device.dwFrame % (u32)ps_r__tex_evict_interval) == 0)
		dxRenderDeviceRender::Instance().Resources->EvictStalledTextures();

	//.	VERIFY					(g_pGameLevel && g_pGameLevel->pHUD);

	// Configure
	RImplementation.o.distortion = FALSE; // disable distorion
	Fcolor sun_color = ((light*)Lights.sun_adapted._get())->color;
	BOOL bSUN = ps_r2_ls_flags.test(R2FLAG_SUN) && (u_diffuse2s(sun_color.r, sun_color.g, sun_color.b)>EPS) && !Core.ParamsData.test(ECoreParams::r4_dev);
	if (o.sunstatic) bSUN = FALSE;
	// Msg						("sstatic: %s, sun: %s",o.sunstatic?;"true":"false", bSUN?"true":"false");

	// HOM
	ViewBase.CreateFromMatrix(Device.mFullTransform, FRUSTUM_P_LRTB + FRUSTUM_P_FAR);
    HOM.MT_RENDER();

	//******* Main calc - DEFERRER RENDERER
	phase = PHASE_NORMAL;
	
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

    GMBase.traverse(RImplementation.pLastSector, ViewBase, Device.vCameraPosition, Device.mFullTransform);
    GMBase.r_dsgraph_capture_static();
    GMBase.r_dsgraph_capture_dynamic();

	// pip reset the per-frame lens, it is re-derived each frame so an un-scoped frame leaves radius 0
	// the debug overlays hold the last radii while the svp stays active so a culled weapon keeps its lines
	{
		auto& vp = Device.m_SecondViewport;
		if (vp.eyepiece.radius > EPS)
		{
			vp.dbg_eyepiece_r = vp.eyepiece.radius;
			vp.dbg_objective_r = vp.objective.radius;
		}
		if (!(Device.true_pip_on && vp.IsSVPActive()))
		{
			vp.dbg_eyepiece_r = 0.f;
			vp.dbg_objective_r = 0.f;
		}
		vp.eyepiece.radius = 0;
		vp.objective.radius = 0;
	}

	// pip a stale hook must never survive into this frame (it captures this + TargetSVP)
	Device.m_SecondViewport.dual_accum = nullptr;
	// pip double-pass, the captured graph renders once for the main viewport and, when a scope drives
	// the SVP, a second time into TargetSVP, true_pip off keeps the single stock main pass
	bool svp = Device.true_pip_on && Device.m_SecondViewport.IsSVPActive();
	// pip lock the adaptive SVP size at ADS-in (svp false -> true) from the disc learned on the last aim, so the
	// target is sized once per aim and never resized mid-ADS (the live disc keeps learning for next time)
	static bool s_prev_svp = false;
	static u32 s_disc_epoch = 0;
	// an optic swap while aimed re-locks from the re-learned disc so the target resizes once for the
	// new optic, the ads-in edge still owns the first lock
	const bool disc_swap = (svp && Device.m_SecondViewport.svp_optic_epoch != s_disc_epoch);
	if ((svp && !s_prev_svp) || disc_swap)
		Device.m_SecondViewport.svp_disc_applied = Device.m_SecondViewport.svp_disc_px;
	s_disc_epoch = Device.m_SecondViewport.svp_optic_epoch;
	const bool svp_edge = (svp != s_prev_svp);
	s_prev_svp = svp;
	// pip allocate the SVP target before the main pass derives the camera into it (lazy, only while a
	// PiP scope is aimed, never when off)
	if (Device.true_pip_on && g_pGamePersistent &&
		g_pGamePersistent->m_pGShaderConstants->hud_params.y > 0.005f)
		EnsureTargetSVP();
	// pip the lens content swaps at the scope edges, both taa histories seed from the current
	// frame so the stale scene never ghosts through the resolve
	if (svp_edge)
	{
		if (TargetMain)
			TargetMain->m_taa_seed_history = true;
		if (TargetSVP)
			TargetSVP->m_taa_seed_history = true;
		// the svp water reflection accumulator still holds the last scope session and its
		// blend converges too slowly for the raise, a horizon tint start hides the stale box
		if (TargetSVP && TargetSVP->rt_ssfx_water && g_pGamePersistent
			&& g_pGamePersistent->Environment().CurrentEnv)
		{
			const Fvector3& fc = g_pGamePersistent->Environment().CurrentEnv->fog_color;
			FLOAT cc[4] = {fc.x, fc.y, fc.z, 1.f};
			HW.pContext->ClearRenderTargetView(TargetSVP->rt_ssfx_water->pRT, cc);
		}
	}
	// creation can fail at VRAM exhaustion, fall back to the single stock pass this frame
	if (svp && !TargetSVP)
		svp = false;
	// pip diag, an aimed weapon without an svp pass (y is nonzero even at hip, gate on x)
	{
		if (ps_r__svp_diag && Device.true_pip_on && !svp && g_pGamePersistent
			&& g_pGamePersistent->m_pGShaderConstants->hud_params.x > 0.05f)
		{
			static u32 s_ns_ms = 0;
			if (Device.dwTimeGlobal - s_ns_ms > 500)
			{
				s_ns_ms = Device.dwTimeGlobal;
				PipMsg("[SVP-STATE] zoomed hud without an svp pass, active=%d target=%d",
					(int)Device.m_SecondViewport.IsSVPActive(), TargetSVP ? 1 : 0);
			}
		}
	}
	// pip [SVP-NVG] tuning diag, edge on generation change plus a slow heartbeat while active
	{
		extern Fvector4 ps_dev_param_7;
		extern Fvector4 ps_dev_param_8;
		extern int ps_r2_nightvision;
		static float s_nvg_gen = -1.f;
		static u32 s_nvg_ms = 0;
		const float genf = floorf(ps_dev_param_8.x);
		const bool edge = _abs(ps_dev_param_8.x - s_nvg_gen) > 0.001f;
		if (ps_r__svp_diag && (edge || (genf >= 1.f && Device.dwTimeGlobal - s_nvg_ms > 2000)))
		{
			s_nvg_gen = ps_dev_param_8.x;
			s_nvg_ms = Device.dwTimeGlobal;
			const float gain_off = floorf(ps_dev_param_8.w) / 10.f;
			PipMsg("[SVP-NVG] gen=%.0f tubes=%.1f gain_cur=%.2f gain_off=%.2f tm_scale=%.1f vig=%.2f r2nv=%d scoped=%d aim=%.2f p7=%.2f/%.2f/%.2f/%.2f",
				genf, (ps_dev_param_8.x - genf) * 10.f,
				floorf(ps_dev_param_8.y) / 10.f, gain_off, 10.f * gain_off,
				floorf(ps_dev_param_8.z) / 100.f, ps_r2_nightvision,
				(int)(Device.true_pip_on && Device.m_SecondViewport.IsSVPActive()),
				g_pGamePersistent ? g_pGamePersistent->m_pGShaderConstants->hud_params.x : 0.f,
				ps_dev_param_7.x, ps_dev_param_7.y, ps_dev_param_7.z, ps_dev_param_7.w);
		}
	}
	if (Device.true_pip_on)
		TargetMain->SetActive();
	svp_stats::section_begin(svp_stats::SEC_MAIN_GBUFFER);
	renderGBuffer(!svp); // keep the priority-0 graph when an SVP pass follows
	svp_stats::section_end(svp_stats::SEC_MAIN_GBUFFER);
	if (svp)
	{
		EnsureTargetSVP();
		// pip CPU-side cost probe for the SVP gbuffer, throttled log while r__svp_diag is on
		CTimer svp_t; svp_t.Start();
		const u32 calls0 = RCache.stat.calls;
		const u32 verts0 = RCache.stat.verts;
		TargetSVP->SetActive();
		svp_stats::section_begin(svp_stats::SEC_SVP_GBUFFER);
		renderGBuffer(true);
		svp_stats::section_end(svp_stats::SEC_SVP_GBUFFER);
		if (ps_r__svp_diag)
		{
			static u32 s_perf_ms = 0;
			if (Device.dwTimeGlobal - s_perf_ms > 1000)
			{
				s_perf_ms = Device.dwTimeGlobal;
				PipMsg("[SVP-PERF] gbuffer %.2fms calls %u verts %uk", svp_t.GetElapsed_sec() * 1000.f,
					RCache.stat.calls - calls0, (RCache.stat.verts - verts0) / 1000);
			}
		}
		// pip stencil the dead corners outside the eyepiece disc while the svp target is still bound,
		// so the lighting and combine passes below skip them
		if (ps_r__svp_corner_mask && !RImplementation.o.dx10_msaa && TargetSVP)
			TargetSVP->stamp_svp_corner_mask();
		TargetMain->SetActive(); // shadow generation + main accumulation run on the main target

		// pip install the dual-accumulate hook, each shadow unit builds its map once on the main atlas
		// then this re-accumulates it into the SVP with no second shadow render
		Device.m_SecondViewport.dual_accum = [this](const std::function<void()>& accum)
		{
			TargetSVP->SetActive();   // SVP gbuffer + accumulator (and, for now, the SVP shadow atlas)
			share_main_smaps();       // re-point the shadow atlas at the main maps the generation built
			Device.m_SecondViewport.force_svp_sss = (ps_r__svp_sss_sun != 0); // sun keeps the SSS contact term
			svp_stats::note_svp_blend();
			svp_stats::section_begin(svp_stats::SEC_SVP_LIGHTS);
			{ PIX_EVENT(SVP_ACCUM); accum(); } // SVP marginal lighting cost: accumulate this unit into the SVP (shared maps)
			svp_stats::section_end(svp_stats::SEC_SVP_LIGHTS);
			Device.m_SecondViewport.force_svp_sss = false;
			TargetMain->SetActive();  // restore for the next unit's generation on the main atlas
		};
	}

	// single shared lighting pass, when svp the render_sun_cascades/render_lights dual-accumulate via
	// the hook and the SVP is combined + captured before the main combine composites it into the lens
	renderSceneLighting(bSUN, svp);
	Device.m_SecondViewport.dual_accum = nullptr;

	// pip close the whole-frame window then draw the stats panel over the finished image
	svp_stats::section_end(svp_stats::SEC_FRAME);
	svp_stats::frame_end(svp);
	svp_stats::draw_overlay();
}

void CRender::renderGBuffer(bool clearGraph)
{
	// label the pass so the SVP (scope) gbuffer is distinguishable from the main one in a capture
	PIX_EVENT_F("RENDER_GBUFFER[%s]", Target == TargetMain ? "MAIN" : "SVP");
	Device.dwViewport++; // pip per-viewport cache counter

	// pip cull the SVP geometry to the scope cone, the captured graph is main frustum so the SVP would
	// otherwise resubmit the whole world through a cone that sees a fraction
	const bool svp_pass = (Target == TargetSVP) && Device.true_pip_on;
	const bool svp_cull = svp_pass && ps_r__svp_cull;
	const bool svp_cull_grass = svp_pass && ps_r__svp_cull_grass && !ps_r__svp_skip_grass;
	if (svp_cull || svp_cull_grass)
	{
		Fmatrix svp_full;
		svp_full.mul(Device.matrices[1].mProject, Device.matrices[1].mView);
		CDSGraphManager::svp_cull_begin(svp_full, svp_cull);
	}
	// pip SVP coverage = (mag * svp_side / main_height)^2, capped at 1: the fraction of the main view's
	// pixel area an object covers in the scope. feeds the LOD scale and the small-object cull threshold
	if (svp_pass && TargetSVP && (ps_r__svp_lod > 0.f || ps_r__svp_cull_ssa > 0.f))
	{
		extern float g_pip_scope_magnification;
		const float mh = (float)Device.dwHeight;
		const float svp_eff = sqrtf((float)TargetSVP->Width * (float)TargetSVP->Height); // geometric-mean side (== Width when square)
		float cov = (mh > 1.f) ? (g_pip_scope_magnification * svp_eff / mh) : 1.f;
		cov *= cov;
		if (cov > 1.f) cov = 1.f;
		if (ps_r__svp_lod > 0.f)
			CDSGraphManager::svp_set_lod_scale(1.f + (cov - 1.f) * ps_r__svp_lod);
		if (ps_r__svp_cull_ssa > 0.f)
			CDSGraphManager::svp_set_ssa_cull(ps_r__svp_cull_ssa, cov);
	}

	phase = PHASE_NORMAL;
	Target->phase_scene_prepare(); // clears + binds this viewport's gbuffer and depth

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
	}

	//******* Main render :: PART-0	-- first
	{
		PIX_EVENT(DEFER_PART0_SPLIT);
		// level, SPLIT
		Target->phase_scene_begin();
		GMBase.r_dsgraph_render_static(0, clearGraph);
		GMBase.r_dsgraph_render_dynamic(0, clearGraph);
		Target->disable_aniso();
	}

	//  Redotix99: for 3D Shader Based Scopes
	// legacy fallback, main view only, skip under true PiP where the fakescope consumer is excluded
	if (scope_3D_fake_enabled && Target == TargetMain
		&& !(Device.true_pip_on && Device.m_SecondViewport.IsSVPActive()))
	{
		ID3D11Resource* zbuffer_res;
		HW.pBaseZB->GetResource(&zbuffer_res);
		HW.pContext->CopyResource(RImplementation.Target->rt_tempzb->pSurface, zbuffer_res);
	}

	if (RImplementation.o.dx10_msaa)
		RCache.set_ZB(RImplementation.Target->rt_MSAADepth->pZRT);

	if (Target == TargetMain) // pip lights captured once on main, the SVP reuses them
	{
		PIX_EVENT(DEFER_TEST_LIGHT_VIS);
		//******* Occlusion testing of volume-limited light-sources
		Target->phase_occq();
		LP_normal.clear();
		LP_pending.clear();
		GMBase.r_dsgraph_capture_lights();
		if (ps_r__svp_stats)
		{
			const u32 lp_total = (u32)(LP_normal.v_point.size() + LP_normal.v_spot.size() + LP_normal.v_shadowed.size()
				+ LP_pending.v_point.size() + LP_pending.v_spot.size() + LP_pending.v_shadowed.size());
			const u32 lp_shadowed = (u32)(LP_normal.v_shadowed.size() + LP_pending.v_shadowed.size());
			svp_stats::note_main_lights(lp_total, lp_shadowed);
		}
	}

	//******* Main render :: PART-1 (second)
	{
		PIX_EVENT(DEFER_PART1_SPLIT);
		// level
		Target->phase_scene_begin();
		if (Target == TargetMain) // pip weapon HUD only in the main view, not the scope image
		{
			GMBase.r_dsgraph_capture_hud();
			// pip snapshot HUD geometry centers before render_hud clears the lists, so the geomscan (in
			// deriveScopeLens, after the clear) can auto-derive the objective distance against the optical axis
			if (scope_svp_enabled || scope_debug >= 2)
				svp_snapshot_hud_geom();
			// pip latch the hud poses this draw uses, logic rewrites the matrices mid render and the
			// scope draws later in the frame must match the housing
			if (Device.true_pip_on)
				GMBase.svp_latch_hud_poses();
			// keep the weapon list when an SVP pass follows, the scope image drains it second
			GMBase.r_dsgraph_render_hud(clearGraph);

			// pip derive the scope lens then build the SVP camera (matrices[1]) while a PiP scope
			// is aimed, zoom-0 tube sights have no zoom fov so ADS + a captured lens also qualifies
			if (scope_svp_enabled && g_pGamePersistent &&
				(g_pGamePersistent->m_pGShaderConstants->hud_params.y > 0.005f
					|| (g_pGamePersistent->m_pGShaderConstants->hud_params.x > 0.05f
						&& !GMBase.RGraph.mapScopeHUDSorted.empty())))
			{
				deriveScopeLens();
				// a culled weapon mid-aim re-arms the stale lens radius, m_W persists on its own
				{
					auto& vp = Device.m_SecondViewport;
					if (vp.eyepiece.radius <= EPS && vp.dbg_eyepiece_r > EPS)
					{
						vp.eyepiece.radius = vp.dbg_eyepiece_r;
						vp.objective.radius = vp.dbg_objective_r;
					}
				}
				if (Device.m_SecondViewport.eyepiece.radius > EPS && TargetSVP)
					svpCamera();
			}
			// pip [3DB] for aimed sights without a captured lens, reflex and irons keep 3d
			// ballistics so the sight independent markers draw, the pip overlay owns the rest
			{
				extern int ps_r__3db_debug;
				extern void ballistics_debug_overlay();
				if (ps_r__3db_debug > 0 && scope_svp_enabled && g_pGamePersistent
					&& g_pGamePersistent->m_pGShaderConstants->hud_params.x > 0.05f
					&& !(Device.m_SecondViewport.eyepiece.radius > EPS && TargetSVP))
					ballistics_debug_overlay();
			}
		}
		else if (svp_pass) // pip the weapon renders through the scope at low mag, one unit inside and out
		{
			extern float g_pip_scope_magnification;
			auto& vp = Device.m_SecondViewport;
			// the weapon drains through the same entrance-pupil camera as the world, the near
			// plane hides everything behind the front lens
			if (scope_svp_enabled >= 2 && vp.eyepiece.radius > EPS)
			{
				RCache.set_xform_view(Device.matrices[1].mView);
				RCache.set_xform_project(Device.matrices[1].mProject);
				GMBase.r_dsgraph_render_hud_svp();
			}
			else
				GMBase.RGraph.mapHUD.clear(); // consume the deferred main-pass clear
		}
		GMBase.r_dsgraph_render_lods(true, clearGraph);
		// pip r__svp_skip_grass drops the near-grass field on the scope pass (mostly off a zoomed cone)
		if (Details && !(svp_pass && ps_r__svp_skip_grass))
		{
			// keep the grass visible set on the main drain when the SVP pass draws it second
			extern bool g_svp_defer_detail_clear;
			g_svp_defer_detail_clear = (!svp_pass && !clearGraph && !ps_r__svp_skip_grass);
			Details->Render();
			g_svp_defer_detail_clear = false;
		}
		Target->phase_scene_end();
	}

	if (svp_cull || svp_cull_grass)
		CDSGraphManager::svp_cull_end(); // pip end SVP cull, the shared shadow/light passes below are unaffected
	if (svp_pass)
	{
		CDSGraphManager::svp_set_lod_scale(1.f); // pip restore full LOD + no cull before the shared light passes
		CDSGraphManager::svp_set_ssa_cull(0.f, 1.f);
	}

	// Wall marks
	if (Wallmarks)
	{
		PIX_EVENT(DEFER_WALLMARKS);
		Target->phase_wallmarks();

		Wallmarks->Render(); // wallmarks has priority as normal geometry
	}

	// full screen pass to mark msaa-edge pixels in highest stencil bit
	if (RImplementation.o.dx10_msaa)
	{
		PIX_EVENT(MARK_MSAA_EDGES);
		Target->mark_msaa_edges();
	}

	//	TODO: DX10: Implement DX10 rain.
	if (ps_r2_ls_flags.test(R3FLAG_DYN_WET_SURF))
	{
		PIX_EVENT(DEFER_RAIN);
		render_rain();
	}

	{
		// Save previus and current matrices
		{
			static Fmatrix mm_saved_viewproj;

			if (!Device.m_SecondViewport.IsSVPFrame())
			{
				Target->Matrix_previous.mul(mm_saved_viewproj, Device.mInvView);
				Target->Matrix_current.set(Device.mProject);
				mm_saved_viewproj.set(Device.mFullTransform);
			}
			else if (svp_pass)
			{
				// pip the scope water SSR and SSDO reproject against these, the setter blocks are main only so
				// the SVP would accumulate against stale matrices, build them from the per viewport matrices
				Fmatrix svp_prev_full, svp_inv_view, svp_prev_inv_view;
				svp_prev_full.mul(Device.matrices_previous[1].mProject, Device.matrices_previous[1].mView);
				svp_inv_view.invert(Device.matrices[1].mView);
				Target->Matrix_previous.mul(svp_prev_full, svp_inv_view);
				Target->Matrix_current.set(Device.matrices[1].mProject);
				svp_prev_inv_view.invert(Device.matrices_previous[1].mView);
				Target->Position_previous.set(svp_prev_inv_view.c);
			}
		}

		// pip the SVP skips its own SSS pass, r__svp_sss_sun computes it so the scope sun keeps the contact term
		if (RImplementation.o.ssfx_sss && (!Device.m_SecondViewport.IsSVPFrame() || (svp_pass && ps_r__svp_sss_sun)))
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
				Target->phase_ssfx_sss_ext(RImplementation.LP_normal);
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

// pip re-point the shadow-atlas textures at the main maps so the SVP accumulation reads the shared
// shadow maps with no second shadow render (SetActive(SVP) had pointed them at the SVP's empty atlas)
void CRender::share_main_smaps()
{
	for (auto& r : TargetMain->RenderTargetRemaps)
		if (r.second == TargetMain->rt_smap_depth ||
			(TargetMain->rt_smap_depth_minmax && r.second == TargetMain->rt_smap_depth_minmax))
			r.first->surface_set(r.second->pSurface);
	RCache.Invalidate();
}

void CRender::renderSceneLighting(BOOL bSUN, bool svp)
{
	// Directional light - fucking sun, cascades build their shadow map once on the main atlas and the
	// accumulate is replayed into the SVP by the dual_accum hook (inside render_sun_cascade + below)
	if (bSUN) //bSUN && Device.dwFrame & 1 --Delayed sun update. Worth to check it in future
	{
		PIX_EVENT(DEFER_SUN);
		svp_stats::note_sun();
		svp_stats::section_begin(svp_stats::SEC_MAIN_LIGHTS);
		RImplementation.stats.l_visible ++;
		render_sun_cascades();
		auto sun_blend = [this] { Target->increment_light_marker(); Target->accum_direct_blend(); };
		sun_blend();
		if (Device.m_SecondViewport.dual_accum)
			Device.m_SecondViewport.dual_accum(sun_blend);
		svp_stats::section_end(svp_stats::SEC_MAIN_LIGHTS);
	}

	phase = PHASE_NORMAL;

	// pip gated self-illum replay into the SVP before the main drain touches the shared list
	if (svp && ps_r__svp_emissive)
	{
		PIX_EVENT(SVP_SELF_ILLUM);
		svp_stats::section_begin(svp_stats::SEC_SVP_EMISSIVE);
		TargetSVP->SetActive();
		TargetSVP->phase_accumulator();
		RCache.set_xform_project(Device.mProject);
		RCache.set_xform_view(Device.mView);
		if (!RImplementation.o.dx10_msaa)
			RCache.set_Stencil(TRUE, D3DCMP_ALWAYS, 0x01, 0xff, 0xff, D3DSTENCILOP_KEEP, D3DSTENCILOP_REPLACE,
			                   D3DSTENCILOP_KEEP);
		else
			RCache.set_Stencil(TRUE, D3DCMP_ALWAYS, 0x01, 0xff, 0x7f, D3DSTENCILOP_KEEP, D3DSTENCILOP_REPLACE,
			                   D3DSTENCILOP_KEEP);
		RCache.set_CullMode(CULL_CCW);
		RCache.set_ColorWriteEnable();
		// the shared list is main frustum wide, reject static items outside the scope cone
		if (ps_r__svp_cull)
		{
			Fmatrix svp_full;
			svp_full.mul(Device.matrices[1].mProject, Device.matrices[1].mView);
			CDSGraphManager::svp_cull_begin(svp_full, true);
		}
		GMBase.r_dsgraph_render_emissive(false);
		// the cone must not leak into the main emissive drain below
		CDSGraphManager::svp_cull_end();
		TargetMain->SetActive();
		svp_stats::section_end(svp_stats::SEC_SVP_EMISSIVE);
	}

	// emissive runs on the main viewport only (active here), the SVP skips self-illum (minor) so it does
	// not re-render + clear the shared GMBase emissive list that the main pass still needs
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
		GMBase.r_dsgraph_render_emissive(RImplementation.o.ssfx_bloom ? false : true);
	}

	if (RImplementation.o.ssfx_bloom)
	{
		// Render Emissive on `rt_ssfx_bloom_emissive`
		FLOAT ColorRGBA[4] = { 0,0,0,0 };
		HW.pContext->ClearRenderTargetView(Target->rt_ssfx_bloom_emissive->pRT, ColorRGBA);
		Target->u_setrt(Target->rt_ssfx_bloom_emissive, NULL, NULL, !RImplementation.o.dx10_msaa ? HW.pBaseZB : Target->rt_MSAADepth->pZRT);
		// Keep regular HUD geometry out of the emissive target. It is already
		// present in the scene texture sampled by the bloom build pass; drawing it
		// here again treats every strict-sorted HUD surface as an emissive source.
		GMBase.r_dsgraph_render_emissive(true, false);
	}

	// Lighting, shadow maps build once on the main atlas, render_lights accumulates per viewport
	// (it replays the accumulate into the SVP via dual_accum, sharing the maps), non dependant on OCCQ
	svp_stats::section_begin(svp_stats::SEC_MAIN_LIGHTS); // svp mirror nests here through the hook
	{
		PIX_EVENT(DEFER_LIGHT_NO_OCCQ);
		Target->phase_accumulator();
		render_lights(LP_normal);
	}

	// Lighting, dependant on OCCQ
	{
		PIX_EVENT(DEFER_LIGHT_OCCQ);
		render_lights(LP_pending);
	}
	svp_stats::section_end(svp_stats::SEC_MAIN_LIGHTS);

	// pip volumetric blur + combine per viewport, the SVP is combined and captured first so the main
	// combine can composite the ready SVP image into the lens
	if (svp)
	{
		svp_stats::section_begin(svp_stats::SEC_SVP_COMBINE);
		TargetSVP->SetActive();
		if (RImplementation.o.ssfx_volumetric)
			Target->phase_ssfx_volumetric_blur();
		phase = PHASE_NORMAL;
		{
			PIX_EVENT(COMBINE_SVP);
			Target->phase_combine();
		}

		TargetSVP->phase_svp_capture(); // SVP combined color -> rt_secondVP, ready for the main lens

		// pip a hybrid magnifier draws the holo dot as a collimated proxy into the captured svp image
		// so it magnifies and tracks, success gates the 1x main-view overlay suppression
		Device.m_SecondViewport.svp_reflex_proxy_ok = false;
		if (ps_r__svp_reflex_capture
			&& !GMBase.RGraph.mapReflexHUDSorted.empty()
			&& !GMBase.RGraph.mapScopeHUDSorted.empty())
		{
			Device.m_SecondViewport.svp_reflex_proxy_ok = TargetSVP->draw_reflex_proxy();
		}

		TargetMain->SetActive();
		svp_stats::section_end(svp_stats::SEC_SVP_COMBINE);
	}

	{
		if (RImplementation.o.ssfx_volumetric)
			Target->phase_ssfx_volumetric_blur();
	}

	phase = PHASE_NORMAL;

	// Postprocess
	{
		PIX_EVENT(DEFER_LIGHT_COMBINE);
		svp_stats::section_begin(svp_stats::SEC_MAIN_COMBINE);
		Target->phase_combine();
		svp_stats::section_end(svp_stats::SEC_MAIN_COMBINE);
	}

	// pip r__scope_debug on-screen inspector overlay (gated + main-only inside the call)
	Target->phase_scope_debug();

	// detail-clear + HUD UI on the main viewport, the scope image has no separate HUD UI
	if (Details)
		Details->details_clear();

	if (g_hud)
	{
		PROF_EVENT("render_hud");
		if (g_hud->RenderActiveItemUIQuery())
			GMBase.r_dsgraph_render_hud_ui();
		if (g_hud->RenderCamAttachedUIQuery())
			GMBase.r_dsgraph_render_cam_ui();
	}
}
#include "../xrRender/CHudInitializer.h"

void CRender::render_forward()
{
	RImplementation.o.distortion = RImplementation.o.distortion_enabled; // enable distorion

	//******* Main render - second order geometry (the one, that doesn't support deffering)
	//.todo: should be done inside "combine" with estimation of of luminance, tone-mapping, etc.
	// combine order, main clears the shared priority-1 forward lists last (keeps smoke + blended fx)
	const bool fwd_clear = svp_clear_shared_list(true);
	if (!fwd_clear) { if (ps_r__svp_stats) ++svp_stats_fwd_keep; svp_ledger_fwd_keep = 1; } // overlay + ledger proof a forward-list keep fired
	{
		// level
		phase = PHASE_NORMAL;
		//	Igor: we don't want to render old lods on next frame.
		GMBase.r_dsgraph_render_static(1, fwd_clear); // normal level, secondary priority
		CParticlesAsync::Wait();
		GMBase.r_dsgraph_render_dynamic(1, fwd_clear);
		GMBase.fade_render(); // faded-portals
		GMBase.r_dsgraph_render_sorted(false); // strict-sorted geoms
		g_pGamePersistent->Environment().RenderLast(); // rain/thunder-bolts
		GMBase.r_dsgraph_render_sorted_hud();
	}

	RImplementation.o.distortion = FALSE; // disable distorion
}

// Redotix99: for 3D Shader Based Scopes
void CRender::render_Reticle()
{
	VERIFY(0 == GMBase.RGraph.mapHUDSorted.Distort.size() + GMBase.RGraph.mapStaticSorted.Distort.size() + GMBase.RGraph.mapDynamicSorted.Distort.size());
	RImplementation.o.distortion = RImplementation.o.distortion_enabled;

	GMBase.r_dsgraph_render_ScopeSorted();

	RImplementation.o.distortion = FALSE;
}

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
