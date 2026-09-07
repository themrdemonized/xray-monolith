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

void CObjectSpace::Load(LPCSTR path, LPCSTR fname, CDB::build_callback build_callback)
{
#ifdef USE_ARENA_ALLOCATOR
	Msg( "CObjectSpace::Load, g_collision_allocator.get_allocated_size() - %d", int(g_collision_allocator.get_allocated_size()/1024.0/1024) );
#endif // #ifdef USE_ARENA_ALLOCATOR
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
