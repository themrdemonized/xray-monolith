#include "stdafx.h"
#include "r__dsgraph_bus_mask.h"

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
#include "particlegroup.h"

#include "../../xrEngine/xr_object.h"

using namespace R_dsgraph;

extern float r_ssaDISCARD;
extern float r_ssaDONTSORT;
extern float r_ssaHZBvsTEX;
extern float r_ssaGLOD_start, r_ssaGLOD_end;

ICF bool R4FullDetailRejectSpatial(ISpatial* spatial, bool normalPhase)
{
#if RENDER == R_R4
	if (ps_r4_full_detail_distance_scale >= 1.f || !normalPhase || !spatial || !g_pGamePersistent ||
		!g_pGamePersistent->Environment().CurrentEnv)
		return false;

	const float farPlane = g_pGamePersistent->Environment().CurrentEnv->far_plane;
	const float fullDetailDistance = std::max(100.f, farPlane * ps_r4_full_detail_distance_scale);
	if (Device.vCameraPosition.distance_to_sqr(spatial->spatial.sphere.P) <=
		_sqr(fullDetailDistance + spatial->spatial.sphere.R))
		return false;

	if (spatial->spatial.type & STYPE_LIGHTSOURCE)
	{
		light* L = (light*)spatial->dcast_Light();
		if (L && L->flags.bHudMode)
			return false;
	}

	return true;
#else
	return false;
#endif
}

ICF float calcLOD(float ssa/*fDistSq*/, float R)
{
	return _sqrt(clampr((ssa - r_ssaGLOD_end) / (r_ssaGLOD_start - r_ssaGLOD_end), 0.f, 1.f));
}

template<typename T, bool Reverse, bool BusMask>
void CDSGraphManager::r_dsgraph_render_graph_sorted(R_dsgraph::mapDSGraphItems<T, Reverse>& graph, bool _clear)
{
    if (graph.empty())
        return;

    if constexpr (!BusMask)
        std::sort(graph.begin(), graph.end());

	for (auto& item : graph)
	{
		dxRender_Visual* V = item.pVisual;
		VERIFY(V && V->shader._get());
		RCache.set_Element(item.pSE);
#if defined(USE_DX10) || defined(USE_DX11)
		if constexpr (BusMask)
			bus_mask_apply(item.bus_tag);
#endif
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

template<bool BusMask>
void CDSGraphManager::r_dsgraph_render_graph(RenderQueueArray& queues, u32 _priority, bool _clear, bool static_geometry)
{
	RCache.set_xform_world(Fidentity);

	for (u32 iPass = 0; iPass < SHADER_PASSES_MAX; ++iPass)
	{
		auto& queue = queues[_priority][iPass];
		if (queue.empty())
			continue;

		// 1. Sort by generated sort key to replicate previous fixed map behaviour
		if constexpr (!BusMask)
		{
			if (queue.size() < 4096)
				std::sort(queue.begin(), queue.end());
			else
				xr_parallel_sort(queue.begin(), queue.end());
		}

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

        u64 high = 0;

        for (auto& packet : queue)
        {
#if defined(USE_DX10) || defined(USE_DX11)
            u32 value = 0;
            if constexpr (BusMask)
            {
                value = bus_mask_value(packet.item.pObject, packet.item.bus_tag);
                if (!value)
                    continue;
            }
#endif

            auto& currentKey = packet.sortKey;
            if (currentKey.high != high)
            {
                high = currentKey.high;

                if (packet.pState != pState)
                {
                    pState = packet.pState;
                    RCache.set_States(pState);
                }

#if defined(USE_DX10) || defined(USE_DX11)
                if (packet.pGS != pGS)
                {
                    pGS = packet.pGS;
                    RCache.set_GS(pGS);
                }
#endif

#ifdef USE_DX11
                if (packet.pHS != pHS)
                {
                    pHS = packet.pHS;
                    RCache.set_HS(pHS);
                }
                if (packet.pDS != pDS)
                {
                    pDS = packet.pDS;
                    RCache.set_DS(pDS);
                }
#endif
            }

            // Compare low key stuff regardless, too high collision probability
            if (packet.pVS != pVS)
            {
                pVS = packet.pVS;
                RCache.set_VS(pVS);
            }

            if constexpr (!BusMask)
            {
                if (packet.pPS != pPS)
                {
                    pPS = packet.pPS;
                    RCache.set_PS(pPS);
                }
            }

            if (packet.pCS != pCS)
            {
                pCS = packet.pCS;
                RCache.set_Constants(pCS);
            }

            if (packet.pTextures != pTextures)
            {
                pTextures = packet.pTextures;
                RCache.set_Textures(pTextures);
                RImplementation.apply_lmaterial();
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
#if defined(USE_DX10) || defined(USE_DX11)
			if constexpr (BusMask)
				bus_mask_apply(value);
#endif
			item.pVisual->Render(LOD);
		}

		if (_clear)
			queue.clear();
	}
}

#if defined(USE_DX10) || defined(USE_DX11)
// tagged world draws redraw with the matrix values part 0 used
static xr_vector<Fmatrix> bus_world_xforms;
#endif

void CDSGraphManager::r_dsgraph_bus_keep_xforms()
{
#if defined(USE_DX10) || defined(USE_DX11)
	bus_world_xforms.clear();
	u32 count = 0;
	for (u32 iPass = 0; iPass < SHADER_PASSES_MAX; ++iPass)
		for (auto& packet : RGraph.mapDynamicPasses[0][iPass])
			if (packet.item.bus_tag)
				++count;
	bus_world_xforms.reserve(count);
	for (u32 iPass = 0; iPass < SHADER_PASSES_MAX; ++iPass)
		for (auto& packet : RGraph.mapDynamicPasses[0][iPass])
			if (packet.item.bus_tag)
			{
				bus_world_xforms.push_back(*packet.item.pMatrix);
				packet.item.pMatrix = &bus_world_xforms.back();
			}
#endif
}

#if defined(USE_DX10) || defined(USE_DX11)
void CDSGraphManager::r_dsgraph_render_bus_mask()
{
	PROF_EVENT("r_dsgraph_render_bus_mask");
	ID3DRenderTargetView* saved[4];
	const bool ids = RImplementation.Target->rt_bus_object_id;
	const u32 layer = bus_layer_bits;
	bus_mask_bind(saved);
	r_dsgraph_render_graph<true>(RGraph.mapDynamicPasses, 0, !ids && !layer, false);
	// the object ids draw in a second walk of the same items with the same depth test
	if (ids)
	{
		bus_mask_pass_begin(bus_pass_id, RImplementation.Target->rt_bus_object_id->pRT, 0);
		r_dsgraph_render_graph<true>(RGraph.mapDynamicPasses, 0, !layer, false);
	}
	// the layer bits draw again with no depth buffer, so walls hide nothing
	if (layer)
	{
		ID3DDepthStencilView* zb = RCache.get_ZB();
		bus_mask_pass_begin(bus_pass_layer, RImplementation.Target->rt_bus_mask_layer->pRT, layer);
		r_dsgraph_render_graph<true>(RGraph.mapDynamicPasses, 0, true, false);
		RCache.set_ZB(zb);
	}
	bus_mask_unbind(saved);
}

// a leaf with its per-object constants over the states the caller set
template <typename Item>
static void bus_leaf_render(const Item& item)
{
	RCache.set_xform_world(*item.pMatrix);
	RImplementation.apply_object(item.pObject);
	RImplementation.apply_lmaterial();

	float LOD = calcLOD(item.ssa, item.pVisual->vis.sphere.R);
#ifdef USE_DX11
	RCache.LOD.set_LOD(LOD);
#endif
	item.pVisual->Render(LOD);
}

// one tagged forward draw into the bound mask target over states already set, a particle keeps its alpha clip and a mesh copies its pass coverage
template <typename Item>
static void bus_forward_draw(const Item& item, u32 value)
{
	bool moved = false;
	if (item.pVisual->dcast_ParticleCustom())
		bus_mask_apply_particle(value);
	else
		moved = bus_mask_apply_mesh(value, item.pSE->passes[0]._get());
	bus_leaf_render(item);
	if (moved)
		bus_mask_slot0_restore();
}

// what a tagged item writes in the walk that runs, a particle its tag in the particle pass alone, a HUD particle or an additive mesh nothing
// a mesh keeps the bits of mesh_bits, the forward opt-in for kept items and every bit for hidden ones
template <typename Item>
static u32 bus_forward_value(const Item& item, u32 mesh_bits)
{
	if (!item.bus_tag)
		return 0;
	if (IParticleCustom* p = item.pVisual->dcast_ParticleCustom())
		return bus_mask_pass == bus_pass_particle && !p->GetHudMode() ? item.bus_tag : 0;
	const u32 tag = item.bus_tag & mesh_bits;
	if (!tag || bus_mask_pass == bus_pass_particle || !bus_mask_mesh_draws(item.pSE->passes[0]._get()))
		return 0;
	return bus_mask_value(item.pObject, tag);
}

// notes whether a tagged item is a mesh of the given bits or a world particle, HUD particles count as neither
template <typename Item>
static void bus_forward_kind(const Item& item, u32 mesh_bits, bool& meshes, bool& particles)
{
	if (!item.bus_tag)
		return;
	if (IParticleCustom* p = item.pVisual->dcast_ParticleCustom())
		particles |= !p->GetHudMode();
	else
		meshes |= !!(item.bus_tag & mesh_bits);
}

// the kept queue 1 packets, the kept sorted items and the hidden list into the target the walk points at
template <typename Packets, typename Sorted, typename Hidden>
static void bus_forward_walk(Packets& packets, Sorted& sorted, Hidden& hidden)
{
	for (auto& packet : packets)
	{
		auto& item = packet.item;
		const u32 value = bus_forward_value(item, CDSGraphManager::bus_forward_bits);
		if (!value)
			continue;
		RCache.set_States(packet.pState);
		RCache.set_GS(packet.pGS);
#ifdef USE_DX11
		RCache.set_HS(packet.pHS);
		RCache.set_DS(packet.pDS);
#endif
		RCache.set_VS(packet.pVS);
		RCache.set_Constants(packet.pCS);
		RCache.set_Textures(packet.pTextures);
		bus_forward_draw(item, value);
	}
	auto elements = [](auto& list, u32 mesh_bits)
	{
		for (auto& item : list)
		{
			const u32 value = bus_forward_value(item, mesh_bits);
			if (!value)
				continue;
			RCache.set_Element(item.pSE);
			bus_forward_draw(item, value);
		}
	};
	elements(sorted, CDSGraphManager::bus_forward_bits);
	elements(hidden, 0u);
}

// the hidden leaves of this walk that write depth, with their own elements into the hidden depth and no colour target
template <typename Hidden>
static void bus_hidden_depth_pass(Hidden& list, ID3DRenderTargetView* rt)
{
	RCache.set_RT(nullptr, 0);
	RCache.set_ZB(RImplementation.Target->rt_bus_hidden_depth->pZRT);
	for (auto& item : list)
	{
		if (item.pVisual->dcast_ParticleCustom())
			continue;
		const SPass* pass = item.pSE->passes[0]._get();
		if (!bus_hidden_writes_depth(pass) || !bus_mask_value(item.pObject, item.bus_tag))
			continue;
		RCache.set_Element(item.pSE);
		bus_hidden_depth_apply();
		bus_leaf_render(item);
	}
	RCache.set_RT(rt, 0);
}

// the hidden leaves into the target the walk points at, over the bound hidden depth
// a leaf that writes depth draws where it is nearest, one that does not keeps its own depth test
template <typename Hidden>
static void bus_hidden_redraw(Hidden& list)
{
	for (auto& item : list)
	{
		if (item.pVisual->dcast_ParticleCustom())
			continue;
		const SPass* pass = item.pSE->passes[0]._get();
		const u32 value = bus_mask_mesh_draws(pass) ? bus_mask_value(item.pObject, item.bus_tag) : 0;
		if (!value)
			continue;
		RCache.set_Element(item.pSE);
		bool moved = false;
		if (bus_hidden_writes_depth(pass))
			bus_mask_apply(value);
		else
			moved = bus_mask_apply_mesh(value, pass);
		bus_leaf_render(item);
		if (moved)
			bus_mask_slot0_restore();
	}
}

// tagged queue 1 and sorted items keep their packets and a copy of their matrix, which their own draw then reads too
void CDSGraphManager::r_dsgraph_bus_keep_forward()
{
	bus_forward_packets.clear();
	bus_forward_sorted.clear();
	bus_forward_xforms.clear();
	u32 count = 0;
	for (auto& packet : RGraph.mapDynamicPasses[1][0])
		count += packet.item.bus_tag != 0;
	for (auto& item : RGraph.mapDynamicSorted.Sorted)
		count += item.bus_tag != 0;
	if (!count)
		return;
	bus_forward_xforms.reserve(count);
	for (auto& packet : RGraph.mapDynamicPasses[1][0])
		if (packet.item.bus_tag)
		{
			bus_forward_xforms.push_back(*packet.item.pMatrix);
			packet.item.pMatrix = &bus_forward_xforms.back();
			bus_forward_packets.push_back(packet);
		}
	for (auto& item : RGraph.mapDynamicSorted.Sorted)
		if (item.bus_tag)
		{
			bus_forward_xforms.push_back(*item.pMatrix);
			item.pMatrix = &bus_forward_xforms.back();
			bus_forward_sorted.push_back(item);
		}
}

// tagged forward, sorted and hidden items over the object mask and particles over their own target, after queue 1 and the sorted draw
void CDSGraphManager::r_dsgraph_render_bus_forward()
{
	PROF_EVENT("r_dsgraph_render_bus_forward");
	CRenderTarget* T = RImplementation.Target;
	bool meshes = false;
	bool particles = false;
	for (auto& packet : bus_forward_packets)
		bus_forward_kind(packet.item, bus_forward_bits, meshes, particles);
	for (auto& item : bus_forward_sorted)
		bus_forward_kind(item, bus_forward_bits, meshes, particles);
	for (auto& item : bus_hidden)
		bus_forward_kind(item, ~0u, meshes, particles);
	particles = particles && T->rt_bus_particle_mask;
	const bool hidden = !bus_hidden.empty() && !bus_hidden_rects.empty() && T->rt_bus_hidden_depth;
	if (meshes || hidden || particles)
	{
		ID3DRenderTargetView* saved[4];
		bus_mask_bind(saved);
		ID3DDepthStencilView* zb = RCache.get_ZB();
		if (meshes || hidden)
		{
			bus_forward_walk(bus_forward_packets, bus_forward_sorted, bus_hidden);
			if (hidden)
			{
				bus_hidden_depth_resolve(bus_hidden_rects);
				bus_hidden_depth_pass(bus_hidden, T->rt_bus_mask->pRT);
				bus_hidden_redraw(bus_hidden);
				RCache.set_ZB(zb);
			}
			if (T->rt_bus_object_id)
			{
				bus_mask_pass_begin(bus_pass_id, T->rt_bus_object_id->pRT, 0);
				bus_forward_walk(bus_forward_packets, bus_forward_sorted, bus_hidden);
				if (hidden)
				{
					RCache.set_ZB(T->rt_bus_hidden_depth->pZRT);
					bus_hidden_redraw(bus_hidden);
					RCache.set_ZB(zb);
				}
			}
			if (bus_layer_bits)
			{
				bus_mask_pass_begin(bus_pass_layer, T->rt_bus_mask_layer->pRT, bus_layer_bits);
				bus_forward_walk(bus_forward_packets, bus_forward_sorted, bus_hidden);
				// hidden leaves on the layer cover through walls, so their depth starts empty
				if (hidden)
				{
					HW.pContext->ClearDepthStencilView(T->rt_bus_hidden_depth->pZRT, D3D_CLEAR_DEPTH, 1.f, 0);
					bus_hidden_depth_pass(bus_hidden, T->rt_bus_mask_layer->pRT);
					bus_hidden_redraw(bus_hidden);
				}
				RCache.set_ZB(zb);
			}
		}
		// particles draw their tags into their own target alone, so the mask keeps the objects behind them
		if (particles)
		{
			bus_mask_pass_begin(bus_pass_particle, T->rt_bus_particle_mask->pRT, 0);
			bus_forward_walk(bus_forward_packets, bus_forward_sorted, bus_hidden);
		}
		bus_mask_unbind(saved);
	}
	bus_hidden.clear();
	bus_hidden_rects.clear();
	bus_hidden_xforms.clear();
	bus_forward_packets.clear();
	bus_forward_sorted.clear();
	bus_forward_xforms.clear();
}

// the tagged objects of the hidden bits the frame drew nowhere, into the hidden mask list far to near
void CDSGraphManager::r_dsgraph_add_bus_hidden()
{
	static xr_vector<IRenderable*> holders;
	static xr_vector<std::pair<float, CObject*>> found;
	static bool truncated = false;
	bus_hidden.clear();
	bus_hidden_rects.clear();
	bus_hidden_xforms.clear();
	bus_hidden_xforms.reserve(bus_hidden_max);
	ShaderBus::mask_holders(bus_hidden_bits, holders);
	found.clear();
	for (IRenderable* r : holders)
	{
		CObject* O = fast_dynamic_cast<CObject*>(r);
		ISpatial* S = r->SpatialComponent.get();
		dxRender_Visual* V = (dxRender_Visual*)r->renderable.visual;
		if (!O || !S || O->H_Parent() || !V || !S->spatial.node_ptr || V->vis.hom_frame == Device.dwFrame)
			continue;
		if (!(S->spatial.type & STYPE_RENDERABLE))
			S->spatial_updatesector();
		if (R4FullDetailRejectSpatial(S, true))
			continue;
		CSector* sector = (CSector*)S->spatial.sector;
		if (!sector || !is_sector_visible(sector))
			continue;
		bool seen = false;
		for (CFrustum& frustum : m_sector_frustums.find(sector)->val.first)
			if (frustum.testSphere_dirty(S->spatial.sphere.P, S->spatial.sphere.R))
			{
				seen = true;
				break;
			}
		if (!seen)
			continue;
		if (!(r->renderable.bus_mask & bus_hidden_bits & bus_layer_bits) && !RImplementation.HOM.visible(S->spatial.sphere))
			continue;
		found.emplace_back(Device.vCameraPosition.distance_to_sqr(S->spatial.sphere.P), O);
	}
	if (found.empty())
		return;

	std::sort(found.begin(), found.end(), [](const std::pair<float, CObject*>& a, const std::pair<float, CObject*>& b) { return a.first < b.first; });
	if (found.size() > bus_hidden_max)
	{
		if (!truncated)
			Msg("! [SHADER-BUS] more than %u hidden tagged objects in view, the farthest draw no mask", bus_hidden_max);
		truncated = true;
		found.resize(bus_hidden_max);
	}
	for (auto it = found.rbegin(); it != found.rend(); ++it)
	{
		CObject* O = it->second;
		dxRender_Visual* V = (dxRender_Visual*)O->renderable.visual;
		set_Object(O);
		val_busTag &= bus_hidden_bits;
		bus_hidden_xforms.push_back(O->renderable.xform);
		Fmatrix& xform = bus_hidden_xforms.back();
		const size_t listed = bus_hidden.size();
		bus_mask_only = true;
		add_Dynamic(O->renderable.visual, &xform);
		bus_mask_only = false;
		// the rectangle of the current pose, for an object whose insert listed a mesh leaf
		bool mesh = false;
		for (size_t i = listed; i < bus_hidden.size(); ++i)
			mesh |= !bus_hidden[i].pVisual->dcast_ParticleCustom();
		if (mesh)
		{
			Fbox3 box = bus_hidden_box(V);
			box.xform(xform);
			const Fvector4 rect = bus_hidden_rect(box);
			if (rect.x < rect.z && rect.y < rect.w)
				bus_hidden_rects.push_back(rect);
		}
		// stamped as drawn and made a crow, so the game updates it like a seen object at any range
		V->vis.hom_frame = Device.dwFrame;
		O->MakeMeCrow();
	}
	set_Object();
}
#endif

//////////////////////////////////////////////////////////////////////////
// HUD render
#if defined(USE_DX10) || defined(USE_DX11)
// the redraw draws with the matrix values the list's own draw used
static xr_vector<Fmatrix> bus_hud_xforms;
#endif

void CDSGraphManager::r_dsgraph_render_hud()
{
	PROF_EVENT("r_dsgraph_render_hud");
	const bool mask = bus_mask_begin();
	CHudInitializer initializer(true);

	// Rendering
	RImplementation.rmNear();
	if (mask) r_dsgraph_render_hud_mask(RGraph.mapHUD);
	else r_dsgraph_render_graph_sorted(RGraph.mapHUD);

	RImplementation.rmNormal();

#if defined(USE_DX11) //  Redotix99: for 3D Shader Based Scopes 		
	if (scope_3D_fake_enabled)
	{
		RCache.set_RT(RImplementation.Target->rt_ssfx_temp->pRT, 3); // Render scope_3D to any buffer

		if (mask) r_dsgraph_render_hud_mask(RGraph.mapScopeHUD);
		else r_dsgraph_render_graph_sorted(RGraph.mapScopeHUD);

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
		if (mask) r_dsgraph_render_hud_mask(RGraph.mapCamAttached);
		else r_dsgraph_render_graph_sorted(RGraph.mapCamAttached);

		RImplementation.rmNormal();
	}
}

void CDSGraphManager::r_dsgraph_render_hud_mask(R_dsgraph::mapDSGraphItems<float, false>& list)
{
#if defined(USE_DX10) || defined(USE_DX11)
	bus_hud_xforms.clear();
	bus_hud_xforms.reserve(list.size());
	for (auto& item : list)
	{
		bus_hud_xforms.push_back(*item.pMatrix);
		item.pMatrix = &bus_hud_xforms.back();
	}
	r_dsgraph_render_graph_sorted(list, false);

	PROF_EVENT("r_dsgraph_render_hud_mask");
	ID3DRenderTargetView* rt[4];
	bus_mask_bind(rt);
	r_dsgraph_render_graph_sorted<float, false, true>(list, true);
	bus_mask_unbind(rt);
#endif
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
		r_dsgraph_render_graph_sorted(RGraph.mapStaticSorted.Sorted, true);
		r_dsgraph_render_graph_sorted(RGraph.mapDynamicSorted.Sorted, true);
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
		r_dsgraph_render_graph_sorted(RGraph.mapCamAttachedSorted.Sorted, true);
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
	r_dsgraph_render_graph_sorted(RGraph.mapScopeHUDSorted, true);
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
	r_dsgraph_render_graph_sorted(RGraph.mapHUDSorted.Sorted, true);
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
		r_dsgraph_render_graph_sorted(RGraph.mapHUDSorted.Sorted, false);

	RImplementation.rmNormal();
#endif
}

void CDSGraphManager::r_dsgraph_render_water_ssr()
{
#ifdef USE_DX11
	PROF_EVENT("r_dsgraph_render_water_ssr");
	std::sort(RGraph.mapWater.begin(), RGraph.mapWater.end());
	for (auto& N : RGraph.mapWater)
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
    std::sort(RGraph.mapWater.begin(), RGraph.mapWater.end());
    for (auto& N : RGraph.mapWater)
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
	r_dsgraph_render_graph_sorted(RGraph.mapStaticSorted.Distort, true);
	r_dsgraph_render_graph_sorted(RGraph.mapDynamicSorted.Distort, true);
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
	const bool dbg_enabled = PortalTraverseDbg_Enabled();
	PortalTraverseDebugStats* dbg = dbg_enabled ? &PortalTraverseDbg_Get() : nullptr;
	const bool opt_bucket = PortalTraverseDbg_IsOptions(i_options);

	if (i_start)
	{
		// Traverse sector/portal structure
		if (psDeviceFlags.test(rsDrawStatic))
		{
			if (dbg)
			{
				const u32 sector_nodes = u32(m_sector_frustums.size());
				dbg->static_sector_nodes += sector_nodes;
				if (opt_bucket)
					dbg->static_sector_nodes_opt += sector_nodes;
				else
					dbg->static_sector_nodes_noopt += sector_nodes;
			}

			// Determine visibility for static geometry hierrarhy
			for (auto& pair : m_sector_frustums)
			{
				xr_vector<CFrustum>& frustums = pair.val.first;
				if (frustums.empty())
					continue;

				if (frustums.size() == 1)
				{
					if (dbg)
					{
						++dbg->static_frustum_nodes;
						++dbg->static_add_root_calls;
						if (opt_bucket)
						{
							++dbg->static_frustum_nodes_opt;
							++dbg->static_add_root_calls_opt;
						}
						else
						{
							++dbg->static_frustum_nodes_noopt;
							++dbg->static_add_root_calls_noopt;
						}
					}
					add_Static((IRenderVisual*)pair.key->root(), frustums.front(), frustums.front().getMask());
					continue;
				}

				xr_vector<u32> masks;
				masks.reserve(frustums.size());
				for (CFrustum& frustum_node : frustums)
					masks.push_back(frustum_node.getMask());

				if (dbg)
				{
					dbg->static_frustum_nodes += u32(frustums.size());
					++dbg->static_add_root_calls;
					if (opt_bucket)
					{
						dbg->static_frustum_nodes_opt += u32(frustums.size());
						++dbg->static_add_root_calls_opt;
					}
					else
					{
						dbg->static_frustum_nodes_noopt += u32(frustums.size());
						++dbg->static_add_root_calls_noopt;
					}
				}

				add_Static_MultiFrustum((IRenderVisual*)pair.key->root(), frustums, masks);
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
	std::sort(lstLights.begin(), lstLights.end(), [](const ISpatialShared& _1, const ISpatialShared& _2) noexcept
	{
		if (!_1.get() || !_2.get()) return false;

		return	_1->spatial.sphere.P.distance_to_sqr(Device.vCameraPosition_saved) < _2->spatial.sphere.P.distance_to_sqr(Device.vCameraPosition_saved);
	});
#endif

	for (ISpatialShared spatial : lstLights)
	{
		if (0 == spatial) continue;
		if (R4FullDetailRejectSpatial(spatial.get(), i_mask[CDSGraphManager::fl_normal])) continue;
		spatial->spatial_updatesector();
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
				if (L->get_LOD() > ps_r2_shadow_lod_min && L->has_light_visible_from_sectors(*this))
				{
					RImplementation.Lights.add_light(L);
				}
#endif
			}
		}
	}
}

// true for a renderable holding a bit on the mask layer or an item with one, particles draw no layer
static bool bus_layer_holder(ISpatial* spatial)
{
	if (spatial->spatial.type & STYPE_PARTICLE)
		return false;
	IRenderable* renderable = spatial->dcast_Renderable();
	return renderable && ((renderable->renderable.bus_mask & CDSGraphManager::bus_layer_bits) || ShaderBus::layer_root(renderable));
}

void CDSGraphManager::r_dsgraph_capture_dynamic(CObject* O)
{
	PROF_EVENT("r_dsgraph_capture_dynamic")
	const bool dbg_enabled = PortalTraverseDbg_Enabled();
	PortalTraverseDebugStats* dbg = dbg_enabled ? &PortalTraverseDbg_Get() : nullptr;
	const bool opt_bucket = PortalTraverseDbg_IsOptions(i_options);

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
			if (dbg)
			{
				const u32 spatial_count = u32(lstRenderables.size());
				dbg->dynamic_spatials += spatial_count;
				if (opt_bucket)
					dbg->dynamic_spatials_opt += spatial_count;
				else
					dbg->dynamic_spatials_noopt += spatial_count;
			}
			set_Object();
#if	RENDER==R_R1
			if (i_mask[CDSGraphManager::fl_normal])//normal phase
			{
				std::sort(lstRenderables.begin(), lstRenderables.end(), [](const ISpatialShared& _1, const ISpatialShared& _2) noexcept
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
				ISpatialShared spatial = lstRenderables[o_it];
				if (0 == spatial) continue;
				if (R4FullDetailRejectSpatial(spatial.get(), i_mask[CDSGraphManager::fl_normal])) continue;
				CSector* sector = (CSector*)spatial->spatial.sector;
				if (0 == sector) continue;

				// an object holding a layer bit, or holding an item with one, stays in the frame when occlusion hides it
				if (i_mask[CDSGraphManager::fl_normal] && !RImplementation.HOM.visible(spatial->spatial.sphere)
					&& !(CDSGraphManager::bus_layer_bits && bus_layer_holder(spatial.get())))
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

				for (CFrustum& frustum : m_sector_frustums.find(sector)->val.first)
				{
					if (dbg)
					{
						++dbg->dynamic_frustum_tests;
						if (opt_bucket)
							++dbg->dynamic_frustum_tests_opt;
						else
							++dbg->dynamic_frustum_tests_noopt;
					}

					if (frustum.testSphere_dirty(spatial->spatial.sphere.P, spatial->spatial.sphere.R))
					{
						if (dbg)
						{
							++dbg->dynamic_frustum_hits;
							if (opt_bucket)
								++dbg->dynamic_frustum_hits_opt;
							else
								++dbg->dynamic_frustum_hits_noopt;
						}

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
						const bool bind = !(spatial->spatial.type & STYPE_PARTICLE) || renderable->renderable.bus_values || (renderable->renderable.bus_mask & bus_live_bits)
							|| (ShaderBus::tint_lane && ShaderBus::tint_lane->bound_forced);
						if (i_mask[CDSGraphManager::fl_normal] && bind)
							set_Object(renderable);

						renderable->renderable_Render(dcast_IPortalTraverser());
						if (dbg)
						{
							++dbg->dynamic_rendered;
							if (opt_bucket)
								++dbg->dynamic_rendered_opt;
							else
								++dbg->dynamic_rendered_noopt;
						}

						if (i_mask[CDSGraphManager::fl_normal] && bind)
							set_Object();
						break;
					}
				}
			}
#if defined(USE_DX10) || defined(USE_DX11)
			// tagged objects the frame drew nowhere join the forward mask draw, in the main capture
			if (i_mask[CDSGraphManager::fl_normal] && bus_hidden_bits)
				r_dsgraph_add_bus_hidden();
#endif
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

template void CDSGraphManager::r_dsgraph_render_graph<false>(RenderQueueArray&, u32, bool, bool);
#if defined(USE_DX10) || defined(USE_DX11)
template void CDSGraphManager::r_dsgraph_render_graph<true>(RenderQueueArray&, u32, bool, bool);
#endif
