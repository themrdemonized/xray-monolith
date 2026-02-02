#version 450
// Cloud vertex shader - renders hemisphere dome with UV for 2D cloud textures
// Wind direction is packed in per-vertex attribute (R8G8B8A8_UNORM)

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec4 a_Wind;   // R8G8B8A8_UNORM: wind direction packed
layout(location = 2) in vec4 a_Color;  // R8G8B8A8_UNORM: cloud vertex color

layout(push_constant) uniform PushConstants {
    mat4 worldViewProj;      // WVP matrix
    vec4 cloudsColor;        // clouds_color from environment (.rgb = tint, .a = opacity)
    float weight;            // blend factor between two cloud textures
    float time;              // accumulated time for UV scrolling
} pc;

layout(location = 0) out vec2 v_UV;
layout(location = 1) out vec4 v_Wind;
layout(location = 2) out vec4 v_Color;

void main()
{
    // UV from hemisphere XZ position: map [-1..1] to [0..1]
    v_UV = a_Position.xz * 0.5 + 0.5;

    // Pass wind and color to fragment shader
    v_Wind = a_Wind;
    v_Color = a_Color;

    // Standard transform (no xyww trick - clouds are at specific distance)
    gl_Position = pc.worldViewProj * vec4(a_Position, 1.0);
}
