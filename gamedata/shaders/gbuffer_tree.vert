#version 450

// ============================================================================
// gbuffer_tree.vert - G-Buffer Vertex Shader for Tree Geometry (stride=12)
// ============================================================================
//
// Trees use position-only vertices (FLOAT3, 12 bytes per vertex).
// Positions are QUANTIZED by FTreeVisual_quant=2048 and need prescaling.
// No normals or UVs are available in vertex data.
//
// ============================================================================

// Vertex inputs (tree geometry: FLOAT3 position only = 12 bytes)
layout(location = 0) in vec3 a_Position;  // Quantized local-space position

// Outputs to fragment shader (must match gbuffer.frag inputs)
layout(location = 0) out vec3 v_PositionEye;  // Eye-space position
layout(location = 1) out vec3 v_NormalEye;    // Eye-space normal
layout(location = 2) out vec2 v_TexCoord;     // Texture coordinates
layout(location = 3) out vec2 v_WorldPosXZ;   // World-space XZ for terrain detail tiling

// Push constants (same layout as gbuffer.vert)
layout(push_constant) uniform PushConstants
{
    mat4 u_Model;       // Model matrix (local -> world) = tree xform
    mat4 u_View;        // View matrix (world -> eye)
    mat4 u_Projection;  // Projection matrix (eye -> clip)
    float u_UVScale;    // Reused as tree position dequant scale (1/2048)
    float _pad196;      // offset 196: reserved (u_SkinMode in skinned shader)
    float u_AlphaRef;   // offset 200: Alpha test threshold (0.5 for trees, -1.0 = disabled)
} pc;

void main()
{
    // Dequantize position: DX11 does pos.xyz *= consts.x where consts.x = 1/2048
    vec3 localPos = a_Position * pc.u_UVScale;

    vec4 worldPos = pc.u_Model * vec4(localPos, 1.0);
    vec4 eyePos   = pc.u_View * worldPos;
    gl_Position   = pc.u_Projection * eyePos;

    v_PositionEye = eyePos.xyz;
    v_WorldPosXZ  = worldPos.xz;

    // Generate filler normal: world-space up vector transformed to eye-space
    v_NormalEye = (pc.u_View * vec4(0.0, 1.0, 0.0, 0.0)).xyz;

    // Procedural UV from world XZ position (better than hardcoded 0,0)
    // This gives basic texture mapping for tree bark/leaves
    v_TexCoord = worldPos.xz * 0.1;
}
