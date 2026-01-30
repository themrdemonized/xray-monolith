#version 450

// Water vertex shader - transforms water geometry with wave displacement

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;

layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec2 fragTexCoord;
layout(location = 3) out vec4 fragScreenPos;

// Set 0: Per-frame uniforms
layout(set = 0, binding = 0) uniform FrameUBO {
    mat4 viewProj;
    mat4 view;
    mat4 proj;
    vec4 cameraPos;
} frame;

// Push constants: wind and water parameters
layout(push_constant) uniform WaterParams {
    float windDir;
    float windVel;
    float time;
    float waterAlpha;
} params;

void main() {
    vec3 worldPos = inPosition;

    // Wave displacement along Y axis
    float waveFreq1 = 0.5;
    float waveFreq2 = 0.3;
    float waveAmp = 0.02 * params.windVel;

    float windRad = params.windDir * 3.14159265 / 180.0;
    vec2 windDir = vec2(cos(windRad), sin(windRad));

    float wave1 = sin(dot(worldPos.xz, windDir) * waveFreq1 + params.time * 2.0) * waveAmp;
    float wave2 = sin(dot(worldPos.xz, windDir.yx) * waveFreq2 + params.time * 1.5) * waveAmp * 0.5;

    worldPos.y += wave1 + wave2;

    fragWorldPos = worldPos;
    fragNormal = inNormal;
    fragTexCoord = inTexCoord;

    vec4 clipPos = frame.viewProj * vec4(worldPos, 1.0);
    fragScreenPos = clipPos;
    gl_Position = clipPos;
}
