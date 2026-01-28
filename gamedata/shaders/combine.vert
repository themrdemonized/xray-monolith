#version 450

// ============================================================================
// combine.vert - Fullscreen Triangle Vertex Shader
// ============================================================================
//
// Phase 2.18.2: Combine Shader
//
// Generates a fullscreen triangle using gl_VertexIndex (no vertex buffer).
//
// Fullscreen Triangle Trick:
// - Vertex 0: (-1, -1) UV (0, 0) - Bottom-left
// - Vertex 1: ( 3, -1) UV (2, 0) - Off-screen right
// - Vertex 2: (-1,  3) UV (0, 2) - Off-screen top
//
// The large triangle covers the entire screen (and beyond).
// Rasterizer clips to viewport automatically.
//
// Benefits:
// - No vertex buffer needed
// - No index buffer needed
// - Single draw call: vkCmdDraw(cmd, 3, 1, 0, 0)
// - Slightly faster than quad (3 vs 6 vertices)
//
// ============================================================================

// Output to fragment shader
layout(location = 0) out vec2 v_TexCoord;

void main()
{
    // Generate fullscreen triangle from vertex index
    // gl_VertexIndex: 0, 1, 2
    //
    // UV calculation:
    // - Index 0: uv = (0, 0) → pos = (-1, -1)
    // - Index 1: uv = (2, 0) → pos = ( 3, -1)
    // - Index 2: uv = (0, 2) → pos = (-1,  3)

    vec2 uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(uv * 2.0 - 1.0, 0.0, 1.0);
    v_TexCoord = uv;
}
