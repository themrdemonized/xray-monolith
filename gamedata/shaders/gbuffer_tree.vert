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
layout(location = 3) out vec3 v_WorldPos;     // World-space position
layout(location = 4) out vec3 v_WorldNormal;  // World-space normal
layout(location = 5) out vec4 v_CurrClipPos;  // Current clip-space position (for motion vectors)
layout(location = 6) out vec4 v_PrevClipPos;  // Previous clip-space position (for motion vectors)

// Push constants (same layout as gbuffer.vert)
layout(push_constant) uniform PushConstants
{
    mat4 u_Model;       // offset 0: Model matrix (local -> world) = tree xform
    mat4 u_PrevModel;   // offset 64: Previous frame model matrix (for per-object motion vectors)
    mat4 u_Projection;  // offset 128: Projection matrix (eye -> clip)
    float u_UVScale;    // Reused as tree position dequant scale (1/2048)
    float _pad196;      // offset 196: reserved (u_SkinMode in skinned shader)
    float u_AlphaRef;   // offset 200: Alpha test threshold (0.5 for trees, -1.0 = disabled)
} pc;

// GlobalLighting UBO (Set 0, Binding 0) — View matrix + prevVP
layout(std140, set = 0, binding = 0) uniform GlobalLighting
{
    vec4 _pad_global[95];   // offsets 0-1519 (not used in this shader)
    mat4 m_prevVP;          // offset 1520: Previous frame ViewProjection
    mat4 m_View;            // offset 1584: Current frame View matrix
} uGlobal;

void main()
{
    // Dequantize position: DX11 does pos.xyz *= consts.x where consts.x = 1/2048
    vec3 localPos = a_Position * pc.u_UVScale;

    vec4 worldPos = pc.u_Model * vec4(localPos, 1.0);
    vec4 eyePos   = uGlobal.m_View * worldPos;
    gl_Position   = pc.u_Projection * eyePos;

    // Motion vectors: current and previous clip-space positions
    v_CurrClipPos = gl_Position;
    v_PrevClipPos = uGlobal.m_prevVP * (pc.u_PrevModel * vec4(localPos, 1.0));

    v_PositionEye = eyePos.xyz;
    v_WorldPos    = worldPos.xyz;

    // Trees have no vertex normals — use world-up as filler
    v_WorldNormal = vec3(0.0, 1.0, 0.0);
    v_NormalEye   = (uGlobal.m_View * vec4(0.0, 1.0, 0.0, 0.0)).xyz;

    // Procedural UV from world XZ position (better than hardcoded 0,0)
    // This gives basic texture mapping for tree bark/leaves
    v_TexCoord = worldPos.xz * 0.1;
}
