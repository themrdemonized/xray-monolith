#version 450

// ============================================================================
// accum_point.vert - Point Light Vertex Shader
// ============================================================================
//
// Phase 2.16.3: Point Light Accumulation
//
// Рендерит fullscreen quad для point light accumulation.
// В отличие от directional light (fullscreen quad), point lights используют
// sphere volume geometry для optimization (только pixels внутри sphere).
//
// For now: простой fullscreen quad подход (как для sun).
// TODO Phase 2.16.4: Replace with actual sphere geometry.
//
// ============================================================================

// ============================================================================
// Inputs
// ============================================================================
// Vertex attributes (fullscreen quad)
layout(location = 0) in vec3 a_Position;  // Quad vertices: {-1,-1,0}, {1,-1,0}, {-1,1,0}, {1,1,0}

// ============================================================================
// Outputs
// ============================================================================
layout(location = 0) out vec2 v_ScreenUV;  // Screen UV coordinates [0,1]

// ============================================================================
// Main
// ============================================================================
void main()
{
    // Output position (NDC space)
    gl_Position = vec4(a_Position.xy, 0.0, 1.0);

    // Convert from NDC [-1,1] to UV [0,1]
    v_ScreenUV = a_Position.xy * 0.5 + 0.5;
}
