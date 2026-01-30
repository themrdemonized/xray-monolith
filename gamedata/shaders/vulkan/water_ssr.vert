#version 450

// Water SSR vertex shader - transforms water geometry for screen-space ray marching

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;

layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec2 fragTexCoord;
layout(location = 3) out vec4 fragClipPos;
layout(location = 4) out vec4 fragPrevClipPos;

// Push constants: camera data and VP matrices
layout(push_constant) uniform WaterSSRParams {
    vec4 camPos;           // Camera position (previous frame)
    mat4 vpCurrent;        // Current view-projection matrix
    mat4 vpPrevious;       // Previous view-projection matrix
} params;

void main() {
    vec3 worldPos = inPosition;

    fragWorldPos = worldPos;
    fragNormal = inNormal;
    fragTexCoord = inTexCoord;

    fragClipPos = params.vpCurrent * vec4(worldPos, 1.0);
    fragPrevClipPos = params.vpPrevious * vec4(worldPos, 1.0);

    gl_Position = fragClipPos;
}
