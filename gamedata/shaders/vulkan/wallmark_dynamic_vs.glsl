#version 450

// ============================================================================
// wallmark_dynamic_vs.glsl - Dynamic Wallmark Vertex Shader
// ============================================================================
//
// Vertex format: FVF::LIT (24 bytes)
//   vec3  position  (12 bytes)
//   u32   color     (4 bytes, R8G8B8A8_UNORM)
//   vec2  texcoord  (8 bytes)
//
// Push constant: mat4 viewProj (64 bytes)
// ============================================================================

layout(location = 0) in vec3  a_Position;
layout(location = 1) in vec4  a_Color;     // R8G8B8A8_UNORM
layout(location = 2) in vec2  a_TexCoord;

layout(location = 0) out vec4  v_Color;
layout(location = 1) out vec2  v_TexCoord;

layout(push_constant) uniform PushConstants
{
    mat4 u_ViewProj;
} pc;

void main()
{
    gl_Position = pc.u_ViewProj * vec4(a_Position, 1.0);
    v_Color     = a_Color;
    v_TexCoord  = a_TexCoord;
}
