#version 450

// ============================================================================
// gbuffer.vert - G-Buffer Vertex Shader
// ============================================================================
//
// Phase 2.21.1: G-Buffer Shaders
//
// Transforms geometry vertices and prepares data for deferred shading.
//
// Process:
// 1. Transform vertex to eye-space (для lighting)
// 2. Transform vertex to clip-space (для rasterization)
// 3. Transform normal to eye-space (для lighting)
// 4. Pass through texture coordinates
//
// ============================================================================

// Vertex inputs (X-Ray FVF format)
layout(location = 0) in vec3 a_Position;  // Local-space position
layout(location = 1) in vec3 a_Normal;    // Local-space normal
layout(location = 2) in vec2 a_TexCoord;  // Texture coordinates (UV)

// Outputs to fragment shader
layout(location = 0) out vec3 v_PositionEye;  // Eye-space position (для lighting)
layout(location = 1) out vec3 v_NormalEye;    // Eye-space normal (для lighting)
layout(location = 2) out vec2 v_TexCoord;     // Texture coordinates

// Push constants (transformation matrices)
layout(push_constant) uniform PushConstants
{
    mat4 u_Model;       // Model matrix (local → world)
    mat4 u_View;        // View matrix (world → eye)
    mat4 u_Projection;  // Projection matrix (eye → clip)
} pc;

void main()
{
    // ========================================================================
    // 1. Transform position to world space
    // ========================================================================
    vec4 posWorld = pc.u_Model * vec4(a_Position, 1.0);

    // ========================================================================
    // 2. Transform position to eye space (для lighting в deferred pass)
    // ========================================================================
    vec4 posEye = pc.u_View * posWorld;
    v_PositionEye = posEye.xyz;

    // ========================================================================
    // 3. Transform position to clip space (для rasterization)
    // ========================================================================
    gl_Position = pc.u_Projection * posEye;

    // ========================================================================
    // 4. Transform normal to eye space
    // ========================================================================
    // Normal transformation requires normal matrix (transpose(inverse(MV)))
    // For uniform scaling: можно использовать upper-left 3x3 of MV
    // For non-uniform scaling: нужно вычислить normal matrix
    //
    // Simplified (assumes uniform scale):
    mat3 normalMatrix = mat3(pc.u_View * pc.u_Model);
    v_NormalEye = normalize(normalMatrix * a_Normal);

    // ========================================================================
    // 5. Pass through texture coordinates
    // ========================================================================
    v_TexCoord = a_TexCoord;
}
