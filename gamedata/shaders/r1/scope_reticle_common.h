#include "lite_scope_common.h"
uniform sampler2D s_base;
uniform float4 scope_reticle_mode;
uniform float4 scope_reticle_masks;
uniform float4 scope_reticle_atlas;

float4 sample_reticle(float2 tc)
{
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
    float2 parallax = scope_parallax(I);
    float2 offset = scope_mark_offset(I, parallax);
    float4 mark = sample_reticle(offset * scope_reticle_projection.x + 0.5);
    float3 color;
    float transmission;
#if defined(SCOPE_RETICLE_DUAL)
    float4 sfp = sample_reticle(offset * scope_reticle_projection.y + 0.5);
    transmission = (1.0 - max(mark.r, sfp.g)) * (1.0 - sfp.b);
    color = scope_reticle_color.rgb * sfp.b;
#elif defined(SCOPE_RETICLE_GIPERON)
    float2 sfp_offset = offset * scope_reticle_projection.y;
    float finder = sample_reticle(sfp_offset + 0.5).g;
    float2 rotated = float2(
        sfp_offset.x * scope_reticle_projection.z - sfp_offset.y * scope_reticle_projection.w,
        sfp_offset.x * scope_reticle_projection.w + sfp_offset.y * scope_reticle_projection.z);
    float numbers = sample_reticle(rotated + 0.5).b;
    transmission = (1.0 - mark.r) * (1.0 - max(finder, numbers));
    color = scope_reticle_color.rgb * (1.0 - transmission);
#else
    float opacity = mark.a * scope_reticle_mode.z;
    color = (mark.rgb * scope_reticle_mode.x * scope_reticle_optics.z +
        scope_reticle_color.rgb * scope_reticle_mode.y) * opacity;
    transmission = 1.0 - opacity * scope_reticle_mode.w;
    float black = 1.0 - mark.r * scope_reticle_masks.x;
    color *= black;
    transmission *= black;
    float led = dot(mark.gb, scope_reticle_masks.yz);
    color = lerp(color, scope_reticle_color.rgb, led);
    transmission *= 1.0 - led;
    float text = mark.b * scope_reticle_masks.w;
    color = lerp(color, scope_reticle_color.w, text);
    transmission *= 1.0 - text;
#endif

    return float4(color, 1.0 - transmission);
}
