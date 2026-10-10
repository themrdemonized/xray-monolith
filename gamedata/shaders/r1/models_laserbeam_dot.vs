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
    float dot_size = floor(shader_param_6.w / 10.0) * 0.5 + 0.5;
    float2 offset = v.tc - 0.5;
    float3 tangent_offset = v.T * offset.x + v.B * offset.y;
    v.pos.xyz += tangent_offset / sqrt(max(length(offset), 0.01)) * 0.06 * (dot_size - 1.0);
    o.hpos = mul(m_WVP, v.pos);
    o.tc0 = v.tc.xy;
    float3 eye = normalize(mul(m_WV, v.pos).xyz);
    float3 normal = normalize(mul((float3x3)m_WV, v.norm));
    o.c0 = float4(1.0, 0.0, 0.0, abs(dot(eye, normal)));
    return o;
}

#define SKIN_VF vf
#include "skin_main.h"
