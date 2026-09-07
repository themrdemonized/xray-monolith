// svp_hooks_lens 20260715 thinhook
// relocated true-PiP flat-screen lens cull, included by scope_custom_lens.h after scope_3dss_common.h
#ifndef SVP_HOOKS_LENS_INCLUDED
#define SVP_HOOKS_LENS_INCLUDED

#include "svp_hooks_common.h"

// a flat screen is a see-through window, not coated glass, drop the whole lens layer under
// true PiP so the world fills the panel (reflections/specular/dirt/vignette would ellipse it)
bool svp_lens_flat_window_cull()
{
	return RETICLE_TYPE == RT_FLAT_SCREEN && shader_scope_params.w < -1.5;
}

#endif
