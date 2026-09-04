#include "stdafx.h"
#pragma hdrstop

#include "renderdoc_integration.h"
#include "renderdoc_app.h"

namespace
{
	const char* const rdc_capture_folder = "renderdoc\\captures\\";

	RENDERDOC_API_1_7_0* s_rdc_api = nullptr;
	RENDERDOC_Version s_rdc_version = eRENDERDOC_API_Version_1_0_0;
	bool s_rdc_initialized = false;
	u32 s_rdc_seen_captures = 0;
	string_path s_rdc_capture_template = {};
	xr_string s_rdc_latest_capture;

	HMODULE rdc_acquire_module()
	{
		HMODULE module = GetModuleHandleW(L"renderdoc.dll");
		if (module || !Core.ParamsData.test(ECoreParams::renderdoc))
			return module;

		wchar_t path[MAX_PATH * 4] = {};
		const DWORD length = GetModuleFileNameW(nullptr, path, DWORD(std::size(path)));
		wchar_t* const leaf = (length && length < std::size(path)) ? wcsrchr(path, L'\\') : nullptr;
		if (!leaf || wcscpy_s(leaf + 1, std::size(path) - size_t(leaf + 1 - path), L"renderdoc.dll"))
		{
			Msg("! [RDC] executable path unavailable, capture disabled");
			return nullptr;
		}

		module = LoadLibraryExW(path, nullptr,
			LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
		if (!module)
			Msg("~ [RDC] renderdoc.dll not loaded from the game bin folder (%lu), capture disabled",
				GetLastError());
		return module;
	}

	// GetAPI answers 1 only for a version it implements, so walk down until one is accepted
	bool rdc_negotiate_api(pRENDERDOC_GetAPI get_api)
	{
		static const RENDERDOC_Version versions[] = {
			eRENDERDOC_API_Version_1_7_0,
			eRENDERDOC_API_Version_1_6_0,
			eRENDERDOC_API_Version_1_5_0,
			eRENDERDOC_API_Version_1_4_0,
			eRENDERDOC_API_Version_1_1_0};

		for (const RENDERDOC_Version version : versions)
		{
			void* api = nullptr;
			if (get_api(version, &api) == 1 && api)
			{
				s_rdc_api = static_cast<RENDERDOC_API_1_7_0*>(api);
				s_rdc_version = version;
				return true;
			}
		}
		return false;
	}

	bool rdc_available()
	{
		if (s_rdc_api)
			return true;

		Msg("~ [RDC] renderdoc.dll is not loaded, inject with RenderDoc or launch with -renderdoc");
		return false;
	}

	bool rdc_ui_connected()
	{
		return s_rdc_api && s_rdc_api->IsTargetControlConnected() == 1;
	}

	void rdc_apply_capture_options()
	{
		struct option_key
		{
			ECoreParams param;
			RENDERDOC_CaptureOption option;
			const char* name;
		};
		static const option_key keys[] = {
			{ECoreParams::rdoc_refall, eRENDERDOC_Option_RefAllResources, "reference all resources"},
			{ECoreParams::rdoc_cmdlists, eRENDERDOC_Option_CaptureAllCmdLists, "capture all command lists"}};

		for (const option_key& key : keys)
		{
			if (!Core.ParamsData.test(key.param))
				continue;

			s_rdc_api->SetCaptureOptionU32(key.option, 1);
			Msg("* [RDC] option %s now %u", key.name, s_rdc_api->GetCaptureOptionU32(key.option));
		}
	}

	bool rdc_ensure_directory(const char* path)
	{
		VerifyPath(path);

		const DWORD attributes = GetFileAttributesA(path);
		return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
	}

	// RenderDoc appends _frameN.rdc to the template and takes a re-apply at any time
	void rdc_prepare_capture_path()
	{
		string_path executable = {};
		if (!GetModuleFileNameA(nullptr, executable, sizeof(executable)))
			return;

		char* const leaf = strrchr(executable, '\\');
		if (!leaf)
			return;
		*leaf = 0;

		char* const extension = strrchr(leaf + 1, '.');
		if (extension)
			*extension = 0;

		string_path directory = {};
		const bool data_root = FS.path_exist("$app_data_root$");
		if (data_root)
			FS.update_path(directory, "$app_data_root$", rdc_capture_folder);

		if (!data_root || !rdc_ensure_directory(directory))
		{
			xr_sprintf(directory, "%s\\%s", executable, rdc_capture_folder);
			rdc_ensure_directory(directory);
			Msg("~ [RDC] engine data root unusable, captures land in %s", directory);
		}

		xr_sprintf(s_rdc_capture_template, "%s%s", directory, leaf + 1);
		s_rdc_api->SetCaptureFilePathTemplate(s_rdc_capture_template);
	}

	void rdc_log_capture(u32 index)
	{
		u32 length = 0;
		u64 timestamp = 0;
		if (!s_rdc_api->GetCapture(index, nullptr, &length, &timestamp) || !length)
			return;

		xr_vector<char> path(size_t(length) + 1, 0);
		if (!s_rdc_api->GetCapture(index, path.data(), &length, &timestamp))
			return;

		// GetCapture hands back the absolute path already
		s_rdc_latest_capture = path.data();
		Msg("* [RDC] capture written %s  timestamp %llu", path.data(), timestamp);
	}
}

void renderdoc_initialize()
{
	if (s_rdc_initialized)
		return;
	s_rdc_initialized = true;

	const HMODULE module = rdc_acquire_module();
	if (!module)
		return;

	const pRENDERDOC_GetAPI get_api =
		reinterpret_cast<pRENDERDOC_GetAPI>(GetProcAddress(module, "RENDERDOC_GetAPI"));
	if (!get_api || !rdc_negotiate_api(get_api))
	{
		Msg("! [RDC] API negotiation failed, capture disabled");
		return;
	}

	// The engine keeps its own crash handler and minidump pipeline
	s_rdc_api->UnloadCrashHandler();

	rdc_apply_capture_options();
	rdc_prepare_capture_path();
	s_rdc_seen_captures = s_rdc_api->GetNumCaptures();

	int major = 0;
	int minor = 0;
	int patch = 0;
	s_rdc_api->GetAPIVersion(&major, &minor, &patch);
	Msg("* [RDC] capture API %d.%d.%d ready, ui %s", major, minor, patch,
		rdc_ui_connected() ? "connected" : "not connected");
	Msg("* [RDC] captures land in %s_frameN.rdc", s_rdc_capture_template);
}

bool renderdoc_api_live()
{
	return s_rdc_api != nullptr;
}

void renderdoc_trigger_capture(u32 frames)
{
	if (!rdc_available())
		return;

	if (frames > 1)
		s_rdc_api->TriggerMultiFrameCapture(frames);
	else
		s_rdc_api->TriggerCapture();

	Msg("* [RDC] queued %u frame(s), each one writes its own rdc file, ui %s", frames,
		rdc_ui_connected() ? "connected" : "not connected");
}

void renderdoc_poll_captures()
{
	if (!s_rdc_api)
		return;

	const u32 count = s_rdc_api->GetNumCaptures();
	for (; s_rdc_seen_captures < count; ++s_rdc_seen_captures)
		rdc_log_capture(s_rdc_seen_captures);
}

void renderdoc_open_replay_ui()
{
	if (!rdc_available())
		return;

	if (rdc_ui_connected() && s_rdc_version >= eRENDERDOC_API_Version_1_5_0)
	{
		const u32 shown = s_rdc_api->ShowReplayUI();
		Msg("%s [RDC] connected ui raise %s", shown ? "*" : "~", shown ? "accepted" : "refused");
		return;
	}

	// The path rides a command line so quotes keep a spaced folder in one argument
	const xr_string quoted = s_rdc_latest_capture.empty()
		? xr_string()
		: xr_string("\"" + s_rdc_latest_capture + "\"");
	const char* const capture = quoted.empty() ? nullptr : quoted.c_str();
	const u32 pid = s_rdc_api->LaunchReplayUI(1, capture);
	if (pid)
		Msg("* [RDC] replay ui launched pid %u %s", pid, capture ? capture : "with no capture");
	else
		Msg("! [RDC] replay ui launch failed");
}

void renderdoc_set_overlay(bool visible)
{
	if (!rdc_available())
		return;

	s_rdc_api->MaskOverlayBits(0, visible ? (eRENDERDOC_Overlay_Default | eRENDERDOC_Overlay_Enabled) : 0);
	Msg("* [RDC] overlay bits 0x%08x", s_rdc_api->GetOverlayBits());
}

bool renderdoc_overlay_enabled()
{
	if (!rdc_available())
		return false;

	return (s_rdc_api->GetOverlayBits() & eRENDERDOC_Overlay_Enabled) != 0;
}

void renderdoc_set_active_window(void* device, void* window)
{
	// SetActiveWindow rejects a null handle, unlike the rest of the API
	if (!s_rdc_api || !device || !window)
		return;

	s_rdc_api->SetActiveWindow(device, window);
}
