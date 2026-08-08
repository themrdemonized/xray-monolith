#include "stdafx.h"

#include "../Include/xrRender/DrawUtils.h"
#include "render.h"
#include "IGame_Persistent.h"
#include "xr_IOConsole.h"
#include "MonitorList.h"

void CRenderDevice::_Destroy(BOOL bKeepTextures)
{
	DU->OnDeviceDestroy();

	// before destroy
	b_is_Ready = FALSE;
	Statistic->OnDeviceDestroy();
	m_imgui.OnDeviceDestroy();
	::Render->destroy();
	m_pRender->OnDeviceDestroy(bKeepTextures);
	//Resources->OnDeviceDestroy (bKeepTextures);
	//RCache.OnDeviceDestroy ();

	Memory.mem_compact();
}

void CRenderDevice::Destroy(void)
{
	if (!b_is_Ready) return;

	Log("Destroying Direct3D...");

	ShowCursor(TRUE);
	ClipCursor(NULL);
	m_pRender->ValidateHW();

	_Destroy(FALSE);

	// real destroy
	m_pRender->DestroyHW();

	//xr_delete (Resources);
	//HW.DestroyDevice ();

	seqRender.R.clear();
	seqAppActivate.R.clear();
	seqAppDeactivate.R.clear();
	seqAppStart.R.clear();
	seqAppEnd.R.clear();
	seqFrame.R.clear();
	seqFrameMT.R.clear();
	seqDeviceReset.R.clear();
	seqParallel.clear();

	RenderFactory->DestroyRenderDeviceRender(m_pRender);
	m_pRender = 0;
	xr_delete(Statistic);
}

#include "IGame_Level.h"
#include "CustomHUD.h"
extern bool use_reshade;
extern bool init_reshade();
extern void unregister_reshade();
extern u32 g_screenmode;
extern void GetMonitorResolution(u32& horizontal, u32& vertical);
extern void GetMonitorPosition(int& x, int& y);
extern ENGINE_API u32 psCurrentVidMode[];

void CRenderDevice::Reset(bool precache)
{
	if (use_reshade)
		unregister_reshade();

	use_reshade = false;

	m_imgui.OnDeviceResetBegin();

	u32 dwWidth_before = dwWidth;
	u32 dwHeight_before = dwHeight;

	ShowCursor(TRUE);
	u32 tm_start = TimerAsync();

	m_pRender->Reset(m_hWnd, dwWidth, dwHeight, fWidth_2, fHeight_2);

	if (g_pGamePersistent)
		g_pGamePersistent->Environment().bNeed_re_create_env = TRUE;

	_SetupStates();
	if (precache)
		PreCache(20, true, false);
	u32 tm_end = TimerAsync();
	Msg("*** RESET [%d ms]", tm_end - tm_start);

	// TODO: Remove this! It may hide crash
	Memory.mem_compact();

	seqDeviceReset.Process(rp_DeviceReset);

	if (dwWidth_before != dwWidth || dwHeight_before != dwHeight)
	{
		seqResolutionChanged.Process(rp_ScreenResolutionChanged);
	}

	if (g_screenmode == 1)
	{
		u32 w, h;
		int monX, monY;
		GetMonitorResolution(w, h);
		GetMonitorPosition(monX, monY);
		SetWindowLongPtr(Device.m_hWnd, GWL_STYLE, WS_VISIBLE | WS_POPUP);
		SetWindowPos(Device.m_hWnd, HWND_TOP, monX, monY, w, h, SWP_FRAMECHANGED);
	}

#ifndef DEDICATED_SERVER
	ShowCursor(FALSE);
	RECT winRect;
	GetClientRect(m_hWnd, &winRect);
	clientWidth = winRect.right;
	clientHeight = winRect.bottom;
	MapWindowPoints(m_hWnd, nullptr, reinterpret_cast<LPPOINT>(&winRect), 2);
	ClipCursor(&winRect);
#endif

	m_imgui.OnDeviceResetEnd();
	use_reshade = init_reshade();
}

bool CRenderDevice::ChangeOutputMonitor(HMONITOR hTargetMon)
{
	static bool s_swap_in_progress = false;
	if (!b_is_Ready)
		return false;
	if (s_swap_in_progress)
		return false;
	if (IsIconic(m_hWnd))
	{
		Msg("* vid_monitor: window is minimised, deferring monitor switch to restart");
		return false;
	}

	if (hTargetMon == GetStartupMonitor())
	{
		Msg("* vid_monitor: already on target monitor, no-op");
		return true;
	}

	struct SwapGuard
	{
		bool& flag;
		SwapGuard(bool& f) : flag(f) { flag = true; }
		~SwapGuard() { flag = false; }
	} guard(s_swap_in_progress);

	if (use_reshade)
		unregister_reshade();
	use_reshade = false;

	m_imgui.OnDeviceResetBegin();
	ShowCursor(TRUE);

	u32 dwWidth_before  = dwWidth;
	u32 dwHeight_before = dwHeight;

	bool switched = m_pRender->SwitchOutputMonitor(hTargetMon, m_hWnd,
	                                               g_screenmode,
	                                               psCurrentVidMode[0], psCurrentVidMode[1]);

	if (!switched)
	{
		m_imgui.OnDeviceResetEnd();
		ShowCursor(FALSE);
		use_reshade = init_reshade();
		return false;
	}

	m_pRender->Reset(m_hWnd, dwWidth, dwHeight, fWidth_2, fHeight_2);

	if (g_pGamePersistent)
		g_pGamePersistent->Environment().bNeed_re_create_env = TRUE;

	_SetupStates();
	PreCache(20, true, false);

	seqDeviceReset.Process(rp_DeviceReset);

	if (dwWidth_before != dwWidth || dwHeight_before != dwHeight)
		seqResolutionChanged.Process(rp_ScreenResolutionChanged);

#ifndef DEDICATED_SERVER
	ShowCursor(FALSE);
	RECT winRect;
	GetClientRect(m_hWnd, &winRect);
	clientWidth  = winRect.right;
	clientHeight = winRect.bottom;
	MapWindowPoints(m_hWnd, nullptr, reinterpret_cast<LPPOINT>(&winRect), 2);
	ClipCursor(&winRect);
#endif

	m_imgui.OnDeviceResetEnd();
	use_reshade = init_reshade();

	Msg("* vid_monitor: live switch succeeded");
	return true;
}
