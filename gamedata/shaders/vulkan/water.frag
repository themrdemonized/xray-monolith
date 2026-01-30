#version 450

// Water fragment shader - SSR lookup, wind ripples, specular highlights

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragTexCoord;
layout(location = 3) in vec4 fragScreenPos;

layout(location = 0) out vec4 outColor;

// Set 0: Per-frame uniforms
layout(set = 0, binding = 0) uniform FrameUBO {
    mat4 viewProj;
    mat4 view;
    mat4 proj;
    vec4 cameraPos;
} frame;

// Set 1: Water textures
layout(set = 1, binding = 0) uniform sampler2D ssfxWater;      // SSR result
layout(set = 1, binding = 1) uniform sampler2D ssfxWaterWaves;  // Wave height/normal

// Push constants
layout(push_constant) uniform WaterParams {
    float windDir;
    float windVel;
    float time;
    float waterAlpha;
} params;

void main() {
    // Screen-space UV from clip position
    vec2 screenUV = (fragScreenPos.xy / fragScreenPos.w) * 0.5 + 0.5;
    screenUV.y = 1.0 - screenUV.y;  // Vulkan Y-flip

    // Sample wave data for normal perturbation
    vec2 waveUV = fragTexCoord * 4.0 + vec2(params.time * 0.02);
    vec4 waveData = texture(ssfxWaterWaves, fract(waveUV));
    vec3 waveNormal = vec3(waveData.rg * 2.0 - 1.0, 1.0);
    waveNormal = normalize(waveNormal);

    // Perturb screen UV by wave normal for ripple distortion
    vec2 perturbedUV = screenUV + waveNormal.xy * 0.01;
    perturbedUV = clamp(perturbedUV, 0.0, 1.0);

    // Sample SSR reflection
    vec4 ssrColor = texture(ssfxWater, perturbedUV);

    // Base water color (deep blue-green)
    vec3 waterBase = vec3(0.02, 0.06, 0.12);

    // View direction for specular
    vec3 viewDir = normalize(frame.cameraPos.xyz - fragWorldPos);
    vec3 normal = normalize(fragNormal + vec3(waveNormal.x * 0.3, 0.0, waveNormal.y * 0.3));

    // Simple sun specular (directional light approximation)
    vec3 lightDir = normalize(vec3(0.5, 0.8, 0.3));
    vec3 halfVec = normalize(lightDir + viewDir);
    float spec = pow(max(dot(normal, halfVec), 0.0), 128.0);
    vec3 specular = vec3(1.0, 0.95, 0.9) * spec * 0.5;

    // Fresnel effect (more reflection at glancing angles)
    float fresnel = pow(1.0 - max(dot(normal, viewDir), 0.0), 3.0);
    fresnel = clamp(fresnel, 0.1, 0.9);

    // Mix base water with SSR reflection
    vec3 finalColor = mix(waterBase, ssrColor.rgb, fresnel * ssrColor.a);
    finalColor += specular;

    outColor = vec4(finalColor, params.waterAlpha);
}
