#pragma once

#include "SoundRender.h"
#include "SoundRender_Environment.h"
#include "SoundRender_Cache.h"
#include "../xrCore/xrSyncronize.h"

class CNotificationClient;

class CSoundRender_Core : public CSound_manager_interface
{
	volatile BOOL bLocked;
protected:
	virtual void _create_data(ref_sound_data& S, LPCSTR fName, esound_type sound_type, int game_type);
	virtual void _destroy_data(ref_sound_data& S);
	CNotificationClient* pSysNotification = nullptr;
public:
	volatile BOOL bPendingDefaultDeviceSwitch = FALSE;
	volatile BOOL bPendingDeviceListRefresh = FALSE;
protected:
	BOOL bListenerMoved;

	CSoundRender_Environment e_current;
	CSoundRender_Environment e_identity;
	CSoundRender_Environment* e_target_ptr;

public:
	typedef std::pair<ref_sound_data_ptr, float> event;
	xr_vector<event> s_events;
public:
	BOOL bPresent;
	BOOL bUserEnvironment;
	BOOL bReady;
	bool m_is_supported; // Boolean variable to indicate presence of EFX Extension

	CTimer Timer;
	float fTimer_Value;
	float fTimer_Delta;
	sound_event* Handler;
protected:
	// Collider
#ifndef _EDITOR
	CDB::COLLIDER geom_DB;
#endif
	CDB::MODEL* geom_SOM;
	CDB::MODEL* geom_MODEL;
	CDB::MODEL* geom_ENV;

	// Containers
	xr_unordered_map<xr_string, CSoundRender_Source*> s_sources;
	xr_vector<CSoundRender_Emitter*> s_emitters;
	u32 s_emitters_u; // emitter update marker
	xr_vector<CSoundRender_Target*> s_targets;
	xr_vector<CSoundRender_Target*> s_targets_defer;
	u32 s_targets_pu; // parameters update
	SoundEnvironment_LIB* s_environment;
	CSoundRender_Environment s_user_environment;

	int m_iPauseCounter;

    // Persistent sound support
    // One entry per persistent emitter: keeps owner_data alive across level GC.
    xr_vector<ref_sound_data_ptr> s_persistent_refs;

	// Background OpenAL update (level load blocks main thread for seconds at a time)
	xrCriticalSection m_api_cs;
	volatile BOOL m_bUpdateThreadRun;
	volatile BOOL m_bUpdateThreadExited;
	volatile BOOL m_heavy_load_active;
	Fvector m_snap_P;
	Fvector m_snap_D;
	Fvector m_snap_N;

	void update_impl(const Fvector& P, const Fvector& D, const Fvector& N);
	void sound_api_enter();
	void sound_api_leave();
	bool use_background_update() const;

	static bool emitter_belongs_to_owner(CSoundRender_Emitter* E, ref_sound_data* owner);
	CSoundRender_Emitter* find_emitter_for_owner(ref_sound_data* owner, bool playing_only) const;

	friend void SoundRender_UpdateThread(void*);
public:
	// Cache
	CSoundRender_Cache cache;
	u32 cache_bytes_per_line;

public:
	CSoundRender_Core();
	virtual ~CSoundRender_Core();

	// General
	virtual void _initialize(int stage) =0;
	virtual void _clear() =0;
	virtual void _restart();
	virtual void switch_device(LPCSTR device_name) {}
	virtual void refresh_devices() {}

	// Sound interface
	void verify_refsound(ref_sound& S);
	virtual void create(ref_sound& S, LPCSTR fName, esound_type sound_type, int game_type);
	virtual void attach_tail(ref_sound& S, LPCSTR fName);

	virtual void clone(ref_sound& S, const ref_sound& from, esound_type sound_type, int game_type);
	virtual void destroy(ref_sound& S);
	virtual void stop_emitters();
	virtual void restart_emitters();
	virtual int pause_emitters(bool val);
    virtual void stop_persistent_emitters() override;
	virtual void stop_emitters_for_owner(ref_sound_data* owner) override;
	virtual bool has_playing_emitter_for_owner(ref_sound_data* owner) const override;
	virtual bool reconcile_emitter_feedback(ref_sound_data* owner) override;
	virtual bool has_playing_persistent() const override;
	virtual void set_heavy_load_active(bool active) override;

    // Called by CSoundRender_Emitter::set_persistent
    void anchor_persistent(CSoundRender_Emitter* E);
    void release_persistent(CSoundRender_Emitter* E);

	virtual void play(ref_sound& S, CObject* O, u32 flags = 0, float delay = 0.f);
	virtual void play_at_pos(ref_sound& S, CObject* O, const Fvector& pos, u32 flags = 0, float delay = 0.f);
	virtual void play_no_feedback(ref_sound& S, CObject* O, u32 flags = 0, float delay = 0.f, Fvector* pos = 0,
	                              float* vol = 0, float* freq = 0, Fvector2* range = 0);
	virtual void set_master_volume(float f) =0;
	virtual void set_geometry_env(IReader* I);
	virtual void set_geometry_som(IReader* I);
	virtual void set_geometry_occ(CDB::MODEL* M);
	virtual void set_handler(sound_event* E);

	virtual void update(const Fvector& P, const Fvector& D, const Fvector& N) override;
	void update_thread_start();
	void update_thread_stop();
	virtual void update_events();
	virtual void statistic(CSound_stats* dest, CSound_stats_ext* ext);

	// listener
	virtual void update_listener(const Fvector& P, const Fvector& D, const Fvector& N, float dt)=0;
	
	//  EFX listener
	virtual void set_listener(const CSoundRender_Environment& env)=0;
	virtual void get_listener(CSoundRender_Environment& env)=0;
	virtual void commit()=0;

#ifdef _EDITOR
	virtual SoundEnvironment_LIB*		get_env_library			()																{ return s_environment; }
	virtual void						refresh_env_library		();
	virtual void						set_user_env			(CSound_environment* E);
	virtual void						refresh_sources			();
    virtual void						set_environment			(u32 id, CSound_environment** dst_env);
    virtual void						set_environment_size	(CSound_environment* src_env, CSound_environment** dst_env);
#endif
public:
	CSoundRender_Source* i_create_source(LPCSTR name);
	void i_destroy_source(CSoundRender_Source* S);
	CSoundRender_Emitter* i_play(ref_sound* S, BOOL _loop, float delay);
	void i_start(CSoundRender_Emitter* E);
	void i_stop(CSoundRender_Emitter* E);
	void i_rewind(CSoundRender_Emitter* E);
	BOOL i_allow_play(CSoundRender_Emitter* E);
	virtual BOOL i_locked() { return bLocked; }
	virtual BOOL is_ready() { return bReady; }

	virtual void object_relcase(CObject* obj);
	void i_create_all_sources();

	virtual float get_occlusion_to(const Fvector& hear_pt, const Fvector& snd_pt, float dispersion = 0.2f);
	float get_occlusion(Fvector& P, float R, Fvector* occ) override;
	CSoundRender_Environment* get_environment(const Fvector& P);

	void env_load();
	void env_unload();
	void env_apply();
};

extern CSoundRender_Core* SoundRender;
