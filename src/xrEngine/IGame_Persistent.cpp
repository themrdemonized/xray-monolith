#include "stdafx.h"
#pragma hdrstop

#include "IGame_Persistent.h"

#ifndef _EDITOR
#include "environment.h"
# include "x_ray.h"
# include "IGame_Level.h"
# include "XR_IOConsole.h"
# include "Render.h"
# include "ps_instance.h"
# include "CustomHUD.h"
#endif

#ifdef _EDITOR
bool g_dedicated_server = false;
#endif

#ifdef INGAME_EDITOR
# include "editor_environment_manager.hpp"
#endif // INGAME_EDITOR

ENGINE_API IGame_Persistent* g_pGamePersistent = NULL;

bool IsMainMenuActive()
{
	return g_pGamePersistent && g_pGamePersistent->m_pMainMenu && g_pGamePersistent->m_pMainMenu->IsActive();
} //ECO_RENDER add

IGame_Persistent::IGame_Persistent()
{
	RDEVICE.seqAppStart.Add(this);
	RDEVICE.seqAppEnd.Add(this);
	RDEVICE.seqFrame.Add(this, REG_PRIORITY_HIGH + 1);
	RDEVICE.seqAppActivate.Add(this);
	RDEVICE.seqAppDeactivate.Add(this);
	m_pGShaderConstants = new ShadersExternalData(); //--#SM+#--

	m_pMainMenu = NULL;

#ifndef INGAME_EDITOR
#ifndef _EDITOR
	pEnvironment = xr_new<CEnvironment>();
#endif
#else // #ifdef INGAME_EDITOR
    if (RDEVICE.editor())
        pEnvironment = xr_new<editor::environment::manager>();
    else
        pEnvironment = xr_new<CEnvironment>();
#endif // #ifdef INGAME_EDITOR
}

IGame_Persistent::~IGame_Persistent()
{
	RDEVICE.seqFrame.Remove(this);
	RDEVICE.seqAppStart.Remove(this);
	RDEVICE.seqAppEnd.Remove(this);
	RDEVICE.seqAppActivate.Remove(this);
	RDEVICE.seqAppDeactivate.Remove(this);
#ifndef _EDITOR
	xr_delete(pEnvironment);
#endif
	xr_delete(m_pGShaderConstants); //--#SM+#--

	VERIFY(m_textures_prefetch_config);
	CInifile::Destroy(m_textures_prefetch_config);
	m_textures_prefetch_config = 0;
}

void IGame_Persistent::OnAppActivate()
{
}

void IGame_Persistent::OnAppDeactivate()
{
}

void IGame_Persistent::OnAppStart()
{
#ifndef _EDITOR
	Environment().load();
#endif

	// Texture Prefetch Config
	string_path file_name;
	m_textures_prefetch_config =
		xr_new<CInifile>(
			FS.update_path(
				file_name,
				"$game_config$",
				"prefetch\\textures.ltx"
			),
			TRUE,
			TRUE,
			FALSE
			);
}

void IGame_Persistent::OnAppEnd()
{
#ifndef _EDITOR
	Environment().unload();
#endif
	OnGameEnd();

#ifndef _EDITOR
	DEL_INSTANCE(g_hud);
#endif
}


void IGame_Persistent::PreStart(LPCSTR op)
{
	string256 prev_type;
	params new_game_params;
	xr_strcpy(prev_type, m_game_params.m_game_type);
	new_game_params.parse_cmd_line(op);

	// change game type
	if (0 != xr_strcmp(prev_type, new_game_params.m_game_type))
	{
		OnGameEnd();
	}
}

void IGame_Persistent::Start(LPCSTR op)
{
	string256 prev_type;
	xr_strcpy(prev_type, m_game_params.m_game_type);
	m_game_params.parse_cmd_line(op);
	// change game type
	if ((0 != xr_strcmp(prev_type, m_game_params.m_game_type)))
	{
		if (*m_game_params.m_game_type)
			OnGameStart();
#ifndef _EDITOR
		if (g_hud)
		DEL_INSTANCE(g_hud);
#endif
	}
	else UpdateGameType();

	VERIFY(ps_destroy.empty());
}

void IGame_Persistent::Disconnect()
{
#ifndef _EDITOR
	// clear "need to play" particles
	destroy_particles(true);

	if (g_hud)
	DEL_INSTANCE(g_hud);
	//. g_hud->OnDisconnected ();
#endif
}

void IGame_Persistent::OnGameStart()
{
#ifndef _EDITOR
	// LoadTitle("st_prefetching_objects");
	LoadTitle();
	if (!strstr(Core.Params, "-noprefetch"))
		Prefetch();
#endif
}

#ifndef _EDITOR
void IGame_Persistent::Prefetch()
{
	Msg("* [x-ray]: Prefetching Data");
	// prefetch game objects & models
	float p_time = 1000.f * Device.GetTimerGlobal()->GetElapsed_sec();
	size_t mem_0 = Memory.mem_usage();

	Log("Loading objects...");
	ObjectPool.prefetch();
	Log("Loading models...");
	Render->models_Prefetch();
	Log("Loading textures...");
	
	const auto loadFileFolder = [&](LPCSTR _folder)
	{
		string_path folder;
		strconcat(sizeof(folder), folder, _folder, "\\*.dds");

		FS_FileSet fset;
		FS.file_list(fset, "$game_textures$", FS_ListFiles, folder);

		for (FS_FileSet::iterator it = fset.begin(); it != fset.end(); it++)
			Device.m_pRender->ResourcesPrefetchCreateTexture(it->name.c_str());
	};

	if (m_textures_prefetch_config->section_exist("prefetch_folders"))
	{
		CInifile::Sect const& sect_f = m_textures_prefetch_config->r_section("prefetch_folders");
		for (CInifile::SectCIt I = sect_f.Data.begin(); I != sect_f.Data.end(); I++)
		{
			if (I->second.size() && !xr_strcmp(*I->second, "*"))
			{
				string_path folder;
				FS.update_path(folder, "$game_textures$", *I->first);
				xr_strcat(folder, sizeof(folder), "\\");

				xr_vector<LPSTR> *subfolders = FS.file_list_open(folder, FS_ListFolders);

				if (subfolders == nullptr)
				{
					FS.file_list_close(subfolders);
					continue;
				}

				for (LPSTR subfolder : *subfolders)
				{
					string_path path;
					strconcat(sizeof(path), path, folder, subfolder);

					loadFileFolder(path);
				}

				FS.file_list_close(subfolders);
			}

			loadFileFolder(*I->first);
		}
	}

	if (m_textures_prefetch_config->section_exist("prefetch_textures"))
	{
		CInifile::Sect const& sect = m_textures_prefetch_config->r_section("prefetch_textures");
		for (CInifile::SectCIt I = sect.Data.begin(); I != sect.Data.end(); I++)
			Device.m_pRender->ResourcesPrefetchCreateTexture(I->first.c_str());
	}

	Device.m_pRender->ResourcesDeferredUpload();

	Msg("* [x-ray]: Prefetched Data");
	p_time = 1000.f * Device.GetTimerGlobal()->GetElapsed_sec() - p_time;
	size_t p_mem = Memory.mem_usage() - mem_0;

	Msg("* [prefetch] time:   %d ms", iFloor(p_time));
	Msg("* [prefetch] memory: %lldKb", p_mem / 1024);
}
#endif


void IGame_Persistent::OnGameEnd()
{
#ifndef _EDITOR
	ObjectPool.clear();
	Render->models_Clear(TRUE);
#endif
}

void IGame_Persistent::OnFrame()
{
#ifndef _EDITOR

	if (!Device.Paused() || Device.dwPrecacheFrame)
		Environment().OnFrame();


	Device.Statistic->Particles_starting = ps_needtoplay.size();
	Device.Statistic->Particles_active = ps_active.size();
	Device.Statistic->Particles_destroy = ps_destroy.size();

	// Play req particle systems
	while (ps_needtoplay.size())
	{
		CPS_Instance* psi = ps_needtoplay.back();
		ps_needtoplay.pop_back();
		psi->Play(false);
	}
	// Destroy inactive particle systems
	while (ps_destroy.size())
	{
		// u32 cnt = ps_destroy.size();
		CPS_Instance* psi = ps_destroy.back();
		VERIFY(psi);
		if (psi->Locked())
		{
			Log("--locked");
			break;
		}
		ps_destroy.pop_back();
		psi->PSI_internal_delete();
	}
#endif
}

void IGame_Persistent::destroy_particles(const bool& all_particles)
{
#ifndef _EDITOR
	ps_needtoplay.clear();

	while (ps_destroy.size())
	{
		CPS_Instance* psi = ps_destroy.back();
		VERIFY(psi);
		VERIFY(!psi->Locked());
		ps_destroy.pop_back();
		psi->PSI_internal_delete();
	}

	// delete active particles
	if (all_particles)
	{
		for (; !ps_active.empty();)
			(*ps_active.begin())->PSI_internal_delete();
	}
	else
	{
		u32 active_size = ps_active.size();
		CPS_Instance** I = (CPS_Instance**)_alloca(active_size * sizeof(CPS_Instance*));
		std::copy(ps_active.begin(), ps_active.end(), I);

		struct destroy_on_game_load
		{
			static IC bool predicate(CPS_Instance* const& object)
			{
				return (!object->destroy_on_game_load());
			}
		};

		CPS_Instance** E = std::remove_if(I, I + active_size, &destroy_on_game_load::predicate);
		for (; I != E; ++I)
			(*I)->PSI_internal_delete();
	}

	VERIFY(ps_needtoplay.empty() && ps_destroy.empty() && (!all_particles || ps_active.empty()));
#endif
}

void IGame_Persistent::OnAssetsChanged()
{
#ifndef _EDITOR
	Device.m_pRender->OnAssetsChanged(); //Resources->m_textures_description.Load();
#endif
}
