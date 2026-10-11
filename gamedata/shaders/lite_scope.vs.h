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
    o.hpos = mul(m_WVP, LITE_MODEL_POSITION);
    o.screen = o.hpos;
    o.tc = v.tc;
    o.eye = -mul(m_WV, LITE_MODEL_POSITION);
    o.tangent = normalize(mul((float3x3)m_WV, v.T));
    o.binormal = normalize(mul((float3x3)m_WV, v.B));
    return o;
}

#ifdef LITE_SCOPE_R1
#define SKIN_VF vf
#include "skin_main.h"
#else
#ifdef SKIN_NONE
vf main(v_model v) { return _main(v); }
#endif
#ifdef SKIN_0
vf main(v_model_skinned_0 v) { return _main(skinning_0(v)); }
#endif
#ifdef SKIN_1
vf main(v_model_skinned_1 v) { return _main(skinning_1(v)); }
#endif
#ifdef SKIN_2
vf main(v_model_skinned_2 v) { return _main(skinning_2(v)); }
#endif
#ifdef SKIN_3
vf main(v_model_skinned_3 v) { return _main(skinning_3(v)); }
#endif
#ifdef SKIN_4
vf main(v_model_skinned_4 v) { return _main(skinning_4(v)); }
#endif
#endif
