#include "stdafx.h"
#include "feel_vision.h"
#include "render.h"
#include "xr_collide_form.h"
#include "igame_level.h"
#include "cl_intersect.h"

using Feel::Vision;

Vision::Vision(CObject const* owner) :
	pure_relcase(&Vision::feel_vision_relcase),
	m_owner(owner)
{
}

Vision::~Vision()
{
}

struct SFeelParam
{
	Vision* parent;
	Vision::feel_visible_Item* item;
	float vis;
	float vis_threshold;

	SFeelParam(Vision* _parent, Vision::feel_visible_Item* _item, float _vis_threshold) : parent(_parent), item(_item), vis(1.f), vis_threshold(_vis_threshold) {}
};

IC BOOL feel_vision_callback(collide::rq_result& result, LPVOID params)
{
	SFeelParam* fp = (SFeelParam*)params;
	float vis = fp->parent->feel_vision_mtl_transp(result.O, result.element);
	fp->vis *= vis;
	if (nullptr == result.O && fis_zero(vis))
	{
		CDB::TRI* T = g_pGameLevel->ObjectSpace.GetStaticTris() + result.element;
		Fvector* V = g_pGameLevel->ObjectSpace.GetStaticVerts();
		fp->item->Cache.verts[0].set(V[T->verts[0]]);
		fp->item->Cache.verts[1].set(V[T->verts[1]]);
		fp->item->Cache.verts[2].set(V[T->verts[2]]);
	}
	return (fp->vis > fp->vis_threshold);
}

#ifdef SPATIAL_CHANGE
IC BOOL feel_vision_test_callback(const collide::ray_defs& rd, CObject* object, LPVOID user_data)
{
	/* Return FALSE to see through object. */
	if (object->SpatialComponent->spatial.type & STYPE_FEELVISIONIGNORE)
	{
		return FALSE;
	}
	return TRUE;
}
#endif

void Vision::o_new(CObject* O)
{
	xrSRWLockGuard guard(&lock_visible, false);
	feel_visible.push_back(feel_visible_Item());
	feel_visible_Item& I = feel_visible.back();
	I.O = O;
	I.Cache_vis = 1.f;
	I.Cache.verts[0].set(0, 0, 0);
	I.Cache.verts[1].set(0, 0, 0);
	I.Cache.verts[2].set(0, 0, 0);
	I.fuzzy = -EPS_S;
	I.cp_LP = O->Position();
	I.cp_LAST = O->Position();
	I.bone_id = u16(-1);
}

void Vision::o_delete(CObject* O)
{
	xrSRWLockGuard guard(&lock_visible, false);
	auto it = std::find_if(feel_visible.begin(), feel_visible.end(),
		[O](const feel_visible_Item& item) { return item.O == O; });

	if (it != feel_visible.end()) {
		feel_visible.erase_fast(it);
	}
}

void Vision::feel_vision_clear()
{
	{
		xrSRWLockGuard guard(&lock_query, false);
		seen.clear();
		query.clear();
		diff.clear();
	}
	{
		xrSRWLockGuard guard(&lock_visible, false);
		feel_visible.clear();
	}
}

void Vision::feel_vision_relcase(CObject* object)
{
	{
		xrSRWLockGuard guard(&lock_query, false);
		xr_vector<CObject*>::iterator Io;
		Io = std::find(seen.begin(), seen.end(), object);
		if (Io != seen.end())seen.erase_fast(Io);
		Io = std::find(query.begin(), query.end(), object);
		if (Io != query.end())query.erase_fast(Io);
		Io = std::find(diff.begin(), diff.end(), object);
		if (Io != diff.end()) diff.erase_fast(Io);
	}
	{
		xrSRWLockGuard guard(&lock_visible, false);
		auto it = std::find_if(feel_visible.begin(), feel_visible.end(),
			[object](const feel_visible_Item& item) { return item.O == object; });

		if (it != feel_visible.end()) {
			feel_visible.erase_fast(it);
		}
	}
}

void Vision::feel_vision_query(Fmatrix& mFull, Fvector& P)
{
	xrSRWLockGuard guard(&lock_query, false);
	
	Frustum.CreateFromMatrix(mFull, FRUSTUM_P_LRTB | FRUSTUM_P_FAR);

	// Traverse object database
	r_spatial.clear();
	g_SpatialSpace->q_frustum
	(
		r_spatial,
		0,
		STYPE_VISIBLEFORAI,
		Frustum
	);

	// Determine visibility for dynamic part of scene
	seen.clear_and_reserve();
	for (u32 o_it = 0; o_it < r_spatial.size(); o_it++)
	{
		ISpatialShared& spatial = r_spatial[o_it];
		CObject* object = spatial->dcast_CObject();
		if (object && feel_vision_isRelevant(object)) seen.push_back(object);
	}
	if (seen.size() > 1)
	{
		std::sort(seen.begin(), seen.end());
		xr_vector<CObject*>::iterator end = std::unique(seen.begin(), seen.end());
		if (end != seen.end()) seen.erase(end, seen.end());
	}
}

void Vision::feel_vision_update(CObject* parent, Fvector& P, float dt, float vis_threshold, const VisionSnapshotList& snapshots)
{
	PROF_EVENT("feel_vision_update");
	{
		xrSRWLockGuard guard(&lock_query, false);
		// B-A = objects, that become visible
		if (!seen.empty())
		{
			xr_vector<CObject*>::iterator E = std::remove(seen.begin(), seen.end(), parent);
			seen.resize(E - seen.begin());

			{
				diff.resize(_max(seen.size(), query.size()));
				xr_vector<CObject*>::iterator	E_ = std::set_difference(
					seen.begin(), seen.end(),
					query.begin(), query.end(),
					diff.begin());
				diff.resize(E_ - diff.begin());
				for (u32 i = 0; i < diff.size(); i++)
					o_new(diff[i]);
			}
		}

		// A-B = objects, that are invisible
		if (!query.empty())
		{
			diff.resize(_max(seen.size(), query.size()));
			xr_vector<CObject*>::iterator	E = std::set_difference(
				query.begin(), query.end(),
				seen.begin(), seen.end(),
				diff.begin());
			diff.resize(E - diff.begin());
			for (u32 i = 0; i < diff.size(); i++)
				o_delete(diff[i]);
		}

		// Copy results and perform traces
		query = seen;
	}
	o_trace(P, dt, vis_threshold, snapshots);
}

void Vision::o_trace(Fvector& P, float dt, float vis_threshold, const VisionSnapshotList& snapshots)
{
	PROF_EVENT("feel_vision_o_trace");
	RQR.r_clear();
	xrSRWLockGuard guard(&lock_visible, true);
	xr_vector<feel_visible_Item>::iterator I = feel_visible.begin(), E = feel_visible.end();
	for (; I != E; I++)
	{
		const VisionSnapshotItem* pSnap = nullptr;
		for (const auto& s : snapshots) {
			if (s.Object == I->O) {
				pSnap = &s;
				break;
			}
		}

		bool HasCFORM = pSnap ? pSnap->HasCFORM : (I->O->CFORM() != nullptr);

		if (!HasCFORM) {
			I->fuzzy = -1;
			continue;
		}

		if (pSnap)
		{
			I->cp_LR_dst = pSnap->Position;
			I->cp_LAST = pSnap->cp_LAST;
		}
		else
		{
			Fvector pivot = I->O->Position();
			I->cp_LR_dst = pivot;
			I->cp_LAST = pivot;
		}
		I->cp_LR_src = P;

		//
		Fvector D, OP = I->cp_LAST;
		D.sub(OP, P);
		if (fis_zero(D.magnitude()))
		{
			I->fuzzy = 1.f;
			continue;
		}

		float f = D.magnitude() + .2f;
		if (f > fuzzy_guaranteed)
		{
			D.div(f);
			// setup ray defs & feel params
			collide::ray_defs RD(P, D, f, CDB::OPT_CULL,
				collide::rq_target(
					collide::rqtStatic | /**/collide::rqtObject | /**/collide::rqtObstacle));
			SFeelParam feel_params(this, &*I, vis_threshold);
			// check cache
			if (I->Cache.result && I->Cache.similar(P, D, f))
			{
				// similar with previous query
				feel_params.vis = I->Cache_vis;
				// Log("cache 0");
			}
			else
			{
				float _u, _v, _range;
				if (CDB::TestRayTri(P, D, I->Cache.verts, _u, _v, _range, false) && (_range > 0 && _range < f))
				{
					feel_params.vis = 0.f;
					// Log("cache 1");
				}
				else
				{
					// cache outdated. real query.
					VERIFY(!fis_zero(RD.dir.magnitude()));

#ifdef SPATIAL_CHANGE
					if (g_pGameLevel->ObjectSpace.RayQuery(RQR, RD, feel_vision_callback, &feel_params, feel_vision_test_callback, const_cast<CObject*>(m_owner)))
#else
					if (g_pGameLevel->ObjectSpace.RayQuery(RQR, RD, feel_vision_callback, &feel_params, NULL, const_cast<CObject*>(m_owner)))
#endif
					{
						I->Cache_vis = feel_params.vis;
						I->Cache.set(P, D, f, TRUE);
					}
					else
					{
						// feel_params.vis = 0.f;
						// I->Cache_vis = feel_params.vis ;
						I->Cache.set(P, D, f, FALSE);
					}
					// Log("query");
				}
			}
			// Log("Vis",feel_params.vis);
			r_spatial.clear_not_free();
			g_SpatialSpace->q_ray(r_spatial, 0, STYPE_VISIBLEFORAI, P, D, f);

			RD.flags = CDB::OPT_ONLYFIRST;

			bool collision_found = false;

			for (ISpatialShared& Ptr : r_spatial)
			{
				CObject const* object = Ptr->dcast_CObject();

				if (object == m_owner)
					continue;

				if (object == I->O)
					continue;

#ifdef SPATIAL_CHANGE
				if (object->SpatialComponent->spatial.type & STYPE_FEELVISIONIGNORE)
					/* See through objects that have the flag. */
					continue;
#endif
				
				RQR.r_clear();
				if (object && object->collidable.model && !object->collidable.model->_RayQuery(RD, RQR))
					continue;

				collision_found = true;
				break;
			}

			if (collision_found)
				feel_params.vis = 0.f;

			if (feel_params.vis < feel_params.vis_threshold)
			{
				// INVISIBLE, choose next point
				I->fuzzy -= fuzzy_update_novis * dt;
				clamp(I->fuzzy, -.5f, 1.f);
				if (pSnap)
				{
					I->cp_LP = pSnap->cp_LP;
					I->bone_id = pSnap->bone_id;
				}
				
			}
			else
			{
				// VISIBLE
				I->fuzzy += fuzzy_update_vis * dt;
				clamp(I->fuzzy, -.5f, 1.f);
			}
		}
		else
		{
			// VISIBLE, 'cause near
			I->fuzzy += fuzzy_update_vis * dt;
			clamp(I->fuzzy, -.5f, 1.f);
		}
	}
}
