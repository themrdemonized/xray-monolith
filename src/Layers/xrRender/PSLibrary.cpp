//----------------------------------------------------
// file: PSLibrary.cpp
//----------------------------------------------------
#include "stdafx.h"
#pragma hdrstop

#include "PSLibrary.h"
#include "ParticleEffect.h"
#include "ParticleGroup.h"
#include "../../xrCore/_thread_types.h"

#ifdef _EDITOR
#	include "ParticleEffectActions.h"
#include "../ECore/Editor/ui_main.h"
#endif

#define _game_data_			"$game_data$"

bool ped_sort_pred(const PS::CPEDef* a, const PS::CPEDef* b) { return xr_strcmp(a->Name(), b->Name()) < 0; }
bool pgd_sort_pred(const PS::CPGDef* a, const PS::CPGDef* b) { return xr_strcmp(a->m_Name, b->m_Name) < 0; }

bool ped_find_pred(const PS::CPEDef* a, LPCSTR b) { return xr_strcmp(a->Name(), b) < 0; }
bool pgd_find_pred(const PS::CPGDef* a, LPCSTR b) { return xr_strcmp(a->m_Name, b) < 0; }
//----------------------------------------------------
void CPSLibrary::OnCreate()
{
#ifdef _EDITOR
    if(pCreateEAction)
    {
        Load2();
    }else
#endif
	{
		string_path fn;
		FS.update_path(fn,_game_data_, "particles.xr");
		Load(fn);
	}
}

void CPSLibrary::OnDestroy()
{
	for (PS::PEDIt e_it = m_PEDs.begin(); e_it != m_PEDs.end(); ++e_it)
		(*e_it)->DestroyShader();

	for (PS::PEDIt e_it = m_PEDs.begin(); e_it != m_PEDs.end(); e_it++)
		xr_delete(*e_it);
	m_PEDs.clear();

	for (PS::PGDIt g_it = m_PGDs.begin(); g_it != m_PGDs.end(); ++g_it)
		xr_delete(*g_it);
	m_PGDs.clear();
    m_all_ps.clear();
}

//----------------------------------------------------
PS::PEDIt CPSLibrary::FindPEDIt(LPCSTR Name)
{
	if (!Name) return m_PEDs.end();
#ifdef _EDITOR
	for (PS::PEDIt it=m_PEDs.begin(); it!=m_PEDs.end(); it++)
    	if (0==xr_strcmp((*it)->Name(),Name)) return it;
	return m_PEDs.end();
#else
	PS::PEDIt I = std::lower_bound(m_PEDs.begin(), m_PEDs.end(), Name, ped_find_pred);
	if (I == m_PEDs.end() || (0 != xr_strcmp((*I)->m_Name, Name))) return m_PEDs.end();
	else return I;
#endif
}

PS::CPEDef* CPSLibrary::FindPED(LPCSTR Name)
{
	PS::PEDIt it = FindPEDIt(Name);
	return (it == m_PEDs.end()) ? 0 : *it;
}

PS::PGDIt CPSLibrary::FindPGDIt(LPCSTR Name)
{
	if (!Name) return m_PGDs.end();
#ifdef _EDITOR
	for (PS::PGDIt it=m_PGDs.begin(); it!=m_PGDs.end(); it++)
    	if (0==xr_strcmp((*it)->m_Name,Name)) return it;
	return m_PGDs.end();
#else
	PS::PGDIt I = std::lower_bound(m_PGDs.begin(), m_PGDs.end(), Name, pgd_find_pred);
	if (I == m_PGDs.end() || (0 != xr_strcmp((*I)->m_Name, Name))) return m_PGDs.end();
	else return I;
#endif
}

PS::CPGDef* CPSLibrary::FindPGD(LPCSTR Name)
{
	PS::PGDIt it = FindPGDIt(Name);
	return (it == m_PGDs.end()) ? 0 : *it;
}

void CPSLibrary::RenamePED(PS::CPEDef* src, LPCSTR new_name)
{
	R_ASSERT(src&&new_name&&new_name[0]);
	src->SetName(new_name);
}

void CPSLibrary::RenamePGD(PS::CPGDef* src, LPCSTR new_name)
{
	R_ASSERT(src&&new_name&&new_name[0]);
	src->SetName(new_name);
}

void CPSLibrary::Remove(const char* nm)
{
	PS::PEDIt it = FindPEDIt(nm);
	if (it != m_PEDs.end())
	{
		(*it)->DestroyShader();
		xr_delete(*it);
		m_PEDs.erase(it);
	}
	else
	{
		PS::PGDIt it = FindPGDIt(nm);
		if (it != m_PGDs.end())
		{
			xr_delete(*it);
			m_PGDs.erase(it);
		}
	}
}

//----------------------------------------------------
bool CPSLibrary::Load2()
{
	FS_FileSet files;
	string_path _path;
	FS.update_path(_path, "$game_particles$", "");

	FS.file_list(files, _path, FS_ListFiles, "*.pe,*.pg");

#ifdef _EDITOR
	SPBItem* pb = NULL;
	if(UI->m_bReady)
    pb 							= UI->ProgressStart(files.size(),"Loading particles...");
#endif
	FS_FileSet::iterator it = files.begin();
	FS_FileSet::iterator it_e = files.end();

	string_path p_path, p_name, p_ext;
	for (; it != it_e; ++it)
	{
		const FS_File& f = (*it);
		_splitpath(f.name.c_str(), 0, p_path, p_name, p_ext);
		FS.update_path(_path, "$game_particles$", f.name.c_str());
		CInifile ini(_path,TRUE,TRUE,FALSE);

#ifdef _EDITOR
        if(pb) pb->Inc					();
#endif

		xr_sprintf(_path, sizeof(_path), "%s%s", p_path, p_name);
		if (0 == stricmp(p_ext, ".pe"))
		{
			PS::CPEDef* def = xr_new<PS::CPEDef>();
			def->m_Name = _path;
			if (def->Load2(ini))
            {
                m_all_ps.push_back(def->m_Name);
                m_PEDs.push_back(def);
            }
			else
				xr_delete(def);
		}
		else if (0 == stricmp(p_ext, ".pg"))
		{
			PS::CPGDef* def = xr_new<PS::CPGDef>();
			def->m_Name = _path;
			if (def->Load2(ini))
            {
                m_all_ps.push_back(def->m_Name);
                m_PGDs.push_back(def);
            }
			else
				xr_delete(def);
		}
		else
		{
			R_ASSERT(0);
		}
	}

	std::sort(m_PEDs.begin(), m_PEDs.end(), ped_sort_pred);
	std::sort(m_PGDs.begin(), m_PGDs.end(), pgd_sort_pred);

	for (PS::PEDIt e_it = m_PEDs.begin(); e_it != m_PEDs.end(); ++e_it)
		(*e_it)->CreateShader();

#ifdef _EDITOR
    if(pb) UI->ProgressEnd		(pb);
#endif
	Msg("Loaded particles :%d", files.size());
	return true;
}


bool CPSLibrary::LoadDefinitions(const char* nm)
{
	CTimer startupTimer;
	startupTimer.Start();
	FS_FileSet files;
	string_path _path;

	FS.update_path(_path, "$game_particles$", "");
	FS.file_list(files, _path, FS_ListFiles, "*.pe,*.pg");

	struct PreparedParticle
	{
		xr_string file;
		shared_str name;
		bool effect = false;
		xr_unique_ptr<PS::CPEDef> effectDefinition;
		xr_unique_ptr<PS::CPGDef> groupDefinition;
	};

	xr_vector<PreparedParticle> prepared(files.size());
	u32 sourceIndex = 0;
	for (const FS_File& file : files)
	{
		string_path path;
		string_path name;
		string_path extension;
		_splitpath(file.name.c_str(), nullptr, path, name, extension);

		PreparedParticle& result = prepared[sourceIndex++];
		result.file = file.name.c_str();
		result.name.printf("%s%s", path, name);
		result.effect = 0 == stricmp(extension, ".pe");
		R_ASSERT(result.effect || 0 == stricmp(extension, ".pg"));
	}

	xr_parallel_for(0u, static_cast<u32>(prepared.size()), [&](u32 index)
	{
		PreparedParticle& result = prepared[index];
		string_path fullPath;
		FS.update_path(fullPath, "$game_particles$", result.file.c_str());
		CInifile ini(fullPath, TRUE, TRUE, FALSE);

		if (result.effect)
		{
			xr_unique_ptr<PS::CPEDef> definition = xr_make_unique<PS::CPEDef>();
			definition->m_Name = result.name;
			if (definition->Load2(ini))
				result.effectDefinition = std::move(definition);
		}
		else
		{
			xr_unique_ptr<PS::CPGDef> definition = xr_make_unique<PS::CPGDef>();
			definition->m_Name = result.name;
			if (definition->Load2(ini))
				result.groupDefinition = std::move(definition);
		}
	});

	for (PreparedParticle& result : prepared)
	{
		if (result.effectDefinition)
		{
			m_all_ps.push_back(result.effectDefinition->m_Name);
			m_PEDs.push_back(result.effectDefinition.release());
		}
		else if (result.groupDefinition)
		{
			m_all_ps.push_back(result.groupDefinition->m_Name);
			m_PGDs.push_back(result.groupDefinition.release());
		}
	}
	m_prepare_loose_ms = startupTimer.GetElapsed_ms();

	bool bRes = true;
	if (FS.exist(nm))
	{
		IReader* F = FS.r_open(nm);
		R_ASSERT(F->find_chunk(PS_CHUNK_VERSION));
		u16 ver = F->r_u16();
		if (ver != PS_VERSION) return false;
		// second generation
		IReader* OBJ;
		OBJ = F->open_chunk(PS_CHUNK_SECONDGEN);
		if (OBJ)
		{
			IReader* O = OBJ->open_chunk(0);
			for (int count = 1; O; count++)
			{
				PS::CPEDef* def = xr_new<PS::CPEDef>();
				if (def->Load(*O))
				{
					bool exist = false;
					for (PS::CPEDef* pdef : m_PEDs)
					{
						if (pdef->m_Name == def->m_Name)
						{
							exist = true;
							xr_delete(def);
							break;
						}
					}

					if (!exist)
						m_PEDs.push_back(def);
				}
				else
				{
					bRes = false;
					xr_delete(def);
				}
				O->close();
				if (!bRes) break;
				O = OBJ->open_chunk(count);
			}
			OBJ->close();
		}
		// second generation
		OBJ = F->open_chunk(PS_CHUNK_THIRDGEN);
		if (OBJ)
		{
			IReader* O = OBJ->open_chunk(0);
			for (int count = 1; O; count++)
			{
				PS::CPGDef* def = xr_new<PS::CPGDef>();
				if (def->Load(*O))
				{
					bool exist = false;
					for (PS::CPGDef* pdef : m_PGDs)
					{
						if (pdef->m_Name == def->m_Name)
						{
							exist = true;
							xr_delete(def);
							break;
						}
					}

					if (!exist)
						m_PGDs.push_back(def);
				}
				else
				{
					bRes = false;
					xr_delete(def);
				}
				O->close();
				if (!bRes) break;
				O = OBJ->open_chunk(count);
			}
			OBJ->close();
		}

		FS.r_close(F);
	}
	m_prepare_library_ms = startupTimer.GetElapsed_ms() - m_prepare_loose_ms;

	std::sort(m_PEDs.begin(), m_PEDs.end(), ped_sort_pred);
	std::sort(m_PGDs.begin(), m_PGDs.end(), pgd_sort_pred);
	m_prepare_sort_ms = startupTimer.GetElapsed_ms() - m_prepare_loose_ms - m_prepare_library_ms;

	return bRes;
}

void CPSLibrary::FinalizeLoad()
{
	CTimer shaderTimer;
	shaderTimer.Start();
	for (PS::PEDIt e_it = m_PEDs.begin(); e_it != m_PEDs.end(); ++e_it)
		(*e_it)->CreateShader();
	Msg("* [STARTUP/RENDER PARTICLES] loose=%u library=%u sort=%u shaders=%u effects=%u groups=%u total=%u ms",
		m_prepare_loose_ms, m_prepare_library_ms, m_prepare_sort_ms, shaderTimer.GetElapsed_ms(),
		static_cast<u32>(m_PEDs.size()), static_cast<u32>(m_PGDs.size()),
		m_prepare_loose_ms + m_prepare_library_ms + m_prepare_sort_ms + shaderTimer.GetElapsed_ms());
}

bool CPSLibrary::Load(const char* nm)
{
	const bool result = LoadDefinitions(nm);
	FinalizeLoad();
	return result;
}

//----------------------------------------------------
void CPSLibrary::Reload()
{
	OnDestroy();
	OnCreate();
	Msg("PS Library was succesfully reloaded.");
}

//----------------------------------------------------

using PS::CPGDef;

CPGDef const* const* CPSLibrary::particles_group_begin() const
{
	return (m_PGDs.size() ? &*m_PGDs.begin() : 0);
}

CPGDef const* const* CPSLibrary::particles_group_end() const
{
	return (m_PGDs.size() ? &*m_PGDs.end() : 0);
}

void CPSLibrary::particles_group_next(PS::CPGDef const* const*& iterator) const
{
	VERIFY(iterator);
	VERIFY(iterator >= particles_group_begin());
	VERIFY(iterator < particles_group_end());
	++iterator;
}

shared_str const& CPSLibrary::particles_group_id(CPGDef const& particles_group) const
{
	return (particles_group.m_Name);
}

//------------------------------------------------------------------------------

bool CPSLibrary::Save()
{
	string_path fn;
	FS.update_path(fn, _game_data_, "particles.xr");
	Save(fn);
	return true;
}

//------------------------------------------------------------------------------

bool CPSLibrary::Save2()
{
	FS.dir_delete("$game_particles$", "", TRUE);
	string_path fn;
	for (PS::PEDIt it = m_PEDs.begin(); it != m_PEDs.end(); ++it)
	{
		PS::CPEDef* pe = (*it);
		FS.update_path(fn, "$game_particles$", pe->m_Name.c_str());
		strcat(fn, ".pe");
		CInifile ini(fn, FALSE, FALSE, FALSE);
		pe->Save2(ini);
		ini.save_as(fn);
	}

	for (PS::PGDIt g_it = m_PGDs.begin(); g_it != m_PGDs.end(); ++g_it)
	{
		PS::CPGDef* pg = (*g_it);
		FS.update_path(fn, "$game_particles$", pg->m_Name.c_str());
		strcat(fn, ".pg");
		CInifile ini(fn, FALSE, FALSE, FALSE);
		pg->Save2(ini);
		ini.save_as(fn);
	}
	return true;
}

bool CPSLibrary::Save(const char* nm)
{
	CMemoryWriter F;

	F.open_chunk(PS_CHUNK_VERSION);
	F.w_u16(PS_VERSION);
	F.close_chunk();

	F.open_chunk(PS_CHUNK_SECONDGEN);
	u32 chunk_id = 0;
	for (PS::PEDIt it = m_PEDs.begin(); it != m_PEDs.end(); ++it, ++chunk_id)
	{
		F.open_chunk(chunk_id);
		(*it)->Save(F);
		F.close_chunk();
	}
	F.close_chunk();

	F.open_chunk(PS_CHUNK_THIRDGEN);
	chunk_id = 0;
	for (PS::PGDIt g_it = m_PGDs.begin(); g_it != m_PGDs.end(); ++g_it, ++chunk_id)
	{
		F.open_chunk(chunk_id);
		(*g_it)->Save(F);
		F.close_chunk();
	}
	F.close_chunk();

	return F.save_to(nm);
}

//------------------------------------------------------------------------------
