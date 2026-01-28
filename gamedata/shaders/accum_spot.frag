#version 450

// ============================================================================
// accum_spot.frag - Spot Light Accumulation Fragment Shader
// ============================================================================
//
// Phase 2.17.3: Spot Light Shaders
//
// Features:
// - G-Buffer sampling (position, normal, albedo, material)
// - Distance attenuation (inverse square + smoothstep falloff)
// - Cone attenuation (smooth falloff based on angle from light direction)
// - 2D shadow map sampling с projective texgen
// - PCF filtering (16 samples) для soft shadows
// - Phong lighting (diffuse + Blinn-Phong specular)
//
// ============================================================================

// Inputs from vertex shader
layout(location = 0) in vec2 v_ScreenUV;

// Output
layout(location = 0) out vec4 o_Color;

// ============================================================================
// Descriptor Sets
// ============================================================================

// Set 1: G-Buffer textures
layout(set = 1, binding = 0) uniform sampler2D s_position;  // Eye-space position
layout(set = 1, binding = 1) uniform sampler2D s_normal;    // Eye-space normal + hemi
layout(set = 1, binding = 2) uniform sampler2D s_color;     // Albedo (sRGB)
layout(set = 1, binding = 3) uniform sampler2D s_material;  // PBR: metallic/roughness/SSS/AO

// Set 3: Spot light data
layout(set = 3, binding = 0) uniform sampler2D s_smap;      // 2D shadow map (depth)

// Spot light parameters (push constants или uniform buffer)
// For now: use push constants
layout(push_constant) uniform SpotLightData
{
    mat4 u_MVP;               // Model-View-Projection (offset 0, size 64)
    vec4 u_Position;          // Eye-space position, .w = 1.0 / (range * range)
    vec4 u_Direction;         // Eye-space direction, .w = cos(inner_cone)
    vec4 u_Color;             // RGB = color, A = specular intensity
    mat4 u_ShadowMatrix;      // World → shadow map UV (for projective texgen)
    vec4 u_ConeParams;        // x = cos(outer_cone), y = 1/(cos(inner)-cos(outer)), z/w = unused
} spot;

// ============================================================================
// PCF Shadow Sampling (16 samples)
// ============================================================================

// Poisson disk sampling offsets для PCF
const vec2 PCF_OFFSETS[16] = vec2[](
    vec2(-0.94201624, -0.39906216),
    vec2(0.94558609, -0.76890725),
    vec2(-0.094184101, -0.92938870),
    vec2(0.34495938, 0.29387760),
    vec2(-0.91588581, 0.45771432),
    vec2(-0.81544232, -0.87912464),
    vec2(-0.38277543, 0.27676845),
    vec2(0.97484398, 0.75648379),
    vec2(0.44323325, -0.97511554),
    vec2(0.53742981, -0.47373420),
    vec2(-0.26496911, -0.41893023),
    vec2(0.79197514, 0.19090188),
    vec2(-0.24188840, 0.99706507),
    vec2(-0.81409955, 0.91437590),
    vec2(0.19984126, 0.78641367),
    vec2(0.14383161, -0.14100790)
);

/**
 * SampleShadowPCF() - Sample 2D shadow map с PCF filtering
 *
 * @param shadowCoord - Shadow map UV coordinates (xy = UV, z = depth)
 * @param bias - Depth bias для reducing shadow acne
 * @return Shadow factor (0 = fully shadowed, 1 = fully lit)
 */
float SampleShadowPCF(vec3 shadowCoord, float bias)
{
    float shadow = 0.0;
    float filterRadius = 0.002;  // PCF filter radius (in UV space)

    // Check if shadow coordinate is in valid range [0..1]
    if (shadowCoord.x < 0.0 || shadowCoord.x > 1.0 ||
        shadowCoord.y < 0.0 || shadowCoord.y > 1.0) {
        // Outside shadow map - no shadow
        return 1.0;
    }

    // 16-tap PCF
    for (int i = 0; i < 16; i++)
    {
        vec2 sampleUV = shadowCoord.xy + PCF_OFFSETS[i] * filterRadius;
        float shadowDepth = texture(s_smap, sampleUV).r;

        // Compare fragment depth with shadow map depth
        float fragDepth = shadowCoord.z;
        shadow += (fragDepth - bias) <= shadowDepth ? 1.0 : 0.0;
    }

    return shadow / 16.0;
}

// ============================================================================
// Spot Light Computation
// ============================================================================

/**
 * ComputeSpotLight() - Compute spot light contribution
 *
 * Features:
 * - Distance attenuation (inverse square + smoothstep)
 * - Cone attenuation (smooth falloff from inner to outer cone)
 * - Shadow mapping (2D shadow map с PCF)
 * - Phong lighting (diffuse + specular)
 *
 * @param P - Fragment position (eye-space)
 * @param N - Fragment normal (eye-space, normalized)
 * @param albedo - Surface albedo color
 * @param roughness - Surface roughness [0..1]
 * @return Accumulated light color (RGB)
 */
vec3 ComputeSpotLight(vec3 P, vec3 N, vec3 albedo, float roughness)
{
    // ========================================================================
    // 1. Light direction and distance
    // ========================================================================
    vec3 L = spot.u_Position.xyz - P;
    float dist = length(L);
    L /= dist;  // Normalize

    // ========================================================================
    // 2. Distance attenuation (inverse square + smoothstep falloff)
    // ========================================================================
    float attFactor = spot.u_Position.w;  // 1.0 / (range * range)
    float attenuation = 1.0 / (dist * dist);

    // Smooth falloff at light range edge (avoid hard cutoff)
    float rangeFactor = 1.0 - smoothstep(0.0, 1.0, sqrt(dist * dist * attFactor));
    attenuation *= rangeFactor;

    // Early exit if too far
    if (rangeFactor <= 0.001) {
        return vec3(0.0);
    }

    // ========================================================================
    // 3. Cone attenuation (spotlight falloff)
    // ========================================================================
    // Compute angle between light direction and fragment direction
    float spotAngle = dot(-L, spot.u_Direction.xyz);

    // Get cone parameters
    float innerCone = spot.u_Direction.w;      // cos(inner_angle)
    float outerCone = spot.u_ConeParams.x;     // cos(outer_angle)

    // Smooth falloff from inner to outer cone
    // smoothstep(edge0, edge1, x) = 0 if x <= edge0, 1 if x >= edge1
    // Note: cos(small_angle) > cos(large_angle), so we use smoothstep(outer, inner, angle)
    float coneAttenuation = smoothstep(outerCone, innerCone, spotAngle);

    // Early exit if outside cone
    if (coneAttenuation <= 0.001) {
        return vec3(0.0);
    }

    // ========================================================================
    // 4. Shadow map lookup (projective texgen)
    // ========================================================================
    // Transform fragment position to shadow map space
    // u_ShadowMatrix converts from eye-space to shadow UV space
    vec4 shadowCoord4 = spot.u_ShadowMatrix * vec4(P, 1.0);
    shadowCoord4.xyz /= shadowCoord4.w;  // Perspective divide

    // Bias для reducing shadow acne
    float bias = 0.005;

    // Sample shadow map с PCF
    float shadowFactor = SampleShadowPCF(shadowCoord4.xyz, bias);

    // Early exit if fully shadowed
    if (shadowFactor <= 0.001) {
        return vec3(0.0);
    }

    // ========================================================================
    // 5. Diffuse lighting (Lambertian)
    // ========================================================================
    float NdotL = max(dot(N, L), 0.0);
    vec3 diffuse = albedo * spot.u_Color.rgb * NdotL;

    // ========================================================================
    // 6. Specular lighting (Blinn-Phong)
    // ========================================================================
    vec3 V = normalize(-P);  // View direction (camera at origin in eye-space)
    vec3 H = normalize(L + V);  // Half-vector
    float NdotH = max(dot(N, H), 0.0);

    // Roughness → shininess mapping
    // roughness = 0 (smooth) → shininess = 256
    // roughness = 1 (rough)  → shininess = 4
    float shininess = mix(256.0, 4.0, roughness);
    float specular = pow(NdotH, shininess) * spot.u_Color.a;

    // ========================================================================
    // 7. Combine all terms
    // ========================================================================
    vec3 lighting = (diffuse + vec3(specular)) * attenuation * coneAttenuation * shadowFactor;

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
    vec4 P4 = texture(s_position, v_ScreenUV);
    vec4 N4 = texture(s_normal, v_ScreenUV);
    vec4 D = texture(s_color, v_ScreenUV);
    vec4 M = texture(s_material, v_ScreenUV);

    vec3 P = P4.xyz;            // Eye-space position
    vec3 N = normalize(N4.xyz); // Eye-space normal
    vec3 albedo = D.rgb;        // Albedo color
    float roughness = M.g;      // Roughness (PBR G channel)

    // ========================================================================
    // Early exit: Check if valid geometry (position != 0)
    // ========================================================================
    if (dot(P, P) < 0.001) {
        // Background pixel (no geometry)
        o_Color = vec4(0.0);
        return;
    }

    // ========================================================================
    // Compute spot light contribution
    // ========================================================================
    vec3 lighting = ComputeSpotLight(P, N, albedo, roughness);

    // ========================================================================
    // Output accumulated light
    // ========================================================================
    o_Color = vec4(lighting, 1.0);
}
