#if !defined(_DSGMANAGE_H_)
#define _DSGMANAGE_H_
#pragma once
#include "r__dsgraph_types.h"
#include "r__sector.h"
#include "../../xrCore/FixedMap.h"
class CDSGraphManager : public IDSGraphManager
{
public:
	enum
	{
		VQ_HOM = (1 << 0),
		VQ_SSA = (1 << 1),
		VQ_FADE = (1 << 2),				// requires SSA to work
	};

	R_dsgraph::DynamicSceneRgraph RGraph;
	u32										i_options;		// input:	culling options
	u32										i_doptions;
	Fvector									i_vBase;		// input:	"view" point
	CFrustum								i_frustum;		// input:	"view" frustum
	Fmatrix									i_mXFORM;		// input:	4x4 xform
	CSector* i_start;		// input:	starting point
	xrCriticalSection						P_CS;
	xrSRWLock								S_LC;

	FixedMAP<CSector*, std::pair<xr_vector<CFrustum>, FixedSet<CPortal*>>>		m_sector_frustums;
	xr_unordered_flat_set<dxRender_Visual*>				m_static_seen;
	xr_vector<ISpatialShared>				lstRenderables, lstLights;
	FixedMAP<CPortal*, float>				f_portals;
	sPoly S, D;
	ref_shader								f_shader;
	ref_geom								f_geom;

	// shader bus object mask bits live this frame, read once at the render start
	static u32								bus_live_bits;
	// mask layer bits this frame, 0 in a second viewport frame or while no layer target exists
	static u32								bus_layer_bits;
	// mask bits that also reach objects the game does not draw, 0 while the mask pass does not run
	static u32								bus_hidden_bits;
	// mask bits that also reach forward and sorted meshes, 0 while the mask pass does not run
	static u32								bus_forward_bits;
	// the world pass keeps queue 0 for the object mask pass this frame
	bool									bus_mask_on = false;
	bool bus_mask_runs() const { return bus_mask_on; }
	// inserts go to the hidden mask list alone, for an object the game does not draw
	bool									bus_mask_only = false;
	// the leaves of hidden tagged objects this frame, drawn into the mask after the sorted draw
	R_dsgraph::mapDSGraphItems<float, false>	bus_hidden;
	// the screen rectangles of the hidden objects listed this frame, where the scene depth is resolved
	xr_vector<Fvector4>						bus_hidden_rects;
	// the hidden objects' matrices, which their rectangles and mask draws read while GameThread moves them
	xr_vector<Fmatrix>						bus_hidden_xforms;
	// tagged queue 1 packets and sorted items kept past their draw, which clears them, and the matrices both draws read
	xr_vector<R_dsgraph::RenderPacket>		bus_forward_packets;
	R_dsgraph::mapDSGraphItems<float, true>	bus_forward_sorted;
	xr_vector<Fmatrix>						bus_forward_xforms;

	CDSGraphManager(u32 options, u32 doptions, bool(&& mask)[7]) : i_options(options), i_doptions(doptions)
	{
		std::copy(mask, mask + 7, i_mask);
	}
	void initialize();
	void destroy();
	void traverse(CSector* start, CFrustum& F, Fvector& vBase, Fmatrix& mXFORM);
	IC bool is_sector_visible(CSector* sector)
	{
		if (sector == i_start) return true;

		{
			xrSRWLockGuard guard(&S_LC, true);
			if(m_sector_frustums.size())
			{
				if (auto node = m_sector_frustums.find(sector))
					return !node->val.first.empty();
			}
		}

		return false;
	};
	CDSGraphManager& get_traverser_safed() { xrSRWLockGuard guard(&S_LC, false); return *this; };
	void fade_portal(CPortal* _p, float ssa);
	void fade_render();

	virtual void set_Object(IRenderable* O = nullptr);

	virtual void add_Static(IRenderVisual* pVisual, CFrustum& frustum, u32 planes);
	virtual void add_Static_MultiFrustum(IRenderVisual* pVisual, const xr_vector<CFrustum>& frustums, const xr_vector<u32>& masks);
	void add_leaf_Static(dxRender_Visual* pVisual);
	void add_leafs_Static(xr_vector<dxRender_Visual*>& children);
	void add_leafs_Static(xr_vector<IRenderVisual*>& children);
	void r_dsgraph_insert_static(dxRender_Visual* pVisual);

	virtual void add_Dynamic(IRenderVisual* piVisual, Fmatrix* xform);
	void add_Dynamic(dxRender_Visual* pVisual, Fmatrix* xform);
	void add_leafs_Dynamic(xr_vector<dxRender_Visual*>& children, Fmatrix* xform);
	void add_leafs_Dynamic(xr_vector<IRenderVisual*>& children, Fmatrix* xform);
	void r_dsgraph_insert_dynamic(dxRender_Visual* pVisual, Fmatrix* xform);

	void AddToRenderQueue(R_dsgraph::RenderQueue& queue, const R_dsgraph::DSGraphItem<u32, false>& item, const SPass& pass);
	template<bool BusMask = false>
	void r_dsgraph_render_graph(R_dsgraph::RenderQueueArray& queue, u32 _priority, bool _clear = true, bool static_geometry = true);
	// copies the matrices of tagged queue 0 items so both draws use one value
	void r_dsgraph_bus_keep_xforms();
#if defined(USE_DX10) || defined(USE_DX11)
	// redraws the tagged priority 0 dynamic items into the shader bus mask and clears them
	void r_dsgraph_render_bus_mask();
	// keeps the tagged queue 1 and sorted items before their draw clears them
	void r_dsgraph_bus_keep_forward();
	// redraws the kept forward and sorted items and the hidden items into the shader bus mask
	void r_dsgraph_render_bus_forward();
	// lists the tagged objects of the hidden bits the frame drew nowhere, for the forward mask draw
	void r_dsgraph_add_bus_hidden();
#endif
	IC void r_dsgraph_render_graph(u32 _priority, bool _clear = true)
	{
		r_dsgraph_render_static(_priority, _clear);
		r_dsgraph_render_dynamic(_priority, _clear);
	}
	IC void r_dsgraph_render_static(u32 _priority, bool _clear = true)
	{
		PROF_EVENT("r_dsgraph_render_static");
		r_dsgraph_render_graph(RGraph.mapStaticPasses, _priority, _clear);
	};
	IC void	r_dsgraph_render_dynamic(u32 _priority, bool _clear = true)
	{
		PROF_EVENT("r_dsgraph_render_dynamic");
		r_dsgraph_render_graph(RGraph.mapDynamicPasses, _priority, _clear, false);
	};

	template<typename T, bool Reverse, bool BusMask = false>
	void r_dsgraph_render_graph_sorted(R_dsgraph::mapDSGraphItems<T, Reverse>& graph, bool _clear = true);
	void r_dsgraph_capture_hud();
	void r_dsgraph_render_hud();
	void r_dsgraph_render_hud_mask(R_dsgraph::mapDSGraphItems<float, false>& list);
	void r_dsgraph_render_hud_ui();
	void r_dsgraph_render_lods(bool _setup_zb, bool _clear);
	void r_dsgraph_render_sorted(bool render_hud = true);
	void r_dsgraph_render_sorted_hud();
	void r_dsgraph_render_emissive(bool clear = true, bool renderHUD = false);
	void r_dsgraph_render_wmarks();
	void r_dsgraph_render_distort();

	// Anomaly
#if defined(USE_DX11)
	void r_dsgraph_render_ScopeSorted();
#endif
	void r_dsgraph_render_cam_ui();
	void r_dsgraph_render_water_ssr();
	void r_dsgraph_render_water();

	void r_dsgraph_capture(bool lights = false, bool dynamic = false, CObject* O = nullptr);
	void r_dsgraph_capture_lights();
	void r_dsgraph_capture_static();
	void r_dsgraph_capture_dynamic(CObject* O = nullptr);

	void clear()
	{
		xrSRWLockGuard guard(&S_LC, false);
		RGraph.clear();
		if (m_sector_frustums.size())
		{
			for (auto& pair : m_sector_frustums)
			{
				pair.val.first.clear();
				pair.val.second.clear();
			}
			m_sector_frustums.clear();
		}
		lstRenderables.clear();
		lstLights.clear();
		f_portals.clear();
		m_static_seen.clear();
	}
};

struct PortalTraverseDebugStats
{
	u32 frame_id = 0;

	u32 traverse_calls = 0;
	u32 traverse_calls_with_options = 0;
	u32 traverse_calls_without_options = 0;
	u32 frustums_pushed = 0;
	u32 max_frustums_in_sector = 0;
	u32 frustums_pushed_opt = 0;
	u32 frustums_pushed_noopt = 0;
	u32 max_frustums_in_sector_opt = 0;
	u32 max_frustums_in_sector_noopt = 0;

	u32 portals_checked = 0;
	u32 portals_checked_opt = 0;
	u32 portals_checked_noopt = 0;
	u32 portals_skipped_already_visited = 0;
	u32 portals_rejected_sphere = 0;
	u32 portals_rejected_sector = 0;
	u32 portals_rejected_ssa = 0;
	u32 portals_rejected_clip = 0;
	u32 portals_rejected_hom = 0;
	u32 portals_recursed = 0;
	u32 portals_recursed_opt = 0;
	u32 portals_recursed_noopt = 0;

	u32 portals_debug_branch_taken = 0;
	u32 portals_debug_branch_precedence_hits = 0;

	u32 static_sector_nodes = 0;
	u32 static_frustum_nodes = 0;
	u32 static_add_root_calls = 0;
	u32 static_sector_nodes_opt = 0;
	u32 static_sector_nodes_noopt = 0;
	u32 static_frustum_nodes_opt = 0;
	u32 static_frustum_nodes_noopt = 0;
	u32 static_add_root_calls_opt = 0;
	u32 static_add_root_calls_noopt = 0;

	u32 dynamic_spatials = 0;
	u32 dynamic_frustum_tests = 0;
	u32 dynamic_frustum_hits = 0;
	u32 dynamic_rendered = 0;
	u32 dynamic_spatials_opt = 0;
	u32 dynamic_spatials_noopt = 0;
	u32 dynamic_frustum_tests_opt = 0;
	u32 dynamic_frustum_tests_noopt = 0;
	u32 dynamic_frustum_hits_opt = 0;
	u32 dynamic_frustum_hits_noopt = 0;
	u32 dynamic_rendered_opt = 0;
	u32 dynamic_rendered_noopt = 0;

	u32 queue_static_packets = 0;
	u32 queue_dynamic_packets = 0;
	u32 queue_static_packets_opt = 0;
	u32 queue_static_packets_noopt = 0;
	u32 queue_dynamic_packets_opt = 0;
	u32 queue_dynamic_packets_noopt = 0;

	u32 static_dedup_seen = 0;
	u32 static_dedup_skipped = 0;
	u32 static_dedup_seen_opt = 0;
	u32 static_dedup_seen_noopt = 0;
	u32 static_dedup_skipped_opt = 0;
	u32 static_dedup_skipped_noopt = 0;

	void reset(u32 frame)
	{
		*this = {};
		frame_id = frame;
	}
};

bool PortalTraverseDbg_Enabled();
bool PortalTraverseDbg_IsOptions(u32 options);
PortalTraverseDebugStats& PortalTraverseDbg_Get();
const PortalTraverseDebugStats& PortalTraverseDbg_Peek();

#endif // !defined(AFX_PORTAL_H__1FC2D371_4A19_49EA_BD1E_2D0F8DEBBF15__INCLUDED_)
