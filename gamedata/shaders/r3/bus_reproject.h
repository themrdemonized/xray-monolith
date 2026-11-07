#ifndef BUS_REPROJECT_H
#define BUS_REPROJECT_H
#if defined(SHADER_BUS) && SHADER_BUS >= 8
// last frame's view projection, include after common.h for m_inv_V
// screenspace_mvectors.h declares the same matrix under SSFX_MV_LOADED
#ifndef SSFX_MV_LOADED
uniform float4x4 m_vp_prev;
#endif
// where a view space point of this frame sat on last frame's screen, false behind that camera or off its screen
bool bus_reproject(float3 view_pos, out float2 tc)
{
	float4 c = mul(m_vp_prev, float4(mul(m_inv_V, float4(view_pos, 1.0)).xyz, 1.0));
	tc = c.w > 0.0001 ? c.xy / c.w * float2(0.5, -0.5) + 0.5 : float2(-1.0, -1.0);
	return all(tc >= 0.0) && all(tc <= 1.0);
}
#endif
#endif
