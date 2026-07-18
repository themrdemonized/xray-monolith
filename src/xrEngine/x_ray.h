#ifndef __X_RAY_H__
#define __X_RAY_H__

// refs
class ENGINE_API CGameFont;

enum ELoadSessionPhase
{
	LoadSessionTeardown,
	LoadSessionServerLua,
	LoadSessionNativeLevel,
	LoadSessionResourceWait,
	LoadSessionClientSpawn,
	LoadSessionPhaseCount
};

#include "../Include/xrRender/FactoryPtr.h"
#include "../Include/xrRender/ApplicationRender.h"

// definition
class ENGINE_API CApplication :
	public pureFrame,
	public IEventReceiver
{
	friend class dxApplicationRender;

	// levels
	struct sLevelInfo
	{
		char* folder;
		char* name;
	};

public:
	string2048 ls_header;
	string2048 ls_tip_number;
	string2048 ls_tip;
private:
	FactoryPtr<IApplicationRender> m_pRender;

	int max_load_stage;

	int load_stage;

	u32 ll_dwReference;

	struct SLoadSession
	{
		bool active;
		bool precache_started;
		bool reconnect_pending;
		string32 scenario;
		u32 started_at;
		u32 precache_started_at;
		u64 native_generation;
		u64 resource_generation;
		u64 client_event_hash;
		u32 client_spawn_count;
		u32 client_event_count;
		u32 precache_frames;
		u32 precache_level_calls;
		u32 precache_loadscreen_calls;
		u32 precache_present_calls;
		u64 precache_wall_ticks;
		u64 precache_frame_move_ticks;
		u64 precache_seq_render_ticks;
		u64 precache_end_ticks;
		u64 precache_present_ticks;
		u64 precache_secondary_wait_ticks;
		u64 precache_level_calculate_ticks;
		u64 precache_level_render_ticks;
		u64 precache_loadscreen_ticks;
		u32 phase_started_at[LoadSessionPhaseCount];
		u32 phase_elapsed[LoadSessionPhaseCount];
		bool phase_running[LoadSessionPhaseCount];
	} m_load_session;
private:
	EVENT eQuit;
	EVENT eStart;
	EVENT eStartLoad;
	EVENT eDisconnect;
	EVENT eConsole;
	EVENT eStartMPDemo;

	void Level_Append(LPCSTR lname);
public:
	CGameFont* pFontSystem;

	// Levels
	xr_vector<sLevelInfo> Levels;
	u32 Level_Current;
	void Level_Scan();
	int Level_ID(LPCSTR name, LPCSTR ver, bool bSet);
	void Level_Set(u32 ID);
	void LoadAllArchives();
	CInifile* GetArchiveHeader(LPCSTR name, LPCSTR ver);

	// Loading
	void LoadBegin();
	void LoadEnd();
	void LoadTitleInt(LPCSTR str1, LPCSTR str2, LPCSTR str3);
	void LoadStage();
	void LoadSwitch();
	void LoadDraw();
	void LoadSessionBegin(LPCSTR scenario);
	void LoadSessionContinue(LPCSTR scenario);
	void LoadSessionExpectReconnect();
	void LoadSessionStartEvent(LPCSTR scenario);
	void LoadSessionCancel(LPCSTR reason);
	void LoadSessionSetScenario(LPCSTR scenario);
	void LoadSessionPhaseBegin(ELoadSessionPhase phase);
	void LoadSessionPhaseEnd(ELoadSessionPhase phase);
	void LoadSessionPrecacheBegin();
	bool LoadSessionMeasurePrecache() const;
	void LoadSessionRecordPrecacheFrame(u64 wall_ticks, u64 frame_move_ticks, u64 seq_render_ticks,
		u64 end_ticks, u64 secondary_wait_ticks);
	void LoadSessionRecordPrecacheLevel(u64 calculate_ticks, u64 render_ticks);
	void LoadSessionRecordPrecacheLoadscreen(u64 ticks);
	void LoadSessionRecordPrecachePresent(u64 ticks);
	void LoadSessionRecordClientEvent(bool spawn, u16 destination, u16 type, const void* packet_data, u32 packet_size);
	void LoadSessionTryFinish(bool level_ready, bool control_ready, bool queues_drained);
	bool LoadSessionActive() const { return m_load_session.active; }
	bool LoadSessionPrecacheStarted() const { return m_load_session.precache_started; }

	virtual void OnEvent(EVENT E, u64 P1, u64 P2);

	// Other
	CApplication();
	virtual ~CApplication();

	virtual void _BCL OnFrame();
	void load_draw_internal();
	void destroy_loading_shaders();
};

extern ENGINE_API CApplication* pApp;
extern ENGINE_API void LogStartupMenuReady();

//Discord
struct rpc_info
{
	bool mainmenu;
	bool loadscreen;
	bool ingame;
	bool ex_update;
	bool ironman;
	bool godmode;
	int possessed_lives;
	int health;
	int lives_left;
	int level_icon_index;
	char task_name[128];
	char faction_name[128];
	char rank_name[128];
	char reputation[128];
	char level_name[128];
	char gamemode[128];
	LPCSTR currenttime;
	LPCSTR faction;
	LPCSTR level;
};

struct rpc_strings
{
	char loading[128];
	char mainmenu[128];
	char paused[128];
	char health[128];
	char livesleft[128];
	char dead[128];
	char livesleftsingle[128];
	char livespossessed[128];
	char livespossessedsingle[128];
	char godmode[128];
};

extern ENGINE_API void updateDiscordPresence();
extern ENGINE_API rpc_info discord_gameinfo;
extern ENGINE_API rpc_strings discord_strings;
extern ENGINE_API float discord_update_rate;

LPCSTR xr_ToUTF8(LPCSTR input, int max_length = 128);

void clearDiscordPresence();

#endif //__XR_BASE_H__
