#version 450

// ============================================================================
// shadow_depth.vert - Shadow Map Depth-Only Vertex Shader
// ============================================================================
//
// Простой depth-only шейдер для рендера shadow map.
// Получает позицию вершины и трансформирует её в light clip space.
//
// Используется для:
// - Directional light (sun) cascade shadow maps
// - Spot light shadow maps
// - Point light shadow cubes (с модификацией)
//
// ============================================================================

// Vertex attributes
layout(location = 0) in vec3 a_Position;  // Model-space position

// Uniforms (push constants или UBO)
layout(push_constant) uniform PushConstants {
    mat4 u_MVP;  // Model-View-Projection matrix (combined shadow matrix)
} pc;

void main() {
    // Transform vertex to light clip space
    gl_Position = pc.u_MVP * vec4(a_Position, 1.0);
}
