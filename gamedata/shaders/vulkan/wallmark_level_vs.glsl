#version 450

// ============================================================================
// wallmark_level_vs.glsl - Wallmark Level Vertex Shader
// ============================================================================
//
// Same vertex inputs as gbuffer.vert (stride-32 level geometry).
// Outputs only gl_Position + v_TexCoord (no normal needed for wallmarks).
//
// ============================================================================

// Vertex inputs (X-Ray level geometry: pos + packed_normal + short2_uv = 32 bytes)
layout(location = 0) in vec3 a_Position;  // Local-space position (FLOAT3)
layout(location = 1) in vec4 a_Normal;    // Packed normal as D3DCOLOR (UBYTE4N, unused)
layout(location = 2) in vec2 a_TexCoord;  // Texture coordinates (SHORT2 -> SSCALED)

// Outputs to fragment shader
layout(location = 0) out vec2 v_TexCoord;

// Push constants (same layout as gbuffer)
layout(push_constant) uniform PushConstants
{
    mat4 u_Model;       // offset 0: Model matrix (local -> world)
    mat4 u_View;        // offset 64: View matrix (world -> eye)
    mat4 u_Projection;  // offset 128: Projection matrix (eye -> clip)
    float u_UVScale;    // offset 192: UV scale (1/1024 for SHORT2)
    float _pad196;      // offset 196: reserved
    float u_AlphaRef;   // offset 200: Alpha test threshold
} pc;

void main()
{
    vec4 worldPos = pc.u_Model * vec4(a_Position, 1.0);
    vec4 eyePos   = pc.u_View * worldPos;
    gl_Position   = pc.u_Projection * eyePos;

    // UV scaling: SHORT2 needs /1024
    v_TexCoord = a_TexCoord * pc.u_UVScale;
}
