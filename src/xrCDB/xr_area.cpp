#include "stdafx.h"

#include "xr_area.h"
#include "../xrengine/xr_object.h"
#include "../xrengine/xrLevel.h"
#include "../xrengine/xr_collide_form.h"

using namespace collide;

namespace
{
struct StaticCformPackage
{
	shared_str key;
	u64 identity = 0;
	Fbox bounds;
	CDB::MODEL model;
	bool materials_remapped = false;
};

xr_vector<StaticCformPackage*>& StaticCformCache()
{
	// Keep the cache alive until process termination. xrCore's allocators can be
	// destroyed before function-local static containers during engine shutdown.
	static xr_vector<StaticCformPackage*>* cache = xr_new<xr_vector<StaticCformPackage*>>();
	return *cache;
}

xrCriticalSection& StaticCformCacheLock()
{
	static xrCriticalSection* lock = xr_new<xrCriticalSection>();
	return *lock;
}

u64 CformIdentity(const CLocatorAPI::file& file)
{
	u64 identity = 1469598103934665603ull;
	auto mix = [&identity](u32 value)
	{
		identity ^= value;
		identity *= 1099511628211ull;
	};
	mix(file.crc);
	mix(file.size_real);
	mix(file.size_compressed);
	mix(file.modif);
	return identity;
}

void EvictStaticCformCacheUnderPressure()
{
	auto& cache = StaticCformCache();
	MEMORYSTATUSEX memory = {};
	memory.dwLength = sizeof(memory);
	while (!cache.empty() && GlobalMemoryStatusEx(&memory))
	{
		const u64 minimum_available = _max(2ull * 1024 * 1024 * 1024, memory.ullTotalPhys * 15 / 100);
		if (memory.ullAvailPhys >= minimum_available)
			break;
		StaticCformPackage* package = cache.front();
		Msg("* [LEVEL CACHE] CFORM evicted under memory pressure: %s", package->key.c_str());
		cache.erase(cache.begin());
		xr_delete(package);
	}
}
}

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
	Static.syncronize();
	if (m_static_cache_key.size())
	{
		StaticCformPackage* package = xr_new<StaticCformPackage>();
		package->key = m_static_cache_key;
		package->identity = m_static_cache_identity;
		package->bounds = m_BoundingVolume;
		package->materials_remapped = m_static_materials_remapped;
		package->model.swap(Static);

		xrCriticalSectionGuard guard(StaticCformCacheLock());
		auto& cache = StaticCformCache();
		for (auto it = cache.begin(); it != cache.end();)
		{
			if ((*it)->key == package->key)
			{
				StaticCformPackage* stale = *it;
				it = cache.erase(it);
				xr_delete(stale);
			}
			else
				++it;
		}
		cache.push_back(package);
		EvictStaticCformCacheUnderPressure();
	}

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

void CObjectSpace::PrepareStatic(LPCSTR level_path)
{
	if (!level_path || !level_path[0])
		return;
	xr_string full_path = level_path;
	if (full_path.back() != '\\' && full_path.back() != '/')
		full_path += '\\';
	full_path += "level.cform";
	const CLocatorAPI::file* file = FS.exist(full_path.c_str());
	if (!file)
		return;
	const shared_str key = file->name;
	const u64 identity = CformIdentity(*file);

	// This lock is also the future for an in-flight prepare: Load blocks here
	// only if the early ALife-overlapped build has not finished yet.
	xrCriticalSectionGuard guard(StaticCformCacheLock());
	auto& cache = StaticCformCache();
	for (auto it = cache.begin(); it != cache.end(); ++it)
	{
		StaticCformPackage* package = *it;
		if (package->key != key)
			continue;
		if (package->identity == identity)
			return;
		cache.erase(it);
		xr_delete(package);
		break;
	}

	IReader* reader = FS.r_open(full_path.c_str());
	R_ASSERT(reader);
	hdrCFORM header;
	reader->r(&header, sizeof(header));
	R_ASSERT(CFORM_CURRENT_VERSION == header.version);
	Fvector* vertices = static_cast<Fvector*>(reader->pointer());
	CDB::TRI* triangles = reinterpret_cast<CDB::TRI*>(vertices + header.vertcount);
	StaticCformPackage* package = xr_new<StaticCformPackage>();
	package->key = key;
	package->identity = identity;
	package->bounds = header.aabb;
	package->model.build(vertices, header.vertcount, triangles, header.facecount);
	FS.r_close(reader);
	cache.push_back(package);
	EvictStaticCformCacheUnderPressure();
	Msg("* [LEVEL PREPARE] CFORM ready: %s", key.c_str());
}

void CObjectSpace::Load(LPCSTR path, LPCSTR fname, CDB::build_callback build_callback)
{
#ifdef USE_ARENA_ALLOCATOR
	Msg( "CObjectSpace::Load, g_collision_allocator.get_allocated_size() - %d", int(g_collision_allocator.get_allocated_size()/1024.0/1024) );
#endif // #ifdef USE_ARENA_ALLOCATOR
	string_path resolved;
	const CLocatorAPI::file* file = FS.exist(resolved, path, fname);
	R_ASSERT(file);
	const shared_str key = file->name;
	const u64 identity = CformIdentity(*file);
	{
		xrCriticalSectionGuard guard(StaticCformCacheLock());
		auto& cache = StaticCformCache();
		for (auto it = cache.begin(); it != cache.end(); ++it)
		{
			StaticCformPackage* package = *it;
			if (package->key != key)
				continue;
			if (package->identity != identity)
			{
				cache.erase(it);
				xr_delete(package);
				break;
			}

			Static.swap(package->model);
			if (!package->materials_remapped && build_callback)
			{
				build_callback(Static.get_verts(), Static.get_verts_count(), Static.get_tris(),
					Static.get_tris_count(), nullptr);
				package->materials_remapped = true;
			}
			m_BoundingVolume = package->bounds;
			m_static_cache_key = key;
			m_static_cache_identity = identity;
			m_static_materials_remapped = package->materials_remapped;
			cache.erase(it);
			xr_delete(package);
			g_SpatialSpace->initialize(m_BoundingVolume);
			g_SpatialSpacePhysic->initialize(m_BoundingVolume);
			g_SpatialSpaceLights->initialize(m_BoundingVolume);
			Msg("* [LEVEL CACHE] CFORM restored: %s", key.c_str());
			return;
		}
	}

	m_static_cache_key = key;
	m_static_cache_identity = identity;
	m_static_materials_remapped = build_callback != nullptr;
	IReader* F = FS.r_open(resolved);
	R_ASSERT(F);
	Load(F, build_callback);
}

void CObjectSpace::Load(IReader* F, CDB::build_callback build_callback)
{
	Static.async_cform_load.wait();

	hdrCFORM H;
	F->r(&H, sizeof(hdrCFORM));
	R_ASSERT(CFORM_CURRENT_VERSION == H.version);
	m_BoundingVolume.set(H.aabb);

	g_SpatialSpace->initialize(m_BoundingVolume);
	g_SpatialSpacePhysic->initialize(m_BoundingVolume);
	g_SpatialSpaceLights->initialize(m_BoundingVolume);

	static DWORD this_thread_id = 0;
	this_thread_id = GetCurrentThreadId();
	Static.async_cform_load.run([=]()
	{
		if (this_thread_id != GetCurrentThreadId()) { PROF_THREAD("X-Ray PPL Thread") }
		PROF_EVENT("Async cform loading");
		Fvector* verts = (Fvector*)F->pointer();
		CDB::TRI* tris = (CDB::TRI*)(verts + H.vertcount);
		Create(verts, tris, H, build_callback, false);
		IReader* reader = F;
		FS.r_close(reader);
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
