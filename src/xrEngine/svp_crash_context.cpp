#include "stdafx.h"
#include "svp_crash_context.h"

// plain ring of the last few scoped frames, render thread writes, the crash path reads
static const u32 SVP_CRASH_RING = 3;
static SvpCrashFrame s_ring[SVP_CRASH_RING] = {};
static u32 s_pushed = 0;

void svp_crash_context_push(const SvpCrashFrame& f)
{
	s_ring[s_pushed % SVP_CRASH_RING] = f;
	++s_pushed;
}

void svp_crash_context_dump()
{
	if (s_pushed == 0)
	{
		Msg("[SVP-CRASH] no scoped frames captured this session");
		return;
	}
	const u32 n = (s_pushed < SVP_CRASH_RING) ? s_pushed : SVP_CRASH_RING;
	Msg("[SVP-CRASH] last %u scoped frame(s), newest first:", n);
	for (u32 i = 0; i < n; ++i)
	{
		const SvpCrashFrame& f = s_ring[(s_pushed - 1 - i) % SVP_CRASH_RING];
		Msg("[SVP-CRASH]  frame=%u mode=%d active=%d rpass=%d front=%.3fm mag=%.2f fov=%.1fdeg disc=%.0fpx",
			f.frame, f.mode, f.active ? 1 : 0, f.render_pass_is_svp ? 1 : 0,
			f.hud_front_m, f.mag, f.fov_deg, f.disc_px);
	}
}
