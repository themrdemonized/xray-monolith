#include "stdafx.h"
#pragma hdrstop

#pragma warning(disable:4995)
#include <d3dx9.h>
#ifndef _EDITOR
#pragma comment( lib, "d3dx9.lib"		)
#include "../../xrEngine/render.h"
#endif
#pragma warning(default:4995)

#include <D3DX10Core.h>

#include "../xrRender/ResourceManager.h"
#include "../xrRender/tss.h"
#include "../xrRender/blenders/blender.h"
#include "../xrRender/blenders/blender_recorder.h"

#include "../xrRenderDX10/dx10BufferUtils.h"
#include "../xrRenderDX10/dx10ConstantBuffer.h"

#include "../xrRender/ShaderResourceTraits.h"

#ifdef USE_DX11
SHS* CResourceManager::_CreateHS(LPCSTR Name, ref_hs* keep_alive)
{
	return CreateShader<SHS>(Name, keep_alive);
}

void CResourceManager::_DeleteHS(const SHS* HS)
{
	DestroyShader(HS);
}

SDS* CResourceManager::_CreateDS(LPCSTR Name, ref_ds* keep_alive)
{
	return CreateShader<SDS>(Name, keep_alive);
}

void CResourceManager::_DeleteDS(const SDS* DS)
{
	DestroyShader(DS);
}

SCS* CResourceManager::_CreateCS(LPCSTR Name, ref_cs* keep_alive)
{
	return CreateShader<SCS>(Name, keep_alive);
}

void CResourceManager::_DeleteCS(const SCS* CS)
{
	DestroyShader(CS);
}
#endif	//	USE_DX10

void fix_texture_name(LPSTR fn);

static xrCriticalSection shaderCreationGuards[64];

static xrCriticalSection& shader_creation_guard(LPCSTR name)
{
	u32 hash = 2166136261u;
	for (; *name; ++name)
		hash = (hash ^ u8(*name)) * 16777619u;
	return shaderCreationGuards[hash % std::size(shaderCreationGuards)];
}

template <class T>
BOOL reclaim(xr_vector<T*>& vec, const T* ptr)
{
	xr_vector<T*>::iterator it = vec.begin();
	xr_vector<T*>::iterator end = vec.end();
	for (; it != end; it++)
		if (*it == ptr)
		{
			vec.erase(it);
			return TRUE;
		}
	return FALSE;
}

template <typename T>
void remove_indexed(xr_unordered_flat_map<u64, xr_vector<T*>>& index, u64 hash, const T* value)
{
	auto bucket = index.find(hash);
	if (bucket == index.end())
		return;
	auto& values = bucket->second;
	values.erase(std::remove(values.begin(), values.end(), value), values.end());
	if (values.empty())
		index.erase(bucket);
}

static u64 hash_pointer(u64 hash, const void* pointer)
{
	const uintptr_t value = reinterpret_cast<uintptr_t>(pointer);
	for (u32 i = 0; i < sizeof(value); ++i)
	{
		hash ^= static_cast<u8>(value >> (i * 8));
		hash *= 1099511628211ull;
	}
	return hash;
}

static u64 pass_hash(const SPass& pass)
{
	u64 hash = 1469598103934665603ull;
	hash = hash_pointer(hash, pass.state._get());
	hash = hash_pointer(hash, pass.ps._get());
	hash = hash_pointer(hash, pass.vs._get());
	hash = hash_pointer(hash, pass.gs._get());
#ifdef USE_DX11
	hash = hash_pointer(hash, pass.hs._get());
	hash = hash_pointer(hash, pass.ds._get());
	hash = hash_pointer(hash, pass.cs._get());
#endif
	hash = hash_pointer(hash, pass.constants._get());
	hash = hash_pointer(hash, pass.T._get());
	hash = hash_pointer(hash, pass.C._get());
#ifdef _EDITOR
	hash = hash_pointer(hash, pass.M._get());
#endif
	return hash;
}

static u64 texture_list_hash(const STextureList& list)
{
	u64 hash = 1469598103934665603ull;
	for (const auto& entry : list)
	{
		hash ^= entry.first;
		hash *= 1099511628211ull;
		hash = hash_pointer(hash, entry.second._get());
	}
	return hash;
}

//--------------------------------------------------------------------------------------------------------------
SState* CResourceManager::_CreateState(SimulatorStates& state_code, ref_state* keep_alive)
{
	xrCriticalSectionGuard guard(creationGuard);
	const u64 hash = state_code.hash();
	auto& candidates = m_state_index[hash];
	for (SState* candidate : candidates)
		if (candidate->state_code.equal(state_code))
		{
			if (keep_alive)
				*keep_alive = candidate;
			return candidate;
		}

	// Create New
	v_states.push_back(xr_new<SState>());
	v_states.back()->dwFlags |= xr_resource_flagged::RF_REGISTERED;
#if defined(USE_DX10) || defined(USE_DX11)
	v_states.back()->state = ID3DState::Create(state_code);
#else	//	USE_DX10
	v_states.back()->state			= state_code.record();
#endif	//	USE_DX10
	v_states.back()->state_code = state_code;
	candidates.push_back(v_states.back());
	if (keep_alive)
		*keep_alive = v_states.back();
	return v_states.back();
}

void CResourceManager::_DeleteState(const SState* state)
{
	if (0 == (state->dwFlags & xr_resource_flagged::RF_REGISTERED)) return;
	xrCriticalSectionGuard guard(creationGuard);
	remove_indexed(m_state_index, state->state_code.hash(), state);
	if (reclaim(v_states, state)) return;
	Msg("! ERROR: Failed to find compiled stateblock");
}

//--------------------------------------------------------------------------------------------------------------
SPass* CResourceManager::_CreatePass(const SPass& proto, ref_pass* keep_alive)
{
	xrCriticalSectionGuard guard(creationGuard);
	const u64 hash = pass_hash(proto);
	auto& candidates = m_pass_index[hash];
	for (SPass* candidate : candidates)
		if (candidate->equal(proto))
		{
			if (keep_alive)
				*keep_alive = candidate;
			return candidate;
		}

	SPass* P = xr_new<SPass>();
	P->dwFlags |= xr_resource_flagged::RF_REGISTERED;
	P->state = proto.state;
	P->ps = proto.ps;
	P->vs = proto.vs;
	P->gs = proto.gs;
#ifdef USE_DX11
	P->hs = proto.hs;
	P->ds = proto.ds;
	P->cs = proto.cs;
#endif
	P->constants = proto.constants;
	P->T = proto.T;
#ifdef _EDITOR
	P->M						=	proto.M;
#endif
	P->C = proto.C;

	v_passes.push_back(P);
	candidates.push_back(P);
	if (keep_alive)
		*keep_alive = P;
	return v_passes.back();
}

void CResourceManager::_DeletePass(const SPass* P)
{
	if (0 == (P->dwFlags & xr_resource_flagged::RF_REGISTERED)) return;
	xrCriticalSectionGuard guard(creationGuard);
	remove_indexed(m_pass_index, pass_hash(*P), P);
	if (reclaim(v_passes, P)) return;
	Msg("! ERROR: Failed to find compiled pass");
}

//--------------------------------------------------------------------------------------------------------------
SVS* CResourceManager::_CreateVS(LPCSTR _name, ref_vs* keep_alive)
{
	xr_string res_name = _name;

	const int m_skinning = Engine.External.GetSkinningMode();
	if (m_skinning > 0)
	{
		res_name += "_" + xr_string::ToString(m_skinning);
	}

	LPCSTR name = res_name.c_str();
	LPSTR N = LPSTR(name);
	xrCriticalSectionGuard shader_guard(shader_creation_guard(name));
	{
		xrCriticalSectionGuard guard(creationGuard);
		map_VS::iterator I = m_vs.find(N);
		if (I != m_vs.end())
		{
			if (keep_alive)
				*keep_alive = I->second;
			return I->second;
		}
	}

	SVS* _vs = xr_new<SVS>();
	_vs->skinning = m_skinning;
	_vs->dwFlags |= xr_resource_flagged::RF_REGISTERED;
	_vs->set_name(name);
		//_vs->vs				= NULL;
		//_vs->signature		= NULL;
		if (0 == stricmp(_name, "null"))
		{
			xrCriticalSectionGuard guard(creationGuard);
			m_vs.insert(mk_pair(*_vs->cName, _vs));
			if (keep_alive)
				*keep_alive = _vs;
			return _vs;
		}

		string_path shName;
		{
			const char* pchr = strchr(_name, '(');
			ptrdiff_t size = pchr ? pchr - _name : xr_strlen(_name);
			strncpy(shName, _name, size);
			shName[size] = 0;
		}

		string_path cname;
		strconcat(sizeof(cname), cname, ::Render->getShaderPath(),/*_name*/shName, ".vs");
		FS.update_path(cname, "$game_shaders$", cname);
		//		LPCSTR						target		= NULL;

		// duplicate and zero-terminate
		IReader* file = FS.r_open(cname);
		//	TODO: DX10: HACK: Implement all shaders. Remove this for PS
		if (!file)
		{
			string1024 tmp;
			xr_sprintf(tmp, "DX10: %s is missing. Replace with stub_default.vs", cname);
			Msg(tmp);
			strconcat(sizeof(cname), cname, ::Render->getShaderPath(), "stub_default", ".vs");
			FS.update_path(cname, "$game_shaders$", cname);
			file = FS.r_open(cname);
		}
		u32 const size = file->length();
		char* const data = (LPSTR)_alloca(size + 1);
		CopyMemory(data, file->pointer(), size);
		data[size] = 0;
		FS.r_close(file);

		// Select target
		LPCSTR c_target = "vs_2_0";
		LPCSTR c_entry = "main";
		if (HW.Caps.geometry_major >= 2) c_target = "vs_2_0";
		else c_target = "vs_1_1";

		if (strstr(data, "main_vs_1_1"))
		{
			c_target = "vs_1_1";
			c_entry = "main_vs_1_1";
		}
		if (strstr(data, "main_vs_2_0"))
		{
			c_target = "vs_2_0";
			c_entry = "main_vs_2_0";
		}

		HRESULT const _hr = ::Render->shader_compile(name, (DWORD const*)data, size, c_entry, c_target,
		                                             D3D10_SHADER_PACK_MATRIX_ROW_MAJOR, (void*&)_vs);

		VERIFY(SUCCEEDED(_hr));

		CHECK_OR_EXIT(
			!FAILED(_hr),
			make_string("Shader compilation failed, check your log file for additional information.")
		);

	{
		xrCriticalSectionGuard guard(creationGuard);
		m_vs.insert(mk_pair(*_vs->cName, _vs));
		if (keep_alive)
			*keep_alive = _vs;
	}
	return _vs;
}

void CResourceManager::_DeleteVS(const SVS* vs)
{
	if (0 == (vs->dwFlags & xr_resource_flagged::RF_REGISTERED)) return;
	xrCriticalSectionGuard guard(creationGuard);
	LPSTR N = LPSTR(*vs->cName);
	map_VS::iterator I = m_vs.find(N);
	if (I != m_vs.end())
	{
		m_vs.erase(I);
		xr_vector<SDeclaration*>::iterator iDecl;
		for (iDecl = v_declarations.begin(); iDecl != v_declarations.end(); ++iDecl)
		{
			xr_map<ID3DBlob*, ID3DInputLayout*>::iterator iLayout;
			iLayout = (*iDecl)->vs_to_layout.find(vs->signature->signature);
			if (iLayout != (*iDecl)->vs_to_layout.end())
			{
				//	Release vertex layout
				_RELEASE(iLayout->second);
				(*iDecl)->vs_to_layout.erase(iLayout);
			}
		}
		return;
	}
	Msg("! ERROR: Failed to find compiled vertex-shader '%s'", *vs->cName);
}

//--------------------------------------------------------------------------------------------------------------
SPS* CResourceManager::_CreatePS(LPCSTR _name, ref_ps* keep_alive)
{
	string_path name;
	xr_strcpy(name, _name);
	if (0 == ::Render->m_MSAASample) xr_strcat(name, "_0");
	if (1 == ::Render->m_MSAASample) xr_strcat(name, "_1");
	if (2 == ::Render->m_MSAASample) xr_strcat(name, "_2");
	if (3 == ::Render->m_MSAASample) xr_strcat(name, "_3");
	if (4 == ::Render->m_MSAASample) xr_strcat(name, "_4");
	if (5 == ::Render->m_MSAASample) xr_strcat(name, "_5");
	if (6 == ::Render->m_MSAASample) xr_strcat(name, "_6");
	if (7 == ::Render->m_MSAASample) xr_strcat(name, "_7");
	LPSTR N = LPSTR(name);
	xrCriticalSectionGuard shader_guard(shader_creation_guard(name));
	{
		xrCriticalSectionGuard guard(creationGuard);
		map_PS::iterator I = m_ps.find(N);
		if (I != m_ps.end())
		{
			if (keep_alive)
				*keep_alive = I->second;
			return I->second;
		}
	}

	SPS* _ps = xr_new<SPS>();
	_ps->dwFlags |= xr_resource_flagged::RF_REGISTERED;
	_ps->set_name(name);
		if (0 == stricmp(_name, "null"))
		{
			_ps->ps = NULL;
			xrCriticalSectionGuard guard(creationGuard);
			m_ps.insert(mk_pair(*_ps->cName, _ps));
			if (keep_alive)
				*keep_alive = _ps;
			return _ps;
		}

		string_path shName;
		const char* pchr = strchr(_name, '(');
		ptrdiff_t strSize = pchr ? pchr - _name : xr_strlen(_name);
		strncpy(shName, _name, strSize);
		shName[strSize] = 0;

		// Open file
		string_path cname;
		strconcat(sizeof(cname), cname, ::Render->getShaderPath(),/*_name*/shName, ".ps");
		FS.update_path(cname, "$game_shaders$", cname);

		// duplicate and zero-terminate
		IReader* file = FS.r_open(cname);
		//	TODO: DX10: HACK: Implement all shaders. Remove this for PS
		if (!file)
		{
			string1024 tmp;
			//	TODO: HACK: Test failure
			//Memory.mem_compact();
			xr_sprintf(tmp, "DX10: %s is missing. Replace with stub_default.ps", cname);
			Msg(tmp);
			strconcat(sizeof(cname), cname, ::Render->getShaderPath(), "stub_default", ".ps");
			FS.update_path(cname, "$game_shaders$", cname);
			file = FS.r_open(cname);
		}

		R_ASSERT2(file, cname);
		u32 const size = file->length();
		char* const data = (LPSTR)_alloca(size + 1);
		CopyMemory(data, file->pointer(), size);
		data[size] = 0;
		FS.r_close(file);

		// Select target
		LPCSTR c_target = "ps_2_0";
		LPCSTR c_entry = "main";
		if (strstr(data, "main_ps_1_1"))
		{
			c_target = "ps_1_1";
			c_entry = "main_ps_1_1";
		}
		if (strstr(data, "main_ps_1_2"))
		{
			c_target = "ps_1_2";
			c_entry = "main_ps_1_2";
		}
		if (strstr(data, "main_ps_1_3"))
		{
			c_target = "ps_1_3";
			c_entry = "main_ps_1_3";
		}
		if (strstr(data, "main_ps_1_4"))
		{
			c_target = "ps_1_4";
			c_entry = "main_ps_1_4";
		}
		if (strstr(data, "main_ps_2_0"))
		{
			c_target = "ps_2_0";
			c_entry = "main_ps_2_0";
		}

		HRESULT const _hr = ::Render->shader_compile(name, (DWORD const*)data, size, c_entry, c_target,
		                                             D3D10_SHADER_PACK_MATRIX_ROW_MAJOR, (void*&)_ps);

		VERIFY(SUCCEEDED(_hr));

		CHECK_OR_EXIT(
			!FAILED(_hr),
			make_string("Shader compilation failed, check your log file for additional information.")
		);

	{
		xrCriticalSectionGuard guard(creationGuard);
		m_ps.insert(mk_pair(*_ps->cName, _ps));
		if (keep_alive)
			*keep_alive = _ps;
	}
	return _ps;
}

void CResourceManager::_DeletePS(const SPS* ps)
{
	if (0 == (ps->dwFlags & xr_resource_flagged::RF_REGISTERED)) return;
	xrCriticalSectionGuard guard(creationGuard);
	LPSTR N = LPSTR(*ps->cName);
	map_PS::iterator I = m_ps.find(N);
	if (I != m_ps.end())
	{
		m_ps.erase(I);
		return;
	}
	Msg("! ERROR: Failed to find compiled pixel-shader '%s'", *ps->cName);
}

//--------------------------------------------------------------------------------------------------------------
SGS* CResourceManager::_CreateGS(LPCSTR name, ref_gs* keep_alive)
{
	xrCriticalSectionGuard guard(creationGuard);
	LPSTR N = LPSTR(name);
	map_GS::iterator I = m_gs.find(N);
	if (I != m_gs.end())
	{
		if (keep_alive)
			*keep_alive = I->second;
		return I->second;
	}
	else
	{
		SGS* _gs = xr_new<SGS>();
		_gs->dwFlags |= xr_resource_flagged::RF_REGISTERED;
		m_gs.insert(mk_pair(_gs->set_name(name), _gs));
		if (0 == stricmp(name, "null"))
		{
			_gs->gs = NULL;
			if (keep_alive)
				*keep_alive = _gs;
			return _gs;
		}

		// Open file
		string_path cname;
		strconcat(sizeof(cname), cname, ::Render->getShaderPath(), name, ".gs");
		FS.update_path(cname, "$game_shaders$", cname);

		// duplicate and zero-terminate
		IReader* file = FS.r_open(cname);
		//	TODO: DX10: HACK: Implement all shaders. Remove this for PS
		if (!file)
		{
			string1024 tmp;
			//	TODO: HACK: Test failure
			//Memory.mem_compact();
			xr_sprintf(tmp, "DX10: %s is missing. Replace with stub_default.gs", cname);
			Msg(tmp);
			strconcat(sizeof(cname), cname, ::Render->getShaderPath(), "stub_default", ".gs");
			FS.update_path(cname, "$game_shaders$", cname);
			file = FS.r_open(cname);
		}

		R_ASSERT2(file, cname);

		// Select target
		LPCSTR c_target = "gs_4_0";
		LPCSTR c_entry = "main";

		HRESULT const _hr = ::Render->shader_compile(name, (DWORD const*)file->pointer(), file->length(), c_entry,
		                                             c_target, D3D10_SHADER_PACK_MATRIX_ROW_MAJOR, (void*&)_gs);

		VERIFY(SUCCEEDED(_hr));

		FS.r_close(file);

		CHECK_OR_EXIT(
			!FAILED(_hr),
			make_string("Shader compilation failed, check your log file for additional information.")
		);

		if (keep_alive)
			*keep_alive = _gs;
		return _gs;
	}
}

void CResourceManager::_DeleteGS(const SGS* gs)
{
	if (0 == (gs->dwFlags & xr_resource_flagged::RF_REGISTERED)) return;
	xrCriticalSectionGuard guard(creationGuard);
	LPSTR N = LPSTR(*gs->cName);
	map_GS::iterator I = m_gs.find(N);
	if (I != m_gs.end())
	{
		m_gs.erase(I);
		return;
	}
	Msg("! ERROR: Failed to find compiled geometry shader '%s'", *gs->cName);
}

//--------------------------------------------------------------------------------------------------------------
static BOOL dcl_equal(D3DVERTEXELEMENT9* a, D3DVERTEXELEMENT9* b)
{
	// check sizes
	u32 a_size = D3DXGetDeclLength(a);
	u32 b_size = D3DXGetDeclLength(b);
	if (a_size != b_size) return FALSE;
	return 0 == memcmp(a, b, a_size * sizeof(D3DVERTEXELEMENT9));
}

SDeclaration* CResourceManager::_CreateDecl(D3DVERTEXELEMENT9* dcl)
{
	xrCriticalSectionGuard guard(creationGuard);
	// Search equal code
	for (u32 it = 0; it < v_declarations.size(); it++)
	{
		SDeclaration* D = v_declarations[it];;
		if (dcl_equal(dcl, &*D->dcl_code.begin())) return D;
	}

	// Create _new
	SDeclaration* D = xr_new<SDeclaration>();
	u32 dcl_size = D3DXGetDeclLength(dcl) + 1;
	//	Don't need it for DirectX 10 here
	//CHK_DX					(HW.pDevice->CreateVertexDeclaration(dcl,&D->dcl));
	D->dcl_code.assign(dcl, dcl + dcl_size);
	dx10BufferUtils::ConvertVertexDeclaration(D->dcl_code, D->dx10_dcl_code);
	D->dwFlags |= xr_resource_flagged::RF_REGISTERED;
	v_declarations.push_back(D);
	return D;
}

void CResourceManager::_DeleteDecl(const SDeclaration* dcl)
{
	if (0 == (dcl->dwFlags & xr_resource_flagged::RF_REGISTERED)) return;
	xrCriticalSectionGuard guard(creationGuard);
	if (reclaim(v_declarations, dcl)) return;
	Msg("! ERROR: Failed to find compiled vertex-declarator");
}

//--------------------------------------------------------------------------------------------------------------
R_constant_table* CResourceManager::_CreateConstantTable(R_constant_table& C, ref_ctable* keep_alive)
{
	if (C.empty())		return NULL;

	xrCriticalSectionGuard guard(creationGuard);
	const u64 hash = C.hash();
	auto& candidates = m_constant_table_index[hash];
	for (R_constant_table* candidate : candidates)
		if (candidate->equal(C))
		{
			if (keep_alive)
				*keep_alive = candidate;
			return candidate;
		}

	auto NewElem = xr_new<R_constant_table>(C);
	//NewElem->_copy(C);
	NewElem->dwFlags |= xr_resource_flagged::RF_REGISTERED;
	v_constant_tables.push_back(NewElem);
	candidates.push_back(NewElem);
	if (keep_alive)
		*keep_alive = NewElem;
	return NewElem;

	return v_constant_tables.back();
}

void CResourceManager::_DeleteConstantTable(const R_constant_table* C)
{
	if (0 == (C->dwFlags & xr_resource_flagged::RF_REGISTERED)) return;
	xrCriticalSectionGuard guard(creationGuard);
	remove_indexed(m_constant_table_index, C->hash(), C);
	if (reclaim(v_constant_tables, C)) return;
	Msg("! ERROR: Failed to find compiled constant-table");
}

//--------------------------------------------------------------------------------------------------------------
#ifdef USE_DX11
CRT* CResourceManager::_CreateRT(LPCSTR Name, u32 w, u32 h, D3DFORMAT f, u32 SampleCount, bool useUAV)
#else
CRT* CResourceManager::_CreateRT(LPCSTR Name, u32 w, u32 h, D3DFORMAT f, u32 SampleCount)
#endif
{
	R_ASSERT(Name && Name[0] && w && h);

	// ***** first pass - search already created RT
	LPSTR N = LPSTR(Name);
	xrCriticalSectionGuard guard(creationGuard);
	map_RT::iterator I = m_rtargets.find(N);
	if (I != m_rtargets.end()) return I->second;
	else
	{
		CRT* RT = xr_new<CRT>();
		RT->dwFlags |= xr_resource_flagged::RF_REGISTERED;
		m_rtargets.insert(mk_pair(RT->set_name(Name), RT));
#ifdef USE_DX11
		if (Device.b_is_Ready) RT->create(Name, w, h, f, SampleCount, useUAV);
#else
		if (Device.b_is_Ready) RT->create(Name, w, h, f, SampleCount);
#endif
		return RT;
	}
}

void CResourceManager::_DeleteRT(const CRT* RT)
{
	if (0 == (RT->dwFlags & xr_resource_flagged::RF_REGISTERED)) return;
	LPSTR N = LPSTR(*RT->cName);
	xrCriticalSectionGuard guard(creationGuard);
	map_RT::iterator I = m_rtargets.find(N);
	if (I != m_rtargets.end())
	{
		m_rtargets.erase(I);
		return;
	}
	Msg("! ERROR: Failed to find render-target '%s'", *RT->cName);
}

//--------------------------------------------------------------------------------------------------------------
void CResourceManager::DBG_VerifyGeoms()
{
	/*
	for (u32 it=0; it<v_geoms.size(); it++)
	{
	SGeometry* G					= v_geoms[it];

	D3DVERTEXELEMENT9		test	[MAX_FVF_DECL_SIZE];
	u32						size	= 0;
	G->dcl->GetDeclaration			(test,(unsigned int*)&size);
	u32 vb_stride					= D3DXGetDeclVertexSize	(test,0);
	u32 vb_stride_cached			= G->vb_stride;
	R_ASSERT						(vb_stride == vb_stride_cached);
	}
	*/
}

SGeometry* CResourceManager::CreateGeom(D3DVERTEXELEMENT9* decl, ID3DVertexBuffer* vb, ID3DIndexBuffer* ib,
	ref_geom* keep_alive)
{
	xrCriticalSectionGuard guard(creationGuard);
	R_ASSERT(decl && vb);

	SDeclaration* dcl = _CreateDecl(decl);
	u32 vb_stride = D3DXGetDeclVertexSize(decl, 0);

	// ***** first pass - search already loaded shader
	for (u32 it = 0; it < v_geoms.size(); it++)
	{
		SGeometry& G = *(v_geoms[it]);
		if ((G.dcl == dcl) && (G.vb == vb) && (G.ib == ib) && (G.vb_stride == vb_stride))
		{
			if (keep_alive)
				*keep_alive = v_geoms[it];
			return v_geoms[it];
		}
	}

	SGeometry* Geom = xr_new<SGeometry>();
	Geom->dwFlags |= xr_resource_flagged::RF_REGISTERED;
	Geom->dcl = dcl;
	Geom->vb = vb;
	Geom->vb_stride = vb_stride;
	Geom->ib = ib;
	v_geoms.push_back(Geom);
	if (keep_alive)
		*keep_alive = Geom;
	return Geom;
}

SGeometry* CResourceManager::CreateGeom(u32 FVF, ID3DVertexBuffer* vb, ID3DIndexBuffer* ib, ref_geom* keep_alive)
{
	D3DVERTEXELEMENT9 dcl [MAX_FVF_DECL_SIZE];
	xrCriticalSectionGuard guard(creationGuard);
	CHK_DX(D3DXDeclaratorFromFVF(FVF,dcl));
	SGeometry* g = CreateGeom(dcl, vb, ib, keep_alive);
	return g;
}

void CResourceManager::DeleteGeom(const SGeometry* Geom)
{
	if (0 == (Geom->dwFlags & xr_resource_flagged::RF_REGISTERED)) return;
	xrCriticalSectionGuard guard(creationGuard);
	if (reclaim(v_geoms, Geom)) return;
	Msg("! ERROR: Failed to find compiled geometry-declaration");
}

//--------------------------------------------------------------------------------------------------------------
void CResourceManager::ResolveTextureSource(LPCSTR name, LPCSTR canonical_level_path, TextureSourceInfo& result)
{
	xr_string key = name;
	key += '\n';
	if (canonical_level_path && canonical_level_path[0])
		key += canonical_level_path;
	else if (FS_Path* level_path = FS.get_path("$level$"))
		key += level_path->m_Path;
	std::transform(key.begin(), key.end(), key.begin(), [](char value)
	{
		if (value == '/')
			return '\\';
		return static_cast<char>(tolower(static_cast<unsigned char>(value)));
	});

	xr_shared_ptr<TextureSourceJob> job;
	bool producer = false;
	{
		xrCriticalSectionGuard guard(textureSourceGuard);
		auto existing = m_textureSourceCache.find(key);
		if (existing != m_textureSourceCache.end())
			job = existing->second;
		else
		{
			job = xr_make_shared<TextureSourceJob>();
			m_textureSourceCache.emplace(key, job);
			producer = true;
		}
	}

	if (producer)
	{
		try
		{
			TextureSourceInfo sourceInfo;
			sourceInfo.loadKind = CTexture::LoadKindDds;
			string_path path = {};
			const CLocatorAPI::file* source = nullptr;
			if (FS.exist(path, "$game_textures$", name, ".ogm"))
				sourceInfo.loadKind = CTexture::LoadKindOgm;
			else if (FS.exist(path, "$game_textures$", name, ".avi"))
				sourceInfo.loadKind = CTexture::LoadKindAvi;
			else if (FS.exist(path, "$game_textures$", name, ".seq"))
				sourceInfo.loadKind = CTexture::LoadKindSequence;
			else if (FS.exist(path, "$game_textures$", name, ".gif"))
				sourceInfo.loadKind = CTexture::LoadKindGif;

			if (sourceInfo.loadKind != CTexture::LoadKindDds)
				sourceInfo.resolvedPath = path;
			else
			{
				if (canonical_level_path && canonical_level_path[0])
				{
					xr_string candidate = canonical_level_path;
					if (candidate.back() != '\\' && candidate.back() != '/')
						candidate += '\\';
					candidate += name;
					candidate += ".dds";
					source = FS.exist(candidate.c_str());
					if (source)
						sourceInfo.resolvedPath = candidate;
				}
				if (!source && (!canonical_level_path || !canonical_level_path[0]))
				{
					source = FS.exist(path, "$level$", name, ".dds");
					if (source)
						sourceInfo.resolvedPath = path;
				}
				sourceInfo.levelLocal = source != nullptr;
				if (!source)
				{
					source = FS.exist(path, "$game_saves$", name, ".dds");
					if (source)
						sourceInfo.resolvedPath = path;
				}
				if (!source)
				{
					source = FS.exist(path, "$game_textures$", name, ".dds");
					if (source)
						sourceInfo.resolvedPath = path;
				}
				if (sourceInfo.levelLocal)
				{
					sourceInfo.crc = source->crc;
					sourceInfo.sizeReal = source->size_real;
					sourceInfo.sizeCompressed = source->size_compressed;
					sourceInfo.modified = source->modif;
				}
			}
			job->source = std::move(sourceInfo);
		}
		catch (...)
		{
			job->failure = std::current_exception();
		}
		SetEvent(job->completed);
	}
	else
		WaitForSingleObject(job->completed, INFINITE);

	if (job->failure)
		std::rethrow_exception(job->failure);
	result = job->source;
}

//--------------------------------------------------------------------------------------------------------------
ref_texture CResourceManager::_CreateTexture(LPCSTR _Name, bool prefetch, LPCSTR canonical_level_path)
{
	PROF_EVENT("_CreateTexture");
	// DBG_VerifyTextures	();
	if (0 == xr_strcmp(_Name, "null")) return ref_texture();
	//Msg("texture %s", _Name);
	R_ASSERT(_Name && _Name[0]);
	string_path Name;
	xr_strcpy(Name, _Name); //. andy if (strext(Name)) *strext(Name)=0;
	fix_texture_name(Name);
	if ((!canonical_level_path || !canonical_level_path[0]) && !g_resource_level_path_override.empty())
		canonical_level_path = g_resource_level_path_override.c_str();

	TextureSourceInfo source;
	ResolveTextureSource(Name, canonical_level_path, source);

	xr_string registryName = Name;
	if (source.levelLocal)
	{
		string128 identity;
		xr_sprintf(identity, "\n@level:%08x:%08x:%08x:%08x:", source.crc, source.sizeReal,
			source.sizeCompressed, source.modified);
		registryName += identity;
		registryName += source.resolvedPath;
	}

	ref_texture texture;
	bool queueLoad = false;
	bool created = false;
	{
		xrCriticalSectionGuard guard(creationGuard);
		map_TextureIt I = m_textures.find(registryName.c_str());
		if (I != m_textures.end())
		{
			texture = ref_texture(I->second);
		}
		else
		{
			CTexture* T = xr_new<CTexture>();
			T->dwFlags |= xr_resource_flagged::RF_REGISTERED;
			m_textures.insert(mk_pair(T->set_name(registryName.c_str()), T));
			T->SetLoadSource(Name, source.resolvedPath.empty() ? nullptr : source.resolvedPath.c_str(),
				static_cast<CTexture::ELoadKind>(source.loadKind));
			T->Preload();
			texture = ref_texture(T);
			created = true;
		}

		if (prefetch)
			m_prefetchedTextures.emplace(texture._get(), texture);
		else
			m_prefetchedTextures.erase(texture._get());

		queueLoad = Device.b_is_Ready;
		if (!queueLoad && created && !texture->is_loaded())
		{
			m_deferredTextureLoads.push_back(texture);
		}
	}

	if (queueLoad)
		QueueTextureLoad(texture);

	return texture;
}

void CResourceManager::_DeleteTexture(const CTexture* T)
{
	// DBG_VerifyTextures	();

	if (0 == (T->dwFlags & xr_resource_flagged::RF_REGISTERED)) return;
	xrCriticalSectionGuard guard(creationGuard);
	LPSTR N = LPSTR(*T->cName);
	map_Texture::iterator I = m_textures.find(N);
	if (I != m_textures.end())
	{
		m_textures.erase(I);
		return;
	}
	Msg("! ERROR: Failed to find texture surface '%s'", *T->cName);
}

#ifdef DEBUG
void	CResourceManager::DBG_VerifyTextures	()
{
	map_Texture::iterator I		= m_textures.begin	();
	map_Texture::iterator E		= m_textures.end	();
	for (; I!=E; I++) 
	{
		R_ASSERT(I->first);
		R_ASSERT(I->second);
		R_ASSERT(I->second->cName);
		R_ASSERT(0==xr_strcmp(I->first,*I->second->cName));
	}
}
#endif

//--------------------------------------------------------------------------------------------------------------
CMatrix* CResourceManager::_CreateMatrix(LPCSTR Name)
{
	R_ASSERT(Name && Name[0]);
	if (0 == stricmp(Name, "$null")) return NULL;

	LPSTR N = LPSTR(Name);
	xrCriticalSectionGuard guard(creationGuard);
	map_Matrix::iterator I = m_matrices.find(N);
	if (I != m_matrices.end()) return I->second;
	else
	{
		CMatrix* M = xr_new<CMatrix>();
		M->dwFlags |= xr_resource_flagged::RF_REGISTERED;
		M->dwReference.store(1, std::memory_order_relaxed);
		m_matrices.insert(mk_pair(M->set_name(Name), M));
		return M;
	}
}

void CResourceManager::_DeleteMatrix(const CMatrix* M)
{
	if (0 == (M->dwFlags & xr_resource_flagged::RF_REGISTERED)) return;
	xrCriticalSectionGuard guard(creationGuard);
	LPSTR N = LPSTR(*M->cName);
	map_Matrix::iterator I = m_matrices.find(N);
	if (I != m_matrices.end())
	{
		m_matrices.erase(I);
		return;
	}
	Msg("! ERROR: Failed to find xform-def '%s'", *M->cName);
}

//--------------------------------------------------------------------------------------------------------------
CConstant* CResourceManager::_CreateConstant(LPCSTR Name)
{
	R_ASSERT(Name && Name[0]);
	if (0 == stricmp(Name, "$null")) return NULL;

	LPSTR N = LPSTR(Name);
	xrCriticalSectionGuard guard(creationGuard);
	map_Constant::iterator I = m_constants.find(N);
	if (I != m_constants.end()) return I->second;
	else
	{
		CConstant* C = xr_new<CConstant>();
		C->dwFlags |= xr_resource_flagged::RF_REGISTERED;
		C->dwReference.store(1, std::memory_order_relaxed);
		m_constants.insert(mk_pair(C->set_name(Name), C));
		return C;
	}
}

void CResourceManager::_DeleteConstant(const CConstant* C)
{
	if (0 == (C->dwFlags & xr_resource_flagged::RF_REGISTERED)) return;
	xrCriticalSectionGuard guard(creationGuard);
	LPSTR N = LPSTR(*C->cName);
	map_Constant::iterator I = m_constants.find(N);
	if (I != m_constants.end())
	{
		m_constants.erase(I);
		return;
	}
	Msg("! ERROR: Failed to find R1-constant-def '%s'", *C->cName);
}

//--------------------------------------------------------------------------------------------------------------
bool cmp_tl(const std::pair<u32, ref_texture>& _1, const std::pair<u32, ref_texture>& _2)
{
	return _1.first < _2.first;
}

STextureList* CResourceManager::_CreateTextureList(STextureList& L, ref_texture_list* keep_alive)
{
	xrCriticalSectionGuard guard(creationGuard);
	std::sort(L.begin(), L.end(), cmp_tl);
	const u64 hash = texture_list_hash(L);
	auto& candidates = m_texture_list_index[hash];
	for (STextureList* candidate : candidates)
		if (L.equal(*candidate))
		{
			if (keep_alive)
				*keep_alive = candidate;
			return candidate;
		}
	STextureList* lst = xr_new<STextureList>(L);
	//lst->_copy(L);
	lst->dwFlags |= xr_resource_flagged::RF_REGISTERED;

	lst_textures.push_back(lst);
	candidates.push_back(lst);
	if (keep_alive)
		*keep_alive = lst;
	return lst;
}

void CResourceManager::_DeleteTextureList(const STextureList* L)
{
	if (0 == (L->dwFlags & xr_resource_flagged::RF_REGISTERED)) return;
	xrCriticalSectionGuard guard(creationGuard);
	remove_indexed(m_texture_list_index, texture_list_hash(*L), L);
	if (reclaim(lst_textures, L)) return;
	Msg("! ERROR: Failed to find compiled list of textures");
}

//--------------------------------------------------------------------------------------------------------------
SMatrixList* CResourceManager::_CreateMatrixList(SMatrixList& L)
{
	BOOL bEmpty = TRUE;
	for (u32 i = 0; i < L.size(); i++)
		if (L[i])
		{
			bEmpty = FALSE;
			break;
		}
	if (bEmpty) return NULL;

	xrCriticalSectionGuard guard(creationGuard);

	for (u32 it = 0; it < lst_matrices.size(); it++)
	{
		SMatrixList* base = lst_matrices[it];
		if (L.equal(*base)) return base;
	}
	SMatrixList* lst = xr_new<SMatrixList>(L);
	//lst->_copy(L);

	lst->dwFlags |= xr_resource_flagged::RF_REGISTERED;
	lst_matrices.push_back(lst);
	return lst;
}

void CResourceManager::_DeleteMatrixList(const SMatrixList* L)
{
	if (0 == (L->dwFlags & xr_resource_flagged::RF_REGISTERED)) return;
	xrCriticalSectionGuard guard(creationGuard);
	if (reclaim(lst_matrices, L)) return;
	Msg("! ERROR: Failed to find compiled list of xform-defs");
}

//--------------------------------------------------------------------------------------------------------------
SConstantList* CResourceManager::_CreateConstantList(SConstantList& L)
{
	BOOL bEmpty = TRUE;
	for (u32 i = 0; i < L.size(); i++)
		if (L[i])
		{
			bEmpty = FALSE;
			break;
		}
	if (bEmpty) return NULL;

	xrCriticalSectionGuard guard(creationGuard);

	for (u32 it = 0; it < lst_constants.size(); it++)
	{
		SConstantList* base = lst_constants[it];
		if (L.equal(*base)) return base;
	}
	SConstantList* lst = xr_new<SConstantList>(L);
	//lst->_copy(L);

	lst->dwFlags |= xr_resource_flagged::RF_REGISTERED;
	lst_constants.push_back(lst);
	return lst;
}

void CResourceManager::_DeleteConstantList(const SConstantList* L)
{
	if (0 == (L->dwFlags & xr_resource_flagged::RF_REGISTERED)) return;
	xrCriticalSectionGuard guard(creationGuard);
	if (reclaim(lst_constants, L)) return;
	Msg("! ERROR: Failed to find compiled list of r1-constant-defs");
}

//--------------------------------------------------------------------------------------------------------------
dx10ConstantBuffer* CResourceManager::_CreateConstantBuffer(ID3DShaderReflectionConstantBuffer* pTable,
	ref_cbuffer* keep_alive)
{
	VERIFY(pTable);
	xrCriticalSectionGuard guard(creationGuard);
	dx10ConstantBuffer* pTempBuffer = xr_new<dx10ConstantBuffer>(pTable);

	for (u32 it = 0; it < v_constant_buffer.size(); it++)
	{
		dx10ConstantBuffer* buf = v_constant_buffer[it];
		if (pTempBuffer->Similar(*buf))
		{
			xr_delete(pTempBuffer);
			if (keep_alive)
				*keep_alive = buf;
			return buf;
		}
	}

	pTempBuffer->dwFlags |= xr_resource_flagged::RF_REGISTERED;
	v_constant_buffer.push_back(pTempBuffer);
	if (keep_alive)
		*keep_alive = pTempBuffer;
	return pTempBuffer;
}

//--------------------------------------------------------------------------------------------------------------
void CResourceManager::_DeleteConstantBuffer(const dx10ConstantBuffer* pBuffer)
{
	if (0 == (pBuffer->dwFlags & xr_resource_flagged::RF_REGISTERED)) return;
	xrCriticalSectionGuard guard(creationGuard);
	if (reclaim(v_constant_buffer, pBuffer)) return;
	Msg("! ERROR: Failed to find compiled constant buffer");
}

//--------------------------------------------------------------------------------------------------------------
SInputSignature* CResourceManager::_CreateInputSignature(ID3DBlob* pBlob, ref_input_sign* keep_alive)
{
	VERIFY(pBlob);
	xrCriticalSectionGuard guard(creationGuard);

	for (u32 it = 0; it < v_input_signature.size(); it++)
	{
		SInputSignature* sign = v_input_signature[it];
		if ((pBlob->GetBufferSize() == sign->signature->GetBufferSize()) &&
			(!(memcmp(pBlob->GetBufferPointer(), sign->signature->GetBufferPointer(), pBlob->GetBufferSize()))))
		{
			if (keep_alive)
				*keep_alive = sign;
			return sign;
		}
	}

	SInputSignature* pSign = xr_new<SInputSignature>(pBlob);

	pSign->dwFlags |= xr_resource_flagged::RF_REGISTERED;
	v_input_signature.push_back(pSign);
	if (keep_alive)
		*keep_alive = pSign;

	return pSign;
}

//--------------------------------------------------------------------------------------------------------------
void CResourceManager::_DeleteInputSignature(const SInputSignature* pSignature)
{
	if (0 == (pSignature->dwFlags & xr_resource_flagged::RF_REGISTERED)) return;
	xrCriticalSectionGuard guard(creationGuard);
	if (reclaim(v_input_signature, pSignature)) return;
	Msg("! ERROR: Failed to find compiled constant buffer");
}
