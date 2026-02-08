#version 450

// ============================================================================
// combine.frag - Combine Pass Fragment Shader
// ============================================================================
//
// Phase 2.18.2: Combine Shader
//
// Combines accumulated lighting with albedo and outputs raw HDR.
// Tonemapping is applied later in phase_tonemap (tonemap_fs.glsl).
//
// Process:
// 1. Sample rt_Accumulator (accumulated lighting from all lights)
// 2. Sample rt_Color (albedo)
// 3. Combine: finalColor = lighting * albedo + ambient
// 4. Apply exposure
// 5. Output raw HDR to rt_HDR
//
// ============================================================================

// Input from vertex shader
layout(location = 0) in vec2 v_TexCoord;

// Output to rt_HDR (raw HDR, no tonemapping)
layout(location = 0) out vec4 o_Color;

// ============================================================================
// Descriptor Sets
// ============================================================================

// Set 1: G-Buffer textures
layout(set = 1, binding = 0) uniform sampler2D s_position;    // Eye-space position (not used in combine)
layout(set = 1, binding = 1) uniform sampler2D s_normal;      // Eye-space normal (not used in combine)
layout(set = 1, binding = 2) uniform sampler2D s_color;       // Albedo (sRGB)
layout(set = 1, binding = 3) uniform sampler2D s_material;    // PBR: metallic/roughness/SSS/AO
layout(set = 1, binding = 4) uniform sampler2D s_accumulator; // Accumulated lighting (HDR)
layout(set = 1, binding = 5) uniform sampler2D s_distortion;  // Distortion map (R=X offset, B=Y offset, A=blur)

// ============================================================================
// Push Constants
// ============================================================================

layout(push_constant) uniform PushConstants
{
    float exposure;       // HDR exposure (default: 1.0)
    float ambientR;       // Ambient light color R
    float ambientG;       // Ambient light color G
    float ambientB;       // Ambient light color B
    uint toneMappingMode; // 0=None, 1=Reinhard, 2=ACES, 3=Uncharted2
    float vignetteInner;  // Vignette inner radius (default: 0.4)
    float vignetteOuter;  // Vignette outer radius (default: 1.0)
    float vignetteIntensity; // Vignette intensity (default: 0.3)
    float distortionScale;   // Distortion strength multiplier (default: 0.08)
    uint enableDistortion;   // 1 = enable distortion, 0 = disable
    float sunDirX;        // Eye-space sun direction X
    float sunDirY;        // Eye-space sun direction Y
    float sunDirZ;        // Eye-space sun direction Z
    float sunColorR;      // Sun color R (from environment)
    float sunColorG;      // Sun color G (from environment)
    float sunColorB;      // Sun color B (from environment)
} pc;

// ============================================================================
// Main
// ============================================================================

void main()
{
    vec2 uv = v_TexCoord;

    // Sample G-Buffer textures
    vec4 posData  = texture(s_position, uv);    // binding 0
    vec4 normData = texture(s_normal, uv);      // binding 1
    vec3 albedo   = texture(s_color, uv).rgb;   // binding 2
    vec4 matData  = texture(s_material, uv);     // binding 3
    vec3 accum    = texture(s_accumulator, uv).rgb; // binding 4

    // ========================================================================
    // Deferred Shading: combine lighting with albedo
    // ========================================================================
    vec3 N = normalize(normData.rgb);
    float ao = matData.a;  // Ambient Occlusion from material

    // Ambient lighting (hemisphere approximation)
    vec3 ambient = vec3(pc.ambientR, pc.ambientG, pc.ambientB);
    // If ambient is zero (uninitialized), use a reasonable default
    if (dot(ambient, ambient) < 0.001) {
        ambient = vec3(0.15, 0.15, 0.18);  // Slight blue-ish ambient
    }

    // Directional sun lighting from environment system (eye-space)
    vec3 sunDir = normalize(vec3(pc.sunDirX, pc.sunDirY, pc.sunDirZ));
    float sunNdotL = max(dot(N, sunDir), 0.0);
    vec3 sunColor = vec3(pc.sunColorR, pc.sunColorG, pc.sunColorB);
    vec3 directLight = sunColor * sunNdotL;

    // Combine: (ambient + sun + accumulated) * albedo
    vec3 lighting = ambient * ao + directLight + accum;
    vec3 color = albedo * lighting;

    // ========================================================================
    // 4. Apply exposure
    // ========================================================================
    // Exposure control для HDR изображения
    // exposure > 1.0 - brighten image
    // exposure < 1.0 - darken image

    color *= pc.exposure;

    // ========================================================================
    // 5. Output raw HDR (tonemapping applied later in phase_tonemap)
    // ========================================================================
    o_Color = vec4(color, 1.0);
}
