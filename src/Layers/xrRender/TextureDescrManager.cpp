#include "stdafx.h"
#pragma hdrstop
#include "TextureDescrManager.h"
#include "ETextureParams.h"
#include "profiler.h"

// eye-params
float r__dtex_range = 50;

class cl_dt_scaler : public R_constant_setup
{
public:
	float scale;

	cl_dt_scaler(float s) : scale(s)
	{
	};

	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, scale, scale, scale, 1 / r__dtex_range);
	}
};

void fix_texture_thm_name(LPSTR fn)
{
	LPSTR _ext = strext(fn);
	if (_ext &&
		(0 == stricmp(_ext, ".tga") ||
			0 == stricmp(_ext, ".thm") ||
			0 == stricmp(_ext, ".dds") ||
			0 == stricmp(_ext, ".bmp") ||
			0 == stricmp(_ext, ".ogm") ||
            0 == stricmp(_ext, ".gif")))
		*_ext = 0;
}

void CTextureDescrMngr::LoadTHM(LPCSTR initial, map_TD& s_texture_details, map_CS& s_detail_scalers)
{
	PROF_EVENT();

	FS_FileSet flist;
	FS.file_list(flist, initial, FS_ListFiles, "*.thm");

	struct PreparedThm
	{
		xr_string file;
		xr_string name;
		xr_string detail_name;
		xr_string bump_name;
		u32 flags = STextureParams::flGenerateMipMaps | STextureParams::flDitherColor;
		u32 material = STextureParams::tmBlin_Phong;
		float detail_scale = 1.f;
		float material_weight = 0.f;
		STextureParams::ETType type = STextureParams::ttImage;
		STextureParams::ETBumpMode bump_mode = STextureParams::tbmNone;
	};

	CTimer timer;
	timer.Start();
	xr_vector<PreparedThm> prepared(flist.size());
	u32 sourceIndex = 0;
	for (const FS_File& fs_iter : flist)
	{
		PreparedThm& result = prepared[sourceIndex++];
		result.file = fs_iter.name.c_str();
		string_path name;
		xr_strcpy(name, fs_iter.name.c_str());
		fix_texture_thm_name(name);
		result.name = name;
	}

	xr_parallel_for(0u, static_cast<u32>(prepared.size()), [&](u32 index)
	{
		PreparedThm& result = prepared[index];
		string_path path;
		FS.update_path(path, initial, result.file.c_str());
		IReader* F = FS.r_open(path);

		R_ASSERT(F->find_chunk(THM_CHUNK_TYPE));
		F->r_u32();
		R_ASSERT(F->find_chunk(THM_CHUNK_TEXTUREPARAM));
		F->r_u32();
		result.flags = F->r_u32();
		if (F->find_chunk(THM_CHUNK_TEXTURE_TYPE))
			result.type = static_cast<STextureParams::ETType>(F->r_u32());
		if (F->find_chunk(THM_CHUNK_DETAIL_EXT))
		{
			F->r_stringZ(result.detail_name);
			result.detail_scale = F->r_float();
		}
		if (F->find_chunk(THM_CHUNK_MATERIAL))
		{
			result.material = F->r_u32();
			result.material_weight = F->r_float();
		}
		if (F->find_chunk(THM_CHUNK_BUMP))
		{
			F->r_float();
			result.bump_mode = static_cast<STextureParams::ETBumpMode>(F->r_u32());
			if (result.bump_mode < STextureParams::tbmNone)
				result.bump_mode = STextureParams::tbmNone;
			F->r_stringZ(result.bump_name);
		}
		FS.r_close(F);
	});
	const u32 prepareMs = timer.GetElapsed_ms();

	for (const PreparedThm& result : prepared)
	{
		if (STextureParams::ttImage == result.type || STextureParams::ttTerrain == result.type ||
			STextureParams::ttNormalMap == result.type)
		{
			texture_desc& desc = s_texture_details[result.name.c_str()];
			cl_dt_scaler*& dts = s_detail_scalers[result.name.c_str()];

			if (!result.detail_name.empty() &&
				(result.flags & (STextureParams::flDiffuseDetail | STextureParams::flBumpDetail)))
			{
				if (desc.m_assoc)
					xr_delete(desc.m_assoc);

				desc.m_assoc = xr_new<texture_assoc>();
				desc.m_assoc->detail_name = result.detail_name.c_str();
				if (dts)
					dts->scale = result.detail_scale;
				else
					/*desc.m_assoc->cs*/dts = xr_new<cl_dt_scaler>(result.detail_scale);

				desc.m_assoc->usage = 0;

				if (result.flags & STextureParams::flDiffuseDetail)
					desc.m_assoc->usage |= (1 << 0);

				if (result.flags & STextureParams::flBumpDetail)
					desc.m_assoc->usage |= (1 << 1);
			}
			if (desc.m_spec)
				xr_delete(desc.m_spec);

			desc.m_spec = xr_new<texture_spec>();
			desc.m_spec->m_material =
				result.material + (result.material < 4 ? result.material_weight : 0);
			desc.m_spec->m_use_steep_parallax = false;

			if (result.bump_mode == STextureParams::tbmUse)
			{
				desc.m_spec->m_bump_name = result.bump_name.c_str();
			}
			else if (result.bump_mode == STextureParams::tbmUseParallax)
			{
				desc.m_spec->m_bump_name = result.bump_name.c_str();
				desc.m_spec->m_use_steep_parallax = true;
			}
		}
	}
	Msg("* [STARTUP/THM] source=%s files=%u prepare=%u ms commit=%u ms total=%u ms",
		initial, static_cast<u32>(prepared.size()), prepareMs, timer.GetElapsed_ms() - prepareMs, timer.GetElapsed_ms());
}

void CTextureDescrMngr::Load()
{
	map_TD gameDetails;
	map_TD levelDetails;
	map_CS gameScalers;
	map_CS levelScalers;
	std::exception_ptr gameFailure;
	std::exception_ptr levelFailure;

	xr_task_group scans;
	scans.run([&]()
	{
		try
		{
			LoadTHM("$game_textures$", gameDetails, gameScalers);
		}
		catch (...)
		{
			gameFailure = std::current_exception();
		}
	});
	scans.run([&]()
	{
		try
		{
			LoadTHM("$level$", levelDetails, levelScalers);
		}
		catch (...)
		{
			levelFailure = std::current_exception();
		}
	});
	scans.wait();

	auto clearTemporary = [](map_TD& details, map_CS& scalers)
	{
		for (auto& item : details)
		{
			xr_delete(item.second.m_assoc);
			xr_delete(item.second.m_spec);
		}
		for (auto& item : scalers)
			xr_delete(item.second);
		details.clear();
		scalers.clear();
	};

	if (gameFailure || levelFailure)
	{
		clearTemporary(gameDetails, gameScalers);
		clearTemporary(levelDetails, levelScalers);
		std::rethrow_exception(gameFailure ? gameFailure : levelFailure);
	}

	xrSRWLockGuard dataGuard(m_data_lock);
	auto merge = [&](map_TD& details, map_CS& scalers)
	{
		for (auto& item : details)
		{
			texture_desc& destination = m_texture_details[item.first];
			xr_delete(destination.m_assoc);
			xr_delete(destination.m_spec);
			destination.m_assoc = item.second.m_assoc;
			destination.m_spec = item.second.m_spec;
			item.second.m_assoc = nullptr;
			item.second.m_spec = nullptr;

			auto sourceScaler = scalers.find(item.first);
			if (sourceScaler == scalers.end())
				continue;
			cl_dt_scaler*& destinationScaler = m_detail_scalers[item.first];
			if (destinationScaler)
			{
				destinationScaler->scale = sourceScaler->second->scale;
				xr_delete(sourceScaler->second);
			}
			else
			{
				destinationScaler = sourceScaler->second;
			}
			sourceScaler->second = nullptr;
		}
		clearTemporary(details, scalers);
	};

	// Level THMs override game THMs deterministically, as intended by the old two-source load.
	merge(gameDetails, gameScalers);
	merge(levelDetails, levelScalers);
}

void CTextureDescrMngr::UnLoad()
{
	xrSRWLockGuard dataGuard(m_data_lock);
	for (auto& it : m_texture_details)
	{
		xr_delete(it.second.m_assoc);
		xr_delete(it.second.m_spec);
	}
	m_texture_details.clear();
}

CTextureDescrMngr::~CTextureDescrMngr()
{
	xrSRWLockGuard dataGuard(m_data_lock);
	map_CS::iterator I = m_detail_scalers.begin();
	map_CS::iterator E = m_detail_scalers.end();

	for (; I != E; ++I)
		xr_delete(I->second);

	m_detail_scalers.clear();
}

shared_str CTextureDescrMngr::GetBumpName(const shared_str& tex_name) const
{
	xrSRWLockGuard dataGuard(m_data_lock, true);
	map_TD::const_iterator I = m_texture_details.find(tex_name);
	if (I != m_texture_details.end())
	{
		if (I->second.m_spec)
		{
			return I->second.m_spec->m_bump_name;
		}
	}
	return "";
}

BOOL CTextureDescrMngr::UseSteepParallax(const shared_str& tex_name) const
{
	xrSRWLockGuard dataGuard(m_data_lock, true);
	map_TD::const_iterator I = m_texture_details.find(tex_name);
	if (I != m_texture_details.end())
	{
		if (I->second.m_spec)
		{
			return I->second.m_spec->m_use_steep_parallax;
		}
	}
	return FALSE;
}

float CTextureDescrMngr::GetMaterial(const shared_str& tex_name) const
{
	xrSRWLockGuard dataGuard(m_data_lock, true);
	map_TD::const_iterator I = m_texture_details.find(tex_name);
	if (I != m_texture_details.end())
	{
		if (I->second.m_spec)
		{
			return I->second.m_spec->m_material;
		}
	}
	return 1.0f;
}

void CTextureDescrMngr::GetTextureUsage(const shared_str& tex_name, BOOL& bDiffuse, BOOL& bBump) const
{
	xrSRWLockGuard dataGuard(m_data_lock, true);
	map_TD::const_iterator I = m_texture_details.find(tex_name);
	if (I != m_texture_details.end())
	{
		if (I->second.m_assoc)
		{
			u8 usage = I->second.m_assoc->usage;
			bDiffuse = !!(usage & (1 << 0));
			bBump = !!(usage & (1 << 1));
		}
	}
}

BOOL CTextureDescrMngr::GetDetailTexture(const shared_str& tex_name, LPCSTR& res, R_constant_setup* & CS) const
{
	xrSRWLockGuard dataGuard(m_data_lock, true);
	map_TD::const_iterator I = m_texture_details.find(tex_name);
	if (I != m_texture_details.end())
	{
		if (I->second.m_assoc)
		{
			texture_assoc* TA = I->second.m_assoc;
			res = TA->detail_name.c_str();
			map_CS::const_iterator It2 = m_detail_scalers.find(tex_name);
			CS = It2 == m_detail_scalers.end() ? 0 : It2->second; //TA->cs;
			return TRUE;
		}
	}
	return FALSE;
}
