#pragma once

//#include "xr_collide_form.h"
#include "xr_collide_defs.h"

#include "../xrCore/intrusive_ptr.h"

// refs
class ISpatial;
using ISpatialShared = intrusive_ptr<ISpatial>;

class ICollisionForm;
class CObject;

#include "../Include/xrRender/FactoryPtr.h"
#include "../Include/xrRender/ObjectSpaceRender.h"
#include "xrXRC.h"

#include "xrcdb.h"

//-----------------------------------------------------------------------------------------------------------
//Space Area
//-----------------------------------------------------------------------------------------------------------
struct hdrCFORM;

class XRCDB_API CObjectSpace
{
private:
	CDB::MODEL Static;
	Fbox m_BoundingVolume;
	shared_str m_static_cache_key;
	u64 m_static_cache_identity = 0;
	bool m_static_materials_remapped = false;
public:
	static void PrepareStatic(LPCSTR level_path);
#ifdef DEBUG
	FactoryPtr<IObjectSpaceRender> *m_pRender;
#endif
private:
	BOOL _RayTest(const Fvector& start, const Fvector& dir, float range, collide::rq_target tgt,
	              collide::ray_cache* cache, CObject* ignore_object);
	BOOL _RayPick(const Fvector& start, const Fvector& dir, float range, collide::rq_target tgt, collide::rq_result& R,
	              CObject* ignore_object);
	BOOL _RayPick(const Fvector& start, const Fvector& dir, float range, collide::rq_target tgt, collide::rq_result& R,
				  xr_vector<CObject*>& ignore_objects);
	BOOL _RayQuery(collide::rq_results& dest, const collide::ray_defs& rq, collide::rq_callback* cb, LPVOID user_data,
	               collide::test_callback* tb, CObject* ignore_object);
	BOOL _RayQuery2(collide::rq_results& dest, const collide::ray_defs& rq, collide::rq_callback* cb, LPVOID user_data,
	                collide::test_callback* tb, CObject* ignore_object);
	BOOL _RayQuery3(collide::rq_results& dest, const collide::ray_defs& rq, collide::rq_callback* cb, LPVOID user_data,
	                collide::test_callback* tb, CObject* ignore_object);
public:
	CObjectSpace();
	~CObjectSpace();

	void Load(CDB::build_callback build_callback);
	void Load(LPCSTR path, LPCSTR fname, CDB::build_callback build_callback);
	void Load(IReader* R, CDB::build_callback build_callback);
	void Create(Fvector* verts, CDB::TRI* tris, const hdrCFORM& H, CDB::build_callback build_callback, bool init_bounds = true);
	// Occluded/No
	BOOL RayTest(const Fvector& start, const Fvector& dir, float range, collide::rq_target tgt,
	             collide::ray_cache* cache, CObject* ignore_object);

	// Game raypick (nearest) - returns object and addititional params
	BOOL RayPick(const Fvector& start, const Fvector& dir, float range, collide::rq_target tgt, collide::rq_result& R,
	             CObject* ignore_object);
	BOOL RayPick(const Fvector& start, const Fvector& dir, float range, collide::rq_target tgt, collide::rq_result& R,
				 xr_vector<CObject*>& ignore_objects);

	// General collision query
	BOOL RayQuery(collide::rq_results& dest, const collide::ray_defs& rq, collide::rq_callback* cb, LPVOID user_data,
	              collide::test_callback* tb, CObject* ignore_object);
	BOOL RayQuery(collide::rq_results& dest, ICollisionForm* target, const collide::ray_defs& rq);

	bool BoxQuery(Fvector const& box_center,
	              Fvector const& box_z_axis,
	              Fvector const& box_y_axis,
	              Fvector const& box_sizes,
	              xr_vector<Fvector>* out_tris);

	int GetNearest(xr_vector<CObject*>& q_nearest, ICollisionForm* obj, float range);
	int GetNearest(xr_vector<CObject*>& q_nearest, const Fvector& point, float range, CObject* ignore_object);
	int GetNearest(xr_vector<ISpatialShared>& q_spatial, xr_vector<CObject*>& q_nearest, const Fvector& point, float range,
	               CObject* ignore_object);

	CDB::TRI* GetStaticTris() { return Static.get_tris(); }
	Fvector* GetStaticVerts() { return Static.get_verts(); }
	CDB::MODEL* GetStaticModel() { return &Static; }

	const Fbox& GetBoundingVolume() { return m_BoundingVolume; }

	// Debugging
#ifdef DEBUG
	void dbgRender();
#endif
};
