// xrRender_R2.cpp : Defines the entry point for the DLL application.
//
#include "stdafx.h"
#include "../xrRender/dxRenderFactory.h"
#include "../xrRender/dxUIRender.h"
#include "../xrRender/dxDebugRender.h"

//#pragma comment(lib,"xrEngine.lib")

//BOOL APIENTRY DllMain( HANDLE hModule,
BOOL DllMainXrRenderR2(HANDLE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
	switch (ul_reason_for_call)
	{
	case DLL_PROCESS_ATTACH:
		::Render = &RImplementation;
		::RenderFactory = &RenderFactoryImpl;
		::DU = &DUImpl;
		//::vid_mode_token			= inited by HW;
		UIRender = &UIRenderImpl;
		DRender	= &DebugRenderImpl;
		xrRender_initconsole();
		break ;
	case DLL_THREAD_ATTACH:
	case DLL_THREAD_DETACH:
	case DLL_PROCESS_DETACH:
		break;
	}
	return TRUE;
}

extern "C" {
bool /*_declspec(dllexport)*/ SupportsAdvancedRendering();
};

bool /*_declspec(dllexport)*/ SupportsAdvancedRendering()
{
	// Save DPI awareness context — Direct3DCreate9() can change the thread's
	// DPI context, which later causes SetWindowPos to use the wrong scale on
	// secondary monitors with different DPI.
	typedef HANDLE(WINAPI* pfnSetThreadDpiAwarenessContext)(HANDLE);
	pfnSetThreadDpiAwarenessContext fnSetCtx = NULL;
	HANDLE prevDpiContext = NULL;
	HMODULE user32 = GetModuleHandleA("user32.dll");
	if (user32)
	{
		fnSetCtx = (pfnSetThreadDpiAwarenessContext)GetProcAddress(user32, "SetThreadDpiAwarenessContext");
		if (fnSetCtx)
			prevDpiContext = fnSetCtx(/*DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2*/ (HANDLE)-4);
	}

	D3DCAPS9 caps;
	CHW _HW;
	_HW.CreateD3D();
	_HW.pD3D->GetDeviceCaps(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, &caps);
	_HW.DestroyD3D();

	// Restore DPI awareness context
	if (fnSetCtx && prevDpiContext)
		fnSetCtx(prevDpiContext);

	u16 ps_ver_major = u16(u32(u32(caps.PixelShaderVersion) & u32(0xf << 8ul)) >> 8);

	if (ps_ver_major < 3)
		return false;
	else
		return true;
}
