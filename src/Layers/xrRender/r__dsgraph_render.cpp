#include "stdafx.h"

#include "../../xrEngine/render.h"
#include "../../xrEngine/irenderable.h"
#include "../../xrEngine/igame_persistent.h"
#include "../../xrEngine/environment.h"
#include "../../xrEngine/CustomHUD.h"

#include "FBasicVisual.h"
#include "CHudInitializer.h"

#include "fhierrarhyvisual.h"
#include "SkeletonCustom.h"
#include "../../xrEngine/fmesh.h"
#include "flod.h"

#include "../../xrEngine/xr_object.h"

using namespace R_dsgraph;

extern float r_ssaDISCARD;
extern float r_ssaDONTSORT;
extern float r_ssaHZBvsTEX;
extern float r_ssaGLOD_start, r_ssaGLOD_end;

ICF float calcLOD(float ssa/*fDistSq*/, float R)
{
	return _sqrt(clampr((ssa - r_ssaGLOD_end) / (r_ssaGLOD_start - r_ssaGLOD_end), 0.f, 1.f));
}

template<typename T>
void CDSGraphManager::r_dsgraph_render_graph_sorted(R_dsgraph::mapDSGraphItems<T>& graph, bool _clear, bool reverse)
{
	static auto sortFunc = [](const R_dsgraph::DSGraphItem<T>& a, const R_dsgraph::DSGraphItem<T>& b) { return a.sortKey < b.sortKey; };
	static auto sortFuncReverse = [](const R_dsgraph::DSGraphItem<T>& a, const R_dsgraph::DSGraphItem<T>& b) { return a.sortKey > b.sortKey; };
	if (reverse)
	{
		if (graph.size() >= 4096)
			xr_parallel_sort(graph, sortFuncReverse);
		else
			xr_sort(graph, sortFuncReverse);
	}
	else
	{
		if (graph.size() >= 4096)
			xr_parallel_sort(graph, sortFunc);
		else
			xr_sort(graph, sortFunc);
	}

	for (R_dsgraph::DSGraphItem<T>& item : graph)
	{
		dxRender_Visual* V = item.pVisual;
		VERIFY(V && V->shader._get());
		RCache.set_Element(item.pSE);
		RCache.set_xform_world(*item.pMatrix);
		RImplementation.apply_object(item.pObject);
		RImplementation.apply_lmaterial();
		//if (item.b_hud_mode)
		//{
		//	//new feature
		//}
		V->Render(calcLOD(item.ssa, V->vis.sphere.R));
	}

	if (_clear)
		graph.clear();

	RCache.set_xform_world(Fidentity);
}

void CDSGraphManager::r_dsgraph_render_graph(RenderQueueArray& queues, u32 _priority, bool _clear, bool static_geometry)
{
	RCache.set_xform_world(Fidentity);

	for (u32 iPass = 0; iPass < SHADER_PASSES_MAX; ++iPass)
	{
		auto& queue = queues[_priority][iPass];
		if (queue.empty())
			continue;

		// 1. Sort by generated sort key to replicate previous fixed map behaviour
		static auto sortFunc = [](const RenderPacket& a, const RenderPacket& b)
		{
			return a.sortKey < b.sortKey;
		};
		if (queue.size() >= 4096)
			xr_parallel_sort(queue, sortFunc);
		else
			xr_sort(queue, sortFunc);

		// 2. Render
		vs_type pVS = nullptr;

#if defined(USE_DX10) || defined(USE_DX11)
		gs_type pGS = nullptr;
#endif

		ps_type pPS = nullptr;

#ifdef USE_DX11
		hs_type pHS = nullptr;
		ds_type pDS = nullptr;
#endif
		
		R_constant_table* pCS = nullptr;
		ID3DState* pState = nullptr;
		STextureList* pTextures = nullptr;

		RenderPacketSortKey key = { 0, 0 };

		for (auto& packet : queue)
		{
			auto& currentKey = packet.sortKey;
			if (currentKey.high != key.high)
			{
				key.high = currentKey.high;

				if (packet.pVS != pVS)
				{
					pVS = packet.pVS;
					RCache.set_VS(pVS);
				}

#if defined(USE_DX10) || defined(USE_DX11)
				if (packet.pGS != pGS)
				{
					pGS = packet.pGS;
					RCache.set_GS(pGS);
				}
#endif

				if (packet.pPS != pPS)
				{
					pPS = packet.pPS;
					RCache.set_PS(pPS);
				}

#ifdef USE_DX11
				if (packet.pHS != pHS)
				{
					pHS = packet.pHS;
					RCache.set_HS(pHS);
				}
#endif
			}

			if (currentKey.low != key.low)
			{
				key.low = currentKey.low;
#ifdef USE_DX11
				if (packet.pDS != pDS)
				{
					pDS = packet.pDS;
					RCache.set_DS(pDS);
				}
#endif

				if (packet.pCS != pCS)
				{
					pCS = packet.pCS;
					RCache.set_Constants(pCS);
				}

				if (packet.pState != pState)
				{
					pState = packet.pState;
					RCache.set_States(pState);
				}

				if (packet.pTextures != pTextures)
				{
					pTextures = packet.pTextures;
					RCache.set_Textures(pTextures);
					RImplementation.apply_lmaterial();
				}
			}

			auto& item = packet.item;
			if (!static_geometry)
			{
				RCache.set_xform_world(*item.pMatrix);
				RImplementation.apply_object(item.pObject);
				RImplementation.apply_lmaterial();
			}

			float LOD = calcLOD(item.ssa, item.pVisual->vis.sphere.R);
#ifdef USE_DX11
			RCache.LOD.set_LOD(LOD);
#endif
			item.pVisual->Render(LOD);
		}

		if (_clear)
			queue.clear();
	}
}

//////////////////////////////////////////////////////////////////////////
// HUD render
void CDSGraphManager::r_dsgraph_render_hud()
{
	PROF_EVENT("r_dsgraph_render_hud");
	CHudInitializer initializer(true);

	// Rendering
	RImplementation.rmNear();
	r_dsgraph_render_graph_sorted(RGraph.mapHUD);

	RImplementation.rmNormal();

#if defined(USE_DX11) //  Redotix99: for 3D Shader Based Scopes 		
	if (scope_3D_fake_enabled)
	{
		RCache.set_RT(RImplementation.Target->rt_ssfx_temp->pRT, 3); // Render scope_3D to any buffer

		r_dsgraph_render_graph_sorted(RGraph.mapScopeHUD);

		if (!RImplementation.o.ssfx_motionvectors)
			RCache.set_RT(NULL, 3);
		else
			RCache.set_RT(RImplementation.Target->rt_ssfx_motion_vectors->pRT, 3);
	}
#endif

	if (RGraph.mapCamAttached.size())
	{
		RImplementation.rmNear();

		// Change projection
		initializer.SetCamMode();

		// Rendering
		r_dsgraph_render_graph_sorted(RGraph.mapCamAttached);

		RImplementation.rmNormal();
	}
}

void CDSGraphManager::r_dsgraph_render_hud_ui()
{
	PROF_EVENT("r_dsgraph_render_hud_ui");
	CHudInitializer initializer(true);

	RImplementation.rmNear();
	g_hud->RenderActiveItemUI();
	RImplementation.rmNormal();
}

void CDSGraphManager::r_dsgraph_render_cam_ui()
{
	PROF_EVENT("r_dsgraph_render_cam_ui");

	// Change projection
	CHudInitializer initializer(2);
	
	// Rendering
	RImplementation.rmNear();
	g_hud->RenderCamAttachedUI();
	RImplementation.rmNormal();
}

//////////////////////////////////////////////////////////////////////////
// strict-sorted render
void CDSGraphManager::r_dsgraph_render_sorted(bool render_hud)
{
	{
		PROF_EVENT("r_dsgraph_render_sorted");
		// Rendering
		r_dsgraph_render_graph_sorted(RGraph.mapStaticSorted.Sorted, true, true);
		r_dsgraph_render_graph_sorted(RGraph.mapDynamicSorted.Sorted, true, true);
	}

	if (render_hud)
		r_dsgraph_render_sorted_hud();

	// Camera Script Attachments
	if (RGraph.mapCamAttachedSorted.Sorted.size())
	{
		RImplementation.rmNear();
		// Change projection
		CHudInitializer initializer(2);

		// Rendering
		r_dsgraph_render_graph_sorted(RGraph.mapCamAttachedSorted.Sorted, true, true);
		RImplementation.rmNormal();
	}
}

void CDSGraphManager::r_dsgraph_capture_hud()
{
	if (g_hud)
	{
		g_hud->Render_Last(dcast_IPortalTraverser());
		set_Object();
	}
}

#if defined(USE_DX11)
//////////////////////////////////////////////////////////////////////////
// strict-sorted render
void CDSGraphManager::r_dsgraph_render_ScopeSorted()  //  Redotix99: for 3D Shader Based Scopes 	
{
	// Change projection
	CHudInitializer initializer(true);

	// Rendering
	RImplementation.rmNear();
	r_dsgraph_render_graph_sorted(RGraph.mapScopeHUDSorted, true, true);
	RImplementation.rmNormal();
}
#endif

void CDSGraphManager::r_dsgraph_render_sorted_hud()
{
	PROF_EVENT("r_dsgraph_render_sorted_hud");
#if	RENDER==R_R4
	HW.pContext->CopyResource(RImplementation.Target->rt_Accumulator->pSurface, RImplementation.Target->rt_Generic_0->pSurface);
#endif
	CHudInitializer initializer(true);

	RImplementation.rmNear();
	r_dsgraph_render_graph_sorted(RGraph.mapHUDSorted.Sorted, true, true);
	RImplementation.rmNormal();
}

//////////////////////////////////////////////////////////////////////////
// strict-sorted render
void CDSGraphManager::r_dsgraph_render_emissive(bool clear, bool renderHUD)
{
	PROF_EVENT("r_dsgraph_render_emissive");
#if	RENDER!=R_R1
	r_dsgraph_render_graph_sorted(RGraph.mapStaticSorted.Emissive, clear);
	r_dsgraph_render_graph_sorted(RGraph.mapDynamicSorted.Emissive, clear);
	
	//	HACK: Calculate this only once
	CHudInitializer initializer(true);

	RImplementation.rmNear();
	r_dsgraph_render_graph_sorted(RGraph.mapHUDSorted.Emissive, clear);
	
	if (renderHUD)
		r_dsgraph_render_graph_sorted(RGraph.mapHUDSorted.Sorted, false, true);

	RImplementation.rmNormal();
#endif
}

void CDSGraphManager::r_dsgraph_render_water_ssr()
{
#ifdef USE_DX11
	PROF_EVENT("r_dsgraph_render_water_ssr");
	static auto sortFunc = [](const R_dsgraph::DSGraphItem<float>& a, const R_dsgraph::DSGraphItem<float>& b) { return a.sortKey < b.sortKey; };
	std::sort(RGraph.mapWater.begin(), RGraph.mapWater.end(), sortFunc);
	for (R_dsgraph::DSGraphItem<float>& N : RGraph.mapWater)
	{
		dxRender_Visual* V = N.pVisual;
		VERIFY(V);

		RCache.set_Shader(RImplementation.Target->s_ssfx_water_ssr);

		RCache.set_xform_world(*N.pMatrix);
		RImplementation.apply_object(N.pObject);
		RImplementation.apply_lmaterial();

		RCache.set_c("cam_pos", RImplementation.Target->Position_previous.x, RImplementation.Target->Position_previous.y, RImplementation.Target->Position_previous.z, 0.0f);

		// Previous matrix data
		RCache.set_c("m_current", RImplementation.Target->Matrix_current);
		RCache.set_c("m_previous", RImplementation.Target->Matrix_previous);

		V->Render(calcLOD(N.ssa, V->vis.sphere.R));
	}
#endif
}

void CDSGraphManager::r_dsgraph_render_water()
{
	PROF_EVENT("r_dsgraph_render_water_ssr");
	static auto sortFunc = [](const R_dsgraph::DSGraphItem<float>& a, const R_dsgraph::DSGraphItem<float>& b) { return a.sortKey < b.sortKey; };
	std::sort(RGraph.mapWater.begin(), RGraph.mapWater.end(), sortFunc);
	for (R_dsgraph::DSGraphItem<float>& N : RGraph.mapWater)
	{
		dxRender_Visual* V = N.pVisual;
		VERIFY(V);

#ifdef USE_DX11
		if (RImplementation.o.ssfx_water)
		{
			RCache.set_Shader(RImplementation.Target->s_ssfx_water);
		}
#endif

		RCache.set_xform_world(*N.pMatrix);
		RImplementation.apply_object(N.pObject);
		RImplementation.apply_lmaterial();

		// Wind settings
		float WindDir = g_pGamePersistent->Environment().CurrentEnv->wind_direction;
		float WindVel = g_pGamePersistent->Environment().CurrentEnv->wind_velocity;
		RCache.set_c("wind_setup", WindDir, WindVel, 0, 0);

		V->Render(calcLOD(N.ssa, V->vis.sphere.R));
	}
	RGraph.mapWater.clear();
}

//////////////////////////////////////////////////////////////////////////
// strict-sorted render
void CDSGraphManager::r_dsgraph_render_wmarks()
{
	PROF_EVENT("r_dsgraph_render_wmarks");
#if	RENDER!=R_R1
	// Rendering
	r_dsgraph_render_graph_sorted(RGraph.mapStaticSorted.Wmark);
	r_dsgraph_render_graph_sorted(RGraph.mapDynamicSorted.Wmark);
	//	HACK: Calculate this only once
	CHudInitializer initalizer(true);

	RImplementation.rmNear();
	r_dsgraph_render_graph_sorted(RGraph.mapHUDSorted.Wmark);
	RImplementation.rmNormal();
#endif
}

//////////////////////////////////////////////////////////////////////////
// strict-sorted render
void CDSGraphManager::r_dsgraph_render_distort()
{
	PROF_EVENT("r_dsgraph_render_distort");
	// Rendering
	r_dsgraph_render_graph_sorted(RGraph.mapStaticSorted.Distort, true, true);
	r_dsgraph_render_graph_sorted(RGraph.mapDynamicSorted.Distort, true, true);
	//	HACK: Calculate this only once
	CHudInitializer initalizer(true);

	RImplementation.rmNear();
	r_dsgraph_render_graph_sorted(RGraph.mapHUDSorted.Distort);
	RImplementation.rmNormal();
}

#include"LightTrack.h"
void CDSGraphManager::r_dsgraph_capture_static()
{
	PROF_EVENT("r_dsgraph_capture_static")
	if (i_start)
	{
		// Traverse sector/portal structure
		if (psDeviceFlags.test(rsDrawStatic))
		{
			// Determine visibility for static geometry hierrarhy
			for (auto& pair : m_sector_frustums)
			{
				for (auto& frustum_node : pair.val.frustums)
					add_Static((IRenderVisual*)pair.key->root(), frustum_node, frustum_node.getMask());
			}
		}
	}
}

void CDSGraphManager::r_dsgraph_capture_lights()
{
	PROF_EVENT("r_dsgraph_capture_lights")
	g_SpatialSpaceLights->q_frustum
	(
		lstLights,
		ISpatial_DB::O_ORDERED,
		STYPE_LIGHTSOURCE,
		i_frustum
	);

#if	RENDER==R_R1
	std::sort(lstLights.begin(), lstLights.end(), [](ISpatialShared& _1, ISpatialShared& _2)
	{
		if (!_1.get() || !_2.get()) return false;

		return	_1->spatial.sphere.P.distance_to_sqr(Device.vCameraPosition_saved) < _2->spatial.sphere.P.distance_to_sqr(Device.vCameraPosition_saved);
	});
#endif

	for (ISpatialShared& spatial : lstLights)
	{
		if (0 == spatial) continue; spatial->spatial_updatesector();
		CSector* sector = (CSector*)spatial->spatial.sector;
		if (0 == sector) continue;

		if (!RImplementation.HOM.visible(spatial->spatial.sphere)) continue;

		if ((spatial->spatial.type & STYPE_LIGHTSOURCE))
		{
			// lightsource
			if (light* L = (light*)(spatial->dcast_Light()))
			{
#if	RENDER==R_R1
				RImplementation.L_DB->add_light(L);
#else
				if (L->get_LOD() > EPS_L && L->has_light_visible_from_sectors(*this))
				{
					RImplementation.Lights.add_light(L);
				}
#endif
			}
		}
	}
}

void CDSGraphManager::r_dsgraph_capture_dynamic(CObject* O)
{
	PROF_EVENT("r_dsgraph_capture_dynamic")
	if (i_start)
	{
		if (psDeviceFlags.test(rsDrawDynamic))
		{
			// Traverse object database
			g_SpatialSpace->q_frustum
			(
				lstRenderables,
				ISpatial_DB::O_ORDERED,
				i_doptions,
				i_frustum
			);
			set_Object();
#if	RENDER==R_R1
			if (i_mask[CDSGraphManager::fl_normal])//normal phase
			{
				std::sort(lstRenderables.begin(), lstRenderables.end(), [](ISpatialShared& _1, ISpatialShared& _2)
				{
					if (!_1.get() || !_2.get()) return false;

					return	_1->spatial.sphere.P.distance_to_sqr(Device.vCameraPosition_saved) < _2->spatial.sphere.P.distance_to_sqr(Device.vCameraPosition_saved);
				});

				if (ps_actor_shadow_flags.test(1))
					g_hud->Render_First(dcast_IPortalTraverser());

				r_dsgraph_capture_hud();
			}
#endif
			u32 uID_LTRACK = u32(-1);
			if (i_mask[CDSGraphManager::fl_normal])//normal phase
			{
				// update light-vis for current entity / actor
				if (CObject* O = g_pGameLevel->CurrentViewEntity())
				{
					if (!O->getDestroy())
					{
						if (CROS_impl* R = (CROS_impl*)O->ROS())
							R->update(O);
					}
				}

				RImplementation.uLastLTRACK++;
				if (!lstRenderables.empty())
				{
					uID_LTRACK = RImplementation.uLastLTRACK % lstRenderables.size();
#if	RENDER!=R_R1
					// update light-vis for selected entity
					// track lighting environment
					if (IRenderable* renderable = (IRenderable*)lstRenderables[uID_LTRACK]->dcast_Renderable())
					{
						if (CROS_impl* T = (CROS_impl*)renderable->renderable_ROS())
							T->update(renderable);
					}
#endif
				}

			}

			// Determine visibility for dynamic part of scene
			for (u32 o_it = 0; o_it < lstRenderables.size(); o_it++)
			{
				ISpatialShared& spatial = lstRenderables[o_it];
				if (0 == spatial) continue;
				CSector* sector = (CSector*)spatial->spatial.sector;
				if (0 == sector) continue;

				if (i_mask[CDSGraphManager::fl_normal] && !RImplementation.HOM.visible(spatial->spatial.sphere))
					continue;

#if	RENDER==R_R1
				if ((spatial->spatial.type & STYPE_GLOW))
				{
					if (CGlow* glow = spatial->dcast_CGlow())
					{
						// It may be an glow
						RImplementation.L_Glows->add(glow);
					}
					continue;
				}
#endif

//				if ((spatial->spatial.type & STYPE_LIGHTSOURCE))
//				{
//					// lightsource
//					if (light* L = (light*)(spatial->dcast_Light()))
//					{
//#if	RENDER==R_R1
//						RImplementation.L_DB->add_light(L);
//#else
//						if (L->get_LOD() > EPS_L && L->has_light_visible_from_sectors(PT))
//						{
//							RImplementation.Lights.add_light(L);
//						}
//#endif
//					}
//					continue;
//				}

				if(!(spatial->spatial.type & STYPE_RENDERABLE) && !(spatial->spatial.type & STYPE_PARTICLE) && !(spatial->spatial.type & STYPE_RENDERABLESHADOW))
					continue;
				if (!is_sector_visible(sector))
					continue;

				for (CFrustum& frustum : m_sector_frustums.find(sector)->val.frustums)
				{
					if (frustum.testSphere_dirty(spatial->spatial.sphere.P, spatial->spatial.sphere.R))
					{
						// renderable
						IRenderable* renderable = spatial->dcast_Renderable();
						if (0 == renderable) break;

						if (O && O->dcast_Renderable() == renderable) break;

						// Rendering
#if	RENDER==R_R1
						if (i_mask[CDSGraphManager::fl_normal] && o_it == uID_LTRACK && renderable->renderable_ROS())
						{
							// track lighting environment
							if(CROS_impl* T = (CROS_impl*)renderable->renderable_ROS())
								T->update(renderable);
						}
#endif
						if (i_mask[CDSGraphManager::fl_normal] && !(spatial->spatial.type & STYPE_PARTICLE))
							set_Object(renderable);

						renderable->renderable_Render(dcast_IPortalTraverser());

						if (i_mask[CDSGraphManager::fl_normal] && !(spatial->spatial.type & STYPE_PARTICLE))
							set_Object();
						break;
					}
				}
			}
		}
	}
}

void CDSGraphManager::r_dsgraph_capture(bool lights, bool dynamic, CObject* O)
{
	PROF_EVENT("r_dsgraph_capture")
	r_dsgraph_capture_static();

	if(lights)
		r_dsgraph_capture_lights();

	if (dynamic)
		r_dsgraph_capture_dynamic(O);
}
