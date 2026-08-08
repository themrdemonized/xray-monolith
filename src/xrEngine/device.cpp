#include "stdafx.h"
#include "../xrCDB/frustum.h"
#include "xr_ioconsole.h"
#include "xr_input.h"
#include "../xrCore/profiler.h"

#pragma warning(disable:4995)
// mmsystem.h
#define MMNOSOUND
#define MMNOMIDI
#define MMNOAUX
#define MMNOMIXER
#define MMNOJOY
#include <mmsystem.h>
// d3dx9.h
#include <d3dx9.h>
#pragma warning(default:4995)

#include "x_ray.h"
#include "discord\discord.h"
#include "render.h"
#include <chrono>

// must be defined before include of FS_impl.h
#define INCLUDE_FROM_ENGINE
#include "../xrCore/FS_impl.h"

#ifdef INGAME_EDITOR
# include "../include/editor/ide.hpp"
# include "engine_impl.hpp"
#endif // #ifdef INGAME_EDITOR

#include "xrSash.h"
#include "igame_persistent.h"

#pragma comment( lib, "d3dx9.lib" )

ENGINE_API CRenderDevice Device;
ENGINE_API CLoadScreenRenderer load_screen_renderer;


ENGINE_API BOOL g_bRendering = FALSE;

BOOL g_bLoaded = FALSE;
ref_light precache_light = 0;

BOOL psLua_ParallelGC = TRUE;
BOOL psLua_ParallelGC_debug = FALSE;
int psLua_ParallelGC_CallAmount = 25;

extern discord::Core* discord_core;
extern bool use_discord;

extern Fvector4 ps_ssfx_grass_interactive;

#ifdef ECO_RENDER
std::chrono::high_resolution_clock::time_point tlastf = std::chrono::high_resolution_clock::now(), tcurrentf = std::
	                                               chrono::high_resolution_clock::now();
std::chrono::duration<float> time_span;
ENGINE_API float refresh_rate = 0;
#endif // ECO_RENDER

BOOL CRenderDevice::Begin()
{
	PROF_EVENT();

#ifndef DEDICATED_SERVER
	switch (m_pRender->GetDeviceState())
	{
	case IRenderDeviceRender::dsOK:
		break;

	case IRenderDeviceRender::dsLost:
		// If the device was lost, do not render until we get it back
		Sleep(33);
		return FALSE;
		break;

	case IRenderDeviceRender::dsNeedReset:
		// Check if the device is ready to be reset
		Reset();
		break;

	default:
		R_ASSERT(0);
	}

	m_pRender->Begin();

	FPU::m24r();
	g_bRendering = TRUE;
#endif
	return TRUE;
}

void CRenderDevice::Clear()
{
	m_pRender->Clear();
}

extern void CheckPrivilegySlowdown();


void CRenderDevice::End(void)
{
	PROF_EVENT();

#ifndef DEDICATED_SERVER


#ifdef INGAME_EDITOR
    bool load_finished = false;
#endif // #ifdef INGAME_EDITOR
	if (dwPrecacheFrame)
	{
		::Sound->set_master_volume(0.f);
		dwPrecacheFrame--;

		if (!dwPrecacheFrame)
		{
#ifdef INGAME_EDITOR
            load_finished = true;
#endif // #ifdef INGAME_EDITOR

			m_pRender->updateGamma();

			if (precache_light)
			{
				precache_light->set_active(false);
				precache_light.destroy();
			}
			::Sound->set_master_volume(1.f);

			m_pRender->ResourcesDestroyNecessaryTextures();

			Msg("* [x-ray]: Handled Necessary Textures Destruction");
			Memory.mem_compact();
			Msg("* MEMORY USAGE: %lld K", Memory.mem_usage() / 1024);
			Msg("* End of synchronization A[%d] R[%d]", b_is_Active, b_is_Ready);

#ifdef FIND_CHUNK_BENCHMARK_ENABLE
            g_find_chunk_counter.flush();
#endif // FIND_CHUNK_BENCHMARK_ENABLE

			CheckPrivilegySlowdown();

			if (g_pGamePersistent->GameType() == 1) //haCk
			{
				WINDOWINFO wi;
				GetWindowInfo(m_hWnd, &wi);
				if (wi.dwWindowStatus != WS_ACTIVECAPTION)
					Pause(TRUE, TRUE, TRUE, "application start");
			}
		}
	}

	g_bRendering = FALSE;
	// end scene
	// Present goes here, so call OA Frame end.
	if (g_SASH.IsBenchmarkRunning())
		g_SASH.DisplayFrame(Device.fTimeGlobal);
	m_pRender->End();

# ifdef INGAME_EDITOR
    if (load_finished && m_editor)
        m_editor->on_load_finished();
# endif // #ifdef INGAME_EDITOR
#endif
}


volatile u32 mt_Thread_marker = 0x12345678;

void mt_Thread(void* ptr)
{
	auto& device = *static_cast<CRenderDevice*>(ptr);
	while (true)
	{
		PROF_EVENT();

		START_PROFILE("Wait for device");
		// waiting for Device permission to execute
		device.mt_csEnter.Enter();

		if (device.mt_bMustExit)
		{
			PROF_EVENT("Must exit");

			device.mt_bMustExit = FALSE; // Important!!!
			device.mt_csEnter.Leave(); // Important!!!
			return;
		}
		// we has granted permission to execute
		mt_Thread_marker = device.dwFrame;
		STOP_PROFILE;

		START_PROFILE("Process seqParallel");
		for (u32 pit = 0; pit < device.seqParallel.size(); pit++)
			device.seqParallel[pit]();
		device.seqParallel.clear_not_free();
		STOP_PROFILE;

		START_PROFILE("Process seqFrameMT");
		device.seqFrameMT.Process(rp_Frame);
		STOP_PROFILE;

		// demonized: While Renderer prepares frame and GPU renders it, use time opportunity to repeatedly call Lua GC with small step value
		// Reduces stutters since less work will be done in main GC step or no work at all
		{
			PROF_EVENT("seqLuaGC");
			if (psLua_ParallelGC && Device.LuaGC)
			{
				// Do at least once
				do
				{
					Device.LuaGCCount++;
					if (Device.LuaGC() == 1) // 1 informs that GC cycle is complete
					{
						Device.LuaGCDone = true;
						break;
					}

				} while (Device.isRendering && Device.LuaGCCount < psLua_ParallelGC_CallAmount);
			}
		}

		START_PROFILE("Synchronization");
		// now we give control to device - signals that we are ended our work
		device.mt_csEnter.Leave();
		// waits for device signal to continue - to start again
		device.mt_csLeave.Enter();
		// returns sync signal to device
		device.mt_csLeave.Leave();
		STOP_PROFILE;
	}
}

#include "igame_level.h"

void CRenderDevice::PreCache(u32 amount, bool b_draw_loadscreen, bool b_wait_user_input)
{
#ifdef DEDICATED_SERVER
    amount = 0;
#else
	if (m_pRender->GetForceGPU_REF())
		amount = 0;
#endif

	dwPrecacheFrame = dwPrecacheTotal = amount;
	if (amount && !precache_light && g_pGameLevel && g_loading_events.empty())
	{
		precache_light = ::Render->light_create();
		precache_light->set_shadow(false);
		precache_light->set_position(vCameraPosition);
		precache_light->set_color(255, 255, 255);
		precache_light->set_range(5.0f);
		precache_light->set_active(true);
	}

	if (amount && b_draw_loadscreen && !load_screen_renderer.b_registered)
	{
		load_screen_renderer.start(b_wait_user_input);
	}
}

int g_svDedicateServerUpdateReate = 100;

ENGINE_API xr_list<LOADING_EVENT> g_loading_events;

extern bool IsMainMenuActive(); //ECO_RENDER add

static HMONITOR g_StartupMonitor = NULL;

#include "MonitorList.h"

static void InitMonitor()
{
	if (g_StartupMonitor)
		return;

	HMONITOR chosen = ResolveSelectedMonitor();
	if (chosen)
	{
		MONITORINFO mi;
		mi.cbSize = sizeof(mi);
		if (GetMonitorInfoA(chosen, &mi))
		{
			g_StartupMonitor = chosen;
			return;
		}
		Msg("! vid_monitor: resolved handle is invalid, using Auto");
	}

	POINT cursorPos;
	GetCursorPos(&cursorPos);
	g_StartupMonitor = MonitorFromPoint(cursorPos, MONITOR_DEFAULTTOPRIMARY);
}

ENGINE_API void ResetStartupMonitor()
{
	g_StartupMonitor = NULL;
}

ENGINE_API void SetStartupMonitor(HMONITOR h)
{
	g_StartupMonitor = h;
}

ENGINE_API HMONITOR GetStartupMonitor()
{
	InitMonitor();
	return g_StartupMonitor;
}

void GetMonitorResolution(u32& horizontal, u32& vertical)
{
	InitMonitor();

	MONITORINFO mi;
	mi.cbSize = sizeof(mi);
	if (GetMonitorInfoA(g_StartupMonitor, &mi))
	{
		horizontal = mi.rcMonitor.right - mi.rcMonitor.left;
		vertical = mi.rcMonitor.bottom - mi.rcMonitor.top;
	}
	else
	{
		RECT desktop;
		const HWND hDesktop = GetDesktopWindow();
		GetWindowRect(hDesktop, &desktop);
		horizontal = desktop.right - desktop.left;
		vertical = desktop.bottom - desktop.top;
	}
}

void GetMonitorPosition(int& x, int& y)
{
	InitMonitor();

	MONITORINFO mi;
	mi.cbSize = sizeof(mi);
	if (GetMonitorInfoA(g_StartupMonitor, &mi))
	{
		x = mi.rcMonitor.left;
		y = mi.rcMonitor.top;
	}
	else
	{
		x = 0;
		y = 0;
	}
}

float GetMonitorRefresh()
{
	DEVMODE lpDevMode;
	memset(&lpDevMode, 0, sizeof(DEVMODE));
	lpDevMode.dmSize = sizeof(DEVMODE);
	lpDevMode.dmDriverExtra = 0;

	if (EnumDisplaySettings(NULL, ENUM_CURRENT_SETTINGS, &lpDevMode) == 0)
	{
		return 1.f / 60.f;
	}
	else
		return 1.f / lpDevMode.dmDisplayFrequency;
}

extern int ps_framelimiter;
extern u32 g_screenmode;

CTimer FreezeTimer;
void mt_FreezeThread(void *ptr) {
	float freezetime = 0.f;
	float repeatcheck = 500.f;

	while (true)
	{
		PROF_EVENT();

		if (g_loading_events.size())
			freezetime = 25000.0f;
		else
			freezetime = 5000.0f;

		repeatcheck = 500.f;

		START_PROFILE("Check timer");
		if (FreezeTimer.GetElapsed_sec()*1000.f > freezetime)
		{
			FlushLog();
			repeatcheck = 5000.f;
		}
		STOP_PROFILE;

		Sleep(repeatcheck);
	}
}

void CRenderDevice::on_idle()
{
	FreezeTimer.Start();

	if (!b_is_Ready)
	{
		Sleep(100);
		return;
	}

	PROF_FRAME("X-RAY Primary thread");
	PROF_EVENT();

#ifdef DEDICATED_SERVER
    u32 FrameStartTime = TimerGlobal.GetElapsed_ms();
#endif

	START_PROFILE("Set stat gathering");
	if (psDeviceFlags.test(rsStatistic))
		g_bEnableStatGather = TRUE;
	else g_bEnableStatGather = FALSE;
	STOP_PROFILE;

	if (g_loading_events.size())
	{
		PROF_EVENT("Pop loading event");
		if (g_loading_events.front()())
			g_loading_events.pop_front();
		pApp->LoadDraw();
		return;
	}

	if (!Device.dwPrecacheFrame && !g_SASH.IsBenchmarkRunning() && g_bLoaded)
	{
		PROF_EVENT("Start xrSASH Benchmark");
		g_SASH.StartBenchmark();
	}

	FrameMove();

	// Precache
	if (dwPrecacheFrame)
	{
		PROF_EVENT("Precache frame");
		float factor = float(dwPrecacheFrame) / float(dwPrecacheTotal);
		float angle = PI_MUL_2 * factor;
		vCameraDirection.set(_sin(angle), 0, _cos(angle));
		vCameraDirection.normalize();
		vCameraTop.set(0, 1, 0);
		vCameraRight.crossproduct(vCameraTop, vCameraDirection);

		mView.build_camera_dir(vCameraPosition, vCameraDirection, vCameraTop);
	}

	// Matrices
	START_PROFILE("Matrices");
	mFullTransform.mul(mProject, mView);
	mFullTransformHud.mul(mProjectHud, mView);
	m_pRender->SetCacheXform(mView, mProject);

	// Previous frame data -- 
	mView_prev = mView_saved;
	mProject_prev = mProject_saved;
	//mFullTransform_prev = mFullTransform_saved; // Unused?

	m_pRender->SetCacheXform_prev(mView_prev, mProject_prev);

	// Save previous frame grass benders data
	IGame_Persistent::grass_data& GData = g_pGamePersistent->grass_shader_data;

	GData.prev_pos[0].set(Device.vCameraPosition.x, Device.vCameraPosition.y, Device.vCameraPosition.z, -1);
	GData.prev_dir[0].set(0.0f, -99.0f, 0.0f, 1.0f);

	for (int pBend = 1; pBend < _min(16, ps_ssfx_grass_interactive.y + 1); pBend++)
	{
		GData.prev_pos[pBend].set(GData.pos[pBend].x, GData.pos[pBend].y, GData.pos[pBend].z, GData.radius_curr[pBend]);
		GData.prev_dir[pBend].set(GData.dir[pBend].x, GData.dir[pBend].y, GData.dir[pBend].z, GData.str[pBend]);
	}

	// Save wind animation position
	wind_anim_prev = wind_anim_saved;
	wind_anim_saved = g_pGamePersistent->Environment().wind_anim;

	//RCache.set_xform_view ( mView );
	//RCache.set_xform_project ( mProject );
	D3DXMatrixInverse((D3DXMATRIX*)&mInvFullTransform, 0, (D3DXMATRIX*)&mFullTransform);

	vCameraPosition_saved = vCameraPosition;
	mFullTransform_saved = mFullTransform;
	mView_saved = mView;
	mProject_saved = mProject;
	STOP_PROFILE;

	Device.isRendering = true;
	Device.LuaGCCount = 0;
	Device.LuaGCDone = false;

	// *** Resume threads
	// Capture end point - thread must run only ONE cycle
	// Release start point - allow thread to run
	START_PROFILE("Resume threads");
	mt_csLeave.Enter();
	mt_csEnter.Leave();
	STOP_PROFILE;

#ifdef ECO_RENDER // ECO_RENDER START
	if (Device.Paused() || IsMainMenuActive() || ps_framelimiter)
	{
		PROF_EVENT("Eco Render");

		if (refresh_rate == 0)
			refresh_rate = GetMonitorRefresh();

		float rr;

		if (ps_framelimiter)
			rr = 1.f / ps_framelimiter;
		else
			rr = refresh_rate;

		time_span = std::chrono::duration_cast<std::chrono::duration<float>>(tcurrentf - tlastf);
		while (time_span.count() < rr)
		{
			tcurrentf = std::chrono::high_resolution_clock::now();
			time_span = std::chrono::duration_cast<std::chrono::duration<float>>(tcurrentf - tlastf);
		}
		tlastf = std::chrono::high_resolution_clock::now();
	}
#endif // ECO_RENDER END

#ifndef DEDICATED_SERVER
	Statistic->RenderTOTAL_Real.FrameStart();
	Statistic->RenderTOTAL_Real.Begin();

	if (b_is_Active && Begin())
	{
		START_PROFILE("Process seqRender");
		seqRender.Process(rp_Render);
		STOP_PROFILE;

		if (psDeviceFlags.test(rsCameraPos) || psDeviceFlags.test(rsStatistic) || Statistic->errors.size())
		{
			PROF_EVENT("Draw statistics");
			Statistic->Show();
		}

		End();
	}
	Statistic->RenderTOTAL_Real.End();
	Statistic->RenderTOTAL_Real.FrameEnd();
	Statistic->RenderTOTAL.accum = Statistic->RenderTOTAL_Real.accum;
#endif // #ifndef DEDICATED_SERVER
	Device.isRendering = false;

	// *** Suspend threads
	// Capture startup point
	// Release end point - allow thread to wait for startup point
	START_PROFILE("Suspend threads");
	mt_csEnter.Enter();
	mt_csLeave.Leave();
	STOP_PROFILE;

	// Ensure, that second thread gets chance to execute anyway
	if (dwFrame != mt_Thread_marker)
	{
		PROF_EVENT("Execute second thread");
		for (u32 pit = 0; pit < Device.seqParallel.size(); pit++)
			Device.seqParallel[pit]();
		Device.seqParallel.clear_not_free();
		seqFrameMT.Process(rp_Frame);
	}

	if (psLua_ParallelGC_debug && psLua_ParallelGC && Device.LuaGCDebug)
	{
		Device.LuaGCDebug();
	}

#ifdef DEDICATED_SERVER
    u32 FrameEndTime = TimerGlobal.GetElapsed_ms();
    u32 FrameTime = (FrameEndTime - FrameStartTime);
    u32 DSUpdateDelta = 1000 / g_svDedicateServerUpdateReate;
    if (FrameTime < DSUpdateDelta)
        Sleep(DSUpdateDelta - FrameTime);
#endif
	if (!b_is_Active)
		Sleep(1);
}

#ifdef INGAME_EDITOR
void CRenderDevice::message_loop_editor()
{
    m_editor->run();
    m_editor_finalize(m_editor);
    xr_delete(m_engine);
}
#endif // #ifdef INGAME_EDITOR

void CRenderDevice::Screenshot()
{
	PROF_EVENT();
	Render->Screenshot();
}

void CRenderDevice::message_loop()
{
#ifdef INGAME_EDITOR
    if (editor())
    {
        message_loop_editor();
        return;
    }
#endif
	MSG msg;
	PeekMessage(&msg, NULL, 0U, 0U, PM_NOREMOVE);
	while (msg.message != WM_QUIT)
	{
		if (PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
			continue;
		}
		on_idle();
	}
}

void mt_DiscordThread(void*)
{
	while (true)
	{
		if (!pApp)
		{
			Msg("[Discord] pApp destroyed, killing thread");
			return;
		}

		//Discord
		if (use_discord && psDeviceFlags2.test(rsDiscord))
		{
			START_PROFILE("Discord");
			discord_core->RunCallbacks();
			updateDiscordPresence();
			STOP_PROFILE;
			Sleep(int(discord_update_rate * 1000));
		}
		else
		{
			Sleep(1000); // Sleep for 1 second if Discord is not used or disabled
		}
	}
}

void CRenderDevice::Run()
{
	// DUMP_PHASE;
	g_bLoaded = FALSE;
	Log("Starting engine...");
	thread_name("X-RAY Primary thread");
	// Startup timers and calculate timer delta
	dwTimeGlobal = 0;
	Timer_MM_Delta = 0;
	{
		u32 time_mm = timeGetTime();
		while (timeGetTime() == time_mm); // wait for next tick
		u32 time_system = timeGetTime();
		u32 time_local = TimerAsync();
		Timer_MM_Delta = time_system - time_local;
	}
	// Start all threads
	// InitializeCriticalSection (&mt_csEnter);
	// InitializeCriticalSection (&mt_csLeave);
	mt_csEnter.Enter();
	mt_bMustExit = FALSE;
	thread_spawn(mt_FreezeThread, "Freeze detecting thread", 0, 0);
	thread_spawn(mt_Thread, "X-RAY Secondary thread", 0, this);
	thread_spawn(mt_DiscordThread, "X-RAY Discord thread", 0, 0);
	// Message cycle
	seqAppStart.Process(rp_AppStart);
	m_pRender->ClearTarget();
	SetForegroundWindow(m_hWnd);
	message_loop();
	seqAppEnd.Process(rp_AppEnd);
	// Stop Balance-Thread
	mt_bMustExit = TRUE;
	mt_csEnter.Leave();
	while (mt_bMustExit) Sleep(0);
	// DeleteCriticalSection (&mt_csEnter);
	// DeleteCriticalSection (&mt_csLeave);
}

u32 app_inactive_time = 0;
u32 app_inactive_time_start = 0;

void CRenderDevice::FrameMove()
{
	PROF_EVENT();

	if (InterlockedExchange(&g_monitor_list_dirty, 0))
		refresh_vid_monitor_list();

	dwFrame++;
	Core.dwFrame = dwFrame;
	dwTimeContinual = TimerMM.GetElapsed_ms() - app_inactive_time;
	if (psDeviceFlags.test(rsConstantFPS))
	{
		PROF_EVENT("Constant FPS");

		// 20ms = 50fps
		//fTimeDelta = 0.020f;
		//fTimeGlobal += 0.020f;
		//dwTimeDelta = 20;
		//dwTimeGlobal += 20;
		// 33ms = 30fps
		fTimeDelta = 0.033f;
		fTimeGlobal += 0.033f;
		dwTimeDelta = 33;
		dwTimeGlobal += 33;
	}
	else
	{
		PROF_EVENT("Timer FPS");

		// Timer
		float fPreviousFrameTime = Timer.GetElapsed_sec();
		Timer.Start(); // previous frame
		fTimeDelta = 0.1f * fTimeDelta + 0.9f * fPreviousFrameTime;
		// smooth random system activity - worst case ~7% error
		//fTimeDelta = 0.7f * fTimeDelta + 0.3f*fPreviousFrameTime; // smooth random system activity
		if (fTimeDelta > .1f)
			fTimeDelta = .1f; // limit to 15fps minimum
		if (fTimeDelta <= 0.f)
			fTimeDelta = EPS_S + EPS_S; // limit to 15fps minimum
		if (Paused())
			fTimeDelta = 0.0f;
		// u64 qTime = TimerGlobal.GetElapsed_clk();
		fTimeGlobal = TimerGlobal.GetElapsed_sec(); //float(qTime)*CPU::cycles2seconds;
		u32 _old_global = dwTimeGlobal;
		dwTimeGlobal = TimerGlobal.GetElapsed_ms();
		dwTimeDelta = dwTimeGlobal - _old_global;
	}
	// Frame move
	Statistic->EngineTOTAL.Begin();
	// TODO: HACK to test loading screen.
	//if(!g_bLoaded)
	START_PROFILE("Process seqFrame");
	Device.seqFrame.Process(rp_Frame);
	STOP_PROFILE;
	g_bLoaded = TRUE;
	//else
	// seqFrame.Process(rp_Frame);
	Statistic->EngineTOTAL.End();
}

ENGINE_API BOOL bShowPauseString = TRUE;
#include "IGame_Persistent.h"

void CRenderDevice::Pause(BOOL bOn, BOOL bTimer, BOOL bSound, LPCSTR reason)
{
	PROF_EVENT();

	static int snd_emitters_ = -1;

	if (g_bBenchmark)
		return;
#ifndef DEDICATED_SERVER
	if (bOn)
	{
		if (!Paused())
			bShowPauseString =
#ifdef INGAME_EDITOR
                editor() ? FALSE :
#endif // #ifdef INGAME_EDITOR
#ifdef DEBUG
                !xr_strcmp(reason, "li_pause_key_no_clip") ? FALSE :
#endif // DEBUG
				TRUE;

		if (bTimer && (!g_pGamePersistent || g_pGamePersistent->CanBePaused()))
		{
			g_pauseMngr().Pause(true);
#ifdef DEBUG
            if (!xr_strcmp(reason, "li_pause_key_no_clip"))
                TimerGlobal.Pause(FALSE);
#endif // DEBUG
		}

		if (bSound && ::Sound)
		{
			snd_emitters_ = ::Sound->pause_emitters(true);
#ifdef DEBUG
			// Log("snd_emitters_[true]",snd_emitters_);
#endif // DEBUG
		}
	}
	else
	{
		if (bTimer && g_pauseMngr().Paused())
		{
			fTimeDelta = EPS_S + EPS_S;
			g_pauseMngr().Pause(false);
		}

		if (bSound)
		{
			if (snd_emitters_ > 0) //avoid crash
			{
				snd_emitters_ = ::Sound->pause_emitters(false);
#ifdef DEBUG
				// Log("snd_emitters_[false]",snd_emitters_);
#endif
			}
			else
			{
#ifdef DEBUG
                Log("Sound->pause_emitters underflow");
#endif
			}
		}
	}

#endif
}

bool CRenderDevice::Paused()
{
	return g_pauseMngr().Paused();
}

void CRenderDevice::OnWM_Activate(WPARAM wParam, LPARAM lParam)
{
	u16 fActive = LOWORD(wParam);
	BOOL fMinimized = (BOOL)HIWORD(wParam);
	BOOL bActive = ((fActive != WA_INACTIVE) && (!fMinimized)) ? TRUE : FALSE;

	if (psDeviceFlags2.test(rsAlwaysActive) && g_screenmode != 2)
	{
		Device.b_is_Active = TRUE;

		if (Device.b_hide_cursor != bActive)
		{
			Device.b_hide_cursor = bActive;

			if (Device.b_hide_cursor)
			{
				ShowCursor(FALSE);
				if (m_hWnd)
				{
					RECT winRect;
					GetClientRect(m_hWnd, &winRect);
					MapWindowPoints(m_hWnd, nullptr, reinterpret_cast<LPPOINT>(&winRect), 2);
					ClipCursor(&winRect);
				}
				pInput->OnAppActivate();
			}
			else
			{
				ShowCursor(TRUE);
				ClipCursor(NULL);
				pInput->OnAppDeactivate();
			}
		}

		return;
	}

	if (bActive != Device.b_is_Active)
	{
		Device.b_is_Active = bActive;

		if (Device.b_is_Active)
		{
			Device.seqAppActivate.Process(rp_AppActivate);
			app_inactive_time += TimerMM.GetElapsed_ms() - app_inactive_time_start;

#ifndef DEDICATED_SERVER
# ifdef INGAME_EDITOR
            if (!editor())
# endif // #ifdef INGAME_EDITOR
			ShowCursor(FALSE);
			if (m_hWnd)
			{
				RECT winRect;
				GetClientRect(m_hWnd, &winRect);
				MapWindowPoints(m_hWnd, nullptr, reinterpret_cast<LPPOINT>(&winRect), 2);
				ClipCursor(&winRect);
			}
#endif // #ifndef DEDICATED_SERVER
		}
		else
		{
			app_inactive_time_start = TimerMM.GetElapsed_ms();
			Device.seqAppDeactivate.Process(rp_AppDeactivate);
			ShowCursor(TRUE);
			ClipCursor(NULL);
		}
	}
}

void CRenderDevice::AddSeqFrame(pureFrame* f, bool mt)
{
	PROF_EVENT();

	if (mt)
		seqFrameMT.Add(f, REG_PRIORITY_HIGH);
	else
		seqFrame.Add(f, REG_PRIORITY_LOW);
}

void CRenderDevice::RemoveSeqFrame(pureFrame* f)
{
	PROF_EVENT();

	seqFrameMT.Remove(f);
	seqFrame.Remove(f);
}

CLoadScreenRenderer::CLoadScreenRenderer()
	: b_registered(false)
{
}

void CLoadScreenRenderer::start(bool b_user_input)
{
	PROF_EVENT();

	Device.seqRender.Add(this, 0);
	b_registered = true;
	b_need_user_input = b_user_input;
}

void CLoadScreenRenderer::stop()
{
	PROF_EVENT();

	if (!b_registered)
		return;
	Device.seqRender.Remove(this);
	pApp->destroy_loading_shaders();
	b_registered = false;
	b_need_user_input = false;
}

void CLoadScreenRenderer::OnRender()
{
	PROF_EVENT();

	pApp->load_draw_internal();
}

void CRenderDevice::CSecondVPParams::SetSVPActive(bool bState) //--#SM+#-- +SecondVP+
{
	isActive = bState;
	if (g_pGamePersistent != NULL)
		g_pGamePersistent->m_pGShaderConstants->m_blender_mode.z = (isActive ? 1.0f : 0.0f);
}

bool CRenderDevice::CSecondVPParams::IsSVPFrame() //--#SM+#-- +SecondVP+
{
	return IsSVPActive() && Device.dwFrame % frameDelay == 0;
}
