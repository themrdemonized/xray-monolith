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
	void r_dsgraph_render_graph(R_dsgraph::RenderQueueArray& queue, u32 _priority, bool _clear = true, bool static_geometry = true);

	// pip SVP geometry cull, active only between begin/end which renderGBuffer brackets around the SVP pass
	static void svp_cull_begin(Fmatrix& full_xform, bool cull_world);
	static void svp_cull_end();
	static bool svp_cull_active();
	static void svp_set_lod_scale(float s); // pip SVP LOD: scale captured ssa to the SVP's pixel coverage
	static void svp_set_ssa_cull(float strength, float cov); // pip SVP small-object cull threshold
	static bool svp_cull_reject(dxRender_Visual* V, Fmatrix* M);
	static bool svp_cull_reject_sphere(const Fvector& c, float r);
	static float svp_drain_lod(float ssa, float R); // pip lod for the drained weapon via the inline lod helpers
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

	template<typename T, bool Reverse>
	void r_dsgraph_render_graph_sorted(R_dsgraph::mapDSGraphItems<T, Reverse>& graph, bool _clear = true);
	void r_dsgraph_capture_hud();
	void r_dsgraph_render_hud(bool _clear = true);
	void r_dsgraph_render_hud_svp();
	// pip hud pose latch, logic rewrites the hud matrices mid render, the SVP drain and the
	// late scope draws reuse the exact pose the housing drew with
	struct
	{
		Fmatrix* src[8];
		Fmatrix val[8];
		u32 count{};
		u32 frame{u32(-1)};
	} m_svp_pose;
	void svp_latch_hud_poses();
	Fmatrix* svp_pose_of(Fmatrix* p);
	// pip lens bone latch, skinned lens bones recompute on the logic thread mid render, the camera
	// derivation and the late lens draws reuse the bone sampled at the housing draw
	struct
	{
		dxRender_Visual* vis[4];
		Fmatrix bone[4];
		u32 count{};
		u32 frame{u32(-1)};
	} m_svp_bone;
	bool svp_lens_bone_of(dxRender_Visual* v, Fmatrix& out);
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
	void r_dsgraph_render_water(bool clearGraph = true); // pip clearGraph false keeps mapWater for the next viewport

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
