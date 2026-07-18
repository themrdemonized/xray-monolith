// TextureManager.h: interface for the CTextureManager class.
//
//////////////////////////////////////////////////////////////////////

#ifndef ResourceManagerH
#define ResourceManagerH
#pragma once

#include	"shader.h"
#include	"tss_def.h"
#include	"TextureDescrManager.h"
// refs
struct lua_State;

class dx10ConstantBuffer;

ECORE_API xrCriticalSection& shader_creation_guard(LPCSTR name);

// defs
class ECORE_API CResourceManager
{
private:
	struct str_pred
	{
		IC bool operator()(LPCSTR x, LPCSTR y) const
		{
			return xr_strcmp(x, y) < 0;
		}
	};

	struct texture_detail
	{
		const char* T;
		R_constant_setup* cs;
	};

public:
	DEFINE_MAP_PRED(const char*, IBlender*, map_Blender, map_BlenderIt, str_pred);
	DEFINE_MAP_PRED(const char*, CTexture*, map_Texture, map_TextureIt, str_pred);
	DEFINE_MAP_PRED(const char*, CMatrix*, map_Matrix, map_MatrixIt, str_pred);
	DEFINE_MAP_PRED(const char*, CConstant*, map_Constant, map_ConstantIt, str_pred);
	DEFINE_MAP_PRED(const char*, CRT*, map_RT, map_RTIt, str_pred);
	//	DX10 cut DEFINE_MAP_PRED(const char*,CRTC*,			map_RTC,		map_RTCIt,			str_pred);
	DEFINE_MAP_PRED(const char*, SVS*, map_VS, map_VSIt, str_pred);
#if defined(USE_DX10) || defined(USE_DX11)
	DEFINE_MAP_PRED(const char*,SGS*,			map_GS,			map_GSIt,			str_pred);
#endif	//	USE_DX10
#ifdef USE_DX11
	DEFINE_MAP_PRED(const char*, SHS*,			map_HS,			map_HSIt,			str_pred);
	DEFINE_MAP_PRED(const char*, SDS*,			map_DS,			map_DSIt,			str_pred);
	DEFINE_MAP_PRED(const char*, SCS*,			map_CS,			map_CSIt,			str_pred);
#endif

	DEFINE_MAP_PRED(const char*, SPS*, map_PS, map_PSIt, str_pred);
	DEFINE_MAP_PRED(const char*, texture_detail, map_TD, map_TDIt, str_pred);
private:
	// data
	map_Blender m_blenders;
	map_Texture m_textures;
	map_Matrix m_matrices;
	map_Constant m_constants;
	map_RT m_rtargets;
	//	DX10 cut map_RTC												m_rtargets_c;
	map_VS m_vs;
	map_PS m_ps;
#if defined(USE_DX10) || defined(USE_DX11)
	map_GS												m_gs;
#endif	//	USE_DX10
	map_TD m_td;

	xr_vector<SState*> v_states;
	xr_vector<SDeclaration*> v_declarations;
	xr_vector<SGeometry*> v_geoms;
	xr_vector<R_constant_table*> v_constant_tables;

#if defined(USE_DX10) || defined(USE_DX11)
	xr_vector<dx10ConstantBuffer*>						v_constant_buffer;
	xr_vector<SInputSignature*>							v_input_signature;
#endif	//	USE_DX10

	// lists
	xr_vector<STextureList*> lst_textures;
	xr_vector<SMatrixList*> lst_matrices;
	xr_vector<SConstantList*> lst_constants;

	// main shader-array
	xr_vector<SPass*> v_passes;
	xr_vector<ShaderElement*> v_elements;
	xr_vector<Shader*> v_shaders;
	xr_unordered_flat_map<u64, xr_vector<SState*>> m_state_index;
	xr_unordered_flat_map<u64, xr_vector<SPass*>> m_pass_index;
	xr_unordered_flat_map<u64, xr_vector<R_constant_table*>> m_constant_table_index;
	xr_unordered_flat_map<u64, xr_vector<STextureList*>> m_texture_list_index;
	xr_unordered_flat_map<u64, xr_vector<ShaderElement*>> m_element_index;
	xr_unordered_flat_map<u64, xr_vector<Shader*>> m_shader_index;
	xr_map<xr_string, ref_shader> m_level_shader_cache;
	struct level_shader_job
	{
		HANDLE completed;
		ref_shader result;
		std::exception_ptr failure;
		level_shader_job() : completed(CreateEvent(nullptr, TRUE, FALSE, nullptr)) { R_ASSERT(completed); }
		~level_shader_job() { CloseHandle(completed); }
	};
	xr_map<xr_string, xr_shared_ptr<level_shader_job>> m_level_shader_jobs;

	xr_vector<ref_texture> m_necessary;
	xr_vector<ref_texture> m_deferredTextureLoads;
	xr_vector<ref_texture> m_ownerTextureLoads;
	xr_vector<ref_texture> m_generationStartingTextureLoads;
	xr_map<CTexture*, ref_texture> m_prefetchedTextures;
	xr_vector<shared_str> m_reduceLodTextureList;
	struct TextureSourceInfo
	{
		xr_string resolvedPath;
		u32 loadKind = 0;
		bool levelLocal = false;
		u32 crc = 0;
		u32 sizeReal = 0;
		u32 sizeCompressed = 0;
		u32 modified = 0;
	};
	struct TextureSourceJob
	{
		HANDLE completed;
		TextureSourceInfo source;
		std::exception_ptr failure;

		TextureSourceJob() : completed(CreateEvent(nullptr, TRUE, FALSE, nullptr)) { R_ASSERT(completed); }
		~TextureSourceJob() { CloseHandle(completed); }
	};
	xrCriticalSection textureSourceGuard;
	xr_map<xr_string, xr_shared_ptr<TextureSourceJob>> m_textureSourceCache;
	struct ResourceLoadGeneration
	{
		const u64 id;
		const u64 native_generation;
		HANDLE completed;
		HANDLE ownerWorkAvailable;
		u32 pending = 0;
		u32 serial = 0;
		bool closed = false;
		bool aborted = false;
		std::exception_ptr failure;
		xr_vector<ref_texture> ownerTextureLoads;

		ResourceLoadGeneration(u64 value, u64 native_value) : id(value), native_generation(native_value),
			completed(CreateEvent(nullptr, TRUE, TRUE, nullptr)),
			ownerWorkAvailable(CreateEvent(nullptr, TRUE, FALSE, nullptr))
		{
			R_ASSERT(completed && ownerWorkAvailable);
		}

		~ResourceLoadGeneration()
		{
			CloseHandle(ownerWorkAvailable);
			CloseHandle(completed);
		}
	};
	using ResourceLoadGenerationPtr = xr_shared_ptr<ResourceLoadGeneration>;
	// misc
	xrCriticalSection creationGuard;
	xrCriticalSection textureLoadGuard;
	xr_task_group textureLoadTasks;
	u32 textureLoadSerial = 0;
	std::exception_ptr textureLoadFailure;
	u64 nextResourceLoadGeneration = 0;
	DWORD textureOwnerThread = 0;
	bool resourceLoadGenerationStarting = false;
	ResourceLoadGenerationPtr activeResourceLoadGeneration;

	void QueueTextureLoad(const ref_texture& texture);
	void ResolveTextureSource(LPCSTR name, LPCSTR canonical_level_path, TextureSourceInfo& result);
	void CompleteTextureLoad(const ResourceLoadGenerationPtr& generation);
	void RecordTextureLoadFailure(const ResourceLoadGenerationPtr& generation, std::exception_ptr failure);
	void DrainOwnerTextureLoads(const ResourceLoadGenerationPtr& generation);
	std::exception_ptr WaitForTextureLoadGeneration(const ResourceLoadGenerationPtr& generation);

public:
	CTextureDescrMngr m_textures_description;
	xr_vector<std::pair<shared_str, R_constant_setup*>> v_constant_setup;
	lua_State* LSVM;
	BOOL bDeferredLoad;
private:
	void LS_Load();
	void LS_Unload();
public:
	// Miscelaneous
	void _ParseList(sh_list& dest, LPCSTR names);
	IBlender* _GetBlender(LPCSTR Name);
	IBlender* _FindBlender(LPCSTR Name);
	void _GetMemoryUsage(u32& m_base, u32& c_base, u32& m_lmaps, u32& c_lmaps);
	void _DumpMemoryUsage();
	//.	BOOL							_GetDetailTexture	(LPCSTR Name, LPCSTR& T, R_constant_setup* &M);

	map_Blender& _GetBlenders() { return m_blenders; }

	// Debug
	void DBG_VerifyGeoms();
	void DBG_VerifyTextures();

	// Editor cooperation
	void ED_UpdateBlender(LPCSTR Name, IBlender* data);
#ifdef _EDITOR
	void							ED_UpdateTextures	(AStringVec* names);
#endif

	// Low level resource creation
	ref_texture _CreateTexture(LPCSTR Name, bool prefetch = false, LPCSTR canonical_level_path = nullptr);
	void _DeleteTexture(const CTexture* T);
	void PrefetchTexture(LPCSTR Name, LPCSTR canonical_level_path = nullptr);
	void InvalidateTextureSourceCache();
	void InvalidateLevelShaderCache();
	void ReleaseLevelShaderCache(LPCSTR canonical_level_path, u64 recipe_identity);
	int GetTextureLoadLod(LPCSTR name) const;

	CMatrix* _CreateMatrix(LPCSTR Name);
	void _DeleteMatrix(const CMatrix* M);

	CConstant* _CreateConstant(LPCSTR Name);
	void _DeleteConstant(const CConstant* C);

	R_constant_table* _CreateConstantTable(R_constant_table& C, ref_ctable* keep_alive = nullptr);
	void _DeleteConstantTable(const R_constant_table* C);

#if defined(USE_DX10) || defined(USE_DX11)
	dx10ConstantBuffer*				_CreateConstantBuffer(ID3DShaderReflectionConstantBuffer* pTable, ref_cbuffer* keep_alive = nullptr);
	void							_DeleteConstantBuffer(const dx10ConstantBuffer* pBuffer);

	SInputSignature*				_CreateInputSignature(ID3DBlob* pBlob, ref_input_sign* keep_alive = nullptr);
	void							_DeleteInputSignature(const SInputSignature* pSignature);
#endif	//	USE_DX10

#ifdef USE_DX11
	CRT*							_CreateRT			(LPCSTR Name, u32 w, u32 h,	D3DFORMAT f, u32 SampleCount = 1, bool useUAV=false );
#else
	CRT* _CreateRT(LPCSTR Name, u32 w, u32 h, D3DFORMAT f, u32 SampleCount = 1);
#endif
	void _DeleteRT(const CRT* RT);

	//	DX10 cut CRTC*							_CreateRTC			(LPCSTR Name, u32 size,	D3DFORMAT f);
	//	DX10 cut void							_DeleteRTC			(const CRTC*	RT	);
#if defined(USE_DX10) || defined(USE_DX11)
	SGS*							_CreateGS			(LPCSTR Name, ref_gs* keep_alive = nullptr);
	void							_DeleteGS			(const SGS*	GS	);
#endif	//	USE_DX10

#ifdef USE_DX11
	SHS*							_CreateHS			(LPCSTR Name, ref_hs* keep_alive = nullptr);
	void							_DeleteHS			(const SHS*	HS	);

	SDS*							_CreateDS			(LPCSTR Name, ref_ds* keep_alive = nullptr);
	void							_DeleteDS			(const SDS*	DS	);

    SCS*							_CreateCS			(LPCSTR Name, ref_cs* keep_alive = nullptr);
	void							_DeleteCS			(const SCS*	CS	);
#endif	//	USE_DX10

	SPS* _CreatePS(LPCSTR Name, ref_ps* keep_alive = nullptr);
	void _DeletePS(const SPS* PS);

	SVS* _CreateVS(LPCSTR Name, ref_vs* keep_alive = nullptr);
	void _DeleteVS(const SVS* VS);

	SPass* _CreatePass(const SPass& proto, ref_pass* keep_alive = nullptr);
	void _DeletePass(const SPass* P);

	// Shader compiling / optimizing
	SState* _CreateState(SimulatorStates& Code, ref_state* keep_alive = nullptr);
	void _DeleteState(const SState* SB);

	SDeclaration* _CreateDecl(D3DVERTEXELEMENT9* dcl);
	void _DeleteDecl(const SDeclaration* dcl);

	STextureList* _CreateTextureList(STextureList& L, ref_texture_list* keep_alive = nullptr);
	void _DeleteTextureList(const STextureList* L);

	SMatrixList* _CreateMatrixList(SMatrixList& L);
	void _DeleteMatrixList(const SMatrixList* L);

	Shader* _CreateShader(Shader* InShader, ref_shader* keep_alive = nullptr);

	SConstantList* _CreateConstantList(SConstantList& L);
	void _DeleteConstantList(const SConstantList* L);

	ShaderElement* _CreateElement(ShaderElement& L, ref_selement* keep_alive = nullptr);
	void _DeleteElement(const ShaderElement* L);

	Shader* _cpp_Create(LPCSTR s_shader, LPCSTR s_textures = 0, LPCSTR s_constants = 0, LPCSTR s_matrices = 0);
	Shader* _cpp_Create(IBlender* B, LPCSTR s_shader = 0, LPCSTR s_textures = 0, LPCSTR s_constants = 0,
	                    LPCSTR s_matrices = 0, bool hud_loading = false, ref_shader* keep_alive = nullptr);
	Shader* _lua_Create(LPCSTR s_shader, LPCSTR s_textures);
	BOOL _lua_HasShader(LPCSTR s_shader);

	CResourceManager() : bDeferredLoad(TRUE)
	{
		textureOwnerThread = GetCurrentThreadId();
	}

	~CResourceManager();

	void OnDeviceCreate(IReader* F);
	void OnDeviceCreate(LPCSTR name);
	void OnDeviceDestroy(BOOL bKeepTextures);

	void reset_begin();
	void reset_end();

	// Creation/Destroying
	Shader* Create(LPCSTR s_shader = 0, LPCSTR s_textures = 0, LPCSTR s_constants = 0, LPCSTR s_matrices = 0);
	Shader* CreateLevelShader(LPCSTR s_shader, LPCSTR s_textures, u64 recipe_identity = 0);
	ref_shader CreateLevelCppShader(LPCSTR s_shader, LPCSTR s_textures, LPCSTR s_constants = nullptr,
		LPCSTR s_matrices = nullptr, u64 recipe_identity = 0, LPCSTR canonical_level_path = nullptr);
	Shader* Create(IBlender* B, LPCSTR s_shader = 0, LPCSTR s_textures = 0, LPCSTR s_constants = 0,
	               LPCSTR s_matrices = 0);
	void Delete(const Shader* S);

	void RegisterConstantSetup(LPCSTR name, R_constant_setup* s)
	{
		v_constant_setup.push_back(mk_pair(shared_str(name), s));
	}

	SGeometry* CreateGeom(D3DVERTEXELEMENT9* decl, ID3DVertexBuffer* vb, ID3DIndexBuffer* ib, ref_geom* keep_alive = nullptr);
	SGeometry* CreateGeom(u32 FVF, ID3DVertexBuffer* vb, ID3DIndexBuffer* ib, ref_geom* keep_alive = nullptr);
	void DeleteGeom(const SGeometry* VS);
	void DeferredLoad(BOOL E) { bDeferredLoad = E; }
	void PrepareLoad();
	void DeferredUpload();
	void DeferredUnload();
	void WaitForTextureLoads();
	bool IsTextureOwnerThread() const { return textureOwnerThread == GetCurrentThreadId(); }
	u64 BeginLoadGeneration();
	void AbortLoadGeneration(u64 generation);
	void FinalizeLoadGeneration(u64 generation);
	void UnloadAllTexturesOnLevelUnload();
	void Evict();
    void EvictStalledTextures();
	void StoreNecessaryTextures();
	void DestroyNecessaryTextures();
	void Dump(bool bBrief);

private:
#ifdef USE_DX11
	map_DS	m_ds;
	map_HS	m_hs;
	map_CS	m_cs;

	template<typename T>
	T& GetShaderMap();

	template<typename T, typename Ref>
	T* CreateShader(const char* name, Ref* keep_alive);

	template<typename T>
	void DestroyShader(const T* sh);

#endif	//	USE_DX10
};

extern thread_local xr_string g_resource_level_path_override;

#endif //ResourceManagerH
