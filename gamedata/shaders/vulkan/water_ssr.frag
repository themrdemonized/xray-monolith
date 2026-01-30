#version 450

// Water SSR fragment shader - screen-space ray marching for water reflections

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragTexCoord;
layout(location = 3) in vec4 fragClipPos;
layout(location = 4) in vec4 fragPrevClipPos;

layout(location = 0) out vec4 outSSR;

// Set 1: G-Buffer textures for SSR ray marching
layout(set = 1, binding = 0) uniform sampler2D gbufferPosition;
layout(set = 1, binding = 1) uniform sampler2D gbufferNormal;
layout(set = 1, binding = 2) uniform sampler2D gbufferColor;
layout(set = 1, binding = 3) uniform sampler2D gbufferDepth;

// Push constants
layout(push_constant) uniform WaterSSRParams {
    vec4 camPos;
    mat4 vpCurrent;
    mat4 vpPrevious;
} params;

// Screen-space ray march
vec4 ssrRayMarch(vec3 rayOrigin, vec3 rayDir, mat4 vp) {
    const int MAX_STEPS = 32;
    const float STEP_SIZE = 0.1;
    const float MAX_DIST = 20.0;

    vec3 currentPos = rayOrigin;

    for (int i = 0; i < MAX_STEPS; i++) {
        currentPos += rayDir * STEP_SIZE * (1.0 + float(i) * 0.2);

        // Project to screen space
        vec4 projected = vp * vec4(currentPos, 1.0);
        vec2 screenUV = (projected.xy / projected.w) * 0.5 + 0.5;
        screenUV.y = 1.0 - screenUV.y;

        // Check bounds
        if (screenUV.x < 0.0 || screenUV.x > 1.0 || screenUV.y < 0.0 || screenUV.y > 1.0)
            break;

        // Sample depth from G-Buffer
        float sceneDepth = texture(gbufferDepth, screenUV).r;
        float rayDepth = projected.z / projected.w;

        // Check intersection
        if (rayDepth > sceneDepth && rayDepth - sceneDepth < 0.01) {
            // Hit - sample color from scene
            vec3 hitColor = texture(gbufferColor, screenUV).rgb;
            float confidence = 1.0 - float(i) / float(MAX_STEPS);
            return vec4(hitColor, confidence);
        }

        float dist = distance(currentPos, rayOrigin);
        if (dist > MAX_DIST) break;
    }

    // No hit - return sky approximation
    return vec4(0.4, 0.5, 0.7, 0.0);
}

void main() {
    vec3 normal = normalize(fragNormal);
    vec3 viewDir = normalize(params.camPos.xyz - fragWorldPos);

    // Reflect view direction around water normal
    vec3 reflDir = reflect(-viewDir, normal);

    // Perform SSR ray march
    outSSR = ssrRayMarch(fragWorldPos, reflDir, params.vpCurrent);
}
