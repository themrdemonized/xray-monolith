#version 450

// ============================================================================
// accum_point.frag - Point Light Fragment Shader
// ============================================================================
//
// Phase 2.16.3: Point Light Accumulation with Shadow Cubes
//
// Вычисляет освещение от point light с omnidirectional shadows.
//
// Features:
// - Samples G-Buffer (position, normal, material)
// - Samples shadow cubemap (samplerCube)
// - Computes distance attenuation (1.0 / (dist * dist))
// - PCF filtering для soft shadows
// - Phong lighting model (diffuse + specular)
//
// ============================================================================

// ============================================================================
// Inputs
// ============================================================================
layout(location = 0) in vec2 v_ScreenUV;  // Screen UV coordinates [0,1]

// ============================================================================
// Outputs
// ============================================================================
layout(location = 0) out vec4 o_Color;  // Output to rt_Accumulator (RGB = lighting, A = specular)

// ============================================================================
// Set 1: G-Buffer Textures
// ============================================================================
layout(set = 1, binding = 0) uniform sampler2D s_position;  // Eye-space position
layout(set = 1, binding = 1) uniform sampler2D s_normal;    // Eye-space normal + hemi
layout(set = 1, binding = 2) uniform sampler2D s_color;     // Albedo (diffuse color)
layout(set = 1, binding = 3) uniform sampler2D s_material;  // PBR: metallic/roughness/SSS/AO

// ============================================================================
// Set 3: Point Light Data
// ============================================================================

// Shadow cubemap (omnidirectional)
layout(set = 3, binding = 0) uniform samplerCube s_smap;

// Point light uniforms
layout(set = 3, binding = 1) uniform PointLight
{
    vec4 position;  // Eye-space position, .w = 1.0 / (range * range)
    vec4 color;     // RGB = color, A = specular intensity
} pointLight;

// ============================================================================
// Constants
// ============================================================================
const float PI = 3.14159265359;

// ============================================================================
// PCF Sampling Offsets для Soft Shadows
// ============================================================================
// 20 samples distributed around a sphere (для cubemap PCF)
const vec3 PCF_OFFSETS[20] = vec3[](
    vec3( 1,  1,  1), vec3( 1, -1,  1), vec3(-1, -1,  1), vec3(-1,  1,  1),
    vec3( 1,  1, -1), vec3( 1, -1, -1), vec3(-1, -1, -1), vec3(-1,  1, -1),
    vec3( 1,  1,  0), vec3( 1, -1,  0), vec3(-1, -1,  0), vec3(-1,  1,  0),
    vec3( 1,  0,  1), vec3(-1,  0,  1), vec3( 1,  0, -1), vec3(-1,  0, -1),
    vec3( 0,  1,  1), vec3( 0, -1,  1), vec3( 0, -1, -1), vec3( 0,  1, -1)
);

// ============================================================================
// SampleShadowCube() - Sample shadow cubemap с PCF filtering
// ============================================================================
//
// Samples shadow cubemap в указанном direction и применяет PCF filtering
// для soft shadows.
//
// Parameters:
//   direction - Direction from light to fragment (world space или eye space)
//   fragDepth - Fragment depth (distance from light)
//   bias      - Depth bias to prevent shadow acne
//
// Returns:
//   Shadow factor [0..1] (0 = fully shadowed, 1 = fully lit)
//
// ============================================================================
float SampleShadowCube(vec3 direction, float fragDepth, float bias)
{
    // ========================================================================
    // Simple shadow sampling (no PCF)
    // ========================================================================
    #if 0
    float shadowDepth = texture(s_smap, direction).r;
    return (fragDepth - bias) <= shadowDepth ? 1.0 : 0.0;
    #endif

    // ========================================================================
    // PCF filtering (20 samples)
    // ========================================================================
    float shadow = 0.0;
    float diskRadius = 0.05;  // Sampling radius (controls shadow softness)

    for (int i = 0; i < 20; i++)
    {
        // Sample offset direction
        vec3 sampleDir = direction + PCF_OFFSETS[i] * diskRadius;

        // Sample cubemap
        float shadowDepth = texture(s_smap, sampleDir).r;

        // Compare depths
        shadow += (fragDepth - bias) <= shadowDepth ? 1.0 : 0.0;
    }

    // Average shadow factor
    shadow /= 20.0;

    return shadow;
}

// ============================================================================
// ComputePointLight() - Compute point light contribution
// ============================================================================
//
// Вычисляет diffuse + specular lighting от point light.
//
// Parameters:
//   P         - Fragment position (eye-space)
//   N         - Fragment normal (eye-space, normalized)
//   albedo    - Surface albedo (diffuse color)
//   roughness - Surface roughness [0..1]
//
// Returns:
//   vec3 - Lighting contribution (RGB)
//
// ============================================================================
vec3 ComputePointLight(vec3 P, vec3 N, vec3 albedo, float roughness)
{
    // ========================================================================
    // Compute light direction and distance
    // ========================================================================
    vec3 L = pointLight.position.xyz - P;  // Vector from fragment to light
    float dist = length(L);
    L /= dist;  // Normalize

    // ========================================================================
    // Distance attenuation (inverse square law)
    // ========================================================================
    // pointLight.position.w = 1.0 / (range * range)
    float attFactor = pointLight.position.w;
    float attenuation = 1.0 / (dist * dist);
    attenuation = min(attenuation, 1.0);  // Clamp to avoid over-brightness

    // Smoothstep falloff at light range
    float rangeFactor = 1.0 - smoothstep(0.0, 1.0, sqrt(dist * dist * attFactor));
    attenuation *= rangeFactor;

    // ========================================================================
    // Shadow sampling
    // ========================================================================
    // Compute direction from light to fragment (for cubemap sampling)
    vec3 fragToLight = P - pointLight.position.xyz;

    // Sample shadow cubemap с PCF
    float bias = 0.05;  // Depth bias (adjust to prevent shadow acne)
    float shadowFactor = SampleShadowCube(fragToLight, dist, bias);

    // ========================================================================
    // Diffuse lighting (Lambertian)
    // ========================================================================
    float NdotL = max(dot(N, L), 0.0);
    vec3 diffuse = albedo * pointLight.color.rgb * NdotL;

    // ========================================================================
    // Specular lighting (Blinn-Phong)
    // ========================================================================
    vec3 V = normalize(-P);  // View direction (eye is at origin in eye-space)
    vec3 H = normalize(L + V);  // Half vector
    float NdotH = max(dot(N, H), 0.0);

    // Compute specular exponent from roughness
    float shininess = mix(256.0, 4.0, roughness);  // High shininess for smooth surfaces
    float specular = pow(NdotH, shininess) * pointLight.color.a;

    // ========================================================================
    // Combine lighting
    // ========================================================================
    vec3 lighting = (diffuse + vec3(specular)) * attenuation * shadowFactor;

    return lighting;
}

// ============================================================================
// Main
// ============================================================================
void main()
{
    // ========================================================================
    // Sample G-Buffer
    // ========================================================================
    vec4 P4 = texture(s_position, v_ScreenUV);  // Eye-space position
    vec4 N4 = texture(s_normal, v_ScreenUV);    // Eye-space normal + hemi
    vec4 D = texture(s_color, v_ScreenUV);      // Albedo (diffuse color)
    vec4 M = texture(s_material, v_ScreenUV);   // Material properties

    // Extract data
    vec3 P = P4.xyz;  // Position
    vec3 N = normalize(N4.xyz);  // Normal
    vec3 albedo = D.rgb;  // Albedo
    float roughness = M.g;  // Roughness (green channel)

    // ========================================================================
    // Check if fragment is valid (non-zero position)
    // ========================================================================
    // If position is zero, fragment is background (skip lighting)
    if (dot(P, P) < 0.001) {
        o_Color = vec4(0.0);
        return;
    }

    // ========================================================================
    // Compute point light contribution
    // ========================================================================
    vec3 lighting = ComputePointLight(P, N, albedo, roughness);

    // ========================================================================
    // Output
    // ========================================================================
    // RGB = lighting, A = specular (for later combine pass)
    o_Color = vec4(lighting, 1.0);
}
