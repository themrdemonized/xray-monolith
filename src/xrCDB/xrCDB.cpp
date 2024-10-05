// xrCDB.cpp : Defines the entry point for the DLL application.
//

#include "stdafx.h"
#pragma hdrstop

#include "xrCDB.h"

#ifdef USE_ARENA_ALLOCATOR
static const u32	s_arena_size = (128+16)*1024*1024;
static char			s_fake_array[s_arena_size];
//doug_lea_allocator	g_collision_allocator( s_fake_array, s_arena_size, "collision" );
#endif // #ifdef USE_ARENA_ALLOCATOR

namespace Opcode
{
#	include "OPC_TreeBuilders.h"
} // namespace Opcode

using namespace CDB;
using namespace Opcode;

//BOOL APIENTRY DllMain( HANDLE hModule, 
BOOL DllMainIgnore1(HANDLE hModule,
                    u32 ul_reason_for_call,
                    LPVOID lpReserved
)
{
	switch (ul_reason_for_call)
	{
	case DLL_PROCESS_ATTACH:
	case DLL_THREAD_ATTACH:
	case DLL_THREAD_DETACH:
	case DLL_PROCESS_DETACH:
		break;
	}
	return TRUE;
}

// Model building
MODEL::MODEL()
#ifdef PROFILE_CRITICAL_SECTIONS
	:cs(MUTEX_PROFILE_ID(MODEL))
#endif // PROFILE_CRITICAL_SECTIONS
{
	tree = 0;
	tris = 0;
	tris_count = 0;
	verts = 0;
	verts_count = 0;
	status = S_INIT;
}

MODEL::~MODEL()
{
	syncronize(); // maybe model still in building
	status = S_INIT;
	CDELETE(tree);
	CFREE(tris);
	tris_count = 0;
	CFREE(verts);
	verts_count = 0;
}

struct BTHREAD_params
{
	MODEL* M;
	Fvector* V;
	int Vcnt;
	TRI* T;
	int Tcnt;
	build_callback* BC;
	void* BCP;
};

void MODEL::build_thread(void* params)
{
	_initialize_cpu_thread();
	FPU::m64r();
	BTHREAD_params P = *((BTHREAD_params*)params);
	P.M->cs.Enter();
	P.M->build_internal(P.V, P.Vcnt, P.T, P.Tcnt, P.BC, P.BCP);
	P.M->status = S_READY;
	P.M->cs.Leave();
	//Msg						("* xrCDB: cform build completed, memory usage: %d K",P.M->memory()/1024);
}

#include "..\xrCore\stream_reader.h"

u64 crc64(void* p, u64 mem_size) {
	u8* temp = (u8*)p;

	u64 crc = 0;
	u64 size = mem_size;
	size_t umem = u64(u32(-1));

	while(mem_size > umem) {
		crc += (u64)crc32(temp, umem);
		mem_size -= umem;
		temp += umem;
	}

	crc += crc32(temp, mem_size);

	return crc;
}

void MODEL::build(Fvector* V, int Vcnt, TRI* T, int Tcnt, build_callback* bc, void* bcp)
{
	auto serialize = [&](pcstr fileName) {
		IWriter* wstream = FS.w_open(fileName);
		if(!wstream)
			return false;

		wstream->w_u32(verts_count);
		wstream->w(verts, sizeof(Fvector) * verts_count);
		wstream->w_u32(tris_count);
		wstream->w(tris, sizeof(TRI) * tris_count);

		if(tree) {
			tree->Save(wstream);
		}

		FS.w_close(wstream);
		return !!tree;
	};

	auto deserialize = [&](pcstr fileName) {
		auto rstream = FS.rs_open(0, fileName);
		if(!rstream)
			return false;

		CFREE(verts);
		CFREE(tris);
		CFREE(tree);

		verts_count = rstream->r_u32();
		verts = CALLOC(Fvector, verts_count);
		const u32 vertsSize = verts_count * sizeof(Fvector);
		rstream->r(verts, vertsSize);

		tris_count = rstream->r_u32();
		tris = CALLOC(TRI, tris_count);
		const u32 trisSize = tris_count * sizeof(TRI);
		rstream->r(tris, trisSize);

		//verts_count = Vcnt;
		//verts = CALLOC(Fvector, verts_count);
		//CopyMemory(verts, V, verts_count * sizeof(Fvector));

		//// tris
		//tris_count = Tcnt;
		//tris = CALLOC(TRI, tris_count);
		//CopyMemory(tris, T, tris_count * sizeof(TRI));

		//// callback
		//if(bc) bc(verts, Vcnt, tris, Tcnt, bcp);

		tree = CNEW(OPCODE_Model)();
		tree->Load(rstream);
		FS.r_close(rstream);
		status = S_READY;
		return true;
	};

	R_ASSERT(S_INIT == status);
	R_ASSERT((Vcnt>=4)&&(Tcnt>=2));

	_initialize_cpu_thread();

	static bool disable_cdb_chace = !!strstr(Core.Params, "-no_cdb");

	if(disable_cdb_chace) {
		build_internal(V, Vcnt, T, Tcnt, bc, bcp);
		status = S_READY;
		
		return;
	}

	std::string hashed_name = "";
	hashed_name += std::to_string(Vcnt) + "_";
	hashed_name += std::to_string(Tcnt) + "_";
	hashed_name += std::to_string(crc64(V, sizeof(Fvector) * Vcnt)) + "_";
	hashed_name += std::to_string(crc64(T, sizeof(TRI) * Tcnt));

	string_path fName{};
	strconcat(sizeof(fName), fName, "cform_cache\\", FS.get_path("$level$")->m_Add, hashed_name.c_str(), ".cdb");
	FS.update_path(fName, "$app_data_root$", fName);

	if(!FS.exist(fName)) {
		Msg("* ObjectSpace cache for '%s' not found. Building the model from scratch..", fName);

		build_internal(V, Vcnt, T, Tcnt, bc, bcp);
		status = S_READY;

		serialize(fName);
	}
	else {
		deserialize(fName);
	}
}

void MODEL::build_internal(Fvector* V, int Vcnt, TRI* T, int Tcnt, build_callback* bc, void* bcp)
{
	// verts
	verts_count = Vcnt;
	verts = CALLOC(Fvector, verts_count);
	CopyMemory(verts, V, verts_count*sizeof(Fvector));

	// tris
	tris_count = Tcnt;
	tris = CALLOC(TRI, tris_count);
	CopyMemory(tris, T, tris_count*sizeof(TRI));

	// callback
	if (bc) bc(verts, Vcnt, tris, Tcnt, bcp);

	// Release data pointers
	status = S_BUILD;

	// Allocate temporary "OPCODE" tris + convert tris to 'pointer' form
	u32* temp_tris = CALLOC(u32, tris_count*3);
	if (0 == temp_tris)
	{
		CFREE(verts);
		CFREE(tris);
		return;
	}
	u32* temp_ptr = temp_tris;
	for (int i = 0; i < tris_count; i++)
	{
		*temp_ptr++ = tris[i].verts[0];
		*temp_ptr++ = tris[i].verts[1];
		*temp_ptr++ = tris[i].verts[2];
	}

	// Build a non quantized no-leaf tree
	OPCODECREATE OPCC;
	OPCC.NbTris = tris_count;
	OPCC.NbVerts = verts_count;
	OPCC.Tris = (unsigned*)temp_tris;
	OPCC.Verts = (Point*)verts;
	OPCC.Rules = SPLIT_COMPLETE | SPLIT_SPLATTERPOINTS | SPLIT_GEOMCENTER;
	OPCC.NoLeaf = true;
	OPCC.Quantized = false;
	// if (Memory.debug_mode) OPCC.KeepOriginal = true;

	tree = CNEW(OPCODE_Model)();
	if (!tree->Build(OPCC))
	{
		CFREE(verts);
		CFREE(tris);
		CFREE(temp_tris);
		return;
	};

	// Free temporary tris
	CFREE(temp_tris);
	return;
}

u32 MODEL::memory()
{
	if (S_BUILD == status)
	{
		Msg("! xrCDB: model still isn't ready");
		return 0;
	}
	u32 V = verts_count * sizeof(Fvector);
	u32 T = tris_count * sizeof(TRI);
	return tree->GetUsedBytes() + V + T + sizeof(*this) + sizeof(*tree);
}

// This is the constructor of a class that has been exported.
// see xrCDB.h for the class definition
COLLIDER::COLLIDER()
{
	ray_mode = 0;
	box_mode = 0;
	frustum_mode = 0;
}

COLLIDER::~COLLIDER()
{
	r_free();
}

RESULT& COLLIDER::r_add()
{
	rd.push_back(RESULT());
	return rd.back();
}

void COLLIDER::r_free()
{
	rd.clear_and_free();
}
