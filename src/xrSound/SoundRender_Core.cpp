#include "stdafx.h"
#pragma hdrstop

#include "../xrEngine/xrLevel.h"

#include "SoundRender_Core.h"
#include "SoundRender_Source.h"
#include "SoundRender_Emitter.h"
#include "SoundRender_CoreA.h"
#include "../xrCore/_math.h"

#include "NotificationClient.h"

#include <AL/efx.h>

float psSpeedOfSound = 1.f;
int psSoundTargets = 1024;
Flags32 psSoundFlags = {ss_Hardware | ss_EFX};
float psSoundOcclusionScale = 0.5f;
float psSoundCull = 0.01f;
float psSoundRolloff = 0.75f;
u32 psSoundModel = 0;
float psSoundVEffects = 1.0f;
float psSoundVFactor = 1.0f;

float psSoundVMusic = 1.f;
float psSoundVMusicFactor = 1.f;
int psSoundCacheSizeMB = 256;

float snd_efx_environment_change_time = 1.66f;

CSoundRender_Core* SoundRender = nullptr;
CSound_manager_interface* Sound = nullptr;

CSoundRender_Core::CSoundRender_Core()
{
	bPresent = FALSE;
	bUserEnvironment = FALSE;
	geom_MODEL = NULL;
	geom_ENV = NULL;
	geom_SOM = NULL;
	s_environment = NULL;
	Handler = NULL;
	s_targets_pu = 0;
	s_emitters_u = 0;
	e_current.set_identity();
	e_identity.set_identity();
	e_target_ptr = &e_identity;
	bListenerMoved = FALSE;
	bReady = FALSE;
	bLocked = FALSE;
	m_bUpdateThreadRun = FALSE;
	m_bUpdateThreadExited = TRUE;
	m_heavy_load_active = FALSE;
	m_snap_P.set(0, 0, 0);
	m_snap_D.set(0, 0, 1);
	m_snap_N.set(0, 1, 0);
	fTimer_Value = Timer.GetElapsed_sec();
	fTimer_Delta = 0.0f;
	m_iPauseCounter = 1;
	pSysNotification = xr_new<CNotificationClient>();
}

CSoundRender_Core::~CSoundRender_Core()
{
	update_thread_stop();
#ifdef _EDITOR
	ETOOLS::destroy_model		(geom_ENV);
	ETOOLS::destroy_model		(geom_SOM);
#else
	xr_delete(geom_ENV);
	xr_delete(geom_SOM);
	xr_delete(pSysNotification);
#endif
}

void CSoundRender_Core::_initialize(int stage)
{
	Timer.Start();

	// load environment
	env_load();

	bPresent = TRUE;

	// Cache
	cache_bytes_per_line = (sdef_target_block / 8) * 276400 / 1000;
	cache.initialize(psSoundCacheSizeMB * 1024, cache_bytes_per_line);

	bReady = TRUE;

	if (Core.ParamsData.test(ECoreParams::prefetch_sounds))
	{
		i_create_all_sources();
	}
}

extern xr_vector<u8> g_target_temp_data;
extern xr_vector<u8> g_target_temp_data_16;

void CSoundRender_Core::_clear()
{
	bReady = FALSE;
	cache.destroy();
	env_unload();

	// remove non-persistent emitters; rescue persistent ones
	xr_vector<CSoundRender_Emitter*> survivors;
	xr_vector<CSoundRender_Source*> persistent_sources;
	for (u32 eit = 0; eit < s_emitters.size(); eit++)
	{
		CSoundRender_Emitter* E = s_emitters[eit];
		if (E->is_persistent())
		{
			// Detach from its current target — the target pool is about
			// to be destroyed too. The emitter enters "simulating" mode
			// so the FSM keeps ticking even without a render target.
			if (E->target)
				E->cancel(); // switches to stSimulating/stSimulatingLooped, releases target

			survivors.push_back(E);
			if (E->owner_data && E->owner_data->handle)
			{
				CSoundRender_Source* src = (CSoundRender_Source*)E->owner_data->handle;
				bool already_saved = false;
				for (u32 i = 0; i < persistent_sources.size(); ++i)
				{
					if (persistent_sources[i] == src)
					{
						already_saved = true;
						break;
					}
				}
				if (!already_saved)
					persistent_sources.push_back(src);
			}
		}
		else
		{
			// Ensure script-side ref_sound no longer points to this emitter.
			// Without this, gameplay sound owners can keep dangling feedback
			// pointers after level transition and crash on next update.
			E->stop(FALSE);
			xr_delete(E);
		}
	}
	s_emitters.clear();
	for (u32 i = 0; i < survivors.size(); ++i)
	{
		CSoundRender_Emitter* E = survivors[i];
		s_emitters.push_back(E);
		if (E->owner_data)
			reconcile_emitter_feedback(E->owner_data._get());
	}

	// Remove sources not used by persistent survivors.
	xr_unordered_map<xr_string, CSoundRender_Source*> kept_sources;
	for (auto& kv : s_sources)
	{
		bool keep = false;
		for (u32 i = 0; i < persistent_sources.size(); ++i)
		{
			if (persistent_sources[i] == kv.second)
			{
				keep = true;
				break;
			}
		}
		if (keep)
			kept_sources[kv.first] = kv.second;
		else
			xr_delete(kv.second);
	}
	s_sources.clear();
	s_sources = std::move(kept_sources);

	g_target_temp_data.clear();
	g_target_temp_data_16.clear();
}

void CSoundRender_Core::update(const Fvector& P, const Fvector& D, const Fvector& N)
{
	m_snap_P = P;
	m_snap_D = D;
	m_snap_N = N;

	if (use_background_update())
		return;

	sound_api_enter();
	update_impl(P, D, N);
	sound_api_leave();
}

void CSoundRender_Core::stop_emitters()
{
	sound_api_enter();
	for (u32 eit = 0; eit < s_emitters.size(); eit++)
	    {
		        CSoundRender_Emitter* E = s_emitters[eit];
		        if (!E->is_persistent())
		            E->stop(FALSE);
		}
	sound_api_leave();
}
// Force-stop all persistent emitters (use at main menu / full game quit).
void CSoundRender_Core::stop_persistent_emitters()
{
	sound_api_enter();
    for (u32 eit = 0; eit < s_emitters.size(); eit++)
    {
        CSoundRender_Emitter* E = s_emitters[eit];
        if (E->is_persistent())
        {
            E->set_persistent(false);       // release anchor + disarm
            E->stop(FALSE);
        }
    }
	sound_api_leave();
}

bool CSoundRender_Core::emitter_belongs_to_owner(CSoundRender_Emitter* E, ref_sound_data* owner)
{
	return owner && E && E->owner_data && E->owner_data._get() == owner;
}

CSoundRender_Emitter* CSoundRender_Core::find_emitter_for_owner(ref_sound_data* owner, bool playing_only) const
{
	if (!owner)
		return nullptr;

	for (u32 it = 0; it < s_emitters.size(); it++)
	{
		CSoundRender_Emitter* E = s_emitters[it];
		if (emitter_belongs_to_owner(E, owner) && (!playing_only || E->isPlaying()))
			return E;
	}
	return nullptr;
}

void CSoundRender_Core::stop_emitters_for_owner(ref_sound_data* owner)
{
	if (!owner)
		return;

	sound_api_enter();
	for (u32 it = 0; it < s_emitters.size(); it++)
	{
		CSoundRender_Emitter* E = s_emitters[it];
		if (!emitter_belongs_to_owner(E, owner))
			continue;
		if (E->is_persistent())
			E->set_persistent(false);
		E->stop(FALSE);
	}
	sound_api_leave();
}

bool CSoundRender_Core::has_playing_emitter_for_owner(ref_sound_data* owner) const
{
	return find_emitter_for_owner(owner, true) != nullptr;
}

bool CSoundRender_Core::reconcile_emitter_feedback(ref_sound_data* owner)
{
	if (!owner)
		return false;

	sound_api_enter();
	CSoundRender_Emitter* E = find_emitter_for_owner(owner, true);
	if (!E && owner->handle)
	{
		// Level transition can recreate Lua sound objects (new owner pointer) while
		// a persistent emitter keeps playing. Reattach by source handle so control
		// (stop/volume/state) continues to work after load without restarting audio.
		for (u32 it = 0; it < s_emitters.size(); it++)
		{
			CSoundRender_Emitter* candidate = s_emitters[it];
			if (!candidate || !candidate->is_persistent() || !candidate->isPlaying())
				continue;
			if (!candidate->owner_data || candidate->owner_data->handle != owner->handle)
				continue;

			ref_sound_data_ptr prev_owner = candidate->owner_data;
			if (prev_owner && prev_owner._get() != owner)
				prev_owner->feedback = nullptr;

			release_persistent(candidate);
			candidate->owner_data = owner;
			anchor_persistent(candidate);
			E = candidate;
			break;
		}
	}
	if (E)
		owner->feedback = E;
	sound_api_leave();
	return E != nullptr;
}

void CSoundRender_Core::restart_emitters()
{
	for (u32 eit = 0; eit < s_emitters.size(); eit++)
		if (s_emitters[eit]->target)
			i_start(s_emitters[eit]);
}

void CSoundRender_Core::set_heavy_load_active(bool active)
{
	m_heavy_load_active = active ? TRUE : FALSE;
}

bool CSoundRender_Core::use_background_update() const
{
	// Dedicated sound thread owns OpenAL buffer updates whenever it is running
	return m_bUpdateThreadRun != FALSE;
}

void CSoundRender_Core::sound_api_enter()
{
	m_api_cs.Enter();
	if (SoundRenderA)
		SoundRenderA->bind_context();
}

void CSoundRender_Core::sound_api_leave()
{
	m_api_cs.Leave();
}

void CSoundRender_Core::update_thread_start()
{
	if (m_bUpdateThreadRun)
		return;
	m_bUpdateThreadExited = FALSE;
	m_bUpdateThreadRun = TRUE;
	thread_spawn(SoundRender_UpdateThread, "X-Ray Sound Update", 0, nullptr);
}

void CSoundRender_Core::update_thread_stop()
{
	if (!m_bUpdateThreadRun)
		return;
	m_bUpdateThreadRun = FALSE;
	// Wait until the worker has finished its current iteration and exited, so the
	// caller (e.g. _clear) can safely tear down OpenAL targets/context afterwards.
	while (!m_bUpdateThreadExited)
		Sleep(1);
}

bool CSoundRender_Core::has_playing_persistent() const
{
	for (u32 it = 0; it < s_emitters.size(); it++)
	{
		CSoundRender_Emitter* E = s_emitters[it];
		if (E->is_persistent() && E->isPlaying())
			return true;
	}
	return false;
}

int CSoundRender_Core::pause_emitters(bool val)
{
	sound_api_enter();
	m_iPauseCounter += val ? +1 : -1;
	VERIFY(m_iPauseCounter>=0);

	for (u32 it = 0; it < s_emitters.size(); it++)
	{
		CSoundRender_Emitter* E = (CSoundRender_Emitter*)s_emitters[it];
		if (E->is_persistent() && E->is_persistent_in_menu())
			continue;
		E->pause(val, val ? m_iPauseCounter : m_iPauseCounter + 1);
	}

	const int counter = m_iPauseCounter;
	sound_api_leave();
	return counter;
}

// Called when an emitter is marked persistent.
// The core grabs a strong ref to owner_data so the sound survives Lua GC.
void CSoundRender_Core::anchor_persistent(CSoundRender_Emitter* E)
{
    if (!E || !E->owner_data)
        return;

    // Only add once
    for (const auto& ref : s_persistent_refs)
        if (ref._get() == E->owner_data._get())
            return;

    s_persistent_refs.push_back(E->owner_data);
}

// Called when an emitter is un-marked persistent.
// The core drops its strong ref; the emitter is now mortal again.
void CSoundRender_Core::release_persistent(CSoundRender_Emitter* E)
{
    if (!E || !E->owner_data)
        return;

    auto it = std::find_if(
        s_persistent_refs.begin(), s_persistent_refs.end(),
        [E](const ref_sound_data_ptr& p) { return p._get() == E->owner_data._get(); }
    );
    if (it != s_persistent_refs.end())
        s_persistent_refs.erase(it);
}

void CSoundRender_Core::env_load()
{
	// Load environment
	string_path fn;
	if (FS.exist(fn, "$game_data$",SNDENV_FILENAME))
	{
		s_environment = xr_new<SoundEnvironment_LIB>();
		s_environment->Load(fn);
	}

	// Load geometry

	// Assosiate geometry
}

void CSoundRender_Core::env_unload()
{
	// Unload 
	if (s_environment)
		s_environment->Unload();
	xr_delete(s_environment);

	// Unload geometry
}

void CSoundRender_Core::_restart()
{
	cache.destroy();
	cache.initialize(psSoundCacheSizeMB * 1024, cache_bytes_per_line);
	env_apply();
}

void CSoundRender_Core::set_handler(sound_event* E)
{
	Handler = E;
}

void CSoundRender_Core::set_geometry_occ(CDB::MODEL* M)
{
	geom_MODEL = M;
}

void CSoundRender_Core::set_geometry_som(IReader* I)
{
#ifdef _EDITOR
	ETOOLS::destroy_model	(geom_SOM);
#else
	xr_delete(geom_SOM);
#endif
	if (0 == I) return;

	// check version
	R_ASSERT(I->find_chunk(0));
	u32 version = I->r_u32();
	VERIFY2(version==0, "Invalid SOM version");
	// load geometry	
	IReader* geom = I->open_chunk(1);
	VERIFY2(geom, "Corrupted SOM file");
	// Load tris and merge them
	struct SOM_poly
	{
		Fvector3 v1;
		Fvector3 v2;
		Fvector3 v3;
		u32 b2sided;
		float occ;
	};
	// Create AABB-tree
#ifdef _EDITOR
	CDB::Collector*	CL			= ETOOLS::create_collector();
	while (!geom->eof()){
		SOM_poly				P;
		geom->r					(&P,sizeof(P));
        ETOOLS::collector_add_face_pd		(CL,P.v1,P.v2,P.v3,*(u32*)&P.occ,0.01f);
		if (P.b2sided)
			ETOOLS::collector_add_face_pd	(CL,P.v3,P.v2,P.v1,*(u32*)&P.occ,0.01f);
	}
	geom_SOM					= ETOOLS::create_model_cl(CL);
    ETOOLS::destroy_collector	(CL);
#else
	CDB::Collector CL;
	while (!geom->eof())
	{
		SOM_poly P;
		geom->r(&P, sizeof(P));
		CL.add_face_packed_D(P.v1, P.v2, P.v3, *(u32*)&P.occ, 0.01f);
		if (P.b2sided)
			CL.add_face_packed_D(P.v3, P.v2, P.v1, *(u32*)&P.occ, 0.01f);
	}
	geom_SOM = xr_new<CDB::MODEL>();
	geom_SOM->build(CL.getV(), int(CL.getVS()), CL.getT(), int(CL.getTS()));
#endif

	geom->close();
}

void CSoundRender_Core::set_geometry_env(IReader* I)
{
#ifdef _EDITOR
	ETOOLS::destroy_model	(geom_ENV);
#else
	xr_delete(geom_ENV);
#endif
	if (0 == I) return;
	if (0 == s_environment) return;

	// Assosiate names
	xr_vector<u16> ids;
	IReader* names = I->open_chunk(0);
	while (!names->eof())
	{
		string256 n;
		names->r_stringZ(n, sizeof(n));
		int id = s_environment->GetID(n);
		R_ASSERT(id>=0);
		ids.push_back(u16(id));
	}
	names->close();

	// Load geometry
	IReader* geom_ch = I->open_chunk(1);

	u8* _data = (u8*)xr_malloc(geom_ch->length());

	Memory.mem_copy(_data, geom_ch->pointer(), geom_ch->length());

	IReader* geom = xr_new<IReader>(_data, geom_ch->length(), 0);

	hdrCFORM H;
	geom->r(&H, sizeof(hdrCFORM));
	Fvector* verts = (Fvector*)geom->pointer();
	CDB::TRI* tris = (CDB::TRI*)(verts + H.vertcount);
	for (u32 it = 0; it < H.facecount; it++)
	{
		CDB::TRI* T = tris + it;
		u16 id_front = (u16)((T->dummy & 0x0000ffff) >> 0); //	front face
		u16 id_back = (u16)((T->dummy & 0xffff0000) >> 16); //	back face
		R_ASSERT(id_front<(u16)ids.size());
		R_ASSERT(id_back<(u16)ids.size());
		T->dummy = u32(ids[id_back] << 16) | u32(ids[id_front]);
	}
#ifdef _EDITOR
	geom_ENV			= ETOOLS::create_model(verts, H.vertcount, tris, H.facecount);
	env_apply			();
#else
	geom_ENV = xr_new<CDB::MODEL>();
	geom_ENV->build(verts, H.vertcount, tris, H.facecount);
#endif
	geom_ch->close();
	geom->close();
	xr_free(_data);
}

void CSoundRender_Core::create(ref_sound& S, const char* fName, esound_type sound_type, int game_type)
{
	if (!bPresent) return;
	S._p = xr_new<ref_sound_data>(fName, sound_type, game_type);
}

void CSoundRender_Core::attach_tail(ref_sound& S, const char* fName)
{
	if (!bPresent) return;
	string_path fn;
	xr_strcpy(fn, fName);
	if (strext(fn)) *strext(fn) = 0;
	if (S._p->fn_attached[0].size() && S._p->fn_attached[1].size())
	{
#ifdef DEBUG
		Msg("! 2 file already in queue [%s][%s]",S._p->fn_attached[0].c_str(),S._p->fn_attached[1].c_str());
#endif // #ifdef DEBUG
		return;
	}

	u32 idx = S._p->fn_attached[0].size() ? 1 : 0;

	S._p->fn_attached[idx] = fn;

	CSoundRender_Source* s = SoundRender->i_create_source(fn);
	S._p->dwBytesTotal += s->bytes_total();
	S._p->fTimeTotal += s->length_sec();
	if (S._feedback())
		((CSoundRender_Emitter*)S._feedback())->fTimeToStop += s->length_sec();

	SoundRender->i_destroy_source(s);
}

void CSoundRender_Core::clone(ref_sound& S, const ref_sound& from, esound_type sound_type, int game_type)
{
	if (!bPresent) return;
	S._p = xr_new<ref_sound_data>();
	S._p->handle = from._p->handle;
	S._p->dwBytesTotal = from._p->dwBytesTotal;
	S._p->fTimeTotal = from._p->fTimeTotal;
	S._p->fn_attached[0] = from._p->fn_attached[0];
	S._p->fn_attached[1] = from._p->fn_attached[1];
	S._p->g_type = (game_type == sg_SourceType) ? S._p->handle->game_type() : game_type;
	S._p->s_type = sound_type;
}


void CSoundRender_Core::play(ref_sound& S, CObject* O, u32 flags, float delay)
{
	if (!bPresent || (0==S._handle())) return;
	sound_api_enter();
	S._p->g_object = O;
	S.reconcile_feedback();
	if (S._feedback()) ((CSoundRender_Emitter*)S._feedback())->rewind();
	else i_play(&S, flags & sm_Looped, delay);

	if ((flags & sm_2D) || (S._handle()->channels_num() == 2))
		S._feedback()->switch_to_2D();

	if (flags & sm_Intro)
	{
		S._feedback()->switch_to_Intro();
	}
	sound_api_leave();
}

void CSoundRender_Core::play_no_feedback(ref_sound& S, CObject* O, u32 flags, float delay, Fvector* pos, float* vol, float* freq, Fvector2* range)
{
	if (!bPresent || (0 == S._handle())) return;
	sound_api_enter();
	ref_sound_data_ptr orig = S._p;
	S._p = xr_new<ref_sound_data>();
	S._p->handle = orig->handle;
	S._p->g_type = orig->g_type;
	S._p->g_object = O;
	S._p->dwBytesTotal = orig->dwBytesTotal;
	S._p->fTimeTotal = orig->fTimeTotal;
	S._p->fn_attached[0] = orig->fn_attached[0];
	S._p->fn_attached[1] = orig->fn_attached[1];

	i_play(&S, flags & sm_Looped, delay);

	if (flags & sm_2D || S._handle()->channels_num() == 2)
		S._feedback()->switch_to_2D();

	if (flags & sm_Intro)
	{
		S._feedback()->switch_to_Intro();
	}

	if (pos) S._feedback()->set_position(*pos);
	if (freq) S._feedback()->set_frequency(*freq);
	if (range) S._feedback()->set_range((*range)[0], (*range)[1]);
	if (vol) S._feedback()->set_volume(*vol);
	S._p = orig;
	sound_api_leave();
}

void CSoundRender_Core::play_at_pos(ref_sound& S, CObject* O, const Fvector &pos, u32 flags, float delay)
{
	if (!bPresent || (0 == S._handle())) return;
	sound_api_enter();
	S._p->g_object = O;
	S.reconcile_feedback();
	if (S._feedback()) ((CSoundRender_Emitter*)S._feedback())->rewind();
	else i_play(&S, flags & sm_Looped, delay);

	S._feedback()->set_position(pos);
	if ((flags & sm_2D) || (S._handle()->channels_num() == 2))
		S._feedback()->switch_to_2D();

	if (flags & sm_Intro)
	{
		S._feedback()->switch_to_Intro();
	}
	sound_api_leave();
}

void CSoundRender_Core::destroy(ref_sound& S)
{
	sound_api_enter();
	if (S._feedback())
	{
		CSoundRender_Emitter* E = (CSoundRender_Emitter*)S._feedback();
		if (!E->is_persistent())
			E->stop(FALSE);
		// else: let it keep playing; the core holds ownership
	}
	S._p = 0;
	sound_api_leave();
}

void CSoundRender_Core::_create_data(ref_sound_data& S, LPCSTR fName, esound_type sound_type, int game_type)
{
	string_path fn;
	xr_strcpy(fn, fName);
	if (strext(fn)) *strext(fn) = 0;
	S.handle = (CSound_source*)SoundRender->i_create_source(fn);
	S.g_type = (game_type == sg_SourceType) ? S.handle->game_type() : game_type;
	S.s_type = sound_type;
	S.feedback = 0;
	S.g_object = 0;
	S.g_userdata = 0;
	S.dwBytesTotal = S.handle->bytes_total();
	S.fTimeTotal = S.handle->length_sec();
}

void CSoundRender_Core::_destroy_data(ref_sound_data& S)
{
	if (S.feedback)
	{
		CSoundRender_Emitter* E = (CSoundRender_Emitter*)S.feedback;
		if (!E->is_persistent())
			E->stop(FALSE);
		// For persistent emitters: do NOT stop.  The core holds a strong
		// ref in s_persistent_refs so owner_data stays alive; the emitter
		// keeps playing.  The script-side ref_sound_data* is going away but
		// the emitter's owner_data ptr still points to the core-held copy.
	}
	if (!S.feedback || !((CSoundRender_Emitter*)S.feedback)->is_persistent())
	    R_ASSERT(0 == S.feedback);
	SoundRender->i_destroy_source((CSoundRender_Source*)S.handle);

	S.handle = NULL;
}

CSoundRender_Environment* CSoundRender_Core::get_environment(const Fvector& P)
{
	PROF_EVENT("CSoundRender_Core::get_environment");
	static CSoundRender_Environment identity;

	if (bUserEnvironment)
	{
		return &s_user_environment;
	}
	else
	{
		if (geom_ENV)
		{
			Fvector dir = {0, -1, 0};
#ifdef _EDITOR
			ETOOLS::ray_options		(CDB::OPT_ONLYNEAREST);
			ETOOLS::ray_query		(geom_ENV,P,dir,1000.f);
			if (ETOOLS::r_count()){
				CDB::RESULT*		r	= ETOOLS::r_begin();
#else
			geom_DB.ray_options(CDB::OPT_ONLYNEAREST);
			geom_DB.ray_query(geom_ENV, P, dir, 1000.f);
			if (geom_DB.r_count())
			{
				CDB::RESULT* r = geom_DB.r_begin();
#endif
				CDB::TRI* T = geom_ENV->get_tris() + r->id;
				Fvector* V = geom_ENV->get_verts();
				Fvector tri_norm;
				tri_norm.mknormal(V[T->verts[0]], V[T->verts[1]], V[T->verts[2]]);
				float dot = dir.dotproduct(tri_norm);
				if (dot < 0)
				{
					u16 id_front = (u16)((T->dummy & 0x0000ffff) >> 0); //	front face
					return s_environment->Get(id_front);
				}
				else
				{
					u16 id_back = (u16)((T->dummy & 0xffff0000) >> 16); //	back face
					return s_environment->Get(id_back);
				}
			}
			else
			{
				identity.set_identity();
				return &identity;
			}
		}
		else
		{
			identity.set_identity();
			return &identity;
		}
	}
}

void CSoundRender_Core::env_apply()
{
	bListenerMoved = TRUE;
}

void CSoundRender_Core::update_listener(const Fvector& P, const Fvector& D, const Fvector& N, float dt)
{
}

void CSoundRender_Core::object_relcase(CObject* obj)
{
	if (obj) {
		for (u32 eit = 0; eit < s_emitters.size(); eit++) {
			if (s_emitters[eit] && s_emitters[eit]->owner_data && (obj == s_emitters[eit]->owner_data->g_object))
				s_emitters[eit]->owner_data->g_object = nullptr;
		}
	}
}

#ifdef _EDITOR
void CSoundRender_Core::set_user_env(CSound_environment* E)
{
	if ((0 == E) && !bUserEnvironment) return;

	if (E)
	{
		s_user_environment	= *((CSoundRender_Environment*)E);
		bUserEnvironment	= TRUE;
	}
	else 
	{
		bUserEnvironment	= FALSE;
	}
	env_apply			();
}

void CSoundRender_Core::refresh_env_library()
{
	env_unload			();
	env_load			();
	env_apply			();
}
void CSoundRender_Core::refresh_sources()
{
	for (u32 eit=0; eit<s_emitters.size(); eit++)
    	s_emitters[eit]->stop(FALSE);
	for (const auto& kv : s_sources)
	{
		CSoundRender_Source* s = kv.second;
    	s->unload		();
		s->load			(*s->fname);
    }
}
void CSoundRender_Core::set_environment_size	(CSound_environment* src_env, CSound_environment** dst_env)
{
	
}
void CSoundRender_Core::set_environment	(u32 id, CSound_environment** dst_env)
{
	
}
#endif
