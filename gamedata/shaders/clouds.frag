#version 450
// Cloud fragment shader - samples two 2D cloud textures with wind-based UV scrolling
// Blends between textures using weight, applies cloud color tint and alpha

layout(location = 0) in vec2 v_UV;
layout(location = 1) in vec4 v_Wind;
layout(location = 2) in vec4 v_Color;

layout(location = 0) out vec4 o_Color;

layout(push_constant) uniform PushConstants {
    mat4 worldViewProj;
    vec4 cloudsColor;    // .rgb = tint, .a = opacity
    float weight;        // blend factor between cloud textures
    float time;          // time for UV scrolling
} pc;

layout(set = 1, binding = 0) uniform sampler2D s_Clouds0; // Current weather clouds
layout(set = 1, binding = 1) uniform sampler2D s_Clouds1; // Next weather clouds

void main()
{
    // Remap wind from [0..1] UNORM to [-0.5..0.5] direction
    vec2 windDir = v_Wind.xz - 0.5;

    // UV scroll by wind direction over time
    vec2 uv0 = v_UV + windDir * pc.time;
    vec2 uv1 = v_UV + windDir * pc.time;

    // Sample both cloud textures
    vec4 cloud0 = texture(s_Clouds0, uv0);
    vec4 cloud1 = texture(s_Clouds1, uv1);

    // Blend between two weather cloud textures
    vec4 cloud = mix(cloud0, cloud1, pc.weight);

    // Apply cloud color tint
    cloud.rgb *= pc.cloudsColor.rgb;

    // Apply cloud alpha from environment
    cloud.a *= pc.cloudsColor.a;

    // Modulate by per-vertex color
    cloud *= v_Color;

    o_Color = cloud;
}
