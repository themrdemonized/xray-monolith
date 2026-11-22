#include "stdafx.h"

#include "xr_area.h"
#include "../xrengine/xr_object.h"
#include "../xrengine/xrLevel.h"
#include "../xrengine/xr_collide_form.h"

using namespace collide;

//----------------------------------------------------------------------
// Class	: CObjectSpace
// Purpose	: stores space slots
//----------------------------------------------------------------------
CObjectSpace::CObjectSpace()
#ifdef DEBUG
	,m_pRender(0)
#endif
{
#ifdef DEBUG
	if( RenderFactory )	
		m_pRender = CNEW(FactoryPtr<IObjectSpaceRender>)() ;

	//sh_debug.create				("debug\\wireframe","$null");
#endif
	m_BoundingVolume.invalidate();
}

//----------------------------------------------------------------------
CObjectSpace::~CObjectSpace()
{
	//moved to ~IGameLevel
	//	Sound->set_geometry_occ		(NULL);
	//	Sound->set_handler			(NULL);
	//
#ifdef DEBUG
	//sh_debug.destroy			();
	CDELETE(m_pRender);
#endif
}

//----------------------------------------------------------------------

//----------------------------------------------------------------------
int CObjectSpace::GetNearest(xr_vector<ISpatialShared>& q_spatial, xr_vector<CObject*>& q_nearest, const Fvector& point, float range, CObject* ignore_object)
{
	q_spatial.clear_not_free();
	// Query objects
	q_nearest.clear_not_free();
	Fsphere Q;
	Q.set(point, range);
	Fvector B;
	B.set(range, range, range);
	g_SpatialSpace->q_box(q_spatial, 0, STYPE_COLLIDEABLE, point, B);

	// Iterate
	auto it = q_spatial.begin();
	auto end = q_spatial.end();
	for (; it != end; it++)
	{
		CObject* O = (*it)->dcast_CObject();
		if (0 == O)
			continue;

		if (O == ignore_object)
			continue;

		Fsphere mS = { O->SpatialComponent->spatial.sphere.P, O->SpatialComponent->spatial.sphere.R };
		if (Q.intersect(mS))
			q_nearest.push_back(O);
	}

	return (int)q_nearest.size();
}

//----------------------------------------------------------------------
int CObjectSpace::GetNearest(xr_vector<CObject*>& q_nearest, ICollisionForm* obj, float range)
{
	CObject* O = obj->Owner();
	return GetNearest(q_nearest, O->SpatialComponent->spatial.sphere.P, range + O->SpatialComponent->spatial.sphere.R, O);
}

//----------------------------------------------------------------------


void CObjectSpace::Load(CDB::build_callback build_callback)
{
	Load("$level$", "level.cform", build_callback);
}

// Level preload: the static collision of a level the player is about to enter, read and built by
// a background task during play. The task also opens the file: from an archive that means
// unpacking tens of MB. The list is used from the main thread, and by Load, which in single player
// runs in the level load task while the main thread loads the ALife objects and leaves the list
// alone; Load only marks a job taken, the main thread frees it. A job's fields other than
// `finished` belong to its task until `finished` is set.
struct SPrebuiltStatic
{
	string_path file;
	hdrCFORM header;
	CDB::MODEL model; // ready once built, and again not once Load has taken it
	float build_ms = 0.f;
	bool released = false; // not wanted any more: freed once its task has finished
	xr_atomic_bool finished{false};
	xr_task_group task;
};
static xr_vector<SPrebuiltStatic*> s_prebuilt;

// A damaged file must not take the game down before the player even enters its level:
// the normal load reports it.
static bool valid_static(IReader* F, const hdrCFORM& H)
{
	if (CFORM_CURRENT_VERSION != H.version || H.vertcount < 4 || H.facecount < 2)
		return false;
	const u64 size = sizeof(hdrCFORM) + u64(H.vertcount) * sizeof(Fvector) + u64(H.facecount) * sizeof(CDB::TRI);
	if (u64(F->length()) < size)
		return false;
	const CDB::TRI* tris = (const CDB::TRI*)((const Fvector*)F->pointer() + H.vertcount);
	for (u32 i = 0; i < H.facecount; ++i)
		if (tris[i].verts[0] >= H.vertcount || tris[i].verts[1] >= H.vertcount || tris[i].verts[2] >= H.vertcount)
			return false;
	return true;
}

void CObjectSpace::prebuild_static(LPCSTR cform_file)
{
	prebuild_collect(); // frees released jobs that have finished, such as one the load took
	for (SPrebuiltStatic* it : s_prebuilt)
	{
		if (0 == _stricmp(it->file, cform_file))
		{
			it->released = false;
			return;
		}
	}
	SPrebuiltStatic* job = xr_new<SPrebuiltStatic>();
	xr_strcpy(job->file, cform_file);
	s_prebuilt.push_back(job);
	job->task.run([job]()
	{
		PROF_EVENT("Level preload cform build");
		CTimer timer;
		timer.Start();
		if (IReader* F = FS.r_open(job->file))
		{
			if (u32(F->length()) >= sizeof(hdrCFORM))
			{
				F->r(&job->header, sizeof(hdrCFORM));
				if (valid_static(F, job->header))
				{
					Fvector* verts = (Fvector*)F->pointer();
					CDB::TRI* tris = (CDB::TRI*)(verts + job->header.vertcount);
					job->model.build(verts, job->header.vertcount, tris, job->header.facecount);
				}
			}
			FS.r_close(F);
		}
		job->build_ms = timer.GetElapsed_sec() * 1000.f;
		job->finished = true;
	});
}

void CObjectSpace::prebuild_release(LPCSTR cform_file)
{
	for (SPrebuiltStatic* it : s_prebuilt)
		if (!cform_file || 0 == _stricmp(it->file, cform_file))
			it->released = true;
	prebuild_collect();
}

void CObjectSpace::prebuild_collect(bool wait)
{
	for (u32 i = 0; i < s_prebuilt.size();)
	{
		SPrebuiltStatic* job = s_prebuilt[i];
		if (!job->released || (!wait && !job->finished))
		{
			++i;
			continue;
		}
		job->task.wait();
		xr_delete(job);
		s_prebuilt.erase(s_prebuilt.begin() + i);
	}
}

// The build callback runs after the tree is built here, not before as in Load: it may change
// what a triangle carries (material, flags, sector), not vertices or indices. It runs before
// Static takes the model, so no query sees the triangles change.
bool CObjectSpace::take_prebuilt_static(LPCSTR cform_file, CDB::build_callback build_callback)
{
	for (SPrebuiltStatic* job : s_prebuilt)
	{
		if (job->released || _stricmp(job->file, cform_file))
			continue;
		// Still building: the normal load builds its tree in the background as well, while the
		// rest of the level loads, so it does not wait for this one.
		job->released = true;
		if (!job->finished || !job->model.ready())
			break;
		Msg("* level preload: static collision was built ahead in %.0f ms", job->build_ms);
		m_BoundingVolume.set(job->header.aabb);
		g_SpatialSpace->initialize(m_BoundingVolume);
		g_SpatialSpacePhysic->initialize(m_BoundingVolume);
		g_SpatialSpaceLights->initialize(m_BoundingVolume);
		if (build_callback)
			build_callback(job->model.get_verts(), job->model.get_verts_count(), job->model.get_tris(),
			               job->model.get_tris_count(), NULL);
		Static.take_over(job->model);
		return true; // freed by the next prebuild_collect on the main thread
	}
	return false;
}

void CObjectSpace::Load(LPCSTR path, LPCSTR fname, CDB::build_callback build_callback)
{
#ifdef USE_ARENA_ALLOCATOR
	Msg( "CObjectSpace::Load, g_collision_allocator.get_allocated_size() - %d", int(g_collision_allocator.get_allocated_size()/1024.0/1024) );
#endif // #ifdef USE_ARENA_ALLOCATOR
	string_path file;
	FS.update_path(file, path, fname);
	if (take_prebuilt_static(file, build_callback))
		return;
	IReader* F = FS.r_open(path, fname);
	R_ASSERT(F);
	Load(F, build_callback);
}

void CObjectSpace::Load(IReader* F, CDB::build_callback build_callback)
{
	hdrCFORM H;
	F->r(&H, sizeof(hdrCFORM));
	R_ASSERT(CFORM_CURRENT_VERSION == H.version);
	m_BoundingVolume.set(H.aabb);

	g_SpatialSpace->initialize(m_BoundingVolume);
	g_SpatialSpacePhysic->initialize(m_BoundingVolume);
	g_SpatialSpaceLights->initialize(m_BoundingVolume);

	// Copy the geometry out of the reader on this thread, so GetStaticVerts() and
	// GetStaticTris() return usable arrays before Load returns. The reader can then
	// be closed here instead of from the task.
	Fvector* verts = (Fvector*)F->pointer();
	CDB::TRI* tris = (CDB::TRI*)(verts + H.vertcount);
	Static.build_arrays(verts, H.vertcount, tris, H.facecount, build_callback);
	FS.r_close(F);

	// Only the OPCODE tree build goes to the background. It is the expensive half,
	// so the overlap with the rest of the level load is kept. The four syncronize()
	// calls in the CDB query paths already cover the tree.
	const DWORD this_thread_id = GetCurrentThreadId();
	Static.async_cform_load.run([this, this_thread_id]()
	{
		if (this_thread_id != GetCurrentThreadId()) { PROF_THREAD("X-Ray PPL Thread") }
		PROF_EVENT("Async cform tree build");
		Static.build_tree();
	});
}

void CObjectSpace::Create(Fvector* verts, CDB::TRI* tris, const hdrCFORM& H, CDB::build_callback build_callback, bool init_bounds)
{
	Static.build(verts, H.vertcount, tris, H.facecount, build_callback);

	if (init_bounds)
	{
		m_BoundingVolume.set(H.aabb);

		g_SpatialSpace->initialize(m_BoundingVolume);
		g_SpatialSpacePhysic->initialize(m_BoundingVolume);
		g_SpatialSpaceLights->initialize(m_BoundingVolume);
	}
}

//----------------------------------------------------------------------
#ifdef DEBUG
void CObjectSpace::dbgRender()
{
	(*m_pRender)->dbgRender();
}
/*
void CObjectSpace::dbgRender()
{
	R_ASSERT(bDebug);

	RCache.set_Shader(sh_debug);
	for (u32 i=0; i<q_debug.boxes.size(); i++)
	{
		Fobb&		obb		= q_debug.boxes[i];
		Fmatrix		X,S,R;
		obb.xform_get(X);
		RCache.dbg_DrawOBB(X,obb.m_halfsize,D3DCOLOR_XRGB(255,0,0));
		S.scale		(obb.m_halfsize);
		R.mul		(X,S);
		RCache.dbg_DrawEllipse(R,D3DCOLOR_XRGB(0,0,255));
	}
	q_debug.boxes.clear();

	for (i=0; i<dbg_S.size(); i++)
	{
		std::pair<Fsphere,u32>& P = dbg_S[i];
		Fsphere&	S = P.first;
		Fmatrix		M;
		M.scale		(S.R,S.R,S.R);
		M.translate_over(S.P);
		RCache.dbg_DrawEllipse(M,P.second);
	}
	dbg_S.clear();
}
*/
#endif
