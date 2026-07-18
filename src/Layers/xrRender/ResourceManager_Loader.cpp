#include "stdafx.h"
#pragma hdrstop

#include "ResourceManager.h"
#include "blenders\blender.h"


void CResourceManager::OnDeviceDestroy(BOOL)
{
	if (RDEVICE.b_is_Ready) return;
	WaitForTextureLoads();
	m_level_shader_cache.clear();
	m_level_shader_jobs.clear();
	m_reduceLodTextureList.clear();
	m_textures_description.UnLoad();

	// Matrices
	for (map_Matrix::iterator m = m_matrices.begin(); m != m_matrices.end(); m++)
	{
		R_ASSERT(1==m->second->dwReference.load(std::memory_order_relaxed));
		xr_delete(m->second);
	}
	m_matrices.clear();

	// Constants
	for (map_Constant::iterator c = m_constants.begin(); c != m_constants.end(); c++)
	{
		R_ASSERT(1==c->second->dwReference.load(std::memory_order_relaxed));
		xr_delete(c->second);
	}
	m_constants.clear();

	// Release blenders
	for (map_BlenderIt b = m_blenders.begin(); b != m_blenders.end(); b++)
	{
		xr_free((char*&)b->first);
		IBlender::Destroy(b->second);
	}
	m_blenders.clear();

	// destroy TD
	for (map_TDIt _t = m_td.begin(); _t != m_td.end(); _t++)
	{
		xr_free((char*&)_t->first);
		xr_free((char*&)_t->second.T);
		xr_delete(_t->second.cs);
	}
	m_td.clear();

	// scripting
#ifndef _EDITOR
	LS_Unload();
#endif
}

void CResourceManager::OnDeviceCreate(IReader* F)
{
	if (!RDEVICE.b_is_Ready) return;

	CTimer startupTimer;
	startupTimer.Start();
	string256 name;

#ifndef _EDITOR
	// scripting
	LS_Load();
#endif
	const u32 scriptingMs = startupTimer.GetElapsed_ms();
	IReader* fs = 0;
	// Load constants
	fs = F->open_chunk(0);
	if (fs)
	{
		while (!fs->eof())
		{
			fs->r_stringZ(name, sizeof(name));
			CConstant* C = _CreateConstant(name);
			C->Load(fs);
		}
		fs->close();
	}

	// Load matrices
	fs = F->open_chunk(1);
	if (fs)
	{
		while (!fs->eof())
		{
			fs->r_stringZ(name, sizeof(name));
			CMatrix* M = _CreateMatrix(name);
			M->Load(fs);
		}
		fs->close();
	}

	// Load blenders
	fs = F->open_chunk(2);
	if (fs)
	{
		IReader* chunk = NULL;
		int chunk_id = 0;

		while ((chunk = fs->open_chunk(chunk_id)) != NULL)
		{
			CBlender_DESC desc;
			chunk->r(&desc, sizeof(desc));
#if RENDER != R_R1
			if (desc.CLS == B_SHADOW_WORLD)
			{
				chunk->close();
				chunk_id += 1;
				continue;
			}
#endif
			IBlender* B = IBlender::Create(desc.CLS);
			if (0 == B)
			{
				Msg("! Renderer doesn't support blender '%s'", desc.cName);
			}
			else
			{
				if (B->getDescription().version != desc.version)
				{
					Msg("! Version conflict in shader '%s'", desc.cName);
				}

				chunk->seek(0);
				B->Load(*chunk, desc.version);

				std::pair<map_BlenderIt, bool> I = m_blenders.insert(mk_pair(xr_strdup(desc.cName), B));
				R_ASSERT2(I.second, "shader.xr - found duplicate name!!!");
			}
			chunk->close();
			chunk_id += 1;
		}
		fs->close();
	}

	const u32 libraryMs = startupTimer.GetElapsed_ms() - scriptingMs;
	m_textures_description.Load();
	const u32 texturesMs = startupTimer.GetElapsed_ms() - scriptingMs - libraryMs;
	m_reduceLodTextureList.clear();
	if (pSettings && pSettings->section_exist("reduce_lod_texture_list"))
	{
		const CInifile::Sect& section = pSettings->r_section("reduce_lod_texture_list");
		m_reduceLodTextureList.reserve(section.Data.size());
		for (CInifile::SectCIt item = section.Data.begin(); item != section.Data.end(); ++item)
			m_reduceLodTextureList.push_back(item->first);
	}
	const u32 settingsMs = startupTimer.GetElapsed_ms() - scriptingMs - libraryMs - texturesMs;
	Msg("* [STARTUP/RENDER RESOURCES] scripting=%u library=%u textures=%u settings=%u total=%u ms",
		scriptingMs, libraryMs, texturesMs, settingsMs, startupTimer.GetElapsed_ms());
}

void CResourceManager::OnDeviceCreate(LPCSTR shName)
{
#ifdef _EDITOR
	if (!FS.exist(shName)) return;
#endif

	// Check if file is compressed already
	string32 ID = "shENGINE";
	string32 id;
	IReader* F = FS.r_open(shName);
	R_ASSERT2(F, shName);
	F->r(&id, 8);
	if (0 == strncmp(id, ID, 8))
	{
		FATAL("Unsupported blender library. Compressed?");
	}
	OnDeviceCreate(F);
	FS.r_close(F);
}

void CResourceManager::StoreNecessaryTextures()
{
	xrCriticalSectionGuard guard(creationGuard);
	if (!m_necessary.empty())
		return;

	map_TextureIt it = m_textures.begin();
	map_TextureIt it_e = m_textures.end();

	for (; it != it_e; ++it)
	{
		LPCSTR texture_name = it->first;
		if (strstr(texture_name, "\\levels\\")) continue;
		if (!strchr(texture_name, '\\')) continue;

		ref_texture T;
		T.create(texture_name);
		m_necessary.push_back(T);
	}
}

void CResourceManager::DestroyNecessaryTextures()
{
	m_necessary.clear();
	xr_map<CTexture*, ref_texture> prefetched;
	{
		xrCriticalSectionGuard guard(creationGuard);
		prefetched.swap(m_prefetchedTextures);
	}
}
