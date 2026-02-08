#version 450

// ============================================================================
// tonemap_vs.glsl - Tonemap Pass Vertex Shader (Fullscreen Triangle)
// ============================================================================
//
// Generates a fullscreen triangle using gl_VertexIndex (no vertex buffer).
// Same as combine.vert — reused for the tonemap pass.
//
// ============================================================================

layout(location = 0) out vec2 v_TexCoord;

void main()
{
    vec2 uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(uv * 2.0 - 1.0, 0.0, 1.0);
    v_TexCoord = uv;
}
