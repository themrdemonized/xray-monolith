#include "stdafx.h"
#include "LevelGameDef.h"
#include "ai_space.h"
#include "script_process.h"
#include "script_engine.h"
#include "script_engine_space.h"
#include "level.h"
#include "game_cl_base.h"
#include "../xrEngine/x_ray.h"
#include "../xrEngine/gamemtllib.h"
#include "../xrphysics/PhysicsCommon.h"
#include "level_sounds.h"
#include "GamePersistent.h"
#include "../xrEngine/Rain.h"
#include "character_community.h"
#include "character_rank.h"
#include "character_reputation.h"
#include "monster_community.h"
#include "HudManager.h"

extern ENGINE_API bool g_dedicated_server;

struct level_prepared_particle
{
	shared_str name;
	Fmatrix transform;
};

struct level_game_specific_prepare
{
	NativeLoadExecutor::Batch batch;
	NativeLoadExecutor::Batch environment_batch;
	xr_task_group fallback_tasks;
	xr_task_group fallback_environment_tasks;
	xr_vector<level_prepared_particle> particles;
	xr_vector<CEnvModifier> environment_modifiers;
	CLevelSoundManager::PreparedData sounds;
	xr_vector<u8> sound_environment;
	xr_vector<u8> sound_occlusion;
	xr_vector<shared_str> random_sounds;
	xr_string level_path;
	u32 game_type = 0;
	bool has_sound_environment = false;
	bool has_sound_occlusion = false;
	bool environment_committed = false;
	bool committed = false;

	void wait_environment()
	{
		if (environment_batch.Valid())
			NativeLoadExecutor::Instance().Wait(environment_batch);
		fallback_environment_tasks.wait();
	}

	void wait()
	{
		std::exception_ptr failure;
		try
		{
			wait_environment();
		}
		catch (...)
		{
			failure = std::current_exception();
		}
		try
		{
			if (batch.Valid())
				NativeLoadExecutor::Instance().Wait(batch);
			fallback_tasks.wait();
		}
		catch (...)
		{
			if (!failure)
				failure = std::current_exception();
		}
		if (failure)
			std::rethrow_exception(failure);
	}
};

namespace
{
xr_string level_resource_path(LPCSTR canonical_level_path, LPCSTR file_name)
{
	xr_string path = canonical_level_path;
	if (!path.empty() && path.back() != '\\' && path.back() != '/')
		path += '\\';
	path += file_name;
	return path;
}

void read_level_resource(LPCSTR canonical_level_path, LPCSTR file_name, xr_vector<u8>& bytes)
{
	const xr_string path = level_resource_path(canonical_level_path, file_name);
	IReader* reader = FS.r_open(path.c_str());
	R_ASSERT3(reader, "Cannot open level resource", path.c_str());
	bytes.resize(reader->length());
	reader->r(bytes.data(), bytes.size());
	FS.r_close(reader);
}
}

bool CLevel::Load_GameSpecific_Before()
{
	// AI space
	string_path fn_game;

	if (GamePersistent().GameType() == eGameIDSingle && !ai().get_alife() && FS.exist(fn_game, "$level$", "level.ai") &&
		!net_Hosts.empty())
		ai().load(net_SessionName());

	if (!g_dedicated_server && !ai().get_alife() && ai().get_game_graph() && FS.exist(fn_game, "$level$", "level.game"))
	{
		IReader* stream = FS.r_open(fn_game);
		ai().patrol_path_storage_raw(*stream);
		FS.r_close(stream);
	}

	CHARACTER_COMMUNITY::Reset();
	CHARACTER_RANK::Reset();
	CHARACTER_REPUTATION::Reset();
	MONSTER_COMMUNITY::Reset();

	return (TRUE);
}

void CLevel::BeginGameSpecificPrepare(LPCSTR canonical_level_path)
{
	R_ASSERT(canonical_level_path && canonical_level_path[0]);
	xr_string level_path = canonical_level_path;
	if (!level_path.empty() && level_path.back() != '\\' && level_path.back() != '/')
		level_path += '\\';
	if (m_game_specific_prepare)
	{
		R_ASSERT3(!stricmp(m_game_specific_prepare->level_path.c_str(), level_path.c_str()),
			"Level prepare target changed", level_path.c_str());
		return;
	}

	level_game_specific_prepare* prepared = xr_new<level_game_specific_prepare>();
	prepared->level_path = std::move(level_path);
	prepared->game_type = u32(g_pGamePersistent->m_game_params.m_e_game_type);
	NativeLoadExecutor& executor = NativeLoadExecutor::Instance();
	prepared->batch = executor.BeginBatch(executor.CurrentGeneration());
	prepared->environment_batch = executor.BeginBatch(executor.CurrentGeneration());
	m_game_specific_prepare = prepared;
	auto submit = [prepared, &executor](NativeLoadPriority priority, auto&& work)
	{
		if (prepared->batch.Valid())
			executor.Submit(prepared->batch, priority, std::forward<decltype(work)>(work));
		else
			prepared->fallback_tasks.run(std::forward<decltype(work)>(work));
	};
	auto submit_environment = [prepared, &executor](auto&& work)
	{
		if (prepared->environment_batch.Valid())
			executor.Submit(prepared->environment_batch, NativeLoadPriority::Environment,
				std::forward<decltype(work)>(work));
		else
			prepared->fallback_environment_tasks.run(std::forward<decltype(work)>(work));
	};

	submit_environment([prepared]()
	{
		CEnvironment::PrepareLevelModifiers(prepared->level_path.c_str(), prepared->environment_modifiers);
	});

	// pSettings is snapshotted on the owner thread. Workers below only own
	// file readers, byte buffers and strings.
	if (pSettings->section_exist("sounds_random"))
	{
		const CInifile::Sect& section = pSettings->r_section("sounds_random");
		prepared->random_sounds.reserve(section.Data.size());
		for (CInifile::SectCIt it = section.Data.begin(); it != section.Data.end(); ++it)
			prepared->random_sounds.push_back(it->first);
	}

	submit(NativeLoadPriority::Environment, [prepared]()
	{
		const xr_string file_name = level_resource_path(prepared->level_path.c_str(), "level.ps_static");
		if (!FS.exist(file_name.c_str()))
			return;

		IReader* F = FS.r_open(file_name.c_str());
		R_ASSERT3(F, "Cannot open level resource", file_name.c_str());

		u32 chunk = 0;
		string256 ref_name;
		Fmatrix transform;
		u32 ver = 0;
		for (IReader* OBJ = F->open_chunk_iterator(chunk); OBJ; OBJ = F->open_chunk_iterator(chunk, OBJ))
		{
			if (chunk == 0)
			{
				if (OBJ->length() == sizeof(u32))
				{
					ver = OBJ->r_u32();
#ifndef MASTER_GOLD
					Msg		("PS new version, %d", ver);
#endif // #ifndef MASTER_GOLD
					continue;
				}
			}
			u16 gametype_usage = 0;
			if (ver > 0)
			{
				gametype_usage = OBJ->r_u16();
			}
			OBJ->r_stringZ(ref_name, sizeof(ref_name));
			OBJ->r(&transform, sizeof(Fmatrix));
			transform.c.y += 0.01f;


			if ((prepared->game_type & u32(gametype_usage)) || (ver == 0))
			{
				prepared->particles.push_back({ref_name, transform});
			}
		}
		FS.r_close(F);
	});

	submit(NativeLoadPriority::Environment, [this, prepared]()
	{
		if (!g_dedicated_server)
			m_level_sound_manager->Prepare(prepared->level_path.c_str(), prepared->sounds);
	});

	submit(NativeLoadPriority::Environment, [prepared]()
	{
		if (g_dedicated_server)
			return;
		const xr_string environment = level_resource_path(prepared->level_path.c_str(), "level.snd_env");
		prepared->has_sound_environment = FS.exist(environment.c_str());
		if (prepared->has_sound_environment)
			read_level_resource(prepared->level_path.c_str(), "level.snd_env", prepared->sound_environment);
		const xr_string occlusion = level_resource_path(prepared->level_path.c_str(), "level.som");
		prepared->has_sound_occlusion = FS.exist(occlusion.c_str());
		if (prepared->has_sound_occlusion)
			read_level_resource(prepared->level_path.c_str(), "level.som", prepared->sound_occlusion);
	});
}

bool CLevel::Load_Prepared_Environment()
{
	if (!m_game_specific_prepare)
		return false;

	level_game_specific_prepare& prepared = *m_game_specific_prepare;
	if (!prepared.environment_committed)
	{
		prepared.wait_environment();
		g_pGamePersistent->Environment().CommitLevelModifiers(prepared.environment_modifiers);
		prepared.environment_committed = true;
	}
	return true;
}

void CLevel::ShutdownGameSpecificPrepare()
{
	if (!m_game_specific_prepare)
		return;

	try
	{
		m_game_specific_prepare->wait();
	}
	catch (...)
	{
	}
	xr_delete(m_game_specific_prepare);
}

bool CLevel::Load_GameSpecific_After()
{
	if (m_game_specific_prepare && m_game_specific_prepare->committed)
		return TRUE;
	R_ASSERT(m_StaticParticles.empty());
	if (!m_game_specific_prepare)
		BeginGameSpecificPrepare(FS.get_path("$level$")->m_Path);

	level_game_specific_prepare& prepared = *m_game_specific_prepare;
	prepared.wait();
	const xr_vector<shared_str>& random_sounds = prepared.random_sounds;

	Fvector zero_vel = {0.f, 0.f, 0.f};
	for (const level_prepared_particle& particle : prepared.particles)
	{
		auto instance = Particles::Details::Create(particle.name.c_str(), FALSE, false);
		instance->UpdateParent(particle.transform, zero_vel);
		instance->Play(false);
		m_StaticParticles.push_back(instance);
	}

	if (!g_dedicated_server)
	{
		VERIFY(m_level_sound_manager);
		m_level_sound_manager->Commit(prepared.sounds);
		if (prepared.has_sound_environment)
		{
			IReader reader(prepared.sound_environment.data(), prepared.sound_environment.size());
			::Sound->set_geometry_env(&reader);
		}
		else
			::Sound->set_geometry_env(nullptr);
		if (prepared.has_sound_occlusion)
		{
			IReader reader(prepared.sound_occlusion.data(), prepared.sound_occlusion.size());
			::Sound->set_geometry_som(&reader);
		}
		else
			::Sound->set_geometry_som(nullptr);

		Sounds_Random.reserve(random_sounds.size());
		for (const shared_str& name : random_sounds)
		{
			Sounds_Random.emplace_back();
			Sound->create(Sounds_Random.back(), name.c_str(), st_Effect, sg_SourceType);
		}
		if (!random_sounds.empty())
		{
			Sounds_Random_dwNextTime = Device.TimerAsync() + 50000;
			Sounds_Random_Enabled = FALSE;
		}

		if (g_pGamePersistent->pEnvironment)
			if (CEffect_Rain* rain = g_pGamePersistent->pEnvironment->eff_Rain)
				rain->InvalidateState();

		// loading scripts
		ai().script_engine().remove_script_process(ScriptEngine::eScriptProcessorLevel);

		if (pLevel->section_exist("level_scripts") && pLevel->line_exist("level_scripts", "script"))
			ai().script_engine().add_script_process(ScriptEngine::eScriptProcessorLevel,
			                                        xr_new<CScriptProcess>(
				                                        "level", pLevel->r_string("level_scripts", "script")));
		else
			ai().script_engine().add_script_process(ScriptEngine::eScriptProcessorLevel,
			                                        xr_new<CScriptProcess>("level", ""));
	}

	BlockCheatLoad();

	g_pGamePersistent->Environment().SetGameTime(GetEnvironmentGameDayTimeSec(), game->GetEnvironmentGameTimeFactor());

	HUD().SetRenderable(true);
	prepared.committed = true;
	prepared.particles.clear();
	prepared.sounds.static_sound_chunks.clear();
	prepared.sound_environment.clear();
	prepared.sound_occlusion.clear();
	prepared.random_sounds.clear();

	return TRUE;
}

struct translation_pair
{
	u32 m_id;
	u16 m_index;

	IC translation_pair(u32 id, u16 index)
	{
		m_id = id;
		m_index = index;
	}

	IC bool operator==(const u16& id) const
	{
		return (m_id == id);
	}

	IC bool operator<(const translation_pair& pair) const
	{
		return (m_id < pair.m_id);
	}

	IC bool operator<(const u16& id) const
	{
		return (m_id < id);
	}
};

void CLevel::Load_GameSpecific_CFORM(CDB::TRI* tris, u32 count)
{
	typedef xr_vector<translation_pair> ID_INDEX_PAIRS;
	ID_INDEX_PAIRS translator;
	translator.reserve(GMLib.CountMaterial());
	u16 default_id = (u16)GMLib.GetMaterialIdx("default");
	translator.push_back(translation_pair(u32(-1), default_id));

	u16 index = 0, static_mtl_count = 1;
	int max_ID = 0;
	int max_static_ID = 0;
	for (GameMtlIt I = GMLib.FirstMaterial(); GMLib.LastMaterial() != I; ++I, ++index)
	{
		if (!(*I)->Flags.test(SGameMtl::flDynamic))
		{
			++static_mtl_count;
			translator.push_back(translation_pair((*I)->GetID(), index));
			if ((*I)->GetID() > max_static_ID) max_static_ID = (*I)->GetID();
		}
		if ((*I)->GetID() > max_ID) max_ID = (*I)->GetID();
	}
	// Msg("* Material remapping ID: [Max:%d, StaticMax:%d]",max_ID,max_static_ID);
	VERIFY(max_static_ID<0xFFFF);

	if (static_mtl_count < 128)
	{
		CDB::TRI* I = tris;
		CDB::TRI* E = tris + count;
		for (; I != E; ++I)
		{
			ID_INDEX_PAIRS::iterator i = std::find(translator.begin(), translator.end(), (u16)(*I).material);
			if (i != translator.end())
			{
				(*I).material = (*i).m_index;
				SGameMtl* mtl = GMLib.GetMaterialByIdx((*i).m_index);
				(*I).suppress_shadows = mtl->Flags.is(SGameMtl::flSuppressShadows);
				(*I).suppress_wm = mtl->Flags.is(SGameMtl::flSuppressWallmarks);
				continue;
			}

			Debug.fatal(DEBUG_INFO, "Game material '%d' not found", (*I).material);
		}
		return;
	}

	std::sort(translator.begin(), translator.end());
	{
		CDB::TRI* I = tris;
		CDB::TRI* E = tris + count;
		for (; I != E; ++I)
		{
			ID_INDEX_PAIRS::iterator i = std::lower_bound(translator.begin(), translator.end(), (u16)(*I).material);
			if ((i != translator.end()) && ((*i).m_id == (*I).material))
			{
				(*I).material = (*i).m_index;
				SGameMtl* mtl = GMLib.GetMaterialByIdx((*i).m_index);
				(*I).suppress_shadows = mtl->Flags.is(SGameMtl::flSuppressShadows);
				(*I).suppress_wm = mtl->Flags.is(SGameMtl::flSuppressWallmarks);
				continue;
			}

			Debug.fatal(DEBUG_INFO, "Game material '%d' not found", (*I).material);
		}
	}
}

void CLevel::BlockCheatLoad()
{
#ifndef	DEBUG
	if (game && (GameID() != eGameIDSingle)) phTimefactor = 1.f;
#endif
}
