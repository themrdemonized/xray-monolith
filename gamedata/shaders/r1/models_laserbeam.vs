#include "common.h"
#include "skin.h"
uniform float4 shader_param_6;

struct vf
{
    float4 hpos : POSITION;
    float2 tc0 : TEXCOORD0;
    float4 c0 : COLOR0;
};

vf _main(v_model v)
{
    vf o;
    float beam_size = shader_param_6.w - floor(shader_param_6.w / 10.0) * 10.0;
    v.pos.xyz += v.T * (v.tc.x - 0.5) * 2.0 * (beam_size - 1.0) * 0.006;
    o.hpos = mul(m_WVP, v.pos);
    o.tc0 = v.tc.xy;
    float3 eye = normalize(mul(m_WV, v.pos).xyz);
    float3 normal = normalize(mul((float3x3)m_WV, v.norm));
    o.c0 = float4(0.0, 0.0, 0.0, abs(dot(eye, normal)));
    return o;
}

#define SKIN_VF vf
#include "skin_main.h"
