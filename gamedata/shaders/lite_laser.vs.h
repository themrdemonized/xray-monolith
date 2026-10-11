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
#ifdef LITE_LASER_DOT
    float dot_size = floor(shader_param_6.w / 10.0) * 0.5 + 0.5;
    float2 offset = v.tc - 0.5;
    float3 tangent_offset = v.T * offset.x + v.B * offset.y;
    LITE_MODEL_POSITION.xyz += tangent_offset / sqrt(max(length(offset), 0.01)) * 0.06 * (dot_size - 1.0);
#else
    float beam_size = shader_param_6.w - floor(shader_param_6.w / 10.0) * 10.0;
    LITE_MODEL_POSITION.xyz += v.T * (v.tc.x - 0.5) * 2.0 * (beam_size - 1.0) * 0.006;
#endif
    o.hpos = mul(m_WVP, LITE_MODEL_POSITION);
    o.tc0 = v.tc.xy;
    float3 eye = normalize(mul(m_WV, LITE_MODEL_POSITION).xyz);
    float3 normal = normalize(mul((float3x3)m_WV, LITE_MODEL_NORMAL));
    o.c0 = float4(0.0, 0.0, 0.0, abs(dot(eye, normal)));
    return o;
}

#ifdef LITE_LASER_R1
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
