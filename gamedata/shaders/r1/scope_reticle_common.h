// Lightweight optics. Ordinary reticles use two texture samples including the scene.
uniform sampler2D s_base;
uniform sampler2D s_scene;
uniform float4 s3ds_param_1;
uniform float4 markswitch_color;
uniform float4 screen_res;
uniform float4 r1_scope_debug;
uniform float4 scope_lens_params;       // inverse scene zoom, XY image shift, inverse exit pupil
uniform float4 scope_reticle_mode;       // texture RGB, LED RGB, alpha opacity, over/additive
uniform float4 scope_reticle_masks;      // black R, illumination G/B, text B
uniform float4 scope_reticle_projection; // inverse FFP/SFP sizes, scale rotation cos/sin
uniform float4 scope_reticle_atlas;      // cell scale and offset; identity except type 10

struct vf
{
    float2 tc : TEXCOORD0;
    float4 screen : TEXCOORD1;
    float3 eye : TEXCOORD2;
    float3 tangent : TEXCOORD3;
    float3 binormal : TEXCOORD4;
};

float4 sample_reticle(float2 tc)
{
    // Clamp for sampling, but discard outside the authored image instead of stretching its edges.
    float2 inside = step(abs(tc - 0.5), 0.5);
#if defined(SCOPE_RETICLE_DUAL) || defined(SCOPE_RETICLE_GIPERON)
    float2 uv = tc;
#else
    float2 uv = tc * scope_reticle_atlas.xy + scope_reticle_atlas.zw;
#endif
    return tex2D(s_base, uv) * (inside.x * inside.y);
}

float4 main(vf I) : COLOR0
{
    float3 eye = normalize(I.eye);
    float2 parallax = float2(dot(eye, I.tangent), dot(eye, I.binormal));
    float2 screen_tc = I.screen.xy / I.screen.w * float2(0.5, -0.5) + 0.5;
    float2 scene_tc = (screen_tc - 0.5) * scope_lens_params.x + 0.5 + screen_res.zw * 0.5;
    scene_tc += parallax * scope_lens_params.yz;
    float3 color = tex2D(s_scene, scene_tc).rgb;

    float2 offset = I.tc - 0.5 - parallax * 10.0;
    float4 mark = sample_reticle(offset * scope_reticle_projection.x + 0.5);
#if defined(SCOPE_RETICLE_DUAL)
    float4 sfp = sample_reticle(offset * scope_reticle_projection.y + 0.5);
    // Shorty: R at FFP, G and LED B at SFP.
    color *= 1.0 - max(mark.r, sfp.g);
    color = lerp(color, markswitch_color.rgb, sfp.b);
#elif defined(SCOPE_RETICLE_GIPERON)
    // 1P59: the main R mask magnifies, the G finder and rotated B scale stay at SFP.
    float2 sfp_offset = offset * scope_reticle_projection.y;
    float finder = sample_reticle(sfp_offset + 0.5).g;
    float2 rotated = float2(
        sfp_offset.x * scope_reticle_projection.z - sfp_offset.y * scope_reticle_projection.w,
        sfp_offset.x * scope_reticle_projection.w + sfp_offset.y * scope_reticle_projection.z);
    float numbers = sample_reticle(rotated + 0.5).b;
    float sfp_mask = max(finder, numbers);
    float opacity = mark.r + sfp_mask * (1.0 - mark.r);
    color = lerp(color, markswitch_color.rgb, opacity);
#else
    float opacity = mark.a * scope_reticle_mode.z;
    float3 reticle_color = mark.rgb * scope_reticle_mode.x + markswitch_color.rgb * scope_reticle_mode.y;
    color = color * (1.0 - opacity * scope_reticle_mode.w) + reticle_color * opacity;
    color *= 1.0 - mark.r * scope_reticle_masks.x;
    color = lerp(color, markswitch_color.rgb, dot(mark.gb, scope_reticle_masks.yz));
    color = lerp(color, 0.3, mark.b * scope_reticle_masks.w);
#endif

    float2 exit_tc = (I.tc - 0.5 + parallax * s3ds_param_1.y) * scope_lens_params.w;
    float visible = saturate((0.5 - length(exit_tc)) / 0.15);
    float3 result = saturate(color) * visible;
    float surface_debug = saturate(r1_scope_debug.x);
    result = lerp(result, float3(1.0, 0.0, 1.0), surface_debug);
    return float4(result, 1.0);
}
