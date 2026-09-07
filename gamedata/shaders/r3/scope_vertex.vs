// PIP_COMPAT_PATCH scope_vertex 20260715
// DO NOT EDIT, OR INCLUDE IN MODS

#include "scope_common.h"

// self-contained TAA jitter so the scope compiles without SSS's screenspace_mvectors.h
// ssfx_jitter is engine-bound and zero when TAA is off, the formula matches SSS exactly
#ifndef SSFX_MV_LOADED
#define SSFX_MV_LOADED
uniform float4 ssfx_jitter;
float2 ssfx_taa_jitter(float4 hpos) { return hpos.xy + ssfx_jitter.xy * hpos.w; }
#endif

struct	v_in
{
	float4	P		: POSITION;		// (float,float,float,1)
	float4	Nh		: NORMAL;		// (nx,ny,nz,hemi occlusion)
	float3	T		: TANGENT;		// (nx,ny,nz)
	float3	B		: BINORMAL;		// (nx,ny,nz)
	float2	tc		: TEXCOORD0;	// (u,v)
};

v_out     main (v_in v)
{
	v.Nh			= unpack_D3DCOLOR(v.Nh);

    v_out o;

    o.hpos = mul(m_WVP, v.P);

	

    o.tc0 = v.tc.xy;

	o.w_P = mul(m_W, v.P).xyz;
	o.w_T = mul(m_W, v.T).xyz;
	o.w_B = mul(m_W, v.B).xyz;
	o.w_N = mul((float3x3)m_W, unpack_bx2(v.Nh));

	o.v_P = float4(mul(m_WV, v.P).xyz, v.Nh.w );
	o.v_N = mul((float3x3)m_WV, unpack_bx2(v.Nh));

	o.ssp_jitter = float2(0,0);

	// true pip draws every phase jittered so the main taa resolve treats the lens like any surface
	if ((scope_phase & SCOPE_PHASE_JITTERFIX) || shader_scope_params.w < -1.5) {
		// pixel space jitter of this draw, consumers subtract it to read unjittered screen positions
		o.ssp_jitter = ssfx_jitter.xy * float2(0.5, -0.5) * screen_res.xy;

		// Jitter the output position to ensure no ring around lens
		o.hpos.xy = ssfx_taa_jitter(o.hpos);
	}

    return o;
}