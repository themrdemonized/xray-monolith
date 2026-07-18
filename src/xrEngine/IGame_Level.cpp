#include "stdafx.h"
#include "igame_level.h"
#include "igame_persistent.h"

#include "x_ray.h"
#include "std_classes.h"
#include "customHUD.h"
#include "render.h"
#include "gamefont.h"
#include "xrLevel.h"
#include "CameraManager.h"
#include "xr_object.h"
#include "feel_sound.h"

#include "../xrCore/profiler.h"

//#include "securom_api.h"

ENGINE_API IGame_Level* g_pGameLevel = NULL;
extern BOOL g_bLoaded;

IGame_Level::IGame_Level()
{
	PROF_EVENT("IGame_Level::IGame_Level");
	m_pCameras = xr_new<CCameraManager>(true);
	g_pGameLevel = this;
	pLevel = NULL;
	bReady = false;
	pCurrentEntity = NULL;
	pCurrentViewEntity = NULL;
	Device.DumpResourcesMemoryUsage();
}

//#include "resourcemanager.h"

IGame_Level::~IGame_Level()
{
	Device.secondary_tasks.wait();

	if (Core.ParamsData.test(ECoreParams::nes_texture_storing))
		Device.m_pRender->ResourcesStoreNecessaryTextures();
	xr_delete(pLevel);

	// Render-level unload
	Render->level_Unload();
	xr_delete(m_pCameras);
	// Unregister
	Device.seqParallel.clear_not_free();
	Device.seqRender.Remove(this);
	Device.seqFrame.Remove(this);
	CCameraManager::ResetPP();
	///////////////////////////////////////////
	Sound->set_geometry_occ(NULL);
	Sound->set_handler(NULL);
	Device.DumpResourcesMemoryUsage();

	u32 m_base = 0, c_base = 0, m_lmaps = 0, c_lmaps = 0;
	if (Device.m_pRender)
		Device.m_pRender->ResourcesGetMemoryUsage(m_base, c_base, m_lmaps, c_lmaps);

	Msg("* [ D3D ]: textures[%d K]", (m_base + m_lmaps) / 1024);
}

void IGame_Level::net_Stop()
{
    for (int i = 0; i < 6; i++)
    {
		if (Objects.o_count() == 0 && Objects.destroy_queues_empty())
			break;

        Objects.Update(false);
        Objects.ProcessDestroyQueue();
    }
		
	// Destroy all objects
	Objects.Unload();
	IR_Release();

	bReady = false;
}

//-------------------------------------------------------------------------------------------
//extern CStatTimer tscreate;
void __stdcall _sound_event(ref_sound_data_ptr S, float range)
{
	if (g_pGameLevel && S && S->feedback) g_pGameLevel->SoundEvent_Register(S, range);
}

static void __stdcall build_callback(Fvector* V, int Vcnt, CDB::TRI* T, int Tcnt, void* params)
{
	g_pGameLevel->Load_GameSpecific_CFORM(T, Tcnt);
}

xrCriticalSection lloadcs;
bool IGame_Level::Load(u32 dwNum)
{
	PROF_EVENT("IGame_Level::Load");
	CTimer level_timer;
	level_timer.Start();
	xrCriticalSectionGuard guard(&lloadcs);
	if (bReady) return TRUE;
	//SECUROM_MARKER_PERFORMANCE_ON(10)

	// Initialize level data
	pApp->Level_Set(dwNum);
	string_path temp;
	if (!FS.exist(temp, "$level$", "level.ltx"))
		Debug.fatal(DEBUG_INFO, "Can't find level configuration file '%s'.", temp);
	pLevel = xr_new<CInifile>(temp);

	// Open
	// g_pGamePersistent->LoadTitle ("st_opening_stream");
	g_pGamePersistent->LoadTitle();
	IReader* LL_Stream = FS.r_open("$level$", "level");
	IReader& fs = *LL_Stream;

	// Header
	hdrLEVEL H;
	fs.r_chunk_safe(fsL_HEADER, &H, sizeof(H));
	R_ASSERT2(XRCL_PRODUCTION_VERSION == H.XRLC_version, "Incompatible level version.");

	// HUD + Environment
	if (!g_hud)
		g_hud = (CCustomHUD*)NEW_INSTANCE(CLSID_HUDMANAGER);

	if (!Load_Prepared_Environment())
		g_pGamePersistent->Environment().mods_load();
	g_pGamePersistent->LoadTitle();

	// CFORM and game-specific navigation data use independent level files and
	// publish to separate subsystems, so overlap them with renderer loading.
	NativeLoadExecutor& load_executor = NativeLoadExecutor::Instance();
	NativeLoadExecutor::Batch level_load_batch = load_executor.BeginBatch(load_executor.CurrentGeneration());
	xr_task_group fallback_level_tasks;
	auto submit_level_task = [&load_executor, &level_load_batch, &fallback_level_tasks](
		NativeLoadPriority priority, auto&& work)
	{
		if (level_load_batch.Valid())
			load_executor.Submit(level_load_batch, priority, std::forward<decltype(work)>(work));
		else
			fallback_level_tasks.run(std::forward<decltype(work)>(work));
	};
	ObjectSpace.Load([](Fvector* V, int Vcnt, CDB::TRI* T, int Tcnt, void* params)
	{
		g_pGameLevel->Load_GameSpecific_CFORM(T, Tcnt);
	});
	submit_level_task(NativeLoadPriority::Geometry, [this]()
	{
		CTimer timer;
		timer.Start();
		ObjectSpace.GetStaticModel()->syncronize();
		Msg("* [LEVEL LOAD] CFORM: %d ms", timer.GetElapsed_ms());
	});
	bool level_tasks_drained = false;
	auto drain_level_tasks = [&]()
	{
		if (level_tasks_drained)
			return;
		std::exception_ptr failure;
		try
		{
			if (level_load_batch.Valid())
				load_executor.Wait(level_load_batch);
		}
		catch (...)
		{
			failure = std::current_exception();
		}
		try
		{
			fallback_level_tasks.wait();
		}
		catch (...)
		{
			if (!failure)
				failure = std::current_exception();
		}
		level_tasks_drained = true;
		if (failure)
			std::rethrow_exception(failure);
	};
	struct level_task_drain_guard
	{
		std::function<void()> drain;
		~level_task_drain_guard()
		{
			try { drain(); } catch (...) {}
		}
	} task_drain_guard{drain_level_tasks};
	CTimer game_specific_before_timer;
	game_specific_before_timer.Start();
	R_ASSERT(Load_GameSpecific_Before());
	Msg("* [LEVEL LOAD] game-specific before: %d ms", game_specific_before_timer.GetElapsed_ms());

	pApp->LoadSwitch();

	// R4 internally submits immutable prepare work to NativeLoadExecutor. Keep
	// the orchestration itself on the render owner because LoadTitle and the
	// prepared registry commits touch the loading screen/immediate context.
	CTimer render_timer;
	render_timer.Start();
	Render->level_BeginAsyncLoad();
	try
	{
		Render->level_Load(LL_Stream);
	}
	catch (...)
	{
		const std::exception_ptr failure = std::current_exception();
		Render->level_AbortAsyncLoad();
		try { drain_level_tasks(); } catch (...) {}
		FS.r_close(LL_Stream);
		std::rethrow_exception(failure);
	}
	Msg("* [LEVEL LOAD] renderer: %d ms", render_timer.GetElapsed_ms());
	CTimer barrier_timer;
	barrier_timer.Start();
	drain_level_tasks();
	Msg("* [LEVEL LOAD] CFORM/AI barrier: %d ms", barrier_timer.GetElapsed_ms());

	Sound->set_geometry_occ(ObjectSpace.GetStaticModel());
	Sound->set_handler(_sound_event);
	// tscreate.FrameEnd ();
	// Msg ("* S-CREATE: %f ms, %d times",tscreate.result,tscreate.count);

	// Objects
	Objects.Load();
	//. ANDY R_ASSERT (Load_GameSpecific_After ());

	// Done
	FS.r_close(LL_Stream);
	bReady = true;
	if (!g_dedicated_server) IR_Capture();
#ifndef DEDICATED_SERVER
	Device.seqRender.Add(this);
#endif

	Device.seqFrame.Add(this);
	Msg("* [LEVEL LOAD] total: %d ms", level_timer.GetElapsed_ms());

	//SECUROM_MARKER_PERFORMANCE_OFF(10)

	return true;
}

#ifndef _EDITOR
#include "../xrCPU_Pipe/ttapi.h"
#endif

int psNET_DedicatedSleep = 5;

void IGame_Level::OnRender()
{
#ifndef DEDICATED_SERVER
	// if (_abs(Device.fTimeDelta)<EPS_S) return;

#ifdef _GPA_ENABLED
    TAL_ID rtID = TAL_MakeID( 1 , Core.dwFrame , 0);
    TAL_CreateID( rtID );
    TAL_BeginNamedVirtualTaskWithID( "GameRenderFrame" , rtID );
    TAL_Parami( "Frame#" , Device.dwFrame );
    TAL_EndVirtualTask();
#endif // _GPA_ENABLED

	// Level render, only when no client output required
	if (!g_dedicated_server)
	{
		const bool measure_precache = pApp && pApp->LoadSessionMeasurePrecache();
		u64 calculate_ticks = 0;
		u64 render_ticks = 0;
		{
			PROF_EVENT("IGame_Level::OnRender: Calculate");
			const u64 started_at = measure_precache ? CPU::QPC() : 0;
			Render->Calculate();
			if (measure_precache)
				calculate_ticks = CPU::QPC() - started_at;
		}
		{
			PROF_EVENT("IGame_Level::OnRender: Render");
			const u64 started_at = measure_precache ? CPU::QPC() : 0;
			Render->Render();
			if (measure_precache)
				render_ticks = CPU::QPC() - started_at;
		}
		if (measure_precache)
			pApp->LoadSessionRecordPrecacheLevel(calculate_ticks, render_ticks);
	}
	else
	{
		Sleep(psNET_DedicatedSleep);
	}

#ifdef _GPA_ENABLED
    TAL_RetireID( rtID );
#endif // _GPA_ENABLED

	// Font
	// pApp->pFontSystem->SetSizeI(0.023f);
	// pApp->pFontSystem->OnRender ();
#endif
}

void IGame_Level::OnFrame()
{
	PROF_EVENT("IGame_Level::OnFrame");
	// Log ("- level:on-frame: ",u32(Device.dwFrame));
	// if (_abs(Device.fTimeDelta)<EPS_S) return;

	// Update all objects
	VERIFY(bReady);
	Objects.Update(false);
	g_hud->OnFrame();

	// Ambience
	if (Sounds_Random.size() && (Device.dwTimeGlobal > Sounds_Random_dwNextTime))
	{
		Sounds_Random_dwNextTime = Device.dwTimeGlobal + ::Random.randI(10000, 20000);
		Fvector pos;
		pos.random_dir().normalize().mul(::Random.randF(30, 100)).add(Device.vCameraPosition);
		int id = ::Random.randI(Sounds_Random.size());
		if (Sounds_Random_Enabled)
		{
			Sounds_Random[id].play_at_pos(0, pos, 0);
			Sounds_Random[id].set_volume(1.f);
			Sounds_Random[id].set_range(10, 200);
		}
	}
}

// ==================================================================================================

void CServerInfo::AddItem(LPCSTR name_, LPCSTR value_, u32 color_)
{
	shared_str s_name(name_);
	AddItem(s_name, value_, color_);
}

void CServerInfo::AddItem(shared_str& name_, LPCSTR value_, u32 color_)
{
	SItem_ServerInfo it;
	// shared_str s_name = CStringTable().translate( name_ );

	// xr_strcpy( it.name, s_name.c_str() );
	xr_strcpy(it.name, name_.c_str());
	xr_strcat(it.name, " = ");
	xr_strcat(it.name, value_);
	it.color = color_;

	if (data.size() < max_item)
	{
		data.push_back(it);
	}
}

void IGame_Level::SetEntity(CObject* O)
{
	if (pCurrentEntity)
		pCurrentEntity->On_LostEntity();

	if (O)
		O->On_SetEntity();

	pCurrentEntity = pCurrentViewEntity = O;
}

void IGame_Level::SetViewEntity(CObject* O)
{
	if (pCurrentViewEntity)
		pCurrentViewEntity->On_LostEntity();

	if (O)
		O->On_SetEntity();

	pCurrentViewEntity = O;
}

void IGame_Level::SoundEvent_Register(ref_sound_data_ptr S, float range)
{
	PROF_EVENT("IGame_Level::SoundEvent_Register");
	if (!g_bLoaded) return;
	if (!S) return;
	if (S->g_object && S->g_object->getDestroy())
	{
		S->g_object = 0;
		return;
	}
	if (0 == S->feedback) return;

	clamp(range, 0.1f, 500.f);

	const CSound_params* p = S->feedback->get_params();
	Fvector snd_position = p->position;
	if (S->feedback->is_2D())
	{
		snd_position.add(Sound->listener_position());
	}

	VERIFY(p && _valid(range));
	range = _min(range, p->max_ai_distance);
	VERIFY(_valid(snd_position));
	VERIFY(_valid(p->max_ai_distance));
	VERIFY(_valid(p->volume));

	// Query objects
	Fvector bb_size = {range, range, range};
	g_SpatialSpace->q_box(snd_ER, 0, STYPE_REACTTOSOUND, snd_position, bb_size);

	// Iterate
	auto it = snd_ER.begin();
	auto end = snd_ER.end();
	for (; it != end; it++)
	{
		Feel::Sound* L = (*it)->dcast_FeelSound();
		if (0 == L) continue;
		CObject* CO = (*it)->dcast_CObject();
		VERIFY(CO);
		if (CO->getDestroy()) continue;

		// Energy and signal
		VERIFY(_valid((*it)->spatial.sphere.P));
		float dist = snd_position.distance_to((*it)->spatial.sphere.P);
		if (dist > p->max_ai_distance) continue;
		VERIFY(_valid(dist));
		VERIFY2(!fis_zero(p->max_ai_distance), S->handle->file_name());
		float Power = (1.f - dist / p->max_ai_distance) * p->volume;
		VERIFY(_valid(Power));
		if (Power > EPS_S)
		{
			float occ = Sound->get_occlusion_to((*it)->spatial.sphere.P, snd_position);
			VERIFY(_valid(occ));
			Power *= occ;
			if (Power > EPS_S)
			{
				_esound_delegate D = {L, S, Power};
				snd_Events.push_back(D);
			}
		}
	}
	snd_ER.clear_not_free();
}

void IGame_Level::SoundEvent_Dispatch()
{
	PROF_EVENT("IGame_Level::SoundEvent_Dispatch");
	while (!snd_Events.empty())
	{
		_esound_delegate& D = snd_Events.back();
		VERIFY(D.dest && D.source);
		if (D.source->feedback)
		{
			D.dest->feel_sound_new(
				D.source->g_object,
				D.source->g_type,
				D.source->g_userdata,

				D.source->feedback->is_2D() ? Device.vCameraPosition : D.source->feedback->get_params()->position,
				D.power
			);
		}
		snd_Events.pop_back();
	}
}

// Lain: added
void IGame_Level::SoundEvent_OnDestDestroy(Feel::Sound* obj)
{
	PROF_EVENT("IGame_Level::SoundEvent_OnDestDestroy");
	struct rem_pred
	{
		rem_pred(Feel::Sound* obj) : m_obj(obj)
		{
		}

		bool operator ()(const _esound_delegate& d)
		{
			return d.dest == m_obj;
		}

	private:
		Feel::Sound* m_obj;
	};

	snd_Events.erase(std::remove_if(snd_Events.begin(), snd_Events.end(), rem_pred(obj)),
	                 snd_Events.end());
}
