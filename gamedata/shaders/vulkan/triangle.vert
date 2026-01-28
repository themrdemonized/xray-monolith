#version 450

// Простейший vertex shader для треугольника
// Координаты в NDC (Normalized Device Coordinates) - без трансформаций

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;

layout(location = 0) out vec3 fragColor;

void main() {
    gl_Position = vec4(inPosition, 1.0);
    fragColor = inColor;
}
