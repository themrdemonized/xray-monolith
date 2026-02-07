#version 450

// ============================================================================
// wallmark_level_fs.glsl - Wallmark Level Fragment Shader
// ============================================================================
//
// Simple: sample s_Diffuse, alpha test, output to single color attachment.
//
// ============================================================================

// Input from vertex shader
layout(location = 0) in vec2 v_TexCoord;

// Single color output (swapchain)
layout(location = 0) out vec4 o_Color;

// Push constants (shared with vertex shader)
layout(push_constant) uniform PushConstants
{
    layout(offset = 200) float u_AlphaRef;  // Alpha test threshold (-1.0 = disabled)
} pc;

// Descriptor Set 1: Material textures (PerMaterial)
layout(set = 1, binding = 0) uniform sampler2D s_Diffuse;

void main()
{
    vec4 albedo = texture(s_Diffuse, v_TexCoord);

    // Alpha test
    if (pc.u_AlphaRef > 0.0)
    {
        if (albedo.a < pc.u_AlphaRef)
            discard;
    }

    o_Color = albedo;
}
