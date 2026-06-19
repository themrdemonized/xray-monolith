// DO NOT EDIT, OR INCLUDE IN MODS

#include "scope_common.h"
#include "screenspace_mvectors.h"

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

	if (scope_phase & SCOPE_PHASE_JITTERFIX) {
		// compute the screenspace jitter offset, used to cancel jitter when sampling
		float2 j = ssfx_taa_jitter(o.hpos) - o.hpos.xy;
		o.ssp_jitter = (j * 0.5 + 0.5) * screen_res.xy;

		// Jitter the output position to ensure no ring around lens
		o.hpos.xy = ssfx_taa_jitter(o.hpos);
	}

    return o;
}