#include "common.h"
#include "skin.h"

struct vf
{
    float4 hpos : POSITION;
    float2 tc : TEXCOORD0;
    float4 screen : TEXCOORD1;
    float3 eye : TEXCOORD2;
    float3 tangent : TEXCOORD3;
    float3 binormal : TEXCOORD4;
};

vf _main(v_model v)
{
    vf o;
    o.hpos = mul(m_WVP, v.pos);
    o.screen = o.hpos;
    o.tc = v.tc;
    o.eye = -mul(m_WV, v.pos);
    o.tangent = normalize(mul((float3x3)m_WV, v.T));
    o.binormal = normalize(mul((float3x3)m_WV, v.B));
    return o;
}

#define SKIN_VF vf
#include "skin_main.h"
