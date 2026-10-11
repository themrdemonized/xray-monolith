// R1/R2: one fetch for the mark, two for glass/dirt; no scene target or reflections.
uniform sampler2D s_base;
uniform float4 reflex_reticle_atlas;
uniform float4 scope_surface_light;

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
    float4 glass = tex2D(s_base, I.tc * 0.5);
    glass.a *= scope_surface_light.w;
    float4 dirt = tex2D(s_base, I.tc * 0.5 + float2(0.0, 0.5));
    dirt.rgb *= scope_surface_light.rgb;
    float alpha = dirt.a + glass.a * (1.0 - dirt.a);
    float3 color = dirt.rgb * dirt.a + glass.rgb * glass.a * (1.0 - dirt.a);
    return float4(color / max(alpha, 0.001), alpha);
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
