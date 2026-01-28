#version 450

// ============================================================================
// shadow_depth.frag - Shadow Map Depth-Only Fragment Shader
// ============================================================================
//
// Для depth-only рендеринга fragment shader вообще не нужен,
// но для совместимости с pipeline создаём минимальный.
//
// Depth записывается автоматически hardware rasterizer'ом,
// никаких вычислений в fragment shader не производится.
//
// ============================================================================

void main() {
    // Empty - depth is written automatically by hardware
    // No color output needed for depth-only rendering
}
