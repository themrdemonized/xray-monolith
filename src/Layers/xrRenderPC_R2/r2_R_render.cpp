#include "stdafx.h"
#include "../../xrEngine/igame_persistent.h"
#include "../xrRender/FBasicVisual.h"
#include "../../xrEngine/customhud.h"
#include "../../xrEngine/xr_object.h"
#include "../xrRender/SkeletonCustom.h"
#include "../../xrParticles/ParticlesAsyncManager.h"

void CRender::render_menu()
{
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
		Target->u_setrt(Target->rt_Generic_1, 0, 0, HW.pBaseZB); // Now RT is a distortion mask
		CHK_DX(HW.pDevice->Clear ( 0L, NULL, D3DCLEAR_TARGET, color_rgba(127,127,0,127), 1.0f, 0L));
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
	bool _menu_pp = g_pGamePersistent ? g_pGamePersistent->OnRenderPPUI_query() : false;
	if (_menu_pp)
	{
		render_menu();
		return;
	};

	IMainMenu* pMainMenu = g_pGamePersistent ? g_pGamePersistent->m_pMainMenu : 0;
	bool bMenu = pMainMenu ? pMainMenu->CanSkipSceneRendering() : false;

	if (!(g_pGameLevel && g_hud) || bMenu) return;

	if (m_bFirstFrameAfterReset)
	{
		for (light* L : v_all_lights)//critical!!!
			L->m_moving_frames = 0;

		m_bFirstFrameAfterReset = false;
		return;
	}

	//.	VERIFY					(g_pGameLevel && g_pGameLevel->pHUD);

	// Configure
	RImplementation.o.distortion = FALSE; // disable distorion
	Fcolor sun_color = ((light*)Lights.sun_adapted._get())->color;
	BOOL bSUN = ps_r2_ls_flags.test(R2FLAG_SUN) && (u_diffuse2s(sun_color.r, sun_color.g, sun_color.b) > EPS);
	if (o.sunstatic) bSUN = FALSE;
	// Msg						("sstatic: %s, sun: %s",o.sunstatic?"true":"false", bSUN?"true":"false");

	// HOM
	ViewBase.CreateFromMatrix(Device.mFullTransform, FRUSTUM_P_LRTB + FRUSTUM_P_FAR);
    HOM.MT_RENDER();

	Target->phase_scene_prepare();

	//******* Main calc - DEFERRER RENDERER
	// Main calc
	Device.Statistic->RenderCALC.Begin();
	phase = PHASE_NORMAL;
	GMBase.traverse(pLastSector, ViewBase, Device.vCameraPosition, Device.mFullTransform);
	GMBase.r_dsgraph_capture(false, true);
	GMBase.r_dsgraph_capture_hud();
	Device.Statistic->RenderCALC.End();

	if (ps_r2_ls_flags.test(R2FLAG_TERRAIN_PREPASS))
	{
		Target->u_setrt(Device.dwWidth, Device.dwHeight, NULL, NULL, NULL, HW.pBaseZB);
	}

	//******* Main render :: PART-0	-- first
	{
		// level, SPLIT
		Target->phase_scene_begin();
		GMBase.r_dsgraph_render_graph(0);
		Target->disable_aniso();
	}

	//******* Occlusion testing of volume-limited light-sources
	{
		Target->phase_occq();
		LP_normal.clear();
		LP_pending.clear();
		GMBase.r_dsgraph_capture_lights();
	}

	//******* Main render :: PART-1 (second)
	{
		// level, SPLIT2
		Target->phase_scene_begin();
		GMBase.r_dsgraph_render_hud();
		GMBase.r_dsgraph_render_lods(true, true);
		if (Details) Details->Render();
		Target->phase_scene_end();
	}

	// Wall marks
	if (Wallmarks)
	{
		Target->phase_wallmarks();

		Wallmarks->Render(); // wallmarks has priority as normal geometry
	}

	// Directional light - fucking sun
	if (bSUN)
	{
		RImplementation.stats.l_visible ++;
		render_sun_cascades();
		Target->increment_light_marker();
		Target->accum_direct_blend();
	}

	{
		Target->phase_accumulator();
		// Render emissive geometry, stencil - write 0x0 at pixel pos
		RCache.set_xform_project(Device.mProject);
		RCache.set_xform_view(Device.mView);
		// Stencil - write 0x1 at pixel pos - 
		RCache.set_Stencil(TRUE, D3DCMP_ALWAYS, 0x01, 0xff, 0xff, D3DSTENCILOP_KEEP, D3DSTENCILOP_REPLACE,
		                   D3DSTENCILOP_KEEP);
		//RCache.set_Stencil						(TRUE,D3DCMP_ALWAYS,0x00,0xff,0xff,D3DSTENCILOP_KEEP,D3DSTENCILOP_REPLACE,D3DSTENCILOP_KEEP);
		RCache.set_CullMode(CULL_CCW);
		RCache.set_ColorWriteEnable();
		GMBase.r_dsgraph_render_emissive();

		// Stencil	- draw only where stencil >= 0x1
		RCache.set_Stencil(TRUE, D3DCMP_LESSEQUAL, 0x01, 0xff, 0x00);
		RCache.set_CullMode(CULL_NONE);
		RCache.set_ColorWriteEnable();
	}

	// Lighting, non dependant on OCCQ
	Target->phase_accumulator();
	render_lights(LP_normal);

	// Lighting, dependant on OCCQ
	render_lights(LP_pending);

	// Postprocess
	Target->phase_combine();

	/*if (Details)
		Details->details_clear();*/

	if (g_hud)
	{
		if (g_hud->RenderActiveItemUIQuery())
			GMBase.r_dsgraph_render_hud_ui();
		if (g_hud->RenderCamAttachedUIQuery())
			GMBase.r_dsgraph_render_cam_ui();
	}
}

void CRender::RenderUI3DDirect()
{
	// Icons need independent depth: later world passes may still use the scene buffer.
	// Query bound surfaces directly because an invalidated RCache may contain null pointers.
	IDirect3DSurface9* saved_depth = nullptr;
	const HRESULT depth_result = HW.pDevice->GetDepthStencilSurface(&saved_depth);
	if (depth_result != D3DERR_NOTFOUND)
		R_CHK(depth_result);
	DWORD saved_z_enable;
	R_CHK(HW.pDevice->GetRenderState(D3DRS_ZENABLE, &saved_z_enable));

	D3DSURFACE_DESC color_desc;
	IDirect3DSurface9* color_target = nullptr;
	R_CHK(HW.pDevice->GetRenderTarget(0, &color_target));
	R_CHK(color_target->GetDesc(&color_desc));
	_RELEASE(color_target);
	D3DFORMAT depth_format = HW.Caps.fDepth;
	if (saved_depth)
	{
		D3DSURFACE_DESC depth_desc;
		R_CHK(saved_depth->GetDesc(&depth_desc));
		depth_format = depth_desc.Format;
	}

	// Recreate only when the UI target changes size, format or multisampling.
	if (Target->ui_icons_depth)
	{
		D3DSURFACE_DESC desc;
		R_CHK(Target->ui_icons_depth->GetDesc(&desc));
		if (desc.Width != color_desc.Width || desc.Height != color_desc.Height ||
			desc.Format != depth_format || desc.MultiSampleType != color_desc.MultiSampleType ||
			desc.MultiSampleQuality != color_desc.MultiSampleQuality)
			_RELEASE(Target->ui_icons_depth);
	}
	if (!Target->ui_icons_depth)
		R_CHK(HW.pDevice->CreateDepthStencilSurface(color_desc.Width, color_desc.Height,
			depth_format, color_desc.MultiSampleType, color_desc.MultiSampleQuality,
			TRUE, &Target->ui_icons_depth, nullptr));

	RCache.set_ZB(Target->ui_icons_depth);
	R_CHK(HW.pDevice->Clear(0, nullptr, D3DCLEAR_ZBUFFER, 0, 1.f, 0));
	// PassSET_ZB configures the comparison/writes, but does not enable depth testing.
	RCache.set_Z(TRUE);
	GMUI3D.r_dsgraph_render_ui_3d();
	RCache.set_ZB(saved_depth);
	_RELEASE(saved_depth);
	RCache.set_Z(saved_z_enable);

}

void CRender::render_forward()
{
	RImplementation.o.distortion = RImplementation.o.distortion_enabled; // enable distorion

	//******* Main render - second order geometry (the one, that doesn't support deffering)
	//.todo: should be done inside "combine" with estimation of of luminance, tone-mapping, etc.
	{
		HOM.Enable();
		// level
		phase = PHASE_NORMAL;
		//	Igor: we don't want to render old lods on next frame.
		GMBase.r_dsgraph_render_static(1);					// normal level, secondary priority
		CParticlesAsync::Wait();
		GMBase.r_dsgraph_render_dynamic(1);
		GMBase.fade_render();					// faded-portals
		GMBase.r_dsgraph_render_sorted();					// strict-sorted geoms
		g_pGamePersistent->Environment().RenderLast(); // rain/thunder-bolts
	}

	RImplementation.o.distortion = FALSE; // disable distorion
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

	IDirect3DSurface9* pBackBuffer = nullptr;
	HW.pDevice->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &pBackBuffer);
	D3DXLoadSurfaceFromSurface((*RT)->pRT, 0, 0, pBackBuffer, 0, 0, D3DX_DEFAULT, 0);
	pBackBuffer->Release();
}
