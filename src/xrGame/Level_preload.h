#pragma once

// Level preload (console: level_preload), see Level_preload.cpp: part of a level is prepared
// during play, before it is loaded. Main thread only.
extern int g_level_preload;
extern float g_level_preload_ms;

namespace level_preload
{
	// Ask for a level. Asked again and again while the level is wanted: for the level changers near
	// the player by update(), and by the console command preload_level.
	void request(LPCSTR level_name);
	void update(); // once per frame during play
	void keep_through_level_end(); // a level change is under way: what was prepared outlives the level
	void on_level_load(LPCSTR level_name); // releases what the level about to load does not use
	void on_level_end();
	void clear(bool wait = false); // wait: for all background work too, and free it (app end)
}
