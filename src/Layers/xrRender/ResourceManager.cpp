
//
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "xrRender_console.h"
#pragma hdrstop

#pragma warning(disable:4995)
#include <d3dx9.h>
#pragma warning(default:4995)

#include "ResourceManager.h"
#include "tss.h"
#include "blenders\blender.h"
#include "blenders\blender_recorder.h"
#include "../../xrCore/_thread_types.h"

//	Already defined in Texture.cpp
void fix_texture_name(LPSTR fn);

/*
void fix_texture_name(LPSTR fn)
{
	LPSTR _ext = strext(fn);
	if(  _ext					&&
	  (0==stricmp(_ext,".tga")	||
		0==stricmp(_ext,".dds")	||
		0==stricmp(_ext,".bmp")	||
		0==stricmp(_ext,".ogm")	) )
		*_ext = 0;
}
*/
//--------------------------------------------------------------------------------------------------------------
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
static void remove_shader_indexed(xr_unordered_flat_map<u64, xr_vector<T*>>& index, u64 hash, const T* value)
{
	auto bucket = index.find(hash);
	if (bucket == index.end())
		return;
	auto& values = bucket->second;
	values.erase(std::remove(values.begin(), values.end(), value), values.end());
	if (values.empty())
		index.erase(bucket);
}

static u64 shader_hash_pointer(u64 hash, const void* pointer)
{
	const uintptr_t value = reinterpret_cast<uintptr_t>(pointer);
	for (u32 i = 0; i < sizeof(value); ++i)
	{
		hash ^= static_cast<u8>(value >> (i * 8));
		hash *= 1099511628211ull;
	}
	return hash;
}

static u64 element_hash(const ShaderElement& element)
{
	u64 hash = 1469598103934665603ull;
	hash ^= element.flags.iPriority | (element.flags.bStrictB2F << 2) | (element.flags.bEmissive << 3) |
		(element.flags.bDistort << 4) | (element.flags.bWmark << 5) | (element.flags.bLandscape << 6) |
		(element.flags.iScopeLense << 7);
	hash *= 1099511628211ull;
	for (const ref_pass& pass : element.passes)
		hash = shader_hash_pointer(hash, pass._get());
	return hash;
}

static u64 shader_hash(const Shader& shader)
{
	u64 hash = 1469598103934665603ull;
	for (u32 i = 0; i < 5; ++i)
		hash = shader_hash_pointer(hash, shader.E[i]._get());
	return hash;
}

//--------------------------------------------------------------------------------------------------------------
IBlender* CResourceManager::_GetBlender(LPCSTR Name)
{
	R_ASSERT(Name && Name[0]);

	LPSTR N = LPSTR(Name);
	map_Blender::iterator I = m_blenders.find(N);
#ifdef _EDITOR
	if (I==m_blenders.end())	return 0;
#else
	//	TODO: DX10: When all shaders are ready switch to common path
#if defined(USE_DX10) || defined(USE_DX11)
	if (I == m_blenders.end())
	{
		Msg("DX10: Shader '%s' not found in library.", Name);
		return 0;
	}
#endif
	if (I == m_blenders.end())
	{
		Debug.fatal(DEBUG_INFO, "Shader '%s' not found in library.", Name);
		return 0;
	}
#endif
	else return I->second;
}

IBlender* CResourceManager::_FindBlender(LPCSTR Name)
{
	if (!(Name && Name[0])) return 0;

	LPSTR N = LPSTR(Name);
	map_Blender::iterator I = m_blenders.find(N);
	if (I == m_blenders.end()) return 0;
	else return I->second;
}

void CResourceManager::ED_UpdateBlender(LPCSTR Name, IBlender* data)
{
	LPSTR N = LPSTR(Name);
	map_Blender::iterator I = m_blenders.find(N);
	if (I != m_blenders.end())
	{
		R_ASSERT(data->getDescription().CLS == I->second->getDescription().CLS);
		xr_delete(I->second);
		I->second = data;
	}
	else
	{
		m_blenders.insert(mk_pair(xr_strdup(Name), data));
	}
}

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////
void CResourceManager::_ParseList(sh_list& dest, LPCSTR names)
{
	if (0 == names || 0 == names[0])
		names = "$null";

	ZeroMemory(&dest, sizeof(dest));
	char* P = (char*)names;
	svector<char, 128> N;

	while (*P)
	{
		if (*P == ',')
		{
			// flush
			N.push_back(0);
			strlwr(N.begin());

			fix_texture_name(N.begin());
			//. andy			if (strext(N.begin())) *strext(N.begin())=0;
			dest.push_back(N.begin());
			N.clear();
		}
		else
		{
			N.push_back(*P);
		}
		P++;
	}
	if (N.size())
	{
		// flush
		N.push_back(0);
		strlwr(N.begin());

		fix_texture_name(N.begin());
		//. andy		if (strext(N.begin())) *strext(N.begin())=0;
		dest.push_back(N.begin());
	}
}

ShaderElement* CResourceManager::_CreateElement(ShaderElement& S, ref_selement* keep_alive)
{
	if (S.passes.empty()) return 0;

	xrCriticalSectionGuard guard(creationGuard);

	const u64 hash = element_hash(S);
	auto& candidates = m_element_index[hash];
	for (ShaderElement* candidate : candidates)
		if (S.equal(*candidate))
		{
			if (keep_alive)
				*keep_alive = candidate;
			return candidate;
		}

	// Create _new_ entry
	ShaderElement* N = xr_new<ShaderElement>(S);
	//N->_copy(S);
	N->dwFlags |= xr_resource_flagged::RF_REGISTERED;
	v_elements.push_back(N);
	candidates.push_back(N);
	if (keep_alive)
		*keep_alive = N;
	return N;
}

void CResourceManager::_DeleteElement(const ShaderElement* S)
{
	xrCriticalSectionGuard guard(creationGuard);
	if (0 == (S->dwFlags & xr_resource_flagged::RF_REGISTERED)) return;
	remove_shader_indexed(m_element_index, element_hash(*S), S);
	if (reclaim(v_elements, S)) return;
	Msg("! ERROR: Failed to find compiled 'shader-element'");
}

Shader* CResourceManager::_cpp_Create(IBlender* B, LPCSTR s_shader, LPCSTR s_textures, LPCSTR s_constants,
	                                      LPCSTR s_matrices, bool hud_loading, ref_shader* keep_alive)
{
	CBlender_Compile C;
	Shader S;

	//.
	// if (strstr(s_shader,"transparent"))	__asm int 3;

	// Access to template
	C.BT = B;
	C.bEditor = FALSE;
	C.bDetail = FALSE;

#if defined(USE_DX11)
	C.HudElement = false;
#endif

#ifdef _EDITOR
	if (!C.BT)			{ ELog.Msg(mtError,"Can't find shader '%s'",s_shader); return 0; }
	C.bEditor			= TRUE;
#endif

	// Parse names
	_ParseList(C.L_textures, s_textures);
	_ParseList(C.L_constants, s_constants);
	_ParseList(C.L_matrices, s_matrices);

#if defined(USE_DX11)
	if (hud_loading && RImplementation.o.ssfx_core)
	{
		C.HudElement = true;
	}
#endif

	// Compile element	(LOD0 - HQ)
	{
		C.iElement = 0;
		C.bDetail = m_textures_description.GetDetailTexture(C.L_textures[0], C.detail_texture, C.detail_scaler);
		//.		C.bDetail			= _GetDetailTexture(*C.L_textures[0],C.detail_texture,C.detail_scaler);
		ShaderElement E;
		C._cpp_Compile(&E);
		_CreateElement(E, &S.E[0]);
	}

	// Compile element	(LOD1)
	{
		C.iElement = 1;
		//.		C.bDetail			= _GetDetailTexture(*C.L_textures[0],C.detail_texture,C.detail_scaler);
		C.bDetail = m_textures_description.GetDetailTexture(C.L_textures[0], C.detail_texture, C.detail_scaler);
		ShaderElement E;
		C._cpp_Compile(&E);
		_CreateElement(E, &S.E[1]);
	}

	// Compile element
	{
		C.iElement = 2;
		C.bDetail = FALSE;
		ShaderElement E;
		C._cpp_Compile(&E);
		_CreateElement(E, &S.E[2]);
	}

	// Compile element
	{
		C.iElement = 3;
		C.bDetail = FALSE;
		ShaderElement E;
		C._cpp_Compile(&E);
		_CreateElement(E, &S.E[3]);
	}

	// Compile element
	{
		C.iElement = 4;
		C.bDetail = TRUE; //.$$$ HACK :)
		ShaderElement E;
		C._cpp_Compile(&E);
		_CreateElement(E, &S.E[4]);
	}

	// Compile element
	{
		C.iElement = 5;
		C.bDetail = FALSE;
		ShaderElement E;
		C._cpp_Compile(&E);
		_CreateElement(E, &S.E[5]);
	}

	// Hacky way to remove from the HUD mask transparent stuff. ( Let's try something better later... )
	if (hud_loading)
	{
		xrCriticalSectionGuard guard(creationGuard);
		if (strstr(s_shader, "lens"))
			S.E[0]->passes[0]->ps->hud_disabled = TRUE;
	}

	// Create _new_ entry
	Shader* ResultShader = _CreateShader(&S, keep_alive);
	return ResultShader;
}

Shader* CResourceManager::_cpp_Create(LPCSTR s_shader, LPCSTR s_textures, LPCSTR s_constants, LPCSTR s_matrices)
{
	//#ifndef DEDICATED_SERVER
#ifndef _EDITOR
	if (!g_dedicated_server)
#endif
	{
		//	TODO: DX10: When all shaders are ready switch to common path
#if defined(USE_DX10) || defined(USE_DX11)
		IBlender* pBlender = _GetBlender(s_shader ? s_shader : "null");
		if (!pBlender) return NULL;
		return _cpp_Create(pBlender, s_shader, s_textures, s_constants, s_matrices, ::Render->hud_loading);
#else	//	USE_DX10
		return _cpp_Create(_GetBlender(s_shader ? s_shader : "null"), s_shader, s_textures, s_constants, s_matrices,
			::Render->hud_loading);
#endif	//	USE_DX10
		//#else
	}
#ifndef _EDITOR
	else
#endif
	{
		return NULL;
	}
	//#endif
}

Shader* CResourceManager::Create(IBlender* B, LPCSTR s_shader, LPCSTR s_textures, LPCSTR s_constants, LPCSTR s_matrices)
{
	//#ifndef DEDICATED_SERVER
#ifndef _EDITOR
	if (!g_dedicated_server)
#endif
	{
		return _cpp_Create(B, s_shader, s_textures, s_constants, s_matrices, ::Render->hud_loading);
		//#else
	}
#ifndef _EDITOR
	else
#endif
	{
		return NULL;
		//#endif
	}
}

Shader* CResourceManager::Create(LPCSTR s_shader, LPCSTR s_textures, LPCSTR s_constants, LPCSTR s_matrices)
{
	//#ifndef DEDICATED_SERVER
#ifndef _EDITOR
	if (!g_dedicated_server)
#endif
	{
		//	TODO: DX10: When all shaders are ready switch to common path
#if defined(USE_DX10) || defined(USE_DX11)
		if (_lua_HasShader(s_shader))
			return _lua_Create(s_shader, s_textures);
		else
		{
			Shader* pShader = _cpp_Create(s_shader, s_textures, s_constants, s_matrices);
			if (pShader)
				return pShader;
			else
			{
				if (_lua_HasShader("stub_default"))
					return _lua_Create("stub_default", s_textures);
				else
				{
					FATAL("Can't find stub_default.s");
					return 0;
				}
			}
		}
#else	//	USE_DX10
#ifndef _EDITOR
		if (_lua_HasShader(s_shader))
			return _lua_Create(s_shader, s_textures);
		else
#endif
			return _cpp_Create(s_shader, s_textures, s_constants, s_matrices);
#endif	//	USE_DX10
	}
		//#else
#ifndef _EDITOR
	else
#endif
	{
		return NULL;
	}
	//#endif
}

Shader* CResourceManager::CreateLevelShader(LPCSTR s_shader, LPCSTR s_textures, u64 recipe_identity)
{
	xr_string key = s_shader;
	key += '\n';
	key += s_textures;
	string_path level_path;
	FS.update_path(level_path, "$level$", "");
	key += '\n';
	key += level_path;
	string32 identity;
	xr_sprintf(identity, "%016llx", recipe_identity);
	key += '\n';
	key += identity;
	{
		xrCriticalSectionGuard guard(creationGuard);
		auto cached = m_level_shader_cache.find(key);
		if (cached != m_level_shader_cache.end())
			return cached->second._get();
	}

	Shader* shader = Create(s_shader, s_textures);
	{
		xrCriticalSectionGuard guard(creationGuard);
		m_level_shader_cache.emplace(std::move(key), ref_shader(shader));
	}
	return shader;
}

ref_shader CResourceManager::CreateLevelCppShader(LPCSTR s_shader, LPCSTR s_textures, LPCSTR s_constants,
	LPCSTR s_matrices, u64 recipe_identity, LPCSTR canonical_level_path)
{
	xr_string key = s_shader ? s_shader : "null";
	key += '\n';
	key += s_textures ? s_textures : "";
	key += '\n';
	key += s_constants ? s_constants : "";
	key += '\n';
	key += s_matrices ? s_matrices : "";
	key += "\nworld";
	key += '\n';
	if (canonical_level_path && canonical_level_path[0])
		key += canonical_level_path;
	else
	{
		string_path level_path;
		FS.update_path(level_path, "$level$", "");
		key += level_path;
	}
	string32 identity;
	xr_sprintf(identity, "%016llx", recipe_identity);
	key += '\n';
	key += identity;

	xr_shared_ptr<level_shader_job> job;
	bool producer = false;
	{
		xrCriticalSectionGuard guard(creationGuard);
		auto cached = m_level_shader_cache.find(key);
		if (cached != m_level_shader_cache.end())
			return cached->second;

		auto pending = m_level_shader_jobs.find(key);
		if (pending != m_level_shader_jobs.end())
			job = pending->second;
		else
		{
			job = xr_make_shared<level_shader_job>();
			m_level_shader_jobs.emplace(key, job);
			producer = true;
		}
	}

	if (!producer)
	{
		WaitForSingleObject(job->completed, INFINITE);
		if (job->failure)
			std::rethrow_exception(job->failure);
		return job->result;
	}

	ref_shader shader;
	try
	{
		// A worker never compiles through the shared shader.xr blender instance.
		// Serialize it to an owned snapshot, then restore a private clone.
		CMemoryWriter snapshot;
		CBlender_DESC description;
		bool have_blender = false;
		{
			xrCriticalSectionGuard guard(creationGuard);
			IBlender* source = _FindBlender(s_shader ? s_shader : "null");
			if (source)
			{
				description = source->getDescription();
				source->Save(snapshot);
				have_blender = true;
			}
		}

		struct level_path_scope
		{
			xr_string previous;
			level_path_scope(LPCSTR path)
			{
				previous.swap(g_resource_level_path_override);
				if (path)
					g_resource_level_path_override = path;
			}
			~level_path_scope() { g_resource_level_path_override.swap(previous); }
		} level_path(canonical_level_path);
		if (have_blender)
		{
			IBlender* blender = IBlender::Create(description.CLS);
			if (blender)
			{
				IReader reader(snapshot.pointer(), snapshot.size());
				blender->Load(reader, description.version);
				_cpp_Create(blender, s_shader, s_textures, s_constants, s_matrices, false, &shader);
				IBlender::Destroy(blender);
			}
		}
		{
			xrCriticalSectionGuard guard(creationGuard);
			if (shader)
				m_level_shader_cache.emplace(key, shader);
			job->result = shader;
			m_level_shader_jobs.erase(key);
		}
	}
	catch (...)
	{
		{
			xrCriticalSectionGuard guard(creationGuard);
			job->failure = std::current_exception();
			m_level_shader_jobs.erase(key);
		}
		SetEvent(job->completed);
		throw;
	}

	SetEvent(job->completed);
	return shader;
}

void CResourceManager::Delete(const Shader* S)
{
	if (0 == (S->dwFlags & xr_resource_flagged::RF_REGISTERED))
		return;

	xrCriticalSectionGuard guard(creationGuard);
	remove_shader_indexed(m_shader_index, shader_hash(*S), S);

	if (reclaim(v_shaders, S))
		return;

	Msg("! ERROR: Failed to find complete shader");
}

void CResourceManager::CompleteTextureLoad(const ResourceLoadGenerationPtr& generation)
{
	xrCriticalSectionGuard guard(textureLoadGuard);
	R_ASSERT(generation->pending);
	if (--generation->pending == 0)
		SetEvent(generation->completed);
}

void CResourceManager::RecordTextureLoadFailure(const ResourceLoadGenerationPtr& generation,
	std::exception_ptr failure)
{
	xrCriticalSectionGuard guard(textureLoadGuard);
	if (!generation->failure)
		generation->failure = failure;
	generation->aborted = true;
}

void CResourceManager::DrainOwnerTextureLoads(const ResourceLoadGenerationPtr& generation)
{
	xr_vector<ref_texture> ownerTextures;
	{
		xrCriticalSectionGuard guard(textureLoadGuard);
		ownerTextures.swap(generation->ownerTextureLoads);
		ResetEvent(generation->ownerWorkAvailable);
	}

	for (const ref_texture& texture : ownerTextures)
	{
		bool aborted;
		{
			xrCriticalSectionGuard guard(textureLoadGuard);
			aborted = generation->aborted;
		}

		try
		{
			if (aborted)
				texture->CancelQueuedLoad();
			else
				texture->LoadQueued();
		}
		catch (...)
		{
			RecordTextureLoadFailure(generation, std::current_exception());
		}
		CompleteTextureLoad(generation);
	}
}

std::exception_ptr CResourceManager::WaitForTextureLoadGeneration(const ResourceLoadGenerationPtr& generation)
{
	for (;;)
	{
		u32 serial;
		{
			xrCriticalSectionGuard guard(textureLoadGuard);
			serial = generation->serial;
		}

		DrainOwnerTextureLoads(generation);
		if (generation->native_generation &&
			NativeLoadExecutor::Instance().HelpGeneration(generation->native_generation))
		{
			continue;
		}
		const HANDLE events[] = {generation->completed, generation->ownerWorkAvailable};
		WaitForMultipleObjects(static_cast<DWORD>(std::size(events)), events, FALSE, INFINITE);

		xrCriticalSectionGuard guard(textureLoadGuard);
		if (serial == generation->serial && generation->pending == 0 && generation->ownerTextureLoads.empty())
			return generation->failure;
	}
}

void CResourceManager::QueueTextureLoad(const ref_texture& texture)
{
	if (!texture)
		return;

	ResourceLoadGenerationPtr generation;
	{
		xrCriticalSectionGuard guard(textureLoadGuard);
		if (resourceLoadGenerationStarting)
		{
			m_generationStartingTextureLoads.push_back(texture);
			return;
		}
		generation = activeResourceLoadGeneration;
		if (generation && (generation->closed || generation->aborted))
			return;
	}

	const bool async = texture->CanLoadAsync();
	const DWORD originThread = GetCurrentThreadId();
	xrCriticalSectionGuard guard(textureLoadGuard);
	if (resourceLoadGenerationStarting)
	{
		m_generationStartingTextureLoads.push_back(texture);
		return;
	}
	if (
		(generation && (activeResourceLoadGeneration != generation || generation->closed || generation->aborted)) ||
		(!generation && activeResourceLoadGeneration))
		return;
	if (!texture->TryQueueLoad())
		return;

	if (!generation)
	{
		if (!async)
		{
			try
			{
				m_ownerTextureLoads.push_back(texture);
			}
			catch (...)
			{
				texture->CancelQueuedLoad();
				throw;
			}
		}
		else
		{
			try
			{
				textureLoadTasks.run([this, texture, originThread]()
				{
					try
					{
						if (originThread != GetCurrentThreadId())
						{
							PROF_THREAD("X-Ray PPL Thread")
						}
						texture->LoadQueued();
					}
					catch (...)
					{
						xrCriticalSectionGuard failureGuard(textureLoadGuard);
						if (!textureLoadFailure)
							textureLoadFailure = std::current_exception();
					}
				});
			}
			catch (...)
			{
				texture->CancelQueuedLoad();
				throw;
			}
		}
		++textureLoadSerial;
		return;
	}

	if (generation->pending++ == 0)
		ResetEvent(generation->completed);
	++generation->serial;
	if (!async)
	{
		try
		{
			generation->ownerTextureLoads.push_back(texture);
		}
		catch (...)
		{
			generation->aborted = true;
			texture->CancelQueuedLoad();
			if (--generation->pending == 0)
				SetEvent(generation->completed);
			throw;
		}
		SetEvent(generation->ownerWorkAvailable);
		return;
	}

	NativeLoadExecutor& executor = NativeLoadExecutor::Instance();
	NativeLoadExecutor::Batch batch;
	try
	{
		batch = executor.BeginBatch(generation->native_generation);
	}
	catch (...)
	{
		generation->aborted = true;
		texture->CancelQueuedLoad();
		if (--generation->pending == 0)
			SetEvent(generation->completed);
		throw;
	}
	if (!batch.Valid())
	{
		generation->aborted = true;
		texture->CancelQueuedLoad();
		if (--generation->pending == 0)
			SetEvent(generation->completed);
		return;
	}

	try
	{
		const bool submitted = executor.Submit(batch, NativeLoadPriority::ShaderTexture,
			[this, texture, generation, originThread]()
			{
				if (originThread != GetCurrentThreadId())
				{
					PROF_THREAD("X-Ray PPL Thread")
				}

				std::exception_ptr failure;
				bool aborted;
				{
					xrCriticalSectionGuard guard(textureLoadGuard);
					aborted = generation->aborted;
				}
				try
				{
					if (aborted)
						texture->CancelQueuedLoad();
					else
						texture->LoadQueued();
				}
				catch (...)
				{
					failure = std::current_exception();
					RecordTextureLoadFailure(generation, failure);
				}
				CompleteTextureLoad(generation);
				if (failure)
					std::rethrow_exception(failure);
			},
			[this, texture, generation]()
			{
				texture->CancelQueuedLoad();
				CompleteTextureLoad(generation);
			});
		if (!submitted)
		{
			generation->aborted = true;
			texture->CancelQueuedLoad();
			if (--generation->pending == 0)
				SetEvent(generation->completed);
		}
	}
	catch (...)
	{
		if (!generation->failure)
			generation->failure = std::current_exception();
		generation->aborted = true;
		texture->CancelQueuedLoad();
		if (--generation->pending == 0)
			SetEvent(generation->completed);
		throw;
	}
}

void CResourceManager::WaitForTextureLoads()
{
	ResourceLoadGenerationPtr generation;
	{
		xrCriticalSectionGuard guard(textureLoadGuard);
		generation = activeResourceLoadGeneration;
	}
	if (generation)
	{
		if (const std::exception_ptr failure = WaitForTextureLoadGeneration(generation))
			std::rethrow_exception(failure);
		return;
	}

	for (;;)
	{
		u32 serial;
		xr_vector<ref_texture> ownerTextures;
		{
			xrCriticalSectionGuard guard(textureLoadGuard);
			serial = textureLoadSerial;
			ownerTextures.swap(m_ownerTextureLoads);
		}

		// Startup fallback: video, sequence and GIF decoders stay on the render/owner thread.
		for (u32 index = 0; index < ownerTextures.size(); ++index)
		{
			try
			{
				ownerTextures[index]->LoadQueued();
			}
			catch (...)
			{
				for (++index; index < ownerTextures.size(); ++index)
					ownerTextures[index]->CancelQueuedLoad();
				throw;
			}
		}
		textureLoadTasks.wait();

		std::exception_ptr failure;
		{
			xrCriticalSectionGuard guard(textureLoadGuard);
			if (serial != textureLoadSerial || !m_ownerTextureLoads.empty())
				continue;
			failure = textureLoadFailure;
			textureLoadFailure = nullptr;
		}
		if (failure)
			std::rethrow_exception(failure);
		return;
	}
}

u64 CResourceManager::BeginLoadGeneration()
{
	{
		xrCriticalSectionGuard guard(textureLoadGuard);
		R_ASSERT2(!activeResourceLoadGeneration && !resourceLoadGenerationStarting,
			"Previous texture load generation is still active");
		resourceLoadGenerationStarting = true;
	}

	try
	{
		WaitForTextureLoads();
	}
	catch (...)
	{
		xrCriticalSectionGuard guard(textureLoadGuard);
		resourceLoadGenerationStarting = false;
		throw;
	}

	xr_vector<ref_texture> crossedRequests;
	ResourceLoadGenerationPtr startedGeneration;
	u64 result;
	{
		xrCriticalSectionGuard guard(textureLoadGuard);
		if (++nextResourceLoadGeneration == 0)
			++nextResourceLoadGeneration;
		try
		{
			const u64 nativeGeneration = NativeLoadExecutor::Instance().CurrentGeneration();
			R_ASSERT2(nativeGeneration, "Native load generation must begin before texture load generation");
			startedGeneration = xr_make_shared<ResourceLoadGeneration>(nextResourceLoadGeneration, nativeGeneration);
			activeResourceLoadGeneration = startedGeneration;
		}
		catch (...)
		{
			resourceLoadGenerationStarting = false;
			throw;
		}
		resourceLoadGenerationStarting = false;
		crossedRequests.swap(m_generationStartingTextureLoads);
		result = nextResourceLoadGeneration;
	}
	try
	{
		for (const ref_texture& texture : crossedRequests)
			QueueTextureLoad(texture);
	}
	catch (...)
	{
		const std::exception_ptr failure = std::current_exception();
		NativeLoadExecutor::Instance().CancelGeneration(startedGeneration->native_generation);
		AbortLoadGeneration(result);
		std::rethrow_exception(failure);
	}
	return result;
}

void CResourceManager::AbortLoadGeneration(u64 generationId)
{
	ResourceLoadGenerationPtr generation;
	{
		xrCriticalSectionGuard guard(textureLoadGuard);
		generation = activeResourceLoadGeneration;
		if (!generation || generation->id != generationId)
			return;
		generation->closed = true;
		generation->aborted = true;
	}

	WaitForTextureLoadGeneration(generation);
	{
		xrCriticalSectionGuard guard(textureLoadGuard);
		if (activeResourceLoadGeneration == generation)
			activeResourceLoadGeneration.reset();
	}
}

void CResourceManager::FinalizeLoadGeneration(u64 generationId)
{
	ResourceLoadGenerationPtr generation;
	{
		xrCriticalSectionGuard guard(textureLoadGuard);
		generation = activeResourceLoadGeneration;
		if (!generation || generation->id != generationId)
			return;
		generation->closed = true;
	}

	const std::exception_ptr failure = WaitForTextureLoadGeneration(generation);
	{
		xrCriticalSectionGuard guard(textureLoadGuard);
		if (activeResourceLoadGeneration == generation)
			activeResourceLoadGeneration.reset();
	}
	if (failure)
		std::rethrow_exception(failure);
}

void CResourceManager::PrefetchTexture(LPCSTR name, LPCSTR canonical_level_path)
{
	_CreateTexture(name, true, canonical_level_path);
}

int CResourceManager::GetTextureLoadLod(LPCSTR name) const
{
	ENGINE_API bool is_enough_address_space_available();
	static const bool enough_address_space_available = is_enough_address_space_available();
	for (const shared_str& reduced : m_reduceLodTextureList)
	{
		if (!strstr(name, reduced.c_str()))
			continue;
		if (psTextureLOD < 1)
			return enough_address_space_available ? 0 : 1;
		return psTextureLOD < 3 ? 1 : 2;
	}
	if (psTextureLOD < 2)
		return 0;
	return psTextureLOD < 4 ? 1 : 2;
}

void CResourceManager::DeferredUpload()
{
	if (!RDEVICE.b_is_Ready) return;

	CTimer timer;
	timer.Start();

	xr_vector<ref_texture> deferredTextures;
	{
		xrCriticalSectionGuard guard(creationGuard);
		deferredTextures.swap(m_deferredTextureLoads);
	}

	for (const ref_texture& texture : deferredTextures)
		QueueTextureLoad(texture);

	WaitForTextureLoads();

	Msg("texture loading time: %d", timer.GetElapsed_ms());
}

void CResourceManager::PrepareLoad()
{
	xr_vector<ref_texture> deferredTextures;
	{
		xrCriticalSectionGuard guard(creationGuard);
		deferredTextures.swap(m_deferredTextureLoads);
	}
	for (const ref_texture& texture : deferredTextures)
		QueueTextureLoad(texture);

	ResourceLoadGenerationPtr generation;
	{
		xrCriticalSectionGuard guard(textureLoadGuard);
		generation = activeResourceLoadGeneration;
	}
	if (generation)
	{
		DrainOwnerTextureLoads(generation);
		std::exception_ptr failure;
		{
			xrCriticalSectionGuard guard(textureLoadGuard);
			failure = generation->failure;
		}
		if (failure)
			std::rethrow_exception(failure);
		return;
	}

	xr_vector<ref_texture> ownerTextures;
	{
		xrCriticalSectionGuard guard(textureLoadGuard);
		ownerTextures.swap(m_ownerTextureLoads);
	}
	for (u32 index = 0; index < ownerTextures.size(); ++index)
	{
		try
		{
			ownerTextures[index]->LoadQueued();
		}
		catch (...)
		{
			for (++index; index < ownerTextures.size(); ++index)
				ownerTextures[index]->CancelQueuedLoad();
			throw;
		}
	}
}

void CResourceManager::DeferredUnload()
{
	if (!RDEVICE.b_is_Ready)
		return;
	WaitForTextureLoads();
	xr_vector<ref_texture> textures;
	{
		xrCriticalSectionGuard guard(creationGuard);
		textures.reserve(m_textures.size());
		for (const auto& pair : m_textures)
			textures.emplace_back(pair.second);
	}
	xr_parallel_foreach(textures.begin(), textures.end(), [](ref_texture& texture)
	{
		texture->Unload();
	});
}

void CResourceManager::UnloadAllTexturesOnLevelUnload()
{
	if (!RDEVICE.b_is_Ready)
		return;
	WaitForTextureLoads();

	    xr_vector<ref_texture> textures_to_unload;
	    {
	        xrCriticalSectionGuard guard(creationGuard);
	        textures_to_unload.reserve(m_textures.size());

	        for (const auto& pair : m_textures)
	        {
	            CTexture* texture = pair.second;
	            if (!texture)
	                continue;

	            if (texture->flags.bUser)
	                continue;

	            // Keep $ textures alive since they are bound to runtime render targets or other important parts
	            if (strstr(*texture->cName, "$"))
	                continue;

	            // Keep UI textures
	            LPCSTR name = pair.first;
	            if (strncmp(name, "ui\\", 3) == 0 || strncmp(name, "ui/", 3) == 0)
	                continue;

	            textures_to_unload.emplace_back(texture);
	        }
	    }

	    for (ref_texture& texture : textures_to_unload)
	        texture->Unload();
}

#ifdef _EDITOR
void	CResourceManager::ED_UpdateTextures(AStringVec* names)
{
	// 1. Unload
	if (names){
		for (u32 nid=0; nid<names->size(); nid++)
		{
			map_TextureIt I = m_textures.find	((*names)[nid].c_str());
			if (I!=m_textures.end())	I->second->Unload();
		}
	}else{
		for (map_TextureIt t=m_textures.begin(); t!=m_textures.end(); t++)
			t->second->Unload();
	}

	// 2. Load
	// DeferredUpload	();
}
#endif

Shader* CResourceManager::_CreateShader(Shader* InShader, ref_shader* keep_alive)
{
	xrCriticalSectionGuard guard(creationGuard);
	const u64 hash = shader_hash(*InShader);
	auto& candidates = m_shader_index[hash];
	for (Shader* candidate : candidates)
		if (InShader->equal(candidate))
		{
			if (keep_alive)
				*keep_alive = candidate;
			return candidate;
		}

	// Create _new_ entry
	Shader* N = xr_new<Shader>(*InShader);
	//N->_copy(*InShader);
	N->dwFlags |= xr_resource_flagged::RF_REGISTERED;
	v_shaders.push_back(N);
	candidates.push_back(N);
	if (keep_alive)
		*keep_alive = N;

	return N;
}

void CResourceManager::_GetMemoryUsage(u32& m_base, u32& c_base, u32& m_lmaps, u32& c_lmaps)
{
	xrCriticalSectionGuard guard(creationGuard);
	m_base = c_base = m_lmaps = c_lmaps = 0;

	map_Texture::iterator I = m_textures.begin();
	map_Texture::iterator E = m_textures.end();
	for (; I != E; I++)
	{
		u32 m = I->second->flags.MemoryUsage;
		if (strstr(I->first, "lmap"))
		{
			c_lmaps ++;
			m_lmaps += m;
		}
		else
		{
			c_base ++;
			m_base += m;
		}
	}
}

void CResourceManager::_DumpMemoryUsage()
{
	xrCriticalSectionGuard guard(creationGuard);
	xr_multimap<u32, std::pair<u32, shared_str>> mtex;

	// sort
	{
		map_Texture::iterator I = m_textures.begin();
		map_Texture::iterator E = m_textures.end();
		for (; I != E; I++)
		{
			u32 m = I->second->flags.MemoryUsage;
			shared_str n = I->second->cName;
			mtex.insert(mk_pair(m, mk_pair(I->second->dwReference.load(std::memory_order_relaxed), n)));
		}
	}

	// dump
	{
		xr_multimap<u32, std::pair<u32, shared_str>>::iterator I = mtex.begin();
		xr_multimap<u32, std::pair<u32, shared_str>>::iterator E = mtex.end();
		for (; I != E; I++)
			Msg("* %4.1f : [%4d] %s", float(I->first) / 1024.f, I->second.first, I->second.second.c_str());
	}
}

void CResourceManager::Evict()
{
	//	TODO: DX10: check if we really need this method
#if !defined(USE_DX10) && !defined(USE_DX11)
	CHK_DX(HW.pDevice->EvictManagedResources());
#endif	//	USE_DX10
}

void CResourceManager::EvictStalledTextures() {
#if defined(USE_DX11)
  if (!ps_r__tex_evict_enabled)
    return;
#ifdef DEBUG
  Msg("* [TexEvict] pass called, frame=%u", RDEVICE.dwFrame);
#endif // DEBUG
  if (!RDEVICE.b_is_Ready)
    return;

  u32 cur_frame = RDEVICE.dwFrame;
  u32 max_age   = (u32)ps_r__tex_evict_age_frames;
#ifdef DEBUG
  u32 evicted      = 0;
  u32 evicted_kb   = 0;
  u32 skip_user    = 0;
  u32 skip_name    = 0;
  u32 skip_unloaded = 0;
  u32 skip_refs    = 0;
  u32 skip_recent  = 0;
#endif // DEBUG

  static xr_string s_evict_cursor;
  const u32 batch_size = (u32)ps_r__tex_evict_batch_size;

  creationGuard.Enter();

  map_Texture::iterator I;
  if (s_evict_cursor.empty()) {
    I = m_textures.begin();
  } else {
    I = m_textures.lower_bound(s_evict_cursor.c_str());
    if (I == m_textures.end())
      I = m_textures.begin();
  }

  u32 scanned = 0;
  for (; I != m_textures.end() && scanned < batch_size; ++I, ++scanned) {
    CTexture *tex = I->second;
    if (tex->flags.bUser) {
#ifdef DEBUG
      skip_user++;
#endif // DEBUG
      continue;
    }
    if (tex->cName.size() && strstr(tex->cName.c_str(), "$user$")) {
#ifdef DEBUG
      skip_user++;
#endif // DEBUG
      continue;
    }
    if (tex->cName.size() && strstr(tex->cName.c_str(), "$null")) {
#ifdef DEBUG
      skip_user++;
#endif // DEBUG
      continue;
    }
    if (!tex->is_loaded()) {
#ifdef DEBUG
      skip_unloaded++;
#endif // DEBUG
      continue;
    }
    // UI textures — PDA, inventory icons, etc. — must never be evicted
    LPCSTR name = I->first;
    if (strncmp(name, "ui\\", 3) == 0 || strncmp(name, "ui/", 3) == 0) {
#ifdef DEBUG
      skip_name++;
#endif // DEBUG
      continue;
    }
    if (tex->dwReference.load(std::memory_order_relaxed) > 1) {
#ifdef DEBUG
      u32 refs = tex->dwReference.load(std::memory_order_relaxed);
      if (refs > 2)
        Msg("* [TexEvict] stuck: [%4d] %s", refs, tex->cName.c_str());
      skip_refs++;
#endif // DEBUG
      continue;
    }
    if (cur_frame - tex->dwLastUsedFrame < max_age) {
#ifdef DEBUG
      skip_recent++;
#endif // DEBUG
      continue;
    }

#ifdef DEBUG
    evicted_kb += tex->flags.MemoryUsage / 1024;
#endif // DEBUG
    tex->Unload();
#ifdef DEBUG
    evicted++;
#endif // DEBUG
  }

  s_evict_cursor = (I != m_textures.end()) ? xr_string(I->first) : xr_string();

  creationGuard.Leave();

#ifdef DEBUG
  Msg("* [TexEvict] frame=%u total=%u scanned=%u evicted=%u skip_user=%u "
      "skip_name=%u skip_unloaded=%u skip_refs=%u skip_recent=%u freed_kb=%u",
      cur_frame, (u32)m_textures.size(), scanned, evicted, skip_user, skip_name,
      skip_unloaded, skip_refs, skip_recent, evicted_kb);
#endif // DEBUG
#endif
}


/*
BOOL	CResourceManager::_GetDetailTexture(LPCSTR Name,LPCSTR& T, R_constant_setup* &CS)
{
	LPSTR N = LPSTR(Name);
	map_TD::iterator I = m_td.find	(N);
	if (I!=m_td.end())
	{
		T	= I->second.T;
		CS	= I->second.cs;
		return TRUE;
	} else {
		return FALSE;
	}
}*/
thread_local xr_string g_resource_level_path_override;
