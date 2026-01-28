#version 450

// ============================================================================
// accum_sun_cascades.frag - Directional Light with Cascade Shadow Maps
// ============================================================================
//
// Читает G-Buffer и shadow map, вычисляет освещение от солнца с тенями.
// Использует 3-cascade shadow maps: NEAR/MIDDLE/FAR
//
// ============================================================================

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
// Shadow Map Textures (Set 3 - Lighting)
// ============================================================================

layout(set = 3, binding = 0) uniform sampler2DShadow s_smap;  // Shadow map depth (D32_SFLOAT)

// ============================================================================
// Light Parameters (Uniform Buffer)
// ============================================================================

layout(set = 3, binding = 1) uniform SunData
{
    vec4 Ldynamic_dir;      // Light direction (view space) + unused w
    vec4 Ldynamic_color;    // RGB + specular intensity

    // Shadow cascade matrices (light view-projection)
    mat4 m_shadow_near;     // Cascade 0: NEAR (0..20m)
    mat4 m_shadow_middle;   // Cascade 1: MIDDLE (20..40m)
    mat4 m_shadow_far;      // Cascade 2: FAR (40..160m)

    // Cascade split distances (in view space depth)
    vec4 cascade_splits;    // x=near, y=middle, z=far, w=unused
} sun;

// ============================================================================
// Output
// ============================================================================

layout(location = 0) out vec4 outColor;  // Accumulated light

// ============================================================================
// Shadow Sampling Functions
// ============================================================================

// Sample shadow map с базовым PCF filtering (2x2)
float SampleShadowPCF(vec3 shadowCoord, float bias)
{
    // Hardware PCF (используем sampler2DShadow)
    // shadowCoord.z содержит depth для сравнения
    float shadow = texture(s_smap, shadowCoord);

    // TODO Phase 2.15.5: Implement PCF filtering
    // Для более качественных теней можно добавить:
    // - 4x4 или 8x8 PCF kernel
    // - Poisson disk sampling
    // - Jittered sampling

    return shadow;
}

// Выбор каскада и shadow matrix на основе view-space depth
int SelectCascade(float viewDepth, out mat4 shadowMatrix)
{
    // View depth = расстояние от камеры (abs(P.z) в view space)
    float depth = abs(viewDepth);

    // Cascade selection based on distance
    if (depth < sun.cascade_splits.x) {
        // NEAR cascade (0..20m)
        shadowMatrix = sun.m_shadow_near;
        return 0;
    } else if (depth < sun.cascade_splits.y) {
        // MIDDLE cascade (20..40m)
        shadowMatrix = sun.m_shadow_middle;
        return 1;
    } else {
        // FAR cascade (40..160m)
        shadowMatrix = sun.m_shadow_far;
        return 2;
    }
}

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

    // ========================================================================
    // Step 1: Select cascade and compute shadow coordinate
    // ========================================================================

    mat4 shadowMatrix;
    int cascadeIndex = SelectCascade(P.z, shadowMatrix);

    // Transform position to light clip space
    vec4 shadowPos = shadowMatrix * vec4(P.xyz, 1.0);

    // Perspective divide (для ortho projection это no-op, но для spot lights нужно)
    shadowPos.xyz /= shadowPos.w;

    // Transform from [-1,1] clip space to [0,1] texture space
    vec3 shadowCoord;
    shadowCoord.xy = shadowPos.xy * 0.5 + 0.5;
    shadowCoord.z = shadowPos.z;

    // ========================================================================
    // Step 2: Sample shadow map
    // ========================================================================

    float bias = 0.0005;  // Small depth bias to avoid shadow acne
    float shadowFactor = SampleShadowPCF(shadowCoord, bias);

    // Clamp to [0, 1] to handle out-of-bounds samples
    // (fragments outside shadow map should be fully lit)
    if (shadowCoord.x < 0.0 || shadowCoord.x > 1.0 ||
        shadowCoord.y < 0.0 || shadowCoord.y > 1.0 ||
        shadowCoord.z < 0.0 || shadowCoord.z > 1.0) {
        shadowFactor = 1.0;  // Outside shadow map = fully lit
    }

    // ========================================================================
    // Step 3: Compute lighting
    // ========================================================================

    // Normalize light direction (view space)
    vec3 L = normalize(sun.Ldynamic_dir.xyz);

    // Normalize normal
    vec3 n = normalize(N.xyz);

    // Lambertian diffuse: max(0, dot(N, L))
    float NdotL = max(0.0, dot(n, L));

    // Diffuse lighting с shadow modulation
    vec3 diffuse = D.rgb * sun.Ldynamic_color.rgb * NdotL * shadowFactor;

    // Specular (Blinn-Phong) с shadow modulation
    vec3 V = normalize(-P.xyz);  // View direction (towards camera)
    vec3 H = normalize(L + V);    // Half vector
    float NdotH = max(0.0, dot(n, H));
    float specPower = 32.0;  // Hardcoded shininess for now
    float spec = pow(NdotH, specPower) * sun.Ldynamic_color.w * shadowFactor;

    vec3 specular = vec3(spec, spec, spec);

    // Final color (diffuse + specular)
    vec3 finalColor = diffuse + specular;

    // ========================================================================
    // DEBUG: Visualize cascade selection
    // ========================================================================
    // Uncomment to see cascade splits
    /*
    if (cascadeIndex == 0) finalColor *= vec3(1.0, 0.5, 0.5);  // Red tint = NEAR
    if (cascadeIndex == 1) finalColor *= vec3(0.5, 1.0, 0.5);  // Green tint = MIDDLE
    if (cascadeIndex == 2) finalColor *= vec3(0.5, 0.5, 1.0);  // Blue tint = FAR
    */

    // Output accumulated light
    outColor = vec4(finalColor, 1.0);
}
