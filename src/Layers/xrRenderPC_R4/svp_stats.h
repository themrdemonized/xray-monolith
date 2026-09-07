#ifndef svp_statsH
#define svp_statsH
#pragma once

// pip per-viewport render-stats overlay, gated by r__svp_stats
// every call self-gates, at r__svp_stats 0 no queries exist and no work runs

namespace svp_stats
{
	enum section_e
	{
		SEC_MAIN_GBUFFER = 0,
		SEC_MAIN_LIGHTS,
		SEC_MAIN_COMBINE,
		SEC_SVP_GBUFFER,
		SEC_SVP_LIGHTS,
		SEC_SVP_EMISSIVE,
		SEC_SVP_COMBINE,
		SEC_FRAME,
		SEC_COUNT
	};

	// frame boundary, disjoint query + ring advance, render thread inside Render() only
	void frame_begin();
	void frame_end(bool svp_active);

	// timing window, cpu qpc + gpu timestamp pair + rcache deltas, multiple pairs per frame accumulate
	void section_begin(section_e s);
	void section_end(section_e s);

	// light and shadow tallies at the accumulation sites
	void note_main_lights(u32 total, u32 shadowed);
	void note_svp_blend();
	void note_sun();

	// two-column panel, main viewport, drawn onto the bound ldr target
	void draw_overlay();

	// release the query pool + font, device reset or disable
	void release();
}

#endif
