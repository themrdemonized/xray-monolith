// Shared lens math for legacy renderers; no HDR, G-buffer or extra scene pass.
uniform float4 scope_lens_params;        // inverse image zoom, XY parallax shift
uniform float4 scope_shadow_params;      // eye relief, inverse pupil/edge width, opacity
uniform float4 scope_reticle_projection; // inverse FFP/SFP size, rotation cos/sin
uniform float4 scope_reticle_optics;     // projection distance, curvature, ADS color fade
uniform float4 scope_reticle_color;      // illumination RGB, ACOG text luminance
uniform float4 scope_transition;         // Specter progress, enable, disk centers X

struct vf
{
    float2 tc : TEXCOORD0;
    float4 screen : TEXCOORD1;
    float3 eye : TEXCOORD2;
    float3 tangent : TEXCOORD3;
    float3 binormal : TEXCOORD4;
};

float2 scope_parallax(vf I)
{
    float3 eye = normalize(I.eye);
    return float2(dot(eye, I.tangent), dot(eye, I.binormal));
}

float2 scope_mark_offset(vf I, float2 parallax)
{
    float2 offset = I.tc - 0.5 - parallax * scope_reticle_optics.x;
    // Lightweight curvature; the optical center stays fixed when the eye is aligned.
    float2 radius = I.tc - 0.5;
    return offset * (1.0 - scope_reticle_optics.y * length(parallax) * dot(radius, radius));
}

float scope_pupil(vf I, float2 parallax)
{
    float2 exit_tc = (I.tc - 0.5 + parallax * scope_shadow_params.x) * scope_shadow_params.y;
    float visible = saturate((0.5 - length(exit_tc)) * scope_shadow_params.z);
    return lerp(1.0, visible, scope_shadow_params.w);
}

float scope_vignette(float2 tc)
{
    return 1.0 - smoothstep(0.4, 2.0, 2.0 * length(tc - 0.5));
}
