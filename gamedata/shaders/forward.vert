#version 450

// ============================================================================
// forward.vert - Forward Rendering Vertex Shader
// ============================================================================
//
// Phase 2.19.3: Forward Shaders
//
// Standard vertex shader для forward rendering (transparent objects).
//
// Transforms vertices and prepares data для lighting в fragment shader.
//
// ============================================================================

// Vertex inputs
layout(location = 0) in vec3 a_Position;  // Local-space position
layout(location = 1) in vec3 a_Normal;    // Local-space normal
layout(location = 2) in vec2 a_TexCoord;  // Texture coordinates

// Outputs to fragment shader
layout(location = 0) out vec3 v_Position;  // Eye-space position (для lighting)
layout(location = 1) out vec3 v_Normal;    // Eye-space normal (для lighting)
layout(location = 2) out vec2 v_TexCoord;  // Texture coordinates

// Push constants (MVP matrices)
layout(push_constant) uniform PushConstants
{
    mat4 u_Model;      // Model matrix
    mat4 u_View;       // View matrix
    mat4 u_Projection; // Projection matrix
} pc;

void main()
{
    // Transform to world space
    vec4 worldPos = pc.u_Model * vec4(a_Position, 1.0);

    // Transform to eye space (для lighting)
    vec4 eyePos = pc.u_View * worldPos;
    v_Position = eyePos.xyz;

    // Transform normal to eye space
    // Note: Should use normal matrix (transpose(inverse(MV)))
    // For now: simplified (assumes uniform scale)
    mat3 normalMatrix = mat3(pc.u_View * pc.u_Model);
    v_Normal = normalize(normalMatrix * a_Normal);

    // Pass through texture coordinates
    v_TexCoord = a_TexCoord;

    // Transform to clip space
    gl_Position = pc.u_Projection * eyePos;
}
