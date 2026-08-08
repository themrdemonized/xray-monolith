#pragma once

struct xr_token;

ENGINE_API extern xr_token* vid_monitor_token;
ENGINE_API extern xr_string  vid_monitor_name;

ENGINE_API void     fill_vid_monitor_list();
ENGINE_API void     free_vid_monitor_list();
ENGINE_API void     refresh_vid_monitor_list();
ENGINE_API HMONITOR ResolveSelectedMonitor();

ENGINE_API extern volatile long g_monitor_list_dirty;

ENGINE_API HMONITOR GetStartupMonitor();
ENGINE_API void     SetStartupMonitor(HMONITOR h);
ENGINE_API void     ResetStartupMonitor();
