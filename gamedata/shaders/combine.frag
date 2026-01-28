#version 450

// ============================================================================
// combine.frag - Combine Pass Fragment Shader
// ============================================================================
//
// Phase 2.18.2: Combine Shader
//
// Combines accumulated lighting with albedo и применяет tone mapping.
//
// Process:
// 1. Sample rt_Accumulator (accumulated lighting from all lights)
// 2. Sample rt_Color (albedo)
// 3. Combine: finalColor = lighting * albedo + ambient
// 4. Apply tone mapping (HDR → LDR)
// 5. Gamma correction (handled automatically if swapchain is SRGB)
// 6. Output to swapchain
//
// ============================================================================

// Input from vertex shader
layout(location = 0) in vec2 v_TexCoord;

// Output to swapchain
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
} pc;

// ============================================================================
// Tone Mapping Operators
// ============================================================================

/**
 * Reinhard Tone Mapping (simple)
 *
 * Simple tone mapping operator.
 * Good for basic HDR → LDR conversion.
 * Can look washed out compared to ACES.
 */
vec3 tonemap_reinhard(vec3 color)
{
    return color / (color + 1.0);
}

/**
 * ACES Filmic Tone Mapping (recommended)
 *
 * ACES (Academy Color Encoding System) approximation.
 * Film-like response, industry standard.
 * Produces cinematic look with nice color grading.
 *
 * Source: https://knarkowicz.wordpress.com/2016/01/06/aces-filmic-tone-mapping-curve/
 */
vec3 tonemap_aces(vec3 x)
{
    float a = 2.51;
    float b = 0.03;
    float c = 2.43;
    float d = 0.59;
    float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

/**
 * Uncharted 2 Tone Mapping (filmic)
 *
 * Used in Uncharted 2 game engine.
 * Good balance between realism and artistic control.
 * Slightly more saturated than ACES.
 *
 * Source: http://filmicworlds.com/blog/filmic-tonemapping-operators/
 */
vec3 tonemap_uncharted2_partial(vec3 x)
{
    float A = 0.15;  // Shoulder strength
    float B = 0.50;  // Linear strength
    float C = 0.10;  // Linear angle
    float D = 0.20;  // Toe strength
    float E = 0.02;  // Toe numerator
    float F = 0.30;  // Toe denominator
    return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
}

vec3 tonemap_uncharted2(vec3 color)
{
    float W = 11.2;  // Linear white point value
    vec3 curr = tonemap_uncharted2_partial(color * 2.0);
    vec3 whiteScale = 1.0 / tonemap_uncharted2_partial(vec3(W));
    return curr * whiteScale;
}

// ============================================================================
// Main
// ============================================================================

void main()
{
    // ========================================================================
    // 0. Apply distortion (magnifier glass effect)
    // ========================================================================
    // Sample distortion map
    // R channel = X offset (127/255 = no offset)
    // B channel = Y offset (127/255 = no offset)
    // A channel = blur amount (not used yet)
    //
    // The magnifier texture encodes UV offsets where:
    // - Values < 127 = negative offset (shift left/up)
    // - Values > 127 = positive offset (shift right/down)
    // - Value == 127 = no offset
    //
    vec2 distortedUV = v_TexCoord;
    float blurAmount = 0.0;

    if (pc.enableDistortion != 0)
    {
        vec4 distort = texture(s_distortion, v_TexCoord);

        // Convert from [0,1] to offset: (value - 0.5) * scale
        // 127/255 ≈ 0.498 → offset ≈ 0
        vec2 offset = (distort.rb - 127.0/255.0) * pc.distortionScale;
        distortedUV = v_TexCoord + offset;

        // Clamp to valid UV range
        distortedUV = clamp(distortedUV, 0.0, 1.0);

        // Blur amount from alpha (for future soft refraction)
        blurAmount = distort.a;
    }

    // ========================================================================
    // 1. Sample textures (using potentially distorted UV)
    // ========================================================================
    vec3 lighting = texture(s_accumulator, distortedUV).rgb;  // Accumulated lighting (HDR)
    vec3 albedo = texture(s_color, distortedUV).rgb;          // Albedo (sRGB)
    vec4 material = texture(s_material, distortedUV);         // PBR properties

    // Extract material properties
    float ao = material.a;  // Ambient occlusion

    // ========================================================================
    // 2. Combine lighting and albedo
    // ========================================================================
    vec3 color = lighting * albedo;

    // ========================================================================
    // 3. Add ambient lighting
    // ========================================================================
    vec3 ambientColor = vec3(pc.ambientR, pc.ambientG, pc.ambientB);

    // When G-Buffer has geometry (albedo > 0), use albedo-modulated ambient
    // When G-Buffer is empty (no geometry), use ambient directly as background
    float hasGeometry = step(0.001, dot(albedo, vec3(1.0)));
    vec3 ambient = mix(ambientColor, albedo * ambientColor * max(ao, 0.1), hasGeometry);
    color += ambient;

    // ========================================================================
    // 4. Apply exposure
    // ========================================================================
    // Exposure control для HDR изображения
    // exposure > 1.0 - brighten image
    // exposure < 1.0 - darken image

    color *= pc.exposure;

    // ========================================================================
    // 5. Apply tone mapping (HDR → LDR)
    // ========================================================================
    // Tone mapping converts HDR (high dynamic range) to LDR (low dynamic range)
    // для отображения на обычных мониторах

    if (pc.toneMappingMode == 1) {
        // Reinhard
        color = tonemap_reinhard(color);
    } else if (pc.toneMappingMode == 2) {
        // ACES (recommended)
        color = tonemap_aces(color);
    } else if (pc.toneMappingMode == 3) {
        // Uncharted 2
        color = tonemap_uncharted2(color);
    }
    // else: No tone mapping (mode == 0)

    // ========================================================================
    // 6. Apply vignette (post-process effect)
    // ========================================================================
    // Vignette затемняет края экрана для cinematic look
    //
    // Calculate distance from center
    vec2 centerOffset = v_TexCoord - 0.5;  // Center at (0.5, 0.5)
    float dist = length(centerOffset * vec2(1.0, 1.0));  // Can adjust aspect ratio

    // Vignette falloff (smoothstep from inner to outer radius)
    float vignette = 1.0 - smoothstep(pc.vignetteInner, pc.vignetteOuter, dist);
    vignette = mix(1.0, vignette, pc.vignetteIntensity);  // Blend with intensity

    // Apply vignette
    color *= vignette;

    // ========================================================================
    // 7. Gamma correction
    // ========================================================================
    // If swapchain format is SRGB (VK_FORMAT_B8G8R8A8_SRGB), gamma correction
    // is applied automatically by hardware.
    //
    // If swapchain format is UNORM (VK_FORMAT_B8G8R8A8_UNORM), we need to
    // apply gamma correction manually:
    // color = pow(color, vec3(1.0 / 2.2));
    //
    // For now: assume SRGB swapchain (automatic gamma correction)

    // ========================================================================
    // 8. Output final color
    // ========================================================================
    o_Color = vec4(color, 1.0);
}
