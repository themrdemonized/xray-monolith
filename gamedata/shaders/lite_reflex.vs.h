#include "common.h"
#include "skin.h"

struct lite_reflex_vf
{
    float4 hpos : POSITION;
    float2 tc : TEXCOORD0;
    float3 eye : TEXCOORD1;
    float3 tangent : TEXCOORD2;
    float3 binormal : TEXCOORD3;
    float3 normal : TEXCOORD4;
};

lite_reflex_vf _main(v_model v)
{
    lite_reflex_vf o;
    o.hpos = mul(m_WVP, LITE_MODEL_POSITION);
    o.tc = v.tc.xy;
    o.eye = -mul(m_WV, LITE_MODEL_POSITION).xyz;
    o.tangent = mul((float3x3)m_WV, v.T);
    o.binormal = mul((float3x3)m_WV, v.B);
    o.normal = mul((float3x3)m_WV, LITE_MODEL_NORMAL);
    return o;
}

#ifdef LITE_REFLEX_R1
#define SKIN_VF lite_reflex_vf
#include "skin_main.h"
#else
#ifdef SKIN_NONE
lite_reflex_vf main(v_model v) { return _main(v); }
#endif
#ifdef SKIN_0
lite_reflex_vf main(v_model_skinned_0 v) { return _main(skinning_0(v)); }
#endif
#ifdef SKIN_1
lite_reflex_vf main(v_model_skinned_1 v) { return _main(skinning_1(v)); }
#endif
#ifdef SKIN_2
lite_reflex_vf main(v_model_skinned_2 v) { return _main(skinning_2(v)); }
#endif
#ifdef SKIN_3
lite_reflex_vf main(v_model_skinned_3 v) { return _main(skinning_3(v)); }
#endif
#ifdef SKIN_4
lite_reflex_vf main(v_model_skinned_4 v) { return _main(skinning_4(v)); }
#endif
#endif
