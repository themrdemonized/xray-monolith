#version 450

// ============================================================================
// gbuffer.vert - G-Buffer Vertex Shader
// ============================================================================
//
// Transforms geometry vertices for deferred shading.
//
// X-Ray Fmatrix is row-major. GLSL mat4 is column-major.
// When row-major data is memcpy'd into column-major mat4, the result is
// the TRANSPOSE of the original matrix.
//
// X-Ray convention: result = v_row * M_rowmajor
// With transposed matrix in GLSL: result = M_glsl * v_column
//
// Therefore the correct multiplication order is: mat4 * vec4
//
// ============================================================================

// Vertex inputs (X-Ray level geometry: pos + packed_normal + tangent + binormal + short2_uv = 32 bytes)
layout(location = 0) in vec3 a_Position;  // Local-space position (FLOAT3)
layout(location = 1) in vec4 a_Normal;    // Packed normal as D3DCOLOR (UBYTE4N → vec4)
layout(location = 2) in vec2 a_TexCoord;  // Texture coordinates (SHORT2 → SSCALED, raw int16 values)

// Outputs to fragment shader
layout(location = 0) out vec3 v_PositionEye;  // Eye-space position
layout(location = 1) out vec3 v_NormalEye;    // Eye-space normal
layout(location = 2) out vec2 v_TexCoord;     // Texture coordinates
layout(location = 3) out vec3 v_WorldPos;     // World-space position (XYZ for triplanar)
layout(location = 4) out vec3 v_WorldNormal;  // World-space normal (for triplanar blend weights)
layout(location = 5) out vec4 v_CurrClipPos;  // Current clip-space position (for motion vectors)
layout(location = 6) out vec4 v_PrevClipPos;  // Previous clip-space position (for motion vectors)

// Push constants (X-Ray Fmatrix — row-major, loaded as transposed in GLSL)
layout(push_constant) uniform PushConstants
{
    mat4 u_Model;       // offset 0: Model matrix (local -> world)
    mat4 u_PrevModel;   // offset 64: Previous frame model matrix (for per-object motion vectors)
    mat4 u_Projection;  // offset 128: Projection matrix (eye -> clip)
    float u_UVScale;    // UV scale: 1/1024 for SHORT2 (stride 32), 1.0 for FLOAT2 (stride 36+)
    float _pad196;      // offset 196: reserved (u_SkinMode in skinned shader)
    float u_AlphaRef;   // offset 200: Alpha test threshold (-1.0 = disabled, 0.5 = enabled)
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
    // mat4 * vec4 is correct when row-major Fmatrix is loaded into column-major mat4
    vec4 worldPos = pc.u_Model * vec4(a_Position, 1.0);
    vec4 eyePos   = uGlobal.m_View * worldPos;
    gl_Position   = pc.u_Projection * eyePos;

    // Motion vectors: current and previous clip-space positions
    v_CurrClipPos = gl_Position;
    v_PrevClipPos = uGlobal.m_prevVP * (pc.u_PrevModel * vec4(a_Position, 1.0));

    v_PositionEye = eyePos.xyz;
    v_WorldPos    = worldPos.xyz;

    // Unpack normal from D3DCOLOR (UBYTE4N → 0..1 range, remap to -1..1)
    // D3DCOLOR stores as BGRA, read as RGBA via R8G8B8A8_UNORM
    vec3 unpackedNormal = a_Normal.xyz * 2.0 - 1.0;

    // Transform normal to eye-space (w=0 for direction vectors)
    vec3 worldNormal = (pc.u_Model * vec4(unpackedNormal, 0.0)).xyz;
    v_WorldNormal    = worldNormal;
    v_NormalEye      = (uGlobal.m_View * vec4(worldNormal, 0.0)).xyz;

    // UV scaling: SHORT2 needs /1024 (u_UVScale = 1/1024), FLOAT2 is direct (u_UVScale = 1.0)
    v_TexCoord = a_TexCoord * pc.u_UVScale;
}
