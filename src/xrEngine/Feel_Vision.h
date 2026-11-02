#pragma once

#include "../xrcdb/xr_collide_defs.h"
#include "render.h"
#include "pure_relcase.h"

class IRender_Sector;
class CObject;
class ISpatial;

namespace Feel
{
	const float fuzzy_update_vis = 1000.f; // speed of fuzzy-logic desisions
	const float fuzzy_update_novis = 1000.f; // speed of fuzzy-logic desisions
	const float fuzzy_guaranteed = 0.001f; // distance which is supposed 100% visible
	const float lr_granularity = 0.1f; // assume similar positions

#if defined(DEBUG) || defined(DEBUG_DRAW)
	// result of the last real visibility trace (Vision::o_trace), read by ai_draw_visibility_rays / ai_draw_info
	enum EDbgTraceResult
	{
		dbg_trace_none = 0,				// not traced yet
		dbg_trace_visible,				// main ray reached the point, nothing in the way
		dbg_trace_attenuated,			// main ray reached the point through semi-transparent materials
		dbg_trace_near,					// point closer than fuzzy_guaranteed, no ray cast
		dbg_trace_blocked,				// main ray (static / objects / obstacles) stopped
		dbg_trace_blocked_cached_tri,	// stopped by the cached blocking triangle, no real ray cast
		dbg_trace_blocked_object,		// main ray passed, second pass hit an object flagged visible-for-AI
		dbg_trace_no_cform,				// target has no collision form, never visible
	};
#endif

#if defined(DEBUG) || defined(DEBUG_DRAW)
	// Console ai_dbg_visibility_ray_bone <bone>|off: vision rays aim at the center of that bone's collision shape on targets
	// that have it (CEntityAlive::get_last_local_point_on_mesh), others keep the random point. o_trace then ray-tests
	// the target's collision form for ai_draw_visibility_rays.
	ENGINE_API extern string64 g_dbg_visibility_ray_bone;
	IC bool dbg_visibility_ray_bone_enabled() { return g_dbg_visibility_ray_bone[0] && 0 != xr_strcmp(g_dbg_visibility_ray_bone, "off"); }
#endif

	class ENGINE_API Vision : private pure_relcase
	{
	private:
		xr_vector<CObject*> seen;
		xr_vector<CObject*> query;
		xr_vector<CObject*> diff;
		collide::rq_results RQR;
		xr_vector<ISpatial*> r_spatial;
		CObject const* m_owner;

		void o_new(CObject* E);
		void o_delete(CObject* E);
		void o_trace(Fvector& P, float dt, float vis_threshold);
	public:
		Vision(CObject const* owner);
		virtual ~Vision();

		// One entry per object currently inside the observer's view frustum (see feel_vision_query /
		// feel_vision_update). Created by o_new when the object enters the frustum, removed by o_delete when
		// it leaves. o_trace casts one ray per entry per trace, from the eye to a single point on the object.
		struct feel_visible_Item
		{
			// Ray cache for this entry:
			//  - start / dir / range / result: the last real ray query. result is TRUE only when that query hit
			//    at least one surface; then Cache_vis holds its visibility and is reused while the next ray is
			//    similar() (same eye position, direction and length), without casting a new ray.
			//  - verts: the last fully opaque static triangle the ray stopped on (written by feel_vision_callback).
			//    Before a real query the ray is tested against this triangle alone; a hit means "blocked"
			//    without querying the level.
			collide::ray_cache Cache;
			// Tested point in local space. With bone_id != u16(-1): relative to that bone, picked by
			// CEntityAlive::get_new_local_point_on_mesh (random bone weighted by collision-shape surface area,
			// random point on that shape). With bone_id == u16(-1): a point inside the object's bounding-box
			// ellipsoid (CObject default / ai_use_old_vision). Re-picked only after a failed trace, so while
			// the ray keeps passing the same body point keeps being tested.
			Fvector cp_LP;
			// "Last relation": observer eye position (src) and target Position() (dst) at the last trace.
			// Only written; the early-out that skipped tracing when neither moved by lr_granularity is commented
			// out in o_trace.
			Fvector cp_LR_src;
			Fvector cp_LR_dst;
			// World-space position of cp_LP at the last trace (get_last_local_point_on_mesh), i.e. the point the
			// last ray was cast at. It was visible only if that trace passed (fuzzy > 0). Returned by
			// feel_vision_get_vispoint and used as the fire-object aim point when the line of fire to the aim
			// bone is blocked (CSightAction::execute_fire_object, state 1).
			Fvector cp_LAST;
			// The tracked object.
			CObject* O;
			// Visibility state, clamped to [-0.5, 1]: + fuzzy_update_vis * dt after a passed trace,
			// - fuzzy_update_novis * dt after a failed one (1000/s, so effectively a binary flip per trace).
			// Starts at -EPS_S; forced to -1 when the object has no CFORM and to 1 when the point coincides with
			// the eye. fuzzy > 0 puts the object in the raw vision list (feel_vision_get), which is what feeds
			// the visual memory / detection meter.
			float fuzzy;
			// Visibility of the last real query that hit something: product of the fVisTransparencyFactor of
			// every surface the ray passed (1 = clear). Reused while Cache.result && Cache.similar(...).
			float Cache_vis;
			// Bone cp_LP is relative to, u16(-1) for the bounding-ellipsoid mode (see cp_LP).
			u16 bone_id;
#if defined(DEBUG) || defined(DEBUG_DRAW)
			Fvector dbg_src;			// ray start (eye) of the last trace
			Fvector dbg_dst;			// tested point on the target mesh
			float dbg_vis;				// transparency accumulated by the main ray (1 = clear)
			float dbg_block_range;		// distance from dbg_src where the ray was stopped, -1 if it was not
			u32 dbg_time;				// Device.dwTimeGlobal of the trace, 0 = never traced
			u8 dbg_result;				// EDbgTraceResult
			bool dbg_cached;			// visibility taken from the ray cache, no ray cast this time
			// ai_dbg_visibility_ray_bone: the target's own collision form tested directly (no vision pass ever tests the target).
			// Point = dbg_src + dbg_dir * dbg_hit_t, and dbg_src + normalized dbg_dir * dbg_expected_t.
			Fvector dbg_dir;			// direction passed to the ray query
			float dbg_range;			// range passed to the ray query
			float dbg_hit_t;			// first hit on the target with dbg_dir / dbg_range, -1 = none
			int dbg_hit_elem;			// bone of that hit
			float dbg_expected_t;		// first hit on the target with a unit direction, meters, -1 = none
			int dbg_expected_elem;		// bone of that hit
#endif
		};

		xr_vector<feel_visible_Item> feel_visible;
	public:
		void feel_vision_clear();
		void feel_vision_query(Fmatrix& mFull, Fvector& P);
		void feel_vision_update(CObject* parent, Fvector& P, float dt, float vis_threshold);
		void __stdcall feel_vision_relcase(CObject* object);

		void feel_vision_get(xr_vector<CObject*>& R)
		{
			R.clear();
			xr_vector<feel_visible_Item>::iterator I = feel_visible.begin(), E = feel_visible.end();
			for (; I != E; ++I) if (positive(I->fuzzy)) R.push_back(I->O);
		}

		Fvector feel_vision_get_vispoint(CObject* _O)
		{
			xr_vector<feel_visible_Item>::iterator I = feel_visible.begin(), E = feel_visible.end();
			for (; I != E; ++I)
				if (_O == I->O)
				{
					VERIFY(positive(I->fuzzy));
					return I->cp_LAST;
				}
			VERIFY2(0, "There is no such object in the potentially visible list");
			return Fvector().set(flt_max, flt_max, flt_max);
		}

		virtual bool feel_vision_isRelevant(CObject* O) = 0;
		virtual float feel_vision_mtl_transp(CObject* O, u32 element) = 0;
	};
};
