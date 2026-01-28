#version 450

// Простой тестовый fragment shader для G-Buffer pass

layout(location = 0) in vec3 fragPosition;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in vec3 fragNormal;

// G-Buffer outputs
layout(location = 0) out vec4 outPosition;  // rt_Position
layout(location = 1) out vec4 outNormal;    // rt_Normal
layout(location = 2) out vec4 outColor;     // rt_Color (albedo)
layout(location = 3) out vec4 outMaterial;  // rt_Material (PBR properties)

layout(set = 1, binding = 0) uniform sampler2D albedoMap;

void main() {
    // Position (eye-space)
    outPosition = vec4(fragPosition, 1.0);

    // Normal (eye-space, normalized)
    outNormal = vec4(normalize(fragNormal), 0.0);

    // Albedo color (sRGB)
    outColor = texture(albedoMap, fragTexCoord);

    // Material properties (placeholder)
    // R: Metallic, G: Roughness, B: SSS, A: AO
    outMaterial = vec4(0.0, 0.8, 0.0, 1.0);
}
