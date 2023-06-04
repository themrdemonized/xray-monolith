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
# include "perlin.h"
#endif

#ifdef _EDITOR
bool g_dedicated_server = false;
#endif

#ifdef INGAME_EDITOR
# include "editor_environment_manager.hpp"
#endif // INGAME_EDITOR

extern Fvector4 ps_ssfx_grass_interactive;

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

	PerlinNoise1D = xr_new<CPerlinNoise1D>(Random.randI(0, 0xFFFF));
	PerlinNoise1D->SetOctaves(2);
	PerlinNoise1D->SetAmplitude(0.66666f);

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
	xr_delete(PerlinNoise1D);
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

void IGame_Persistent::GrassBendersUpdate(u16 id, u8& data_idx, u32& data_frame, Fvector& position, float init_radius, float init_str, bool CheckDistance)
{
	// Interactive grass disabled
	if (ps_ssfx_grass_interactive.y < 1)
		return;

	// Just update position if not NULL
	if (data_idx != NULL)
	{
		// Explosions can take the mem spot, unassign and try to get a spot later.
		if (grass_shader_data.id[data_idx] != id)
		{
			data_idx = NULL;
			data_frame = RDEVICE.dwFrame + Random.randI(10, 35);
		}
		else
		{
			grass_shader_data.pos[data_idx] = position;
		}
	}

	if (RDEVICE.dwFrame < data_frame)
		return;

	// Wait some random frames to split the checks
	data_frame = RDEVICE.dwFrame + Random.randI(10, 35);

	// Check Distance
	if (CheckDistance)
	{
		if (position.distance_to_xz_sqr(Device.vCameraPosition) > ps_ssfx_grass_interactive.z)
		{
			GrassBendersRemoveByIndex(data_idx);
			return;
		}
	}

	CFrustum& view_frust = ::Render->ViewBase;
	u32 mask = 0xff;
	float rad = data_idx == NULL ? 1.0 : std::max(1.0f, grass_shader_data.radius_curr[data_idx] + 0.5f);

	// In view frustum?
	if (!view_frust.testSphere(position, rad, mask))
	{
		GrassBendersRemoveByIndex(data_idx);
		return;
	}

	// Empty slot, let's use this
	if (data_idx == NULL)
	{
		u8 idx = grass_shader_data.index + 1;

		// Add to grass blenders array
		if (grass_shader_data.id[idx] == NULL)
		{
			data_idx = idx;
			GrassBendersSet(idx, id, position, Fvector3().set(0, -99, 0), 0, 0, 0.0f, init_radius, BENDER_ANIM_DEFAULT, true);

			grass_shader_data.str_target[idx] = init_str;
			grass_shader_data.radius_curr[idx] = init_radius;
		}
		// Back to 0 when the array limit is reached
		grass_shader_data.index = idx < ps_ssfx_grass_interactive.y ? idx : 0;
	}
	else
	{
		// Already inview, let's add more time to re-check
		data_frame += 60;
		grass_shader_data.pos[data_idx] = position;
	}
}

void IGame_Persistent::GrassBendersAddExplosion(u16 id, Fvector position, Fvector3 dir, float fade, float speed, float intensity, float radius)
{
	if (ps_ssfx_grass_interactive.y < 1)
		return;

	for (int idx = 1; idx < ps_ssfx_grass_interactive.y + 1; idx++)
	{
		// Add explosion to any spot not already taken by an explosion.
		if (grass_shader_data.anim[idx] != BENDER_ANIM_EXPLOSION)
		{
			// Add 99 to the ID to avoid conflicts between explosions and basic benders happening at the same time with the same ID.
			GrassBendersSet(idx, id + 99, position, dir, fade, speed, intensity, radius, BENDER_ANIM_EXPLOSION, true);
			grass_shader_data.str_target[idx] = intensity;
			break;
		}
	}
}

void IGame_Persistent::GrassBendersAddShot(u16 id, Fvector position, Fvector3 dir, float fade, float speed, float intensity, float radius)
{
	// Is disabled?
	if (ps_ssfx_grass_interactive.y < 1 || intensity <= 0.0f)
		return;

	// Check distance
	if (position.distance_to_xz_sqr(Device.vCameraPosition) > ps_ssfx_grass_interactive.z)
		return;

	int AddAt = -1;

	// Look for a spot
	for (int idx = 1; idx < ps_ssfx_grass_interactive.y + 1; idx++)
	{
		// Already exist, just update and increase intensity
		if (grass_shader_data.id[idx] == id)
		{
			float currentSTR = grass_shader_data.str[idx];
			GrassBendersSet(idx, id, position, dir, fade, speed, currentSTR, radius, BENDER_ANIM_EXPLOSION, false);
			grass_shader_data.str_target[idx] += intensity;
			AddAt = -1;
			break;
		}
		else
		{
			// Check all indexes and keep usable index to use later if needed...
			if (AddAt == -1 && grass_shader_data.radius[idx] == NULL)
				AddAt = idx;
		}
	}

	// We got an available index... Add bender at AddAt
	if (AddAt != -1)
	{
		GrassBendersSet(AddAt, id, position, dir, fade, speed, 0.001f, radius, BENDER_ANIM_EXPLOSION, true);
		grass_shader_data.str_target[AddAt] = intensity;
	}
}

void IGame_Persistent::GrassBendersUpdateAnimations()
{
	for (int idx = 1; idx < ps_ssfx_grass_interactive.y + 1; idx++)
	{
		if (grass_shader_data.id[idx] != NULL)
		{
			switch (grass_shader_data.anim[idx])
			{
			case BENDER_ANIM_EXPLOSION: // Internal Only ( You can use BENDER_ANIM_PULSE for anomalies )
			{
				// Radius
				grass_shader_data.time[idx] += Device.fTimeDelta * grass_shader_data.speed[idx];
				grass_shader_data.radius_curr[idx] = grass_shader_data.radius[idx] * std::min(1.0f, grass_shader_data.time[idx]);

				grass_shader_data.str_target[idx] = std::min(1.0f, grass_shader_data.str_target[idx]);

				// Easing
				float diff = abs(grass_shader_data.str[idx] - grass_shader_data.str_target[idx]);
				diff = std::max(0.1f, diff);

				// Intensity
				if (grass_shader_data.str_target[idx] <= grass_shader_data.str[idx])
				{
					grass_shader_data.str[idx] -= Device.fTimeDelta * grass_shader_data.fade[idx] * diff;
				}
				else
				{
					grass_shader_data.str[idx] += Device.fTimeDelta * grass_shader_data.speed[idx] * diff;

					if (grass_shader_data.str[idx] >= grass_shader_data.str_target[idx])
						grass_shader_data.str_target[idx] = 0;
				}

				// Remove Bender
				if (grass_shader_data.str[idx] < 0.0f)
					GrassBendersReset(idx);
			}
			break;

			case BENDER_ANIM_WAVY:
			{
				// Anim Speed
				grass_shader_data.time[idx] += Device.fTimeDelta * 1.5f * grass_shader_data.speed[idx];

				// Curve
				float curve = sin(grass_shader_data.time[idx]);

				// Intensity using curve
				grass_shader_data.str[idx] = curve * cos(curve * 1.4f) * 1.8f * grass_shader_data.str_target[idx];
			}

			break;

			case BENDER_ANIM_SUCK:
			{
				// Anim Speed
				grass_shader_data.time[idx] += Device.fTimeDelta * grass_shader_data.speed[idx];

				// Perlin Noise
				float curve = clampr(PerlinNoise1D->GetContinious(grass_shader_data.time[idx]) + 0.5f, 0.f, 1.f) * -1.0;

				// Intensity using Perlin
				grass_shader_data.str[idx] = curve * grass_shader_data.str_target[idx];
			}
			break;

			case BENDER_ANIM_BLOW:
			{
				// Anim Speed
				grass_shader_data.time[idx] += Device.fTimeDelta * 1.2f * grass_shader_data.speed[idx];

				// Perlin Noise
				float curve = clampr(PerlinNoise1D->GetContinious(grass_shader_data.time[idx]) + 1.0f, 0.f, 2.0f) * 0.25f;

				// Intensity using Perlin
				grass_shader_data.str[idx] = curve * grass_shader_data.str_target[idx];
			}
			break;

			case BENDER_ANIM_PULSE:
			{
				// Anim Speed
				grass_shader_data.time[idx] += Device.fTimeDelta * grass_shader_data.speed[idx];

				// Radius
				grass_shader_data.radius_curr[idx] = grass_shader_data.radius[idx] * std::min(1.0f, grass_shader_data.time[idx]);

				// Diminish intensity when radius target is reached
				if (grass_shader_data.radius_curr[idx] >= grass_shader_data.radius[idx])
					grass_shader_data.str[idx] += GrassBenderToValue(grass_shader_data.str[idx], 0.0f, grass_shader_data.speed[idx] * 0.6f, true);

				// Loop when intensity is <= 0
				if (grass_shader_data.str[idx] <= 0.0f)
				{
					grass_shader_data.str[idx] = grass_shader_data.str_target[idx];
					grass_shader_data.radius_curr[idx] = 0.0f;
					grass_shader_data.time[idx] = 0.0f;
				}

			}
			break;

			case BENDER_ANIM_DEFAULT:

				// Just fade to target strength
				grass_shader_data.str[idx] += GrassBenderToValue(grass_shader_data.str[idx], grass_shader_data.str_target[idx], 2.0f, true);

				break;
			}
		}
	}
}

void IGame_Persistent::GrassBendersRemoveByIndex(u8& idx)
{
	if (idx != NULL)
	{
		GrassBendersReset(idx);
		idx = NULL;
	}
}

void IGame_Persistent::GrassBendersRemoveById(u16 id)
{
	// Search by Object ID ( Used when removing benders CPHMovementControl::DestroyCharacter() )
	for (int i = 1; i < ps_ssfx_grass_interactive.y + 1; i++)
		if (grass_shader_data.id[i] == id)
			GrassBendersReset(i);
}

void IGame_Persistent::GrassBendersReset(u8 idx)
{
	// Reset Everything
	GrassBendersSet(idx, NULL, Fvector3().set(0, 0, 0), Fvector3().set(0, -99, 0), 0, 0, 0, 0, BENDER_ANIM_DEFAULT, true);
	grass_shader_data.str_target[idx] = 0;
}

void IGame_Persistent::GrassBendersSet(u8 idx, u16 id, Fvector position, Fvector3 dir, float fade, float speed, float intensity, float radius, GrassBenders_Anim anim, bool resetTime)
{
	// Set values
	grass_shader_data.anim[idx] = anim;
	grass_shader_data.pos[idx] = position;
	grass_shader_data.id[idx] = id;
	grass_shader_data.radius[idx] = radius;
	grass_shader_data.str[idx] = intensity;
	grass_shader_data.fade[idx] = fade;
	grass_shader_data.speed[idx] = speed;
	grass_shader_data.dir[idx] = dir;

	if (resetTime)
	{
		grass_shader_data.radius_curr[idx] = 0.01f;
		grass_shader_data.time[idx] = 0;
	}
}

float IGame_Persistent::GrassBenderToValue(float& current, float go_to, float intensity, bool use_easing)
{
	float diff = abs(current - go_to);

	float r_value = Device.fTimeDelta * intensity * (use_easing ? std::min(0.5f, diff) : 1.0f);

	if (diff - r_value <= 0)
	{
		current = go_to;
		return 0;
	}

	return current < go_to ? r_value : -r_value;
}