#version 450

// Простой тестовый vertex shader для проверки загрузки SPIR-V

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inTexCoord;
layout(location = 2) in vec3 inNormal;

layout(set = 0, binding = 0) uniform PerFrame {
    mat4 viewProj;
    vec4 cameraPos;
} frame;

layout(set = 2, binding = 0) uniform PerObject {
    mat4 world;
} object;

layout(location = 0) out vec3 fragPosition;
layout(location = 1) out vec2 fragTexCoord;
layout(location = 2) out vec3 fragNormal;

void main() {
    vec4 worldPos = object.world * vec4(inPosition, 1.0);
    gl_Position = frame.viewProj * worldPos;
    fragPosition = worldPos.xyz;
    fragTexCoord = inTexCoord;
    fragNormal = mat3(object.world) * inNormal;
}
