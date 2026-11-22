#include "stdafx.h"
#include "Level_preload.h"
#include "Level.h"
#include "Actor.h"
#include "level_changer.h"
#include "../Include/xrRender/RenderDeviceRender.h"

// Level preload (console: level_preload).
// While the player is near a level changer, part of the level behind it is prepared during play:
// the renderer creates its materials (shaders and their textures) a few per frame, and a background
// task reads and builds its static collision. Both are kept through the level change, so the load
// of that level finds them ready instead of creating them behind the loading screen. What the load
// did not use is released when it has finished.
// Everything here runs on the main thread: CLevel::OnFrame, the level change messages, the console,
// the level load. The load itself may run in a task (single player: while the main thread loads the
// ALife objects); it takes what it finds and calls nothing here, and nothing is asked for until it
// has finished.

int g_level_preload = 0; // console: level_preload
// console: level_preload_ms, main-thread time per frame for creating materials. One material is never
// split: a large texture without a shader cache can take tens of ms on its own.
float g_level_preload_ms = 3.f;

extern xr_vector<CLevelChanger*> g_lchangers;

namespace
{
	struct SLevelPreload
	{
		shared_str level;
		string_path folder; // with the trailing separator
		string_path cform; // its static collision file, empty if it has none
		u32 asked = 0; // Device.dwTimeContinual when it was last asked for
	};

	// Two level changers can be in reach at once.
	const u32 SLOTS = 2;
	// With all slots taken, a level nobody asked for in this long gives its slot to another one.
	const u32 REPLACE_AFTER_MS = 5000;
	// A level nobody asked for in this long is released: the player walked away.
	const u32 RELEASE_AFTER_MS = 120000;
	// Level changers closer than this to the player ask for their level.
	const float DISTANCE = 150.f;

	xr_vector<SLevelPreload> preloads;
	bool keep = false;

	void release(const SLevelPreload& preload)
	{
		Device.m_pRender->ResourcesPreloadRelease(preload.folder);
		if (preload.cform[0])
			CObjectSpace::prebuild_release(preload.cform);
	}
}

void level_preload::request(LPCSTR level_name)
{
	if (!g_level_preload || keep || g_dedicated_server || !g_pGameLevel || !g_pGameLevel->bReady ||
	    0 == _stricmp(Level().name().c_str(), level_name))
		return;

	const u32 now = Device.dwTimeContinual;
	u32 oldest = 0;
	for (u32 i = 0; i < preloads.size(); ++i)
	{
		if (0 == _stricmp(preloads[i].level.c_str(), level_name))
		{
			preloads[i].asked = now;
			return;
		}
		if (preloads[i].asked < preloads[oldest].asked)
			oldest = i;
	}
	if (preloads.size() >= SLOTS && now - preloads[oldest].asked < REPLACE_AFTER_MS)
		return;
	string_path relative, folder, file;
	strconcat(sizeof(relative), relative, level_name, "\\");
	FS.update_path(folder, "$game_levels$", relative);
	if (!FS.exist(strconcat(sizeof(file), file, folder, "level")))
		return;
	// The load of a level mounts its own archive first (CApplication::Level_ID): what is in it can
	// replace the files seen now.
	for (const CLocatorAPI::archive& archive : FS.m_archives)
		if (!archive.hSrcFile && archive.header && archive.header->line_exist("header", "level_name") &&
		    0 == _stricmp(archive.header->r_string("header", "level_name"), level_name))
			return;
	if (preloads.size() >= SLOTS)
	{
		release(preloads[oldest]);
		preloads.erase(preloads.begin() + oldest);
	}
	preloads.emplace_back();
	SLevelPreload& preload = preloads.back();
	preload.level = level_name;
	xr_strcpy(preload.folder, folder);
	preload.asked = now;
	Device.m_pRender->ResourcesPreloadLevel(folder);
	if (FS.exist(strconcat(sizeof(preload.cform), preload.cform, folder, "level.cform")))
		CObjectSpace::prebuild_static(preload.cform);
	else
		preload.cform[0] = 0;
	Msg("* level preload: %s", level_name);
}

void level_preload::update()
{
	if (g_dedicated_server || keep)
		return;
	if (!g_level_preload && !preloads.empty())
		clear();
	if (g_level_preload)
	{
		if (CActor* actor = Actor())
		{
			for (CLevelChanger* changer : g_lchangers)
				if (LPCSTR level = changer->preload_destination(actor->Position(), DISTANCE))
					request(level);
		}
		const u32 now = Device.dwTimeContinual;
		for (u32 i = 0; i < preloads.size(); ++i)
		{
			if (now - preloads[i].asked > RELEASE_AFTER_MS)
			{
				release(preloads[i]);
				preloads.erase(preloads.begin() + i);
				break;
			}
		}
	}
	// Also frees what was released and has finished its background work
	Device.m_pRender->ResourcesPreloadUpdate(g_level_preload_ms);
	CObjectSpace::prebuild_collect();
}

void level_preload::keep_through_level_end()
{
	// The level change replaces the level folder and unloads textures: no background work of the
	// renderer may run into it, also of levels released before. The static collision tasks read
	// files by their full path.
	if (Device.m_pRender)
		Device.m_pRender->ResourcesPreloadWait();
	// A disconnect already queued wins over the change (CLevel::MakeReconnect): the level ends for
	// good. A second change message finds the disconnect the first one queued.
	keep = keep || (!preloads.empty() && !Engine.Event.Peek("KERNEL:disconnect"));
}

void level_preload::on_level_load(LPCSTR level_name)
{
	CObjectSpace::prebuild_collect(); // released ones that finished, also from a game before
	for (u32 i = 0; i < preloads.size();)
	{
		if (_stricmp(preloads[i].level.c_str(), level_name))
		{
			release(preloads[i]);
			preloads.erase(preloads.begin() + i);
		}
		else
			++i;
	}
}

void level_preload::on_level_end()
{
	if (!keep)
	{
		// Quit to the menu or another save. The renderer frees a level once its background work has
		// run, and must not run into the unload; a collision build only uses its own memory and is
		// freed by a later prebuild_collect (the next game, or clear(true) at the end).
		clear();
		if (Device.m_pRender)
			Device.m_pRender->ResourcesPreloadWait();
	}
	keep = false;
}

void level_preload::clear(bool wait)
{
	preloads.clear();
	keep = false;
	if (Device.m_pRender)
	{
		Device.m_pRender->ResourcesPreloadRelease(nullptr);
		if (wait)
			Device.m_pRender->ResourcesPreloadWait();
	}
	CObjectSpace::prebuild_release(nullptr);
	if (wait)
		CObjectSpace::prebuild_collect(true);
}
