#ifndef svp_crash_contextH
#define svp_crash_contextH
#pragma once

// snapshot of svp (true-PiP) runtime latch state for one scoped frame, pushed from the renderer
// and dumped into the crash log on the crash path (cvar values ride the existing dump_cvar dump)
struct SvpCrashFrame
{
	u32   frame;
	int   mode;               // r__svpscope
	bool  active;             // IsSVPActive
	bool  render_pass_is_svp;
	float hud_front_m;
	float mag;
	float fov_deg;
	float disc_px;
};

// render thread pushes once per scoped frame, the crash handler reads the ring
ENGINE_API void svp_crash_context_push(const SvpCrashFrame& f);
ENGINE_API void svp_crash_context_dump();

#endif
