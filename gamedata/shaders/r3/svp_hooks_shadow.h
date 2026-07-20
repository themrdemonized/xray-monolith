// PIP_HOOK svp_hooks_shadow 20260715
// true pip logic for the 3DSS shadow chunk, rides mod.db0, the compat patch includes it

#ifndef SVP_HOOKS_SHADOW_H
#define SVP_HOOKS_SHADOW_H

#include "svp_hooks_common.h"

// the swing side slides the pupil center so the shadow enters from the side of the motion
// and a symmetric ring cannot form at center
float2 svp_shadow_swing(float2 off)
{
	if (shader_scope_params.w < -1.5)
		off += svp_exposure.zw;
	return off;
}

// the parallax crescent rides the swing envelope, calm aim shows none and a hard weapon
// swing sweeps in a dark crescent that fades back out quickly
float4 svp_shadow_soften(float4 shadow)
{
	if (shader_scope_params.w < -1.5)
	{
		shadow.rgb = 0;
		shadow.a *= saturate((svp_glass4.w - 0.3) / 0.35);
	}
	return shadow;
}

// the ocular field stop, a static rim vignette where the physical field ends
// onset rides the exit-pupil geometry and the field-curve control widens the edge
float svp_field_stop_alpha(Scope S)
{
	if (shader_scope_params.w < -1.5 && svp_glass.y > 0.001 && RETICLE_TYPE != RT_FLAT_SCREEN)
	{
		float onset = svp_glass3.y;
		float band = (1.0 - onset) * svp_glass.y;
		if (band > 0.0)
		{
			float r = length((S.tc0.xy - 0.5) * 2.0);
			return saturate((r - onset) / band);
		}
	}
	return 0.0;
}

#endif
