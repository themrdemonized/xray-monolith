// EngineAPI.cpp: implementation of the CEngineAPI class.
//
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "EngineAPI.h"
#include "../xrcdb/xrXRC.h"

//#include "securom_api.h"

//#define STATIC_RENDERER_R1
//#define STATIC_RENDERER_R2
//#define STATIC_RENDERER_R3
//#define STATIC_RENDERER_R4

extern xr_token* vid_quality_token;

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

void __cdecl dummy(void)
{
};

// xrSound
// libogg_static.lib;libvorbis_static.lib;libvorbisfile_static.lib;OpenAL32.lib
// xrPhysics
// libvorbisfile_static.lib;libogg_static.lib;OpenAL32.lib
// xrNetServer
// Ws2_32.lib;dxerr.lib
// xrEngine
// vfw32.lib;libogg_static.lib;libtheora_static.lib
// xrRenderR4
// dxguid.lib;d3dx11.lib;D3DCompiler.lib;d3d11.lib;dxgi.lib;nvapi.lib;dxerr.lib;d3d10.lib
// xrRenderR3
// dxguid.lib;d3dcompiler.lib;d3d10.lib;d3dx10.lib;dxgi.lib;nvapi.lib
// xrRenderR2
// nvapi.lib
// xrRenderR1
// d3dx9.lib;nvapi.lib
// OpenAL32
// version.lib;winmm.lib

#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "vfw32.lib")
#pragma comment(lib, "nvapi.lib")

// Only Vulkan renderer is supported (R1/R2/R3/R4 have been removed)
#ifndef STATIC_RENDERER_VULKAN
	#error STATIC_RENDERER_VULKAN must be defined
#endif

#pragma comment(lib, "xrRender_Vulkan.lib")
#pragma comment(lib, "vulkan-1.lib")

CEngineAPI::CEngineAPI()
{
	//hGame = 0;
	//hRender = 0;
	hTuner = 0;
	pCreate = 0;
	pDestroy = 0;
	tune_pause = dummy;
	tune_resume = dummy;
}

CEngineAPI::~CEngineAPI()
{
	// destroy quality token here
	if (vid_quality_token)
	{
		xr_free(vid_quality_token);
		vid_quality_token = NULL;
	}
}

extern u32 renderer_value; //con cmd
ENGINE_API int g_current_renderer = 0;

ENGINE_API bool is_enough_address_space_available()
{
	return true; // we are on 64 bit, so it's always true
	//SYSTEM_INFO system_info;
	//    GetSystemInfo(&system_info);
	//    return (*(u32*)&system_info.lpMaximumApplicationAddress) > 0x90000000;
}

#ifndef DEDICATED_SERVER

extern BOOL DllMainXrRenderR1(HANDLE hModule, DWORD ul_reason_for_call, LPVOID lpReserved);
extern BOOL DllMainXrRenderR2(HANDLE hModule, DWORD ul_reason_for_call, LPVOID lpReserved);
extern BOOL DllMainXrRenderR3(HANDLE hModule, DWORD ul_reason_for_call, LPVOID lpReserved);
extern BOOL DllMainXrRenderR4(HANDLE hModule, DWORD ul_reason_for_call, LPVOID lpReserved);
extern BOOL DllMainXrRenderVulkan(HANDLE hModule, DWORD ul_reason_for_call, LPVOID lpReserved);

// Only Vulkan renderer supported
#define DLL_MAIN_RENDERER DllMainXrRenderVulkan

void CEngineAPI::InitializeNotDedicated()
{
	// Initialize Vulkan renderer (R1/R2/R3/R4 removed)
	LPCSTR vulkan_name = "xrRender_Vulkan.dll";

	psDeviceFlags.set(rsR2, FALSE);
	psDeviceFlags.set(rsR3, FALSE);
	psDeviceFlags.set(rsR4, FALSE);

	Log("Loading Renderer:", vulkan_name);
	DllMainXrRenderVulkan(NULL, DLL_PROCESS_ATTACH, NULL);
	g_current_renderer = 4; // Vulkan = 4
}
#endif // DEDICATED_SERVER

extern BOOL DllMainXrGame(HANDLE hModule, u32 ul_reason_for_call, LPVOID lpReserved);

extern "C"
DLL_Pure* __cdecl xrFactory_Create(CLASS_ID clsid);
extern "C"
void __cdecl xrFactory_Destroy(DLL_Pure* O);


void CEngineAPI::Initialize(void)
{
	//////////////////////////////////////////////////////////////////////////
	// Initialize renderer (Vulkan only)
#ifndef DEDICATED_SERVER
	InitializeNotDedicated();
#endif // DEDICATED_SERVER

	Device.ConnectToRender();

	// game
	{
		LPCSTR g_name = "xrGame.dll";
		Log("Loading DLL:", g_name);
		//hGame = LoadLibrary(g_name);
		DllMainXrGame(NULL, DLL_PROCESS_ATTACH, NULL);
		//if (0 == hGame) R_CHK(GetLastError());
		//R_ASSERT2(hGame, "Game DLL raised exception during loading or there is no game DLL at all");
		//pCreate = (Factory_Create*)GetProcAddress(hGame, "xrFactory_Create");
		pCreate = xrFactory_Create;
		R_ASSERT(pCreate);
		//pDestroy = (Factory_Destroy*)GetProcAddress(hGame, "xrFactory_Destroy");
		pDestroy = xrFactory_Destroy;
		R_ASSERT(pDestroy);
	}

	//////////////////////////////////////////////////////////////////////////
	// vTune
	tune_enabled = FALSE;
	if (strstr(Core.Params, "-tune"))
	{
		LPCSTR g_name = "vTuneAPI.dll";
		Log("Loading DLL:", g_name);
		hTuner = LoadLibrary(g_name);
		if (0 == hTuner)
			R_CHK(GetLastError());
		R_ASSERT2(hTuner, "Intel vTune is not installed");
		tune_enabled = TRUE;
		tune_pause = (VTPause*)GetProcAddress(hTuner, "VTPause");
		R_ASSERT(tune_pause);
		tune_resume = (VTResume*)GetProcAddress(hTuner, "VTResume");
		R_ASSERT(tune_resume);
	}
}

void CEngineAPI::Destroy(void)
{
	//if (hGame) { FreeLibrary(hGame); hGame = 0; }
	DllMainXrGame(NULL, DLL_PROCESS_DETACH, NULL);
	//if (hRender) { FreeLibrary(hRender); hRender = 0; }
	DLL_MAIN_RENDERER(NULL, DLL_PROCESS_DETACH, NULL);
	pCreate = 0;
	pDestroy = 0;
	Engine.Event._destroy();
	XRC.r_clear_compact();
}

// DirectX renderer support functions removed (R1/R2/R3/R4 removed)

void CEngineAPI::CreateRendererList()
{
#ifdef DEDICATED_SERVER

    vid_quality_token = xr_alloc<xr_token>(2);

    vid_quality_token[0].id = 0;
    vid_quality_token[0].name = xr_strdup("renderer_r1");

    vid_quality_token[1].id = -1;
    vid_quality_token[1].name = NULL;

#else
	// Only Vulkan renderer is supported (R1/R2/R3/R4 removed)
	if (vid_quality_token != NULL) return;

	xr_vector<LPCSTR> _tmp;
	_tmp.push_back("renderer_vk");

	R_ASSERT2(_tmp.size() != 0, "No valid renderer found, please use a render system that's supported by your PC");

	u32 _cnt = _tmp.size() + 1;
	vid_quality_token = xr_alloc<xr_token>(_cnt);

	vid_quality_token[_cnt - 1].id = -1;
	vid_quality_token[_cnt - 1].name = NULL;

	//#ifdef DEBUG
	Msg("Available render modes[%d]:", _tmp.size());
	//#endif // DEBUG
	for (u32 i = 0; i < _tmp.size(); ++i)
	{
		vid_quality_token[i].id = i;
		vid_quality_token[i].name = _tmp[i];
		//#ifdef DEBUG
		Msg("[%s]", _tmp[i]);
		//#endif // DEBUG
	}
#endif //#ifndef DEDICATED_SERVER
}
