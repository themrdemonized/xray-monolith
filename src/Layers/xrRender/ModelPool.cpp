#include "stdafx.h"
#pragma hdrstop

#include "ModelPool.h"

#ifndef _EDITOR
#include "../../xrEngine/IGame_Persistent.h"
#include "../../xrEngine/fmesh.h"
#include "fhierrarhyvisual.h"
#include "SkeletonAnimated.h"
#include "fvisual.h"
#include "fprogressive.h"
#include "fskinned.h"
#include "flod.h"
#include "ftreevisual.h"
#include "ParticleGroup.h"
#include "ParticleEffect.h"
#else
    #include "fmesh.h"
    #include "fvisual.h"
    #include "fprogressive.h"
    #include "ParticleEffect.h"
    #include "ParticleGroup.h"
	#include "fskinned.h"
    #include "fhierrarhyvisual.h"
    #include "SkeletonAnimated.h"
	#include "IGame_Persistent.h"
#endif

namespace
{
xr_string NormalizeModelName(LPCSTR source)
{
	if (!source || !source[0])
		return {};
	string_path name;
	xr_strcpy(name, source);
	xr_strlwr(name);
	for (LPSTR cursor = name; *cursor; ++cursor)
		if (*cursor == '/')
			*cursor = '\\';
	LPSTR extension = strext(name);
	if (extension && !xr_strcmp(extension, ".ogf"))
		*extension = 0;
	return name;
}

void AppendTextureList(LPCSTR source, xr_vector<xr_string>& textures)
{
	for (int index = 0, count = _GetItemCount(source, ','); index < count; ++index)
	{
		string_path texture;
		_GetItem(source, index, texture, ',');
		xr_strlwr(texture);
		for (LPSTR cursor = texture; *cursor; ++cursor)
			if (*cursor == '/')
				*cursor = '\\';
		if (texture[0] && xr_strcmp(texture, "null") && xr_strcmp(texture, "$null"))
			textures.emplace_back(texture);
	}
}

bool ResolveModelFile(LPCSTR source, LPCSTR canonical_level_path, xr_string& resolved)
{
	const xr_string model = NormalizeModelName(source);
	if (model.empty())
		return false;
	string_path file_name;
	xr_strcpy(file_name, model.c_str());
	if (!strext(file_name))
		xr_strcat(file_name, ".ogf");
	if (canonical_level_path && canonical_level_path[0])
	{
		resolved = canonical_level_path;
		if (resolved.back() != '\\' && resolved.back() != '/')
			resolved += '\\';
		resolved += file_name;
		if (FS.exist(resolved.c_str()))
			return true;
	}
	string_path mesh_path;
	if (!FS.exist(mesh_path, "$game_meshes$", file_name))
		return false;
	resolved = mesh_path;
	return true;
}

void CollectModelTextures(LPCSTR source, LPCSTR canonical_level_path, xr_vector<xr_string>& textures,
	xr_set<xr_string>& visited);

void CollectModelReaderTextures(IReader& data, LPCSTR canonical_level_path, xr_vector<xr_string>& textures,
	xr_set<xr_string>& visited)
{
	if (data.find_chunk(OGF_TEXTURE))
	{
		string256 texture_list;
		string256 shader;
		data.r_stringZ(texture_list, sizeof(texture_list));
		data.r_stringZ(shader, sizeof(shader));
		AppendTextureList(texture_list, textures);
	}
	if (IReader* lod = data.open_chunk(OGF_S_LODS))
	{
		string_path lod_name;
		lod->r_string(lod_name, sizeof(lod_name));
		lod->close();
		CollectModelTextures(lod_name, canonical_level_path, textures, visited);
	}
	IReader* children = data.open_chunk(OGF_CHILDREN);
	if (!children)
		return;
	for (u32 index = 0;; ++index)
	{
		IReader* child = children->open_chunk(index);
		if (!child)
			break;
		CollectModelReaderTextures(*child, canonical_level_path, textures, visited);
		child->close();
	}
	children->close();
}

void CollectModelTextures(LPCSTR source, LPCSTR canonical_level_path, xr_vector<xr_string>& textures,
	xr_set<xr_string>& visited)
{
	const xr_string model = NormalizeModelName(source);
	if (model.empty() || !visited.insert(model).second)
		return;
	xr_string path;
	if (!ResolveModelFile(model.c_str(), canonical_level_path, path))
		return;
	IReader* reader = FS.r_open(path.c_str());
	if (!reader)
		return;
	CollectModelReaderTextures(*reader, canonical_level_path, textures, visited);
	FS.r_close(reader);
}
}

CModelPool::ModelBlueprint::ModelBlueprint()
	: completed(CreateEvent(nullptr, TRUE, FALSE, nullptr)), preparedVisual(nullptr), found(false)
{
	R_ASSERT(completed);
}

CModelPool::ModelBlueprint::~ModelBlueprint()
{
	if (preparedVisual)
	{
		preparedVisual->Release();
		xr_delete(preparedVisual);
	}
	CloseHandle(completed);
}

dxRender_Visual* CModelPool::Instance_Create(u32 type)
{
	dxRender_Visual* V = NULL;

	// Check types
	switch (type)
	{
	case MT_NORMAL: // our base visual
		V = xr_new<Fvisual>();
		break;
	case MT_HIERRARHY:
		V = xr_new<FHierrarhyVisual>();
		break;
	case MT_PROGRESSIVE: // dynamic-resolution visual
		V = xr_new<FProgressive>();
		break;
	case MT_SKELETON_ANIM:
		V = xr_new<CKinematicsAnimated>();
		break;
	case MT_SKELETON_RIGID:
		V = xr_new<CKinematics>();
		break;
	case MT_SKELETON_GEOMDEF_PM:
		V = xr_new<CSkeletonX_PM>();
		break;
	case MT_SKELETON_GEOMDEF_ST:
		V = xr_new<CSkeletonX_ST>();
		break;
	case MT_PARTICLE_EFFECT:
		V = xr_new<PS::CParticleEffect>();
		break;
	case MT_PARTICLE_GROUP:
		V = xr_new<PS::CParticleGroup>();
		break;
#ifndef _EDITOR
	case MT_LOD:
		V = xr_new<FLOD>();
		break;
	case MT_TREE_ST:
		V = xr_new<FTreeVisual_ST>();
		break;
	case MT_TREE_PM:
		V = xr_new<FTreeVisual_PM>();
		break;
#endif
	default:
		FATAL("Unknown visual type");
		break;
	}
	R_ASSERT(V);
	V->Type = type;
	return V;
}

dxRender_Visual* CModelPool::Instance_Duplicate(dxRender_Visual* V)
{
	R_ASSERT(V);
	dxRender_Visual* N = Instance_Create(V->Type);
	N->Copy(V);
	N->Spawn();
	{
		// inc ref counter
		xrSRWLockGuard g(ModelsLock, true);
		auto it = std::lower_bound(Models.begin(), Models.end(), V, std::less<>{});
		if (it != Models.end() && it->model == V)
		{
			it->refs++;
		}
		return N;
	}
}

dxRender_Visual* CModelPool::Instance_Load(const char* N, BOOL allow_register, bool assert)
{
	dxRender_Visual* V;
	string_path fn;
	string_path name;

	// Add default ext if no ext at all
	if (0 == strext(N)) strconcat(sizeof(name), name, N, ".ogf");
	else xr_strcpy(name, sizeof(name), N);

	// Load data from MESHES or LEVEL
	if (!FS.exist(N))
	{
		if (!FS.exist(fn, "$level$", name))
			if (!FS.exist(fn, "$game_meshes$", name))
			{
#ifdef _EDITOR
				Msg("!Can't find model file '%s'.",name);
                return 0;
#else
				if (assert)	
					Debug.fatal(DEBUG_INFO, "Can't find model file '%s'.", name);
				else
					return nullptr;
#endif
			}
	}
	else
	{
		xr_strcpy(fn, N);
	}

	// Actual loading
#ifdef DEBUG
	if (bLogging)		Msg		("- Uncached model loading: %s",fn);
#endif // DEBUG

	IReader* data = FS.r_open(fn);
	ogf_header H;
	data->r_chunk_safe(OGF_HEADER, &H, sizeof(H));
	V = Instance_Create(H.type);
	V->Load(N, data, 0);
	FS.r_close(data);
	g_pGamePersistent->RegisterModel(V);

	// Registration
	if (allow_register) 
		V = Instance_Register(N, V);

	return V;
}

dxRender_Visual* CModelPool::Instance_Load(LPCSTR name, IReader* data, BOOL allow_register)
{
	dxRender_Visual* V;

	ogf_header H;
	data->r_chunk_safe(OGF_HEADER, &H, sizeof(H));
	V = Instance_Create(H.type);
	V->Load(name, data, 0);

	// Registration
	if (allow_register) 
		V = Instance_Register(name, V);
	return V;
}

dxRender_Visual* CModelPool::Instance_Register(LPCSTR N, dxRender_Visual* V)
{
	// Registration
	shared_str S(N);
	xrSRWLockGuard g(ModelsLock);

	// Double-check for duplicate model
	for (auto& M : Models)
	{
		if (M.name == S)
		{
			// Increment the reference count
			M.refs++;

			// Destroy the redundant one we just loaded
			V->Release();
			xr_delete(V);

			// Return the existing model
			return M.model;
		}
	}

	ModelDef M;
	M.name = S;
	M.model = V;

	auto it = std::lower_bound(Models.begin(), Models.end(), V, std::less<>{});
	Models.insert(it, std::move(M));

	return V;
}


void CModelPool::Destroy()
{
	InvalidateBlueprints();

	// Pool
	Pool.clear();

	// Registry
	while (!Registry.empty())
	{
		REGISTRY_IT it = Registry.begin();
		dxRender_Visual* V = (dxRender_Visual*)it->first;
		if (!V)
		{
			Registry.erase(it);
			continue;
		}
#ifdef _DEBUG
		Msg				("ModelPool: Destroy object: '%s'",*V->dbg_name);
#endif
		DeleteInternal(V,TRUE);
	}

	// Base/Reference
	{
		xrSRWLockGuard g(ModelsLock);
		for (auto& M : Models)
		{
			M.model->Release();
			xr_delete(M.model);
		}
		Models.clear();
	}

	// cleanup motions container
	g_pMotionsContainer->clean(false);
}

CModelPool::CModelPool()
{
	bLogging = TRUE;
	bForceDiscard = FALSE;
	bAllowChildrenDuplicate = TRUE;
	g_pMotionsContainer = xr_new<motions_container>();
}

CModelPool::~CModelPool()
{
	Destroy();
	xr_delete(g_pMotionsContainer);
}

void CModelPool::InvalidateBlueprints()
{
	xrCriticalSectionGuard guard(modelBlueprintLock);
	modelBlueprints.clear();
}

dxRender_Visual* CModelPool::Instance_Find(LPCSTR N)
{
	shared_str S(N);
	dxRender_Visual* Model = 0;
	xrSRWLockGuard g(ModelsLock, true);

	for (auto& M : Models)
	{
		if (S == M.name)
		{
			Model = M.model;
			break;
		}
	}
	return Model;
}

dxRender_Visual* CModelPool::Create(const char* name, IReader* data, bool assert)
{
#ifdef _EDITOR
	if (!name||!name[0])	return 0;
#endif
	string_path low_name;
	VERIFY(xr_strlen(name)<sizeof(low_name));
	xr_strcpy(low_name, name);
	strlwr(low_name);
	if (strext(low_name)) *strext(low_name) = 0;
	
	// 0. Search POOL
	POOL_IT it = Pool.find(low_name);
	if (it != Pool.end())
	{
		// 1. Instance found
		dxRender_Visual* Model = it->second;
		Model->Spawn();
		Pool.erase(it);
		return Model;
	}
	else
	{
		// 1. Search for already loaded model (reference, base model)
		dxRender_Visual* Base = Instance_Find(low_name);

		if (0 == Base)
		{
			// 2. If not found
			bAllowChildrenDuplicate = FALSE;
			if (data) Base = Instance_Load(low_name, data, TRUE);
			else Base = Instance_Load(low_name, TRUE, assert);
			if (!Base)
			{
				// If not found and assert is false, return nullptr
				return nullptr;
			}
			bAllowChildrenDuplicate = TRUE;
#ifdef _EDITOR
			if (!Base)		return 0;
#endif
		}
		// 3. If found - return (cloned) reference
		dxRender_Visual* Model = Instance_Duplicate(Base);
		Registry.insert(mk_pair(Model, low_name));
		return Model;
	}
}

dxRender_Visual* CModelPool::CreateChild(LPCSTR name, IReader* data)
{
	string256 low_name;
	VERIFY(xr_strlen(name)<256);
	xr_strcpy(low_name, name);
	strlwr(low_name);
	if (strext(low_name)) *strext(low_name) = 0;

	// 1. Search for already loaded model
	dxRender_Visual* Base = Instance_Find(low_name);
	//.	if (0==Base) Base	 	= Instance_Load(name,data,FALSE);
	if (0 == Base)
	{
		if (data) Base = Instance_Load(low_name, data,FALSE);
		else Base = Instance_Load(low_name,FALSE);
	}

	dxRender_Visual* Model = bAllowChildrenDuplicate ? Instance_Duplicate(Base) : Base;
	return Model;
}

extern  xr_atomic_bool ENGINE_API g_bRendering; 

void CModelPool::DeleteInternal(dxRender_Visual* & V, BOOL bDiscard)
{
	VERIFY(!g_bRendering);
	if (!V) return;
	V->Depart();
	if (bDiscard || bForceDiscard)
	{
		Discard(V, TRUE);
	}
	else
	{
		//
		REGISTRY_IT it = Registry.find(V);
		if (it != Registry.end())
		{
			// Registry entry found - move it to pool and reset changed shader/texture if necessary
			xr_vector<IRenderVisual*>* children = V->get_children();
			if (children)
				for (auto* child : *children)
					child->ResetShaderTexture();
			else
				V->ResetShaderTexture();

			Pool.insert(mk_pair(it->second, V));
		}
		else
		{
			// Registry entry not-found - just special type of visual / particles / etc.
			xr_delete(V);
		}
	}
	V = NULL;
}

void CModelPool::DeleteDeffered(dxRender_Visual* &V)
{
	if (nullptr==V)
		return;

	xrCriticalSectionGuard guard(&deffered_del_lock);

    if (std::find(ModelsToDeleteDeffer.begin(), ModelsToDeleteDeffer.end(), V) == ModelsToDeleteDeffer.end())
	    ModelsToDeleteDeffer.push_back(V);
	V = nullptr;
}

void CModelPool::Delete(dxRender_Visual* &V, BOOL bDiscard)
{
	if (nullptr==V)
		return;
	if (g_bRendering)
	{
		VERIFY(!bDiscard);
		ModelsToDelete.push_back(V);
	}
	else
	{
		DeleteInternal(V,bDiscard);
	}
	V =	nullptr;
}

void CModelPool::DeleteQueue()
{
	for (u32 it = 0; it < ModelsToDelete.size(); it++)
		DeleteInternal(ModelsToDelete[it]);
	ModelsToDelete.clear();
}

void CModelPool::DeleteQueuedDeffer()
{
	xrCriticalSectionGuard guard(&deffered_del_lock);

    for (dxRender_Visual* Vis : ModelsToDeleteDeffer)
    {
        if (Vis)
            DeleteInternal(Vis);
    }	

	ModelsToDeleteDeffer.clear();
}

void CModelPool::Discard(dxRender_Visual* & V, BOOL b_complete)
{
	//
	REGISTRY_IT it = Registry.find(V);
	if (it != Registry.end())
	{
		// Base
		const shared_str& name = it->second;
		xrSRWLockGuard g(ModelsLock);

		for (u32 i = 0; i < Models.size(); i++)
		{
			auto I = Models.begin() + i;
			if (I->name == name)
			{
				if (b_complete || strchr(*name, '#'))
				{
					VERIFY(I->refs>0);
					I->refs--;
					if (0 == I->refs)
					{
						bForceDiscard = TRUE;
						I->model->Release();
						xr_delete(I->model);
						Models.erase(I);
						bForceDiscard = FALSE;
					}
					break;
				}
				else
				{
					if (I->refs > 0)
						I->refs--;
					break;
				}
			}
		}
		// Registry
		xr_delete(V);
		Registry.erase(it);
	}
	else
	{
		// Registry entry not-found - just special type of visual / particles / etc.
		xr_delete(V);
	}
	V = NULL;
}

void CModelPool::Prefetch()
{
	Logging(FALSE);
	// prefetch visuals
	string256 section;
	strconcat(sizeof(section), section, "prefetch_visuals_", g_pGamePersistent->m_game_params.m_game_type);
	CInifile::Sect& sect = pSettings->r_section(section);
	for (CInifile::SectCIt I = sect.Data.begin(); I != sect.Data.end(); I++)
	{
		const CInifile::Item& item = *I;
		dxRender_Visual* V = Create(item.first.c_str());
		Delete(V,FALSE);
	}
	Logging(TRUE);
}

void CModelPool::Prefetch_One(LPCSTR N, bool assert)
{
	dxRender_Visual* V = Create(N, 0, assert);
	if (V)
		Delete(V,FALSE);
}

xr_shared_ptr<CModelPool::ModelBlueprint> CModelPool::PrepareBlueprint(LPCSTR name, LPCSTR canonical_level_path)
{
	const xr_string normalized = NormalizeModelName(name);
	xr_string resolved_path;
	const bool resolved = ResolveModelFile(name, canonical_level_path, resolved_path);
	xr_string key = normalized;
	key += '\n';
	key += resolved ? resolved_path : (canonical_level_path ? canonical_level_path : "");
	std::transform(key.begin(), key.end(), key.begin(), [](char value)
	{
		return value == '/' ? '\\' : char(tolower(u8(value)));
	});
	if (resolved)
		if (const CLocatorAPI::file* file = FS.exist(resolved_path.c_str()))
		{
			string128 identity;
			xr_sprintf(identity, "\n%08x:%08x:%08x:%08x", file->crc, file->size_real,
				file->size_compressed, file->modif);
			key += identity;
		}

	xr_shared_ptr<ModelBlueprint> blueprint;
	bool producer = false;
	{
		xrCriticalSectionGuard guard(modelBlueprintLock);
		auto found = modelBlueprints.find(key);
		if (found != modelBlueprints.end())
			blueprint = found->second;
		else
		{
			blueprint = xr_make_shared<ModelBlueprint>();
			modelBlueprints.emplace(std::move(key), blueprint);
			producer = true;
		}
	}

	if (producer)
	{
		try
		{
			if (resolved)
			{
				IReader* source = FS.r_open(resolved_path.c_str());
				if (source)
				{
					int size = 0;
					try
					{
						size = source->length();
						R_ASSERT(size > 0);
						blueprint->data.resize(size);
						CopyMemory(blueprint->data.data(), source->pointer(), size);
					}
					catch (...)
					{
						FS.r_close(source);
						throw;
					}
					FS.r_close(source);
					blueprint->found = true;

					xr_set<xr_string> visited;
					visited.insert(normalized);
					IReader texture_reader(blueprint->data.data(), size);
					CollectModelReaderTextures(texture_reader, canonical_level_path, blueprint->textures, visited);

#if RENDER == R_R4
					IReader header_reader(blueprint->data.data(), size);
					ogf_header header;
					R_ASSERT(header_reader.r_chunk_safe(OGF_HEADER, &header, sizeof(header)));
					IReader container_reader(blueprint->data.data(), size);
					const bool references_level_geometry = container_reader.find_chunk(OGF_GCONTAINER) ||
						container_reader.find_chunk(OGF_VCONTAINER) || container_reader.find_chunk(OGF_ICONTAINER) ||
						container_reader.find_chunk(OGF_FASTPATH);
					IReader local_geometry_reader(blueprint->data.data(), size);
					const bool has_local_geometry = local_geometry_reader.find_chunk(OGF_VERTICES) &&
						local_geometry_reader.find_chunk(OGF_INDICES);
					if (!references_level_geometry && has_local_geometry &&
						(header.type == MT_NORMAL || header.type == MT_PROGRESSIVE))
					{
						blueprint->preparedVisual = Instance_Create(header.type);
						const bool previous_defer = g_defer_visual_shader_creation;
						g_defer_visual_shader_creation = true;
						try
						{
							IReader visual_reader(blueprint->data.data(), size);
							blueprint->preparedVisual->Load(normalized.c_str(), &visual_reader, 0);
						}
						catch (...)
						{
							g_defer_visual_shader_creation = previous_defer;
							throw;
						}
						g_defer_visual_shader_creation = previous_defer;
					}
#endif
				}
			}
			std::sort(blueprint->textures.begin(), blueprint->textures.end());
			blueprint->textures.erase(std::unique(blueprint->textures.begin(), blueprint->textures.end()),
				blueprint->textures.end());
		}
		catch (...)
		{
			blueprint->failure = std::current_exception();
			SetEvent(blueprint->completed);
			throw;
		}
		SetEvent(blueprint->completed);
	}
	else
	{
		WaitForSingleObject(blueprint->completed, INFINITE);
		if (blueprint->failure)
			std::rethrow_exception(blueprint->failure);
	}
	return blueprint;
}

void CModelPool::CollectTextures(LPCSTR name, LPCSTR canonical_level_path, xr_vector<xr_string>& textures)
{
	xr_shared_ptr<ModelBlueprint> blueprint = PrepareBlueprint(name, canonical_level_path);
	textures.insert(textures.end(), blueprint->textures.begin(), blueprint->textures.end());
	std::sort(textures.begin(), textures.end());
	textures.erase(std::unique(textures.begin(), textures.end()), textures.end());
}

bool CModelPool::PrefetchPrepared(LPCSTR name, LPCSTR canonical_level_path, bool assert)
{
	xr_shared_ptr<ModelBlueprint> blueprint = PrepareBlueprint(name, canonical_level_path);
	if (!blueprint->found)
	{
		if (assert)
			Prefetch_One(name, true);
		return false;
	}

	xrCriticalSectionGuard guard(blueprint->commitLock);
	const xr_string normalized = NormalizeModelName(name);
	dxRender_Visual* prepared = blueprint->preparedVisual;
	blueprint->preparedVisual = nullptr;
	if (Instance_Find(normalized.c_str()))
	{
		if (prepared)
		{
			prepared->Release();
			xr_delete(prepared);
		}
	}
	else
	{
		dxRender_Visual* base = prepared;
		try
		{
			if (base)
				base->CommitShaderTexture();
			else
			{
				IReader data(blueprint->data.data(), static_cast<int>(blueprint->data.size()));
				const BOOL previous_allow_children_duplicate = bAllowChildrenDuplicate;
				bAllowChildrenDuplicate = FALSE;
				try
				{
					base = Instance_Load(normalized.c_str(), &data, FALSE);
				}
				catch (...)
				{
					bAllowChildrenDuplicate = previous_allow_children_duplicate;
					throw;
				}
				bAllowChildrenDuplicate = previous_allow_children_duplicate;
			}
			g_pGamePersistent->RegisterModel(base);
			Instance_Register(normalized.c_str(), base);
			base = nullptr;
		}
		catch (...)
		{
			if (base)
			{
				base->Release();
				xr_delete(base);
			}
			throw;
		}
	}

	Prefetch_One(normalized.c_str(), assert);
	return true;
}

bool CModelPool::Exists(LPCSTR N)
{
	string_path low_name;
	VERIFY(xr_strlen(N) < sizeof(low_name));
	xr_strcpy(low_name, N);
	strlwr(low_name);
	if (strext(low_name)) *strext(low_name) = 0;

	// Search pool and return early if exists
	POOL_IT it = Pool.find(low_name);
	if (it != Pool.end())
		return true;

	// Search for already loaded model (reference, base model) and return early if exists
	dxRender_Visual* Base = Instance_Find(low_name);
	if (Base)
		return true;

	// Prefetch model
	dxRender_Visual* V = Create(N, 0, false);
	if (V) 
	{
		Delete(V, FALSE);
		return true;
	}

	return false;
}

dxRender_Visual* CModelPool::CreatePE(PS::CPEDef* source)
{
	PS::CParticleEffect* V = (PS::CParticleEffect*)Instance_Create(MT_PARTICLE_EFFECT);
	V->Compile(source);
	return V;
}

dxRender_Visual* CModelPool::CreatePG(PS::CPGDef* source)
{
	PS::CParticleGroup* V = (PS::CParticleGroup*)Instance_Create(MT_PARTICLE_GROUP);
	V->Compile(source);
	return V;
}

void CModelPool::ClearPool(BOOL b_complete)
{
	POOL_IT _I = Pool.begin();
	POOL_IT _E = Pool.end();
	for (; _I != _E; _I++)
	{
		Discard(_I->second, b_complete);
	}
	Pool.clear();
}

void CModelPool::dump()
{
	Log("--- model pool --- begin:");
	u32 sz = 0;
	u32 k = 0;
	xrSRWLockGuard g(ModelsLock, true);
	for (xr_vector<ModelDef>::iterator I = Models.begin(); I != Models.end(); I++)
	{
		CKinematics* K = PCKinematics(I->model);
		if (K)
		{
			u32 cur = K->mem_usage(false);
			sz += cur;
			Msg("#%3d: [%3d/%5d Kb] - %s", k++, I->refs, cur / 1024, I->name.c_str());
		}
	}
	Msg("--- models: %d, mem usage: %d Kb ", k, sz / 1024);
	sz = 0;
	k = 0;
	int free_cnt = 0;
	for (REGISTRY_IT it = Registry.begin(); it != Registry.end(); it++)
	{
		CKinematics* K = PCKinematics((dxRender_Visual*)it->first);
		VERIFY(K);
		if (K)
		{
			u32 cur = K->mem_usage(true);
			sz += cur;
			bool b_free = (Pool.find(it->second) != Pool.end());
			if (b_free) ++free_cnt;
			Msg("#%3d: [%s] [%5d Kb] - %s", k++, (b_free) ? "free" : "used", cur / 1024, it->second.c_str());
		}
	}
	Msg("--- instances: %d, free %d, mem usage: %d Kb ", k, free_cnt, sz / 1024);
	Log("--- model pool --- end.");
}

void CModelPool::memory_stats(u32& vb_mem_video, u32& vb_mem_system, u32& ib_mem_video, u32& ib_mem_system)
{
	vb_mem_video = 0;
	vb_mem_system = 0;
	ib_mem_video = 0;
	ib_mem_system = 0;

	xr_vector<ModelDef>::iterator it = Models.begin();
	xr_vector<ModelDef>::const_iterator en = Models.end();
	xrSRWLockGuard g(ModelsLock, true);

	for (; it != en; ++it)
	{
		dxRender_Visual* ptr = it->model;
		Fvisual* vis_ptr = fast_dynamic_cast<Fvisual*>(ptr);

		if (vis_ptr == NULL)
			continue;
#if !defined(USE_DX10) && !defined(USE_DX11)
		D3DINDEXBUFFER_DESC IB_desc;
		D3DVERTEXBUFFER_DESC VB_desc;

		vis_ptr->m_fast->p_rm_Indices->GetDesc(&IB_desc);

		if (IB_desc.Pool == D3DPOOL_DEFAULT ||
			IB_desc.Pool == D3DPOOL_MANAGED)
			ib_mem_video += IB_desc.Size;

		if (IB_desc.Pool == D3DPOOL_MANAGED ||
			IB_desc.Pool == D3DPOOL_SCRATCH)
			ib_mem_system += IB_desc.Size;

		vis_ptr->m_fast->p_rm_Vertices->GetDesc(&VB_desc);

		if (VB_desc.Pool == D3DPOOL_DEFAULT ||
			VB_desc.Pool == D3DPOOL_MANAGED)
			vb_mem_video += IB_desc.Size;

		if (VB_desc.Pool == D3DPOOL_MANAGED ||
			VB_desc.Pool == D3DPOOL_SCRATCH)
			vb_mem_system += IB_desc.Size;

#else
		D3D_BUFFER_DESC IB_desc;
		D3D_BUFFER_DESC VB_desc;

		vis_ptr->m_fast->p_rm_Indices->GetDesc(&IB_desc);

		ib_mem_video += IB_desc.ByteWidth;
		ib_mem_system += IB_desc.ByteWidth;

		vis_ptr->m_fast->p_rm_Vertices->GetDesc(&VB_desc);

		vb_mem_video += IB_desc.ByteWidth;
		vb_mem_system += IB_desc.ByteWidth;

#endif
	}
}

#ifdef _EDITOR
IC bool	_IsBoxVisible(dxRender_Visual* visual, const Fmatrix& transform)
{
    Fbox 		bb; 
    bb.xform	(visual->vis.box,transform);
    return 		::Render->occ_visible(bb);
}
IC bool	_IsValidShader(dxRender_Visual* visual, u32 priority, bool strictB2F)
{
	if (visual->shader)
        return (priority==visual->shader->E[0]->flags.iPriority)&&(strictB2F==visual->shader->E[0]->flags.bStrictB2F);
    return false;
}

void 	CModelPool::Render(dxRender_Visual* m_pVisual, const Fmatrix& mTransform, int priority, bool strictB2F, float m_fLOD)
{
    // render visual
    xr_vector<dxRender_Visual*>::iterator I,E;
    switch (m_pVisual->Type){
    case MT_SKELETON_ANIM:
    case MT_SKELETON_RIGID:{
        if (_IsBoxVisible(m_pVisual,mTransform)){
            CKinematics* pV		= fast_dynamic_cast<CKinematics*>(m_pVisual); VERIFY(pV);
            if (fis_zero(m_fLOD,EPS)&&pV->m_lod){
		        if (_IsValidShader(pV->m_lod,priority,strictB2F)){
	                RCache.set_Shader		(pV->m_lod->shader?pV->m_lod->shader:EDevice.m_WireShader);
    	            RCache.set_xform_world	(mTransform);
        	        pV->m_lod->Render		(1.f);
                }
            }else{
                I = pV->children.begin		();
                E = pV->children.end		();
                for (; I!=E; I++){
                    if (_IsValidShader(*I,priority,strictB2F)){
                        RCache.set_Shader		((*I)->shader?(*I)->shader:EDevice.m_WireShader);
                        RCache.set_xform_world	(mTransform);
                        (*I)->Render		 	(m_fLOD);
                    }
                }
            }
        }
    }break;
    case MT_HIERRARHY:{
        if (_IsBoxVisible(m_pVisual,mTransform)){
            FHierrarhyVisual* pV		= fast_dynamic_cast<FHierrarhyVisual*>(m_pVisual); VERIFY(pV);
            I = pV->children.begin		();
            E = pV->children.end		();
            for (; I!=E; I++){
		        if (_IsValidShader(*I,priority,strictB2F)){
	                RCache.set_Shader		((*I)->shader?(*I)->shader:EDevice.m_WireShader);
    	            RCache.set_xform_world	(mTransform);
        	        (*I)->Render		 	(m_fLOD);
                }
            }
        }
    }break;
    case MT_PARTICLE_GROUP:{
        PS::CParticleGroup* pG			= fast_dynamic_cast<PS::CParticleGroup*>(m_pVisual); VERIFY(pG);
//		if (_IsBoxVisible(m_pVisual,mTransform))
        {
            RCache.set_xform_world	  		(mTransform);
            for (PS::CParticleGroup::SItemVecIt i_it=pG->items.begin(); i_it!=pG->items.end(); i_it++){
                xr_vector<dxRender_Visual*>	visuals;
                i_it->GetVisuals			(visuals);
                for (xr_vector<dxRender_Visual*>::iterator it=visuals.begin(); it!=visuals.end(); it++)
                    Render					(*it,Fidentity,priority,strictB2F,m_fLOD);
            }
        }
    }break;
    case MT_PARTICLE_EFFECT:{
//		if (_IsBoxVisible(m_pVisual,mTransform))
        {
            if (_IsValidShader(m_pVisual,priority,strictB2F)){
                RCache.set_Shader			(m_pVisual->shader?m_pVisual->shader:EDevice.m_WireShader);
                RCache.set_xform_world		(mTransform);
                m_pVisual->Render		 	(m_fLOD);
            }
        }
    }break;
    default:
        if (_IsBoxVisible(m_pVisual,mTransform)){
            if (_IsValidShader(m_pVisual,priority,strictB2F)){
                RCache.set_Shader			(m_pVisual->shader?m_pVisual->shader:EDevice.m_WireShader);
                RCache.set_xform_world		(mTransform);
                m_pVisual->Render		 	(m_fLOD);
            }
        }
        break;
    }
}

void 	CModelPool::RenderSingle(dxRender_Visual* m_pVisual, const Fmatrix& mTransform, float m_fLOD)
{
	for (int p=0; p<4; p++){
    	Render(m_pVisual,mTransform,p,false,m_fLOD);
    	Render(m_pVisual,mTransform,p,true,m_fLOD);
    }
}
void CModelPool::OnDeviceDestroy()
{
	Destroy();
}
#endif
