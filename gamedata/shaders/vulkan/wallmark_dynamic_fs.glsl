#version 450

// ============================================================================
// wallmark_dynamic_fs.glsl - Dynamic Wallmark Fragment Shader
// ============================================================================
//
// Samples diffuse texture modulated by vertex color (alpha fade).
// Descriptor: sampler2D at set=0, binding=0
// ============================================================================

layout(location = 0) in vec4  v_Color;
layout(location = 1) in vec2  v_TexCoord;

layout(location = 0) out vec4 o_Color;

layout(set = 0, binding = 0) uniform sampler2D s_Diffuse;

void main()
{
    vec4 texel = texture(s_Diffuse, v_TexCoord);
    o_Color = texel * v_Color;
}
