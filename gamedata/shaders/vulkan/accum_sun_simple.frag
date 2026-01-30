#version 450

// Simple directional light (sun) accumulation with hemisphere lighting
// Reads G-Buffer and accumulates lighting into rt_Accumulator
//
// Features:
// - Directional sun lighting (Lambertian diffuse + Blinn-Phong specular)
// - Hemisphere ambient lighting (sky hemisphere)
// - Flat ambient lighting

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
// Light Parameters (Set 0)
// ============================================================================

layout(set = 0, binding = 0) uniform GlobalLighting
{
    vec4 L_hemi_color;   // Hemisphere sky color (RGB) + intensity (A)
    vec4 L_ambient;      // Flat ambient color (RGB) + unused (A)
    vec4 L_sun_color;    // Sun color (RGB) + unused (A)
    vec4 L_sun_dir_w;    // Sun direction world-space (XYZ) + unused (W)
    mat4 m_invV;         // Inverse view matrix (eye-space -> world-space)
} globals;

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
// Hemisphere Lighting Function (from common_functions.h)
// ============================================================================

// calc_model_hemi_r1() - Hemisphere lighting based on normal Y component
vec3 calc_model_hemi_r1(vec3 norm_w, vec3 hemi_color)
{
    // Upper hemisphere (norm.y > 0) is lit, lower hemisphere is dark
    return max(0.0, norm_w.y) * hemi_color;
}

// ============================================================================
// Main
// ============================================================================

void main()
{
    // ========================================================================
    // Sample G-Buffer
    // ========================================================================
    vec4 P = texture(s_position, vTexCoord);  // Eye-space position
    vec4 N = texture(s_normal, vTexCoord);    // Eye-space normal (xyz) + hemi (w)
    vec4 D = texture(s_diffuse, vTexCoord);   // Albedo (RGB) + alpha
    vec4 M = texture(s_material, vTexCoord);  // PBR material

    // Check if pixel has geometry (position.w != 0)
    if (P.w == 0.0) {
        outColor = vec4(0.0, 0.0, 0.0, 0.0);
        return;
    }

    // ========================================================================
    // Transform normal to world space for hemisphere lighting
    // ========================================================================
    vec3 n_eye = normalize(N.xyz);  // Eye-space normal
    vec3 n_world = normalize((globals.m_invV * vec4(n_eye, 0.0)).xyz);  // World-space normal

    // ========================================================================
    // Hemisphere Ambient Lighting
    // ========================================================================
    // Hemisphere lighting based on world-space normal Y component
    vec3 hemi = calc_model_hemi_r1(n_world, globals.L_hemi_color.rgb);

    // Flat ambient lighting
    vec3 ambient = globals.L_ambient.rgb;

    // Total ambient = hemisphere + flat ambient
    vec3 ambientTotal = hemi + ambient;

    // ========================================================================
    // Directional Sun Lighting
    // ========================================================================
    // Normalize light direction (view space)
    vec3 L = normalize(light.Ldynamic_dir.xyz);

    // Lambertian diffuse: max(0, dot(N, L))
    float NdotL = max(0.0, dot(n_eye, L));

    // Diffuse lighting
    vec3 diffuse = D.rgb * light.Ldynamic_color.rgb * NdotL;

    // ========================================================================
    // Specular Lighting (Blinn-Phong)
    // ========================================================================
    vec3 V = normalize(-P.xyz);  // View direction (towards camera)
    vec3 H = normalize(L + V);    // Half vector
    float NdotH = max(0.0, dot(n_eye, H));

    // Use roughness from material for specular power
    float roughness = M.g;  // Roughness in green channel
    float specPower = mix(128.0, 8.0, roughness);  // High power for smooth, low for rough
    float spec = pow(NdotH, specPower) * light.Ldynamic_color.w;  // w = specular intensity

    vec3 specular = vec3(spec, spec, spec);

    // ========================================================================
    // Combine Lighting
    // ========================================================================
    // Final color = ambient (hemi + flat) + diffuse + specular
    vec3 finalColor = D.rgb * ambientTotal + diffuse + specular;

    // Output accumulated light
    outColor = vec4(finalColor, 1.0);
}
