// svp_hooks_reticle 20260715 thinhook
// relocated true-PiP sight-reticle terms, included by scope_custom_reticle.h after scope_3dss_common.h
#ifndef SVP_HOOKS_RETICLE_INCLUDED
#define SVP_HOOKS_RETICLE_INCLUDED

#include "svp_hooks_common.h"

bool svp_reticle_kill_chroma()
{
	return shader_scope_params.w < -1.5 && svp_control.y > 0.5 && IMAGE_TYPE != IT_THERMAL && IMAGE_TYPE != IT_THERMAL_COLOR;
}

// ACOG fiber brightness source, sun visibility (fiber gathers sunlight) or scene luminance
float svp_reticle_acog_fiber(float lum)
{
	return (shader_scope_params.w < -1.5 && svp_glass.z > 0.5)
		? saturate(dot(L_hemi_color.rgb, float3(0.299, 0.587, 0.114)) * 3.0)
		: lum;
}

// Sight reticle. true PiP (w < -1.5) swaps the eye-coupled field for a centered one of the
// same slope plus a true-scale parallax term (svp_optics.y, ~0.15 mrad at full eye deflection)
float2 svp_reticle_t_field(Scope S, float2 V_tangent)
{
	float2 t_field = V_tangent.xy * mas_scale();
	if (shader_scope_params.w < -1.5)
		t_field = -(S.tc0 - 0.5) * (mas_scale() * svp_optics.x) + V_tangent.xy * (mas_scale() * svp_optics.y);
	return t_field;
}

float svp_reticle_pip_pin()
{
	return (shader_scope_params.w < -1.5) ? 0.0 : 1.0;
}

// mirror the sample V around the reticle center under true PiP so it matches the auto-flipped world
// same raw mesh UV test as the image chunk so the two can never disagree
void svp_reticle_flip(inout float2 reticle_tc, inout float2 reticle_lens_tc, Scope S)
{
	if (RETICLE_TYPE != RT_SCREEN && RETICLE_TYPE != RT_FLAT_SCREEN
		&& shader_scope_params.w < -1.5 && svp_glass.w > 0.5 && ddy(S.tc0.y) < 0.0)
	{
		reticle_tc.y = 1.0 - reticle_tc.y;
		reticle_lens_tc.y = 1.0 - reticle_lens_tc.y;
	}
}

// true PiP illuminated reticle wash-out, the glow loses contrast against a bright background (black lines are rgb 0, untouched)
void svp_reticle_washout(inout float4 result, float lum)
{
	if (shader_scope_params.w < -1.5 && svp_glass.x > 0.001)
		result.rgb *= max(0.65, 1.0 - svp_glass.x * (1.0 - lum) * 0.3);
}

#endif
