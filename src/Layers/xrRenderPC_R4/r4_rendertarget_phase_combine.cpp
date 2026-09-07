#include "stdafx.h"
#include "../../xrEngine/igame_persistent.h"
#include "../../xrEngine/environment.h"

#include "../xrRender/dxEnvironmentRender.h"

#define STENCIL_CULL 0

void CRenderTarget::DoAsyncScreenshot()
{
	//	Igor: screenshot will not have postprocess applied.
	//	TODO: fox that later
	if (RImplementation.m_bMakeAsyncSS)
	{
		HRESULT hr;

		//	HACK: unbind RT. CopyResourcess needs src and targetr to be unbound.
		//u_setrt				( Device.dwWidth,Device.dwHeight,HW.pBaseRT,NULL,NULL,HW.pBaseZB);

		//ID3DTexture2D *pTex = 0;
		//if (RImplementation.o.dx10_msaa)
		//	pTex = rt_Generic->pSurface;
		//else
		//	pTex = rt_Color->pSurface;


		//HW.pDevice->CopyResource( t_ss_async, pTex );
		ID3DTexture2D* pBuffer;
		hr = HW.m_pSwapChain->GetBuffer(0, __uuidof( ID3D10Texture2D), (LPVOID*)&pBuffer);
		// TODO: this won't work in DX11 with HDR due to texture format incompat
		HW.pContext->CopyResource(t_ss_async, pBuffer);


		RImplementation.m_bMakeAsyncSS = false;
	}
}

float hclip(float v, float dim) { return 2.f * v / dim - 1.f; }

void CRenderTarget::phase_combine()
{
	PIX_EVENT(phase_combine);
    PROF_EVENT("phase_combine");
	
	bool ssfx_PrevPos_Requiered = false;

	//	TODO: DX10: Remove half poxel offset
	bool _menu_pp = g_pGamePersistent ? g_pGamePersistent->OnRenderPPUI_query() : false;

	u32 Offset = 0;
	Fvector2 p0, p1;

	//*** exposure-pipeline
	if (t_LUM_src != rt_LUM_pool[0]->pTexture)
		t_LUM_src->surface_set(rt_LUM_pool[0]->pSurface);
	if (t_LUM_dest != rt_LUM_pool[1]->pTexture)
		t_LUM_dest->surface_set(rt_LUM_pool[1]->pSurface);

	if (RImplementation.o.ssao_hdao && RImplementation.o.ssao_ultra)
	{
		if (ps_r_ssao > 0)
		{
			PROF_EVENT("PHASE_AMBIENT_OCCLUSION");
			phase_hdao();
		}
	}
	else
	{
		if (RImplementation.o.ssao_opt_data)
		{
			phase_downsamp();
			//phase_ssao();
		}
		else if (RImplementation.o.ssao_blur_on)
			phase_ssao();
	}

	// Save previus and current matrices
	Fvector2 m_blur_scale;
	{
		static Fmatrix m_saved_viewproj;

		if (!Device.m_SecondViewport.IsSVPFrame())
		{
			static Fvector3 saved_position;
			Position_previous.set(saved_position);
			saved_position.set(Device.vCameraPosition);

			Matrix_previous.mul(m_saved_viewproj, Device.mInvView);
			Matrix_current.set(Device.mProject);
			m_saved_viewproj.set(Device.mFullTransform);
		}
		float scale = ps_r2_mblur / 2.f;
		m_blur_scale.set(scale, -scale).div(12.f);
	}

	{
		// pip AO + IL must run for the true-PiP SVP or its shadows go near-black (no ambient/indirect
		// fill), the legacy fake-SVP frame still skips them (stock !IsSVPFrame) so off is unchanged
		if (Device.true_pip_on || !Device.m_SecondViewport.IsSVPFrame())
		{
			// Clear RT
			FLOAT ColorRGBA[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
			HW.pContext->ClearRenderTargetView(rt_ssfx_temp->pRT, ColorRGBA);
			HW.pContext->ClearRenderTargetView(rt_ssfx_temp2->pRT, ColorRGBA);

			if (RImplementation.o.ssfx_ao && ps_ssfx_ao.y > 0)
			{
				ssfx_PrevPos_Requiered = true;
				phase_ssfx_ao(); // [SSFX] - New AO Phase
			}

			if (RImplementation.o.ssfx_il && ps_ssfx_il.y > 0)
			{
				ssfx_PrevPos_Requiered = true;
				phase_ssfx_il(); // [SSFX] - New IL Phase
			}
		}
	}

	FLOAT ColorRGBA[4] = {0.0f, 0.0f, 0.0f, 0.0f};
	// low/hi RTs
	if (!RImplementation.o.dx10_msaa)
	{
		HW.pContext->ClearRenderTargetView(rt_Generic_0->pRT, ColorRGBA);
		HW.pContext->ClearRenderTargetView(rt_Generic_1->pRT, ColorRGBA);
		u_setrt(rt_Generic_0, rt_Generic_1, rt_Heat, HW.pBaseZB);	//--DSR-- HeatVision
	}
	else
	{
		HW.pContext->ClearRenderTargetView(rt_Generic_0_r->pRT, ColorRGBA);
		HW.pContext->ClearRenderTargetView(rt_Generic_1_r->pRT, ColorRGBA);
		u_setrt(rt_Generic_0_r, rt_Generic_1_r, rt_Heat, RImplementation.Target->rt_MSAADepth->pZRT);	//--DSR-- HeatVision
	}
	RCache.set_CullMode(CULL_NONE);
	RCache.set_Stencil(FALSE);

	BOOL split_the_scene_to_minimize_wait = FALSE;
	if (ps_r2_ls_flags.test(R2FLAG_EXP_SPLIT_SCENE)) split_the_scene_to_minimize_wait = TRUE;

	// draw skybox
	if (1)
	{
		//	Moved to shader!
		//RCache.set_ColorWriteEnable					();
		//	Moved to shader!
		//RCache.set_Z(FALSE);

		// nvg clouds prescale by s_tonemap in the vs and the svp meter mismeasures packed channels,
		// pin the svp sky pass to the main view adaptation so the scope clouds match the eye view
		extern Fvector4 ps_dev_param_8;
		const bool svp_nvg_sky = Device.true_pip_on && Device.m_SecondViewport.m_render_pass_is_svp
			&& ps_dev_param_8.x >= 1.f && RImplementation.TargetMain && this != RImplementation.TargetMain;
		if (svp_nvg_sky) { if (ps_r__svp_stats) ++svp_stats_nvg_sky; svp_ledger_nvg_sky = 1; } // overlay + ledger proof the nvg sky lum remap fired
		if (svp_nvg_sky)
			t_LUM_dest->surface_set(RImplementation.TargetMain->rt_LUM_pool[0]->pSurface);

		g_pGamePersistent->Environment().RenderSky();

		//	Igor: Render clouds before compine without Z-test
		//	to avoid siluets. HOwever, it's a bit slower process.
		g_pGamePersistent->Environment().RenderClouds();

		if (svp_nvg_sky)
			t_LUM_dest->surface_set(rt_LUM_pool[1]->pSurface);

		//	Moved to shader!
		//RCache.set_Z(TRUE);
	}

	// 
	//if (RImplementation.o.bug)	{
	RCache.set_Stencil(TRUE, D3DCMP_LESSEQUAL, 0x01, 0xff, 0x00); // stencil should be >= 1
	if (RImplementation.o.nvstencil)
	{
		u_stencil_optimize(CRenderTarget::SO_Combine);
		RCache.set_ColorWriteEnable();
	}
	//}

	// calc m-blur matrices
	/*Fmatrix m_previous, m_current;
	Fvector2 m_blur_scale;
	{
		static Fmatrix m_saved_viewproj;

		// (new-camera) -> (world) -> (old_viewproj)
		m_previous.mul(m_saved_viewproj, Device.mInvView);
		m_current.set(Device.mProject);
		m_saved_viewproj.set(Device.mFullTransform);
		float scale = ps_r2_mblur / 2.f;
		m_blur_scale.set(scale, -scale).div(12.f);
	}*/

	// Draw full-screen quad textured with our scene image
	if (!_menu_pp)
	{
		PIX_EVENT(combine_1);
		// Compute params
		CEnvDescriptorMixer& envdesc = *g_pGamePersistent->Environment().CurrentEnv;
		const float minamb = 0.001f;
		Fvector4 ambclr = {
			_max(envdesc.ambient.x * 2, minamb), _max(envdesc.ambient.y * 2, minamb),
			_max(envdesc.ambient.z * 2, minamb), 0
		};
		ambclr.mul(ps_r2_sun_lumscale_amb + g_pGamePersistent->nv_shader_data.lum_factor);

		//.		Fvector4	envclr			= { envdesc.sky_color.x*2+EPS,	envdesc.sky_color.y*2+EPS,	envdesc.sky_color.z*2+EPS,	envdesc.weight					};
		Fvector4 envclr = {
			envdesc.hemi_color.x * 2 + EPS, envdesc.hemi_color.y * 2 + EPS, envdesc.hemi_color.z * 2 + EPS,
			envdesc.weight
		};

		Fvector4 fogclr = { envdesc.fog_color.x, envdesc.fog_color.y, envdesc.fog_color.z, 0 };
		envclr.x *= 2 * ps_r2_sun_lumscale_hemi;
		envclr.y *= 2 * ps_r2_sun_lumscale_hemi;
		envclr.z *= 2 * ps_r2_sun_lumscale_hemi;
		Fvector4 sunclr, sundir;

		float fSSAONoise = 2.0f;
		fSSAONoise *= tan(deg2rad(67.5f / 2.0f));
		fSSAONoise /= tan(deg2rad(Device.fFOV / 2.0f));

		float fSSAOKernelSize = 150.0f;
		fSSAOKernelSize *= tan(deg2rad(67.5f / 2.0f));
		fSSAOKernelSize /= tan(deg2rad(Device.fFOV / 2.0f));


		// sun-params
		{
			light* fuckingsun = (light*)RImplementation.Lights.sun_adapted._get();
			Fvector L_dir, L_clr;
			float L_spec;
			L_clr.set(fuckingsun->color.r, fuckingsun->color.g, fuckingsun->color.b);
			L_spec = u_diffuse2s(L_clr);
			Device.mView.transform_dir(L_dir, fuckingsun->direction);
			L_dir.normalize();

			sunclr.set(L_clr.x, L_clr.y, L_clr.z, L_spec);
			sundir.set(L_dir.x, L_dir.y, L_dir.z, 0);
		}

		/*
		// Fill VB
		//float	_w					= float(Device.dwWidth);
		//float	_h					= float(Device.dwHeight);
		//p0.set						(.5f/_w, .5f/_h);
		//p1.set						((_w+.5f)/_w, (_h+.5f)/_h );
		//p0.set						(.5f/_w, .5f/_h);
		//p1.set						((_w+.5f)/_w, (_h+.5f)/_h );

		// Fill vertex buffer
		Fvector4* pv				= (Fvector4*)	RCache.Vertex.Lock	(4,g_combine_VP->vb_stride,Offset);
		//pv->set						(hclip(EPS,		_w),	hclip(_h+EPS,	_h),	p0.x, p1.y);	pv++;
		//pv->set						(hclip(EPS,		_w),	hclip(EPS,		_h),	p0.x, p0.y);	pv++;
		//pv->set						(hclip(_w+EPS,	_w),	hclip(_h+EPS,	_h),	p1.x, p1.y);	pv++;
		//pv->set						(hclip(_w+EPS,	_w),	hclip(EPS,		_h),	p1.x, p0.y);	pv++;
		pv->set						(-1,	1,	0, 1);	pv++;
		pv->set						(-1,	-1,	0, 0);	pv++;
		pv->set						(1,		1,	1, 1);	pv++;
		pv->set						(1,		-1,	1, 0);	pv++;
		RCache.Vertex.Unlock		(4,g_combine_VP->vb_stride);
		*/

		// Fill VB
		float scale_X = float(Device.dwWidth) / float(TEX_jitter);
		float scale_Y = float(Device.dwHeight) / float(TEX_jitter);

		// Fill vertex buffer
		FVF::TL* pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
		pv->set(-1, 1, 0, 1, 0, 0, scale_Y);
		pv++;
		pv->set(-1, -1, 0, 0, 0, 0, 0);
		pv++;
		pv->set(1, 1, 1, 1, 0, scale_X, scale_Y);
		pv++;
		pv->set(1, -1, 1, 0, 0, scale_X, 0);
		pv++;
		RCache.Vertex.Unlock(4, g_combine->vb_stride);

		dxEnvDescriptorMixerRender& envdescren = *(dxEnvDescriptorMixerRender*)(&*envdesc.m_pDescriptorMixer);

		// Setup textures
		ID3DBaseTexture* e0 = _menu_pp ? 0 : envdescren.sky_r_textures_env[0].second->surface_get();
		ID3DBaseTexture* e1 = _menu_pp ? 0 : envdescren.sky_r_textures_env[1].second->surface_get();
		t_envmap_0->surface_set(e0);
		_RELEASE(e0);
		t_envmap_1->surface_set(e1);
		_RELEASE(e1);

		// Draw
		RCache.set_Element(s_combine->E[0]);
		//RCache.set_Geometry			(g_combine_VP		);
		RCache.set_Geometry(g_combine);

		RCache.set_c("m_v2w", Device.mInvView);
		RCache.set_c("L_ambient", ambclr);

		RCache.set_c("Ldynamic_color", sunclr);
		RCache.set_c("Ldynamic_dir", sundir);

		RCache.set_c("env_color", envclr);
		RCache.set_c("fog_color", fogclr);

		RCache.set_c("ssao_noise_tile_factor", fSSAONoise);
		RCache.set_c("ssao_kernel_size", fSSAOKernelSize);

		if (!RImplementation.o.dx10_msaa)
			RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);
		else
		{
			RCache.set_Stencil(TRUE, D3DCMP_EQUAL, 0x01, 0x81, 0);
			RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);
			if (RImplementation.o.dx10_msaa_opt)
			{
				RCache.set_Element(s_combine_msaa[0]->E[0]);
				RCache.set_Stencil(TRUE, D3DCMP_EQUAL, 0x81, 0x81, 0);
				RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);
			}
			else
			{
				for (u32 i = 0; i < RImplementation.o.dx10_msaa_samples; ++i)
				{
					RCache.set_Element(s_combine_msaa[i]->E[0]);
					StateManager.SetSampleMask(u32(1) << i);
					RCache.set_Stencil(TRUE, D3DCMP_EQUAL, 0x81, 0x81, 0);
					RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);
				}
				StateManager.SetSampleMask(0xffffffff);
			}
			RCache.set_Stencil(FALSE, D3DCMP_EQUAL, 0x01, 0xff, 0);
		}
	}

	//Copy previous rt
	if (!RImplementation.o.dx10_msaa)
		HW.pContext->CopyResource(rt_Generic_temp->pTexture->surface_get(), rt_Generic_0->pTexture->surface_get());
	else
		HW.pContext->CopyResource(rt_Generic_temp->pTexture->surface_get(), rt_Generic_0_r->pTexture->surface_get());

	// pip the SVP runs SSR + water like the main view, off and legacy fake SVP keep the stock skip
	const bool svp_pass = Device.true_pip_on && Device.m_SecondViewport.m_render_pass_is_svp;
	// pip the deferred SSR runs on the scope at levels 0/1 (reflective surfaces), skipped at 2 (matte)
	if (RImplementation.o.ssfx_ssr && ((svp_pass && ps_r__svp_skip_ssr < 2) || !Device.m_SecondViewport.IsSVPFrame()))
	{
		ssfx_PrevPos_Requiered = true;
		phase_ssfx_ssr(); // [SSFX] - New SSR Phase
	}

	// pip water SSR only at level 0 (the reflective water below needs it, the SSS shader discards it
	// otherwise), always on the main
	if (RImplementation.o.ssfx_water && ((svp_pass && ps_r__svp_skip_ssr == 0) || !Device.m_SecondViewport.IsSVPFrame()))
	{
		FLOAT ColorRGBA[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
		HW.pContext->ClearRenderTargetView(rt_ssfx_temp->pRT, ColorRGBA);
		HW.pContext->ClearRenderTargetView(rt_ssfx_temp2->pRT, ColorRGBA);

		if (!RImplementation.o.dx10_msaa)
			u_setrt(rt_ssfx_temp, 0, 0, 0);
		else
			u_setrt(rt_ssfx_temp, 0, 0, 0);

		float w = float(Device.dwWidth);
		float h = float(Device.dwHeight);

		// Render Scale
		set_viewport_size(HW.pContext, w / ps_ssfx_water.x, h / ps_ssfx_water.x);

		// Render Water SSR
		RCache.set_xform_world(Fidentity);
		RImplementation.GMBase.r_dsgraph_render_water_ssr();

		// Restore Viewport
		set_viewport_size(HW.pContext, w, h);

		// Save Frame
		HW.pContext->CopyResource(rt_ssfx_water->pTexture->surface_get(), rt_ssfx_temp->pTexture->surface_get());

		// Water SSR Blur
		phase_ssfx_water_blur();

		// Water waves
		phase_ssfx_water_waves();
	}

	if (!RImplementation.o.dx10_msaa)
		u_setrt(rt_Generic_0, 0, 0, HW.pBaseZB);
	else
		u_setrt(rt_Generic_0_r, 0, 0, rt_MSAADepth->pZRT);

	// Final water rendering ( All the code above can be omitted if the Water module isn't installed )
	// pip the SSS water shader flattens the scope water (ssfx_issvp), force_water_reflect turns the
	// reflection back on for the SVP draw at level 0, do not clear the shared mapWater (main pass needs it)
	Device.m_SecondViewport.force_water_reflect = svp_pass && (ps_r__svp_skip_ssr == 0);
	RCache.set_xform_world(Fidentity);
	RImplementation.GMBase.r_dsgraph_render_water(!svp_pass);
	Device.m_SecondViewport.force_water_reflect = false;
	
	{
		if (RImplementation.o.ssfx_rain)
		{
			phase_ssfx_rain(); // Render a small color buffer to do the refraction and more

			if (!RImplementation.o.dx10_msaa)
				u_setrt(rt_Generic_0, 0, rt_ssfx_motion_vectors, HW.pBaseZB);
			else
				u_setrt(rt_Generic_0_r, 0, rt_ssfx_motion_vectors, rt_MSAADepth->pZRT);
		}

		g_pGamePersistent->Environment().RenderLast(); // rain/thunder-bolts
	}

	/*if (ssfx_PrevPos_Requiered)
		HW.pContext->CopyResource(rt_ssfx_prevPos->pTexture->surface_get(), rt_Position->pTexture->surface_get());*/

	// Update rt_Generic_temp ( rain and water )
	if (RImplementation.o.ssfx_glass)
	{
		if (!RImplementation.o.dx10_msaa)
			HW.pContext->CopyResource(rt_Generic_temp->pTexture->surface_get(), rt_Generic_0->pTexture->surface_get());
		else
			HW.pContext->CopyResource(rt_Generic_temp->pTexture->surface_get(), rt_Generic_0_r->pTexture->surface_get());
	}

	// Forward rendering
	{
		PIX_EVENT(Forward_rendering);

		//--DSR-- HeatVision_start
		if (!RImplementation.o.dx10_msaa)
			u_setrt(rt_Generic_0, rt_Heat, rt_ssfx_motion_vectors, HW.pBaseZB); // LDR RT
		else
			u_setrt(rt_Generic_0_r, rt_Heat, rt_ssfx_motion_vectors, RImplementation.Target->rt_MSAADepth->pZRT); // LDR RT
		//--DSR-- HeatVision_end

		RCache.set_CullMode(CULL_CCW);
		RCache.set_Stencil(FALSE);
		RCache.set_ColorWriteEnable();
		//	TODO: DX10: CHeck this!
		//g_pGamePersistent->Environment().RenderClouds	();
		RImplementation.render_forward();
		if (g_pGamePersistent) g_pGamePersistent->OnRenderPPUI_main(); // PP-UI
	}

	//	Igor: for volumetric lights
	//	combine light volume here
	// pip r__svp_skip_volumetric drops god rays on the scope pass (subtle at magnification)
	if (!(svp_pass && ps_r__svp_skip_volumetric))
	{
		if (RImplementation.o.ssfx_volumetric)
		{
			if (m_bHasActiveVolumetric || m_bHasActiveVolumetric_spot)
				phase_combine_volumetric();
		}
		else
		{
			if (m_bHasActiveVolumetric)
				phase_combine_volumetric();
		}
	}

	// Perform blooming filter and distortion if needed
	RCache.set_Stencil(FALSE);

	if (RImplementation.o.dx10_msaa)
	{
		// we need to resolve rt_Generic_1 into rt_Generic_1_r
		HW.pContext->ResolveSubresource(rt_Generic_1->pTexture->surface_get(), 0,
		                                rt_Generic_1_r->pTexture->surface_get(), 0, DXGI_FORMAT_R8G8B8A8_UNORM);
		HW.pContext->ResolveSubresource(rt_Generic_0->pTexture->surface_get(), 0,
		                                rt_Generic_0_r->pTexture->surface_get(), 0, DXGI_FORMAT_R8G8B8A8_UNORM);
	}

	// for msaa we need a resolved color buffer - Holger
	phase_bloom(); // HDR RT invalidated here

	//RImplementation.rmNormal();
	//u_setrt(rt_Generic_1,0,0,HW.pBaseZB);

	// Distortion filter
	BOOL bDistort = RImplementation.o.distortion_enabled; // This can be modified
	{
		if ((
			0 == RImplementation.GMBase.RGraph.mapStaticSorted.Distort.size() &&
			0 == RImplementation.GMBase.RGraph.mapDynamicSorted.Distort.size() &&
			0 == RImplementation.GMBase.RGraph.mapHUDSorted.Distort.size()
			) && !_menu_pp
		)		
			bDistort = FALSE;
		if (bDistort)
		{
			PIX_EVENT(render_distort_objects);
			FLOAT ColorRGBA[4] = {127.0f / 255.0f, 127.0f / 255.0f, 0.0f, 127.0f / 255.0f};
			if (!RImplementation.o.dx10_msaa)
			{
				u_setrt(rt_Generic_1, 0, 0, HW.pBaseZB); // Now RT is a distortion mask
				HW.pContext->ClearRenderTargetView(rt_Generic_1->pRT, ColorRGBA);
			}
			else
			{
				u_setrt(rt_Generic_1_r, 0, 0, RImplementation.Target->rt_MSAADepth->pZRT);
				// Now RT is a distortion mask
				HW.pContext->ClearRenderTargetView(rt_Generic_1_r->pRT, ColorRGBA);
			}
			RCache.set_CullMode(CULL_CCW);
			RCache.set_Stencil(FALSE);
			RCache.set_ColorWriteEnable();
			//CHK_DX(HW.pDevice->Clear	( 0L, NULL, D3DCLEAR_TARGET, color_rgba(127,127,0,127), 1.0f, 0L));
			RImplementation.GMBase.r_dsgraph_render_distort();
			if (g_pGamePersistent) g_pGamePersistent->OnRenderPPUI_PP(); // PP-UI
			// pip the composited lens must not warp, stamp the mask neutral over its footprint
			if (ps_r__svp_distort_guard && Device.true_pip_on && this == RImplementation.TargetMain
				&& Device.m_SecondViewport.IsSVPActive()
				&& !RImplementation.GMBase.RGraph.mapScopeHUDSorted.empty())
			{
				if (ps_r__svp_stats) ++svp_stats_distort_guard; // overlay proof the distort guard stamped
				svp_ledger_distort_guard = 1;
				EnsureScopeShaders();
				draw_scope(s_svp_distort_stamp, []() { RCache.set_c("scope_phase", 0); });
			}
		}
	}

	/*
	   if( RImplementation.o.dx10_msaa )
	   {
	      // we need to resolve rt_Generic_1 into rt_Generic_1_r
	      if( bDistort )
	         HW.pDevice->ResolveSubresource( rt_Generic_1_r->pTexture->surface_get(), 0, rt_Generic_1->pTexture->surface_get(), 0, DXGI_FORMAT_R8G8B8A8_UNORM );
	   }
	   */
	RCache.set_Stencil(FALSE);




	if (!_menu_pp)
	{
		if (ps_sunshafts_mode == R2SS_SCREEN_SPACE || ps_sunshafts_mode == R2SS_COMBINE_SUNSHAFTS)
			phase_sunshafts();
	}

	if (RImplementation.o.ssfx_fog && ps_ssfx_fog_scattering > 0)
	{
		phase_ssfx_fog_scattering();
	}

	// pip with DLSS on, skip the SVP engine AA/post so rt_Generic_0$svp stays aliased + jittered for the
	// eval (DLSS does its own reconstruction), main + off + gate-0 keep the stock AA path unchanged
	const bool svp_dlss_skip_aa = (ps_r__svp_dlss != 0 && Device.true_pip_on && Device.m_SecondViewport.m_render_pass_is_svp);

	// pip the SVP runs TAA before motion blur + the lens paint so it reprojects with the SVP's own
	// motion vectors, the main view keeps its TAA at the end (below)
	if (RImplementation.o.ssfx_taa && ps_ssfx_taa.x > 0 &&
		Device.true_pip_on && Device.m_SecondViewport.m_render_pass_is_svp && !svp_dlss_skip_aa)
	{
		phase_ssfx_taa();
	}

	// pip r__svp_skip_motionblur drops motion blur on the scope pass, magnified blur is an artifact
	if (RImplementation.o.ssfx_motionblur && ps_ssfx_motionblur.y > 0 && !svp_dlss_skip_aa
		&& !(svp_pass && ps_r__svp_skip_motionblur))
	{
		phase_ssfx_motion_blur();
	}

	// pip composite the lens once on the MAIN view only, whenever a 3DSS scope is aimed under true_pip
	// (magnified or a 1x reflex/eyepiece was captured), true_pip off keeps the stock scope_3D_fake_enabled gate
	if (this == RImplementation.TargetMain
		&& (scope_3D_fake_enabled
			|| (Device.true_pip_on
				&& (Device.m_SecondViewport.IsSVPActive()
					|| !RImplementation.GMBase.RGraph.mapReflexHUDSorted.empty()
					|| !RImplementation.GMBase.RGraph.mapScopeHUDSorted.empty()))))
	{
		phase_3DSSReticle(); // Redotix99 3D Shader Based Scopes / pip true-PiP lens composite
	}

	// Compute blur textures, pip run it for the SVP too so the scope lens is anti-aliased like the main
	// view (its buffers are per-target), off keeps the stock !IsSVPFrame skip
	if ((Device.true_pip_on && Device.m_SecondViewport.m_render_pass_is_svp && !svp_dlss_skip_aa) || !Device.m_SecondViewport.IsSVPFrame())
		phase_blur();

	//Compute bloom (new)
	if (RImplementation.o.ssfx_bloom)
	{
		// pip run bloom on the SVP pass too so magnified bright sources flare (per-target buffers)
		if ((Device.true_pip_on && Device.m_SecondViewport.m_render_pass_is_svp && ps_r__svp_bloom)
			|| !Device.m_SecondViewport.IsSVPFrame())
			phase_ssfx_bloom();
		else
			HW.pContext->ClearRenderTargetView(rt_ssfx_bloom1->pRT, ColorRGBA);
	}
	else
	{
		phase_pp_bloom();
	}
	
	// pip dof/lut run in the SVP combine AND again over the composited lens in the main pass, the
	// skip cvars land each exactly once on scope pixels (default 0 keeps the current doubled look)
	const bool svp_pass_now = Device.true_pip_on && Device.m_SecondViewport.m_render_pass_is_svp;
	if (ps_r2_ls_flags.test(R2FLAG_DOF) && !(svp_pass_now && ps_r__svp_skip_dof))
	{
		phase_dof();
	}

	if (!(svp_pass_now && ps_r__svp_skip_lut))
		phase_lut();

	// pip the eye-side overlays (gasmask, nvg, heatvision) apply once on the main pass
	if(ps_r2_mask_control.x > 0 && !svp_pass_now)
	{
		phase_gasmask_dudv();
		if (ps_r2_drops_control.x > 0)
		{
			phase_gasmask_drops();
		}
	}

	if(ps_r2_nightvision > 0 && !svp_pass_now)
	{
		// pip the offset nvg tube mask both draws the tube and clips the composited scope disc, a
		// lens stencil stamp splits the post so the disc greens centered while the tube stays put
		extern Fvector4 ps_dev_param_8;
		const float nv_fr = ps_dev_param_8.x - floorf(ps_dev_param_8.x);
		const bool nv_offset_tube = _abs(nv_fr - 0.11f) < 0.005f || _abs(nv_fr - 0.12f) < 0.005f;
		const bool nv_split = Device.true_pip_on && Device.m_SecondViewport.IsSVPActive()
			&& nv_offset_tube && !RImplementation.o.dx10_msaa
			&& this == RImplementation.TargetMain
			&& !RImplementation.GMBase.RGraph.mapScopeHUDSorted.empty();
		if (!nv_split)
			phase_nightvision();
		else
		{
			PIX_EVENT(svp_nvg_tube_split);
			if (ps_r__svp_stats) ++svp_stats_nvg_split; // overlay proof the tube split fired
			EnsureScopeShaders();
			const u32 nv_c = color_rgba(0, 0, 0, 255);
			const float nv_w = float(Device.dwWidth);
			const float nv_h = float(Device.dwHeight);

			// the stock dest plus the base depth-stencil, one surface carries the stamp and both passes
			u_setrt(rt_Color, 0, 0, HW.pBaseZB);
			RCache.set_CullMode(CULL_NONE);

			auto nv_quad = [&]()
			{
				FVF::TL* pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
				pv->set(0, nv_h, EPS_S, 1.f, nv_c, 0.f, 1.f); pv++;
				pv->set(0, 0, EPS_S, 1.f, nv_c, 0.f, 0.f); pv++;
				pv->set(nv_w, nv_h, EPS_S, 1.f, nv_c, 1.f, 1.f); pv++;
				pv->set(nv_w, 0, EPS_S, 1.f, nv_c, 1.f, 0.f); pv++;
				RCache.Vertex.Unlock(4, g_combine->vb_stride);
				RCache.set_Geometry(g_combine);
				RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);
			};

			// zero bit 0x80 screen wide first, the light maskers replace whole stencil bytes so
			// stale marker garbage can sit in it
			RCache.set_Element(s_nightvision->E[0]);
			RCache.set_ColorWriteEnable(0);
			RCache.set_Stencil(TRUE, D3DCMP_ALWAYS, 0x00, 0xff, 0x80,
				D3DSTENCILOP_KEEP, D3DSTENCILOP_REPLACE, D3DSTENCILOP_KEEP);
			nv_quad();

			// stamp bit 0x80 over the lens footprint, the same draw the distort guard ships
			draw_scope(s_svp_distort_stamp, []()
			{
				RCache.set_c("scope_phase", 0);
				RCache.set_ColorWriteEnable(0);
				RCache.set_Stencil(TRUE, D3DCMP_ALWAYS, 0x80, 0xff, 0x80,
					D3DSTENCILOP_KEEP, D3DSTENCILOP_REPLACE, D3DSTENCILOP_KEEP);
			});
			RCache.set_ColorWriteEnable();

			// disc pass, the flag routes the binder remap so the tube reads centered here only
			Device.m_SecondViewport.svp_nvg_disc_pass = true;
			RCache.set_Constants((R_constant_table*)nullptr); // the ctable cache would skip the loaders
			RCache.set_Element(s_nightvision->E[ps_r2_nightvision]);
			RCache.set_Stencil(TRUE, D3DCMP_EQUAL, 0x80, 0x80, 0x00);
			nv_quad();

			// periphery pass, honest offset value
			Device.m_SecondViewport.svp_nvg_disc_pass = false;
			RCache.set_Constants((R_constant_table*)nullptr);
			RCache.set_Element(s_nightvision->E[ps_r2_nightvision]);
			RCache.set_Stencil(TRUE, D3DCMP_EQUAL, 0x00, 0x80, 0x00);
			nv_quad();

			RCache.set_Stencil(FALSE);
			// the stock terminal copy back into the shared color chain
			HW.pContext->CopyResource(rt_Generic_0->pSurface, rt_Color->pSurface);
		}
	}

	//--DSR-- HeatVision_start
	if (ps_r2_heatvision > 0 && !svp_pass_now)
		phase_heatvision();
	//--DSR-- HeatVision_end

	// pip the physical scope renders the sight when the true-PiP SVP is live, the 2D shader-scope
	// overlay (crookr fakescope) would paint its own floating reticle over the rigid image
	if (scope_fake_enabled && !(Device.true_pip_on && Device.m_SecondViewport.IsSVPActive()))
	{
		phase_fakescope(); //crookr
	}

    //SMAA
	if (ps_smaa_quality && !svp_dlss_skip_aa) // pip skip on the DLSS SVP pass (eval reconstructs)
	{
        //PIX_EVENT(SMAA);
        phase_smaa();
        RCache.set_Stencil(FALSE);
    }
	
	// main + off path TAA at the stock position (the true-PiP SVP ran its TAA early, above)
	if (RImplementation.o.ssfx_taa && ps_ssfx_taa.x > 0 &&
		!(Device.true_pip_on && Device.m_SecondViewport.m_render_pass_is_svp))
	{
		phase_ssfx_taa();
	}

	// pip clear the scope capture maps at the main-pass tail after every consumer (lens composite,
	// nvg split, taa mask stamp), unconditional of the taa path, frame-start clear covers staleness
	if (this == RImplementation.TargetMain)
	{
		RImplementation.GMBase.RGraph.mapScopeHUDSorted.clear();
		RImplementation.GMBase.RGraph.mapScopeHUDObjective.clear();
		RImplementation.GMBase.RGraph.mapReflexHUDSorted.clear();
	}

	if (ssfx_PrevPos_Requiered)
		HW.pContext->CopyResource(rt_ssfx_prevPos->pTexture->surface_get(), rt_Position->pTexture->surface_get());

	// PP enabled ?
	//	Render to RT texture to be able to copy RT even in windowed mode.
	BOOL PP_Complex = u_need_PP() | (BOOL)RImplementation.m_bMakeAsyncSS;
	if (_menu_pp) PP_Complex = FALSE;

	// HOLGER - HACK
	PP_Complex = TRUE;

	// Combine everything + perform AA
	if (RImplementation.o.dx10_msaa)
	{
		if (PP_Complex) u_setrt(rt_Generic, 0, 0, HW.pBaseZB); // LDR RT
		else u_setrt(Device.dwWidth, Device.dwHeight, HW.pBaseRT,NULL,NULL, HW.pBaseZB);
	}
	else
	{
		if (PP_Complex) u_setrt(rt_Color, 0, 0, HW.pBaseZB); // LDR RT
		else u_setrt(Device.dwWidth, Device.dwHeight, HW.pBaseRT,NULL,NULL, HW.pBaseZB);
	}
	//. u_setrt				( Device.dwWidth,Device.dwHeight,HW.pBaseRT,NULL,NULL,HW.pBaseZB);
	RCache.set_CullMode(CULL_NONE);
	RCache.set_Stencil(FALSE);


	if (1)
	{
		PIX_EVENT(combine_2);
		// 
		struct v_aa
		{
			Fvector4 p;
			Fvector2 uv0;
			Fvector2 uv1;
			Fvector2 uv2;
			Fvector2 uv3;
			Fvector2 uv4;
			Fvector4 uv5;
			Fvector4 uv6;
		};

		float _w = float(Device.dwWidth);
		float _h = float(Device.dwHeight);
		float ddw = 1.f / _w;
		float ddh = 1.f / _h;
		p0.set(.5f / _w, .5f / _h);
		p1.set((_w + .5f) / _w, (_h + .5f) / _h);

		// Fill vertex buffer
		v_aa* pv = (v_aa*)RCache.Vertex.Lock(4, g_aa_AA->vb_stride, Offset);
		pv->p.set(EPS, float(_h + EPS), EPS, 1.f);
		pv->uv0.set(p0.x, p1.y);
		pv->uv1.set(p0.x - ddw, p1.y - ddh);
		pv->uv2.set(p0.x + ddw, p1.y + ddh);
		pv->uv3.set(p0.x + ddw, p1.y - ddh);
		pv->uv4.set(p0.x - ddw, p1.y + ddh);
		pv->uv5.set(p0.x - ddw, p1.y, p1.y, p0.x + ddw);
		pv->uv6.set(p0.x, p1.y - ddh, p1.y + ddh, p0.x);
		pv++;
		pv->p.set(EPS, EPS, EPS, 1.f);
		pv->uv0.set(p0.x, p0.y);
		pv->uv1.set(p0.x - ddw, p0.y - ddh);
		pv->uv2.set(p0.x + ddw, p0.y + ddh);
		pv->uv3.set(p0.x + ddw, p0.y - ddh);
		pv->uv4.set(p0.x - ddw, p0.y + ddh);
		pv->uv5.set(p0.x - ddw, p0.y, p0.y, p0.x + ddw);
		pv->uv6.set(p0.x, p0.y - ddh, p0.y + ddh, p0.x);
		pv++;
		pv->p.set(float(_w + EPS), float(_h + EPS), EPS, 1.f);
		pv->uv0.set(p1.x, p1.y);
		pv->uv1.set(p1.x - ddw, p1.y - ddh);
		pv->uv2.set(p1.x + ddw, p1.y + ddh);
		pv->uv3.set(p1.x + ddw, p1.y - ddh);
		pv->uv4.set(p1.x - ddw, p1.y + ddh);
		pv->uv5.set(p1.x - ddw, p1.y, p1.y, p1.x + ddw);
		pv->uv6.set(p1.x, p1.y - ddh, p1.y + ddh, p1.x);
		pv++;
		pv->p.set(float(_w + EPS), EPS, EPS, 1.f);
		pv->uv0.set(p1.x, p0.y);
		pv->uv1.set(p1.x - ddw, p0.y - ddh);
		pv->uv2.set(p1.x + ddw, p0.y + ddh);
		pv->uv3.set(p1.x + ddw, p0.y - ddh);
		pv->uv4.set(p1.x - ddw, p0.y + ddh);
		pv->uv5.set(p1.x - ddw, p0.y, p0.y, p1.x + ddw);
		pv->uv6.set(p1.x, p0.y - ddh, p0.y + ddh, p1.x);
		pv++;
		RCache.Vertex.Unlock(4, g_aa_AA->vb_stride);

		//	Set up variable


		// Draw COLOR
		if (!RImplementation.o.dx10_msaa)
		{
			if (ps_r2_ls_flags.test(R2FLAG_AA)) RCache.set_Element(s_combine->E[bDistort ? 3 : 1]);
				// look at blender_combine.cpp
			else RCache.set_Element(s_combine->E[bDistort ? 4 : 2]); // look at blender_combine.cpp
		}
		else
		{
			if (ps_r2_ls_flags.test(R2FLAG_AA)) RCache.set_Element(s_combine_msaa[0]->E[bDistort ? 3 : 1]);
				// look at blender_combine.cpp
			else RCache.set_Element(s_combine_msaa[0]->E[bDistort ? 4 : 2]); // look at blender_combine.cpp
		}
		RCache.set_c("e_barrier", ps_r2_aa_barier.x, ps_r2_aa_barier.y, ps_r2_aa_barier.z, 0);
		RCache.set_c("e_weights", ps_r2_aa_weight.x, ps_r2_aa_weight.y, ps_r2_aa_weight.z, 0);
		RCache.set_c("e_kernel", ps_r2_aa_kernel, ps_r2_aa_kernel, ps_r2_aa_kernel, 0);
		RCache.set_c("m_current", Matrix_current);
		RCache.set_c("m_previous", Matrix_previous);
		RCache.set_c("m_blur", m_blur_scale.x, m_blur_scale.y, 0, 0);
		/////lvutner		
		RCache.set_c("mask_control", ps_r2_mask_control.x, ps_r2_mask_control.y, ps_r2_mask_control.z, ps_r2_mask_control.w);

		RCache.set_c("tnmp_a", ps_r2_tnmp_a);
		RCache.set_c("tnmp_b", ps_r2_tnmp_b);
		RCache.set_c("tnmp_c", ps_r2_tnmp_c);
		RCache.set_c("tnmp_d", ps_r2_tnmp_d);
		RCache.set_c("tnmp_e", ps_r2_tnmp_e);
		RCache.set_c("tnmp_f", ps_r2_tnmp_f);
		RCache.set_c("tnmp_w", ps_r2_tnmp_w);

		RCache.set_c("tnmp_exposure", ps_r2_tnmp_exposure);
		RCache.set_c("tnmp_gamma", ps_r2_tnmp_gamma);
		RCache.set_c("tnmp_onoff", ps_r2_tnmp_onoff);

		//////////

		RCache.set_Geometry(g_aa_AA);
		RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);
	}
	RCache.set_Stencil(FALSE);

	if (RImplementation.o.dx11_hdr10) {
		// TODO: we should be able to avoid a copy if both are enabled
		if (ps_r4_hdr10_bloom_on) {
			HW.pContext->CopyResource(rt_Generic_0->pTexture->surface_get(), rt_Color->pTexture->surface_get());
			phase_hdr10_bloom(); // samples from rt_Generic_0, writes to rt_Color
		}
		if (ps_r4_hdr10_flare_on) {
			HW.pContext->CopyResource(rt_Generic_0->pTexture->surface_get(), rt_Color->pTexture->surface_get());
			phase_hdr10_lens_flare(); // samples from rt_Generic_0, writes to rt_Color
		}
	}

	//	if FP16-BLEND !not! supported - draw flares here, overwise they are already in the bloom target
	/* if (!RImplementation.o.fp16_blend)*/
	if (ps_r2_anomaly_flags.test(R2_AN_FLAG_FLARES) && ps_r2_heatvision == 0) //--DSR-- HeatVision
		g_pGamePersistent->Environment().RenderFlares(); // lens-flares

	//	PP-if required
	// pip the svp pass skips phase_pp, it writes the real backbuffer
	if (PP_Complex && !(Device.true_pip_on && Device.m_SecondViewport.m_render_pass_is_svp))
	{
		PIX_EVENT(phase_pp);
		phase_pp();
	}

	//	Re-adapt luminance
	RCache.set_Stencil(FALSE);

	//*** exposure-pipeline-clear
	{
		std::swap(rt_LUM_pool[0], rt_LUM_pool[1]);
		t_LUM_src->surface_set(NULL);
		t_LUM_dest->surface_set(NULL);
	}

#ifdef DEBUG
	RCache.set_CullMode	( CULL_CCW );
	static	xr_vector<Fplane>		saved_dbg_planes;
	if (bDebug)		saved_dbg_planes= dbg_planes;
	else			dbg_planes		= saved_dbg_planes;
	if (1) for (u32 it=0; it<dbg_planes.size(); it++)
	{
		Fplane&		P	=	dbg_planes[it];
		Fvector		zero	;
		zero.mul	(P.n,P.d);
		
		Fvector             L_dir,L_up=P.n,L_right;
		L_dir.set           (0,0,1);                if (_abs(L_up.dotproduct(L_dir))>.99f)  L_dir.set(1,0,0);
		L_right.crossproduct(L_up,L_dir);           L_right.normalize       ();
		L_dir.crossproduct  (L_right,L_up);         L_dir.normalize         ();

		Fvector				p0,p1,p2,p3;
		float				sz	= 100.f;
		p0.mad				(zero,L_right,sz).mad	(L_dir,sz);
		p1.mad				(zero,L_right,sz).mad	(L_dir,-sz);
		p2.mad				(zero,L_right,-sz).mad	(L_dir,-sz);
		p3.mad				(zero,L_right,-sz).mad	(L_dir,+sz);
		RCache.dbg_DrawTRI	(Fidentity,p0,p1,p2,0xffffffff);
		RCache.dbg_DrawTRI	(Fidentity,p2,p3,p0,0xffffffff);
	}

	static	xr_vector<dbg_line_t>	saved_dbg_lines;
	if (bDebug)		saved_dbg_lines	= dbg_lines;
	else			dbg_lines		= saved_dbg_lines;
	if (1) for (u32 it=0; it<dbg_lines.size(); it++)
	{
		RCache.dbg_DrawLINE		(Fidentity,dbg_lines[it].P0,dbg_lines[it].P1,dbg_lines[it].color);
	}
#endif

	// ********************* Debug
	/*
	if (0)		{
		u32		C					= color_rgba	(255,255,255,255);
		float	_w					= float(Device.dwWidth)/3;
		float	_h					= float(Device.dwHeight)/3;

		// draw light-spheres
#ifdef DEBUG
		if (0) for (u32 it=0; it<dbg_spheres.size(); it++)
		{
			Fsphere				S	= dbg_spheres[it].first;
			Fmatrix				M;	
			u32				ccc		= dbg_spheres[it].second.get();
			M.scale					(S.R,S.R,S.R);
			M.translate_over		(S.P);
			RCache.dbg_DrawEllipse	(M,ccc);
			RCache.dbg_DrawAABB		(S.P,.05f,.05f,.05f,ccc);
		}
#endif
		// Draw quater-screen quad textured with our direct-shadow-map-image
		if (1) 
		{
			u32							IX=0,IY=1;
			p0.set						(.5f/_w, .5f/_h);
			p1.set						((_w+.5f)/_w, (_h+.5f)/_h );

			// Fill vertex buffer
			FVF::TL* pv					= (FVF::TL*) RCache.Vertex.Lock	(4,g_combine->vb_stride,Offset);
			pv->set						((IX+0)*_w+EPS,	(IY+1)*_h+EPS,	EPS,	1.f, C, p0.x, p1.y);	pv++;
			pv->set						((IX+0)*_w+EPS,	(IY+0)*_h+EPS,	EPS,	1.f, C, p0.x, p0.y);	pv++;
			pv->set						((IX+1)*_w+EPS,	(IY+1)*_h+EPS,	EPS,	1.f, C, p1.x, p1.y);	pv++;
			pv->set						((IX+1)*_w+EPS,	(IY+0)*_h+EPS,	EPS,	1.f, C, p1.x, p0.y);	pv++;
			RCache.Vertex.Unlock		(4,g_combine->vb_stride);

			// Draw COLOR
			RCache.set_Shader			(s_combine_dbg_0);
			RCache.set_Geometry			(g_combine);
			RCache.Render				(D3DPT_TRIANGLELIST,Offset,0,4,0,2);
		}

		// Draw quater-screen quad textured with our accumulator
		if (0)
		{
			u32							IX=1,IY=1;
			p0.set						(.5f/_w, .5f/_h);
			p1.set						((_w+.5f)/_w, (_h+.5f)/_h );

			// Fill vertex buffer
			FVF::TL* pv					= (FVF::TL*) RCache.Vertex.Lock	(4,g_combine->vb_stride,Offset);
			pv->set						((IX+0)*_w+EPS,	(IY+1)*_h+EPS,	EPS,	1.f, C, p0.x, p1.y);	pv++;
			pv->set						((IX+0)*_w+EPS,	(IY+0)*_h+EPS,	EPS,	1.f, C, p0.x, p0.y);	pv++;
			pv->set						((IX+1)*_w+EPS,	(IY+1)*_h+EPS,	EPS,	1.f, C, p1.x, p1.y);	pv++;
			pv->set						((IX+1)*_w+EPS,	(IY+0)*_h+EPS,	EPS,	1.f, C, p1.x, p0.y);	pv++;
			RCache.Vertex.Unlock		(4,g_combine->vb_stride);

			// Draw COLOR
			RCache.set_Shader			(s_combine_dbg_1);
			RCache.set_Geometry			(g_combine);
			RCache.Render				(D3DPT_TRIANGLELIST,Offset,0,4,0,2);
		}
	}
	*/
#ifdef DEBUG
	dbg_spheres.clear	();
	dbg_lines.clear		();
	dbg_planes.clear	();
#endif
}

void CRenderTarget::phase_wallmarks()
{
	// Targets
	RCache.set_RT(NULL, 2);
	RCache.set_RT(NULL, 1);
	if (!RImplementation.o.dx10_msaa)
		u_setrt(rt_Color,NULL,NULL, HW.pBaseZB);
	else
		u_setrt(rt_Color,NULL,NULL, rt_MSAADepth->pZRT);
	// Stencil	- draw only where stencil >= 0x1
	RCache.set_Stencil(TRUE, D3DCMP_LESSEQUAL, 0x01, 0xff, 0x00);
	RCache.set_CullMode(CULL_CCW);
	RCache.set_ColorWriteEnable(D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_BLUE);
}

void CRenderTarget::phase_combine_volumetric()
{
	PIX_EVENT(phase_combine_volumetric);
	u32 Offset = 0;
	//Fvector2	p0,p1;

	//	TODO: DX10: Remove half pixel offset here

	//u_setrt(rt_Generic_0,0,0,HW.pBaseZB );			// LDR RT
	if (!RImplementation.o.dx10_msaa)
		u_setrt(rt_Generic_0, rt_Generic_1, 0, HW.pBaseZB);
	else
		u_setrt(rt_Generic_0_r, rt_Generic_1_r, 0, RImplementation.Target->rt_MSAADepth->pZRT);
	//	Sets limits to both render targets
	RCache.set_ColorWriteEnable(D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_BLUE);
	{
		// Fill VB
		//float scale_X = float(Device.dwWidth) / float(TEX_jitter);
		//float scale_Y = float(Device.dwHeight) / float(TEX_jitter);

		// Fill vertex buffer
		FVF::TL* pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
		pv->set(-1, 1, 0, 1, 0, 0, 1); pv++;
		pv->set(-1, -1, 0, 0, 0, 0, 0); pv++;
		pv->set(1, 1, 1, 1, 0, 1, 1); pv++;
		pv->set(1, -1, 1, 0, 0, 1, 0); pv++;
		RCache.Vertex.Unlock(4, g_combine->vb_stride);

		// Draw
		RCache.set_Element(s_combine_volumetric->E[0]);
		//RCache.set_Geometry			(g_combine_VP		);
		RCache.set_Geometry(g_combine);
		RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);
	}
	RCache.set_ColorWriteEnable();
}
