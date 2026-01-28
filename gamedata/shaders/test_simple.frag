#version 450

// Упрощённый fragment shader для G-Buffer pass
// Без текстур - просто яркие цвета для визуализации

layout(location = 0) in vec3 fragColor;

// G-Buffer outputs
layout(location = 0) out vec4 outPosition;  // rt_Position
layout(location = 1) out vec4 outNormal;    // rt_Normal
layout(location = 2) out vec4 outColor;     // rt_Color (albedo)
layout(location = 3) out vec4 outMaterial;  // rt_Material (PBR properties)

void main() {
    // Position - записываем clip-space координаты
    outPosition = vec4(gl_FragCoord.xyz, 1.0);

    // Normal - направлен вверх (0, 0, 1)
    outNormal = vec4(0.0, 0.0, 1.0, 0.0);

    // Albedo color - яркие цвета (red, green, blue) для каждой вершины
    outColor = vec4(fragColor, 1.0);

    // Material properties (roughness = 0.8)
    outMaterial = vec4(0.0, 0.8, 0.0, 1.0);
}
