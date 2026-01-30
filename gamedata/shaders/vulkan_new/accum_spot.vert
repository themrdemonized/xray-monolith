#version 450

// ============================================================================
// accum_spot.vert - Spot Light Volume Vertex Shader
// ============================================================================
//
// Phase 2.17.3: Spot Light Shaders
//
// Transforms cone volume vertices using MVP matrix.
// Cone is scaled by light range and rotated to match light direction.
//
// Input:
// - a_Position: Cone vertex position (local space, unit cone)
//
// Output:
// - gl_Position: Clip-space position
// - v_ScreenUV: Screen-space UV для G-Buffer sampling
//
// ============================================================================

// Vertex inputs
layout(location = 0) in vec4 a_Position;  // Cone vertex (local space)

// Outputs to fragment shader
layout(location = 0) out vec2 v_ScreenUV;

// Push constants (MVP matrix)
layout(push_constant) uniform PushConstants
{
    mat4 u_MVP;  // Model-View-Projection matrix
} pc;

void main()
{
    // Transform cone vertex to clip space
    gl_Position = pc.u_MVP * a_Position;

    // Compute screen-space UV (0..1) для G-Buffer sampling
    // Convert from clip space [-1..1] to UV space [0..1]
    v_ScreenUV = gl_Position.xy / gl_Position.w;
    v_ScreenUV = v_ScreenUV * 0.5 + 0.5;
}
