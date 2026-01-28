#version 450

// Упрощённый vertex shader для первого треугольника
// Без трансформаций - треугольник в clip-space coordinates

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inTexCoord;
layout(location = 2) in vec3 inNormal;

layout(location = 0) out vec3 fragColor;

void main() {
    // Прямо используем входные координаты как clip-space
    gl_Position = vec4(inPosition, 1.0);

    // Простой цвет - red, green, blue для вершин 0, 1, 2
    fragColor = vec3(
        float(gl_VertexIndex == 0),
        float(gl_VertexIndex == 1),
        float(gl_VertexIndex == 2)
    );
}
