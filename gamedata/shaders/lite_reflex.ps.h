// R1/R2 material compatibility: one texture fetch, no scene target or reflections.
uniform sampler2D s_base;
uniform float4 reflex_reticle_atlas;

struct lite_reflex_vf
{
    float2 tc : TEXCOORD0;
    float3 eye : TEXCOORD1;
    float3 tangent : TEXCOORD2;
    float3 binormal : TEXCOORD3;
    float3 normal : TEXCOORD4;
};

float4 main(lite_reflex_vf I) : COLOR0
{
#ifdef LITE_REFLEX_LENS
    // 3DSS lens atlas: base glass at top-left; the other tiles hold dirt/reflections.
    return tex2D(s_base, I.tc * 0.5);
#else
    float3 eye = normalize(float3(
        dot(I.eye, I.tangent + 0.0039),
        dot(I.eye, I.binormal + 0.0039),
        dot(I.eye, I.normal)));
    float2 tc = (I.tc - 0.5 - eye.xy * LITE_REFLEX_PARALLAX) / LITE_REFLEX_SCALE + 0.5;
    float2 inside = step(abs(tc - 0.5), 0.5);
#ifndef LITE_REFLEX_SIMPLE
    tc = saturate(tc) * reflex_reticle_atlas.xy + reflex_reticle_atlas.zw;
#endif
    return tex2D(s_base, tc) * (inside.x * inside.y);
#endif
}
