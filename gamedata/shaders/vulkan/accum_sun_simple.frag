#version 450

// Simple directional light (sun) accumulation - no shadows
// Reads G-Buffer and accumulates lighting into rt_Accumulator

// ============================================================================
// Inputs
// ============================================================================

layout(location = 0) in vec2 vTexCoord;  // Screen-space UV

// ============================================================================
// G-Buffer Textures (Set 1)
// ============================================================================

layout(set = 1, binding = 0) uniform sampler2D s_position;  // Eye-space position
layout(set = 1, binding = 1) uniform sampler2D s_normal;    // Eye-space normal + hemi
layout(set = 1, binding = 2) uniform sampler2D s_diffuse;   // Albedo (sRGB)
layout(set = 1, binding = 3) uniform sampler2D s_material;  // PBR: Metallic/Roughness/SSS/AO

// ============================================================================
// Light Parameters (Push Constants)
// ============================================================================

layout(push_constant) uniform LightData
{
    vec4 Ldynamic_dir;    // Light direction (view space) + unused w
    vec4 Ldynamic_color;  // RGB + specular
} light;

// ============================================================================
// Output
// ============================================================================

layout(location = 0) out vec4 outColor;  // Accumulated light

// ============================================================================
// Main
// ============================================================================

void main()
{
    // Sample G-Buffer
    vec4 P = texture(s_position, vTexCoord);  // Eye-space position
    vec4 N = texture(s_normal, vTexCoord);    // Eye-space normal (xyz) + hemi (w)
    vec4 D = texture(s_diffuse, vTexCoord);   // Albedo (RGB) + alpha
    vec4 M = texture(s_material, vTexCoord);  // PBR material

    // Check if pixel has geometry (position.w != 0)
    if (P.w == 0.0) {
        outColor = vec4(0.0, 0.0, 0.0, 0.0);
        return;
    }

    // Normalize light direction (view space)
    vec3 L = normalize(light.Ldynamic_dir.xyz);

    // Normalize normal
    vec3 n = normalize(N.xyz);

    // Lambertian diffuse: max(0, dot(N, L))
    float NdotL = max(0.0, dot(n, L));

    // Simple diffuse lighting
    vec3 diffuse = D.rgb * light.Ldynamic_color.rgb * NdotL;

    // Simple specular (Blinn-Phong)
    vec3 V = normalize(-P.xyz);  // View direction (towards camera)
    vec3 H = normalize(L + V);    // Half vector
    float NdotH = max(0.0, dot(n, H));
    float specPower = 32.0;  // Hardcoded shininess for now
    float spec = pow(NdotH, specPower) * light.Ldynamic_color.w;  // w = specular intensity

    vec3 specular = vec3(spec, spec, spec);

    // Final color (diffuse + specular)
    vec3 finalColor = diffuse + specular;

    // Output accumulated light
    outColor = vec4(finalColor, 1.0);
}
