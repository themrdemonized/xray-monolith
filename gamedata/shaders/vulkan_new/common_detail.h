// ============================================================================
// common_detail.h - Detail Texture System
// ============================================================================
//
// Phase 1.2: Detail Textures
//
// Provides functions for detail texture blending:
// - Distance-based detail fade
// - Detail texture overlay blending
// - Detail normal blending
// - Multi-scale detail (close-up quality enhancement)
//
// ============================================================================

#ifndef COMMON_DETAIL_H
#define COMMON_DETAIL_H

// ============================================================================
// Distance-Based Detail Blending
// ============================================================================

/**
 * Calculate detail texture blend factor based on distance
 *
 * Detail textures are only visible at close range to enhance surface quality.
 * At far distances, they fade out to avoid visual noise.
 *
 * @param positionEye  Fragment position in eye-space
 * @param detailStart  Distance where detail starts fading (default: 5.0)
 * @param detailEnd    Distance where detail fully fades (default: 20.0)
 * @return             Blend factor (1.0 = full detail, 0.0 = no detail)
 */
float calculate_detail_blend(vec3 positionEye, float detailStart, float detailEnd)
{
    // Calculate distance from camera
    float dist = length(positionEye);

    // Smooth fade from start to end distance
    // 1.0 - smoothstep because we want detail at close range
    return 1.0 - smoothstep(detailStart, detailEnd, dist);
}

/**
 * Calculate detail blend with custom curve
 *
 * Allows different fade curves for different detail types.
 *
 * @param positionEye  Fragment position in eye-space
 * @param detailStart  Start distance
 * @param detailEnd    End distance
 * @param power        Fade curve power (1.0 = linear, 2.0 = quadratic)
 * @return             Blend factor
 */
float calculate_detail_blend_power(
    vec3 positionEye,
    float detailStart,
    float detailEnd,
    float power)
{
    float dist = length(positionEye);
    float t = clamp((dist - detailStart) / (detailEnd - detailStart), 0.0, 1.0);
    return 1.0 - pow(t, power);
}

// ============================================================================
// Detail Texture Overlay Blending
// ============================================================================

/**
 * Apply detail texture using overlay blend mode
 *
 * Overlay blend mode:
 * - Detail < 0.5: Darkens base color
 * - Detail > 0.5: Lightens base color
 * - Detail = 0.5: No change
 *
 * This preserves base color while adding high-frequency detail.
 *
 * @param baseColor       Base diffuse color
 * @param detailSample    Detail texture sample
 * @param blendFactor     Detail strength (0.0 = no detail, 1.0 = full)
 * @return                Blended color
 */
vec3 apply_detail_overlay(vec3 baseColor, vec3 detailSample, float blendFactor)
{
    // Convert detail to overlay blend (centered around 0.5)
    // detail < 0.5 = darken, detail > 0.5 = lighten
    vec3 overlay = (detailSample - 0.5) * 2.0;

    // Apply overlay with strength control
    // 0.2 = strength multiplier (prevents too strong detail)
    return baseColor + overlay * blendFactor * 0.2;
}

/**
 * Apply detail texture using multiply blend mode
 *
 * Multiply mode is simpler than overlay but can only darken.
 * Good for adding dirt, grime, weathering effects.
 *
 * @param baseColor       Base diffuse color
 * @param detailSample    Detail texture sample
 * @param blendFactor     Detail strength
 * @return                Blended color
 */
vec3 apply_detail_multiply(vec3 baseColor, vec3 detailSample, float blendFactor)
{
    // Lerp between base and base*detail
    vec3 multiplied = baseColor * detailSample;
    return mix(baseColor, multiplied, blendFactor);
}

/**
 * Apply detail texture using soft light blend mode
 *
 * Soft light is similar to overlay but more subtle.
 * Better for natural surfaces (stone, concrete, wood).
 *
 * @param baseColor       Base diffuse color
 * @param detailSample    Detail texture sample
 * @param blendFactor     Detail strength
 * @return                Blended color
 */
vec3 apply_detail_soft_light(vec3 baseColor, vec3 detailSample, float blendFactor)
{
    vec3 result;

    for (int i = 0; i < 3; i++)
    {
        if (detailSample[i] < 0.5)
        {
            // Darken
            result[i] = 2.0 * baseColor[i] * detailSample[i];
        }
        else
        {
            // Lighten
            result[i] = 1.0 - 2.0 * (1.0 - baseColor[i]) * (1.0 - detailSample[i]);
        }
    }

    return mix(baseColor, result, blendFactor);
}

// ============================================================================
// Complete Detail Texture Application
// ============================================================================

/**
 * Apply detail texture with distance fade
 *
 * Complete pipeline: sample → blend → fade by distance.
 *
 * @param baseColor       Base diffuse color
 * @param detailMap       Detail texture sampler
 * @param uv              Base UV coordinates
 * @param detailScale     Detail texture tiling scale (default: 10.0)
 * @param positionEye     Fragment position (eye-space)
 * @param detailStart     Detail fade start distance
 * @param detailEnd       Detail fade end distance
 * @param detailStrength  Overall detail strength (0.0-1.0)
 * @return                Color with detail applied
 */
vec3 apply_detail_texture(
    vec3 baseColor,
    sampler2D detailMap,
    vec2 uv,
    float detailScale,
    vec3 positionEye,
    float detailStart,
    float detailEnd,
    float detailStrength)
{
    // Calculate distance-based blend factor
    float distBlend = calculate_detail_blend(positionEye, detailStart, detailEnd);

    // Early exit if detail is not visible (far away)
    if (distBlend < 0.001)
        return baseColor;

    // Sample detail texture at higher frequency (tiled)
    vec3 detailSample = texture(detailMap, uv * detailScale).rgb;

    // Apply overlay blending
    vec3 detailColor = apply_detail_overlay(baseColor, detailSample, detailStrength);

    // Blend between base and detailed based on distance
    return mix(baseColor, detailColor, distBlend);
}

// ============================================================================
// Detail Normal Blending (for use with TBN)
// ============================================================================

/**
 * Blend base normal with detail normal
 *
 * Uses Reoriented Normal Mapping (UDN blending) for better results.
 * This method preserves detail better than simple lerp.
 *
 * Source: "Blending in Detail" by Colin Barré-Brisebois (Ubisoft)
 *
 * @param baseNormal      Base normal (tangent-space)
 * @param detailNormal    Detail normal (tangent-space)
 * @param blendFactor     Detail strength (0.0-1.0)
 * @return                Blended normal (tangent-space)
 */
vec3 blend_detail_normal(vec3 baseNormal, vec3 detailNormal, float blendFactor)
{
    // Reoriented Normal Mapping (UDN blending)
    // More accurate than simple lerp or add
    vec3 t = baseNormal.xyz + vec3(0.0, 0.0, 1.0);
    vec3 u = detailNormal.xyz * vec3(-1.0, -1.0, 1.0);
    vec3 r = t * dot(t, u) - u * t.z;

    // Blend between base and detailed
    return normalize(mix(baseNormal, r, blendFactor));
}

/**
 * Blend base normal with detail normal (simple method)
 *
 * Simpler than UDN but less accurate.
 * Use this for performance if UDN is too expensive.
 *
 * @param baseNormal      Base normal (tangent-space)
 * @param detailNormal    Detail normal (tangent-space)
 * @param blendFactor     Detail strength
 * @return                Blended normal (tangent-space)
 */
vec3 blend_detail_normal_simple(vec3 baseNormal, vec3 detailNormal, float blendFactor)
{
    // Simple additive blending
    vec3 blended = vec3(
        baseNormal.xy + detailNormal.xy * blendFactor,
        baseNormal.z
    );
    return normalize(blended);
}

// ============================================================================
// Multi-Layer Detail (Advanced)
// ============================================================================

/**
 * Apply two detail layers with different scales
 *
 * Layer 1: Close detail (small scale, high frequency)
 * Layer 2: Medium detail (larger scale, medium frequency)
 *
 * @param baseColor       Base diffuse color
 * @param detailMap1      Close detail texture
 * @param detailMap2      Medium detail texture
 * @param uv              Base UV
 * @param scale1          Close detail scale (e.g., 20.0)
 * @param scale2          Medium detail scale (e.g., 10.0)
 * @param positionEye     Fragment position
 * @param start1          Close detail start distance
 * @param end1            Close detail end distance
 * @param start2          Medium detail start distance
 * @param end2            Medium detail end distance
 * @param strength        Overall detail strength
 * @return                Color with multi-layer detail
 */
vec3 apply_detail_multilayer(
    vec3 baseColor,
    sampler2D detailMap1,
    sampler2D detailMap2,
    vec2 uv,
    float scale1,
    float scale2,
    vec3 positionEye,
    float start1,
    float end1,
    float start2,
    float end2,
    float strength)
{
    // Close detail (highest frequency, shortest range)
    float blend1 = calculate_detail_blend(positionEye, start1, end1);
    vec3 detail1 = texture(detailMap1, uv * scale1).rgb;
    vec3 color1 = apply_detail_overlay(baseColor, detail1, strength * blend1);

    // Medium detail (medium frequency, medium range)
    float blend2 = calculate_detail_blend(positionEye, start2, end2);
    vec3 detail2 = texture(detailMap2, uv * scale2).rgb;
    vec3 color2 = apply_detail_overlay(color1, detail2, strength * blend2);

    return color2;
}

// ============================================================================
// Utility Functions
// ============================================================================

/**
 * Check if detail texture has data (not default)
 *
 * @param detailSample  Detail texture sample
 * @return              true if detail has actual data
 */
bool has_detail_data(vec3 detailSample)
{
    // Default detail is usually neutral gray (0.5, 0.5, 0.5)
    const vec3 DEFAULT_DETAIL = vec3(0.5);
    return length(detailSample - DEFAULT_DETAIL) > 0.01;
}

/**
 * Calculate optimal detail scale based on texel density
 *
 * Helps maintain consistent detail density across surfaces.
 *
 * @param worldScale  World-space size of surface
 * @param texelSize   Detail texture resolution
 * @return            Optimal detail scale
 */
float calculate_detail_scale(float worldScale, float texelSize)
{
    // Target: ~10 detail pixels per world unit
    return worldScale / (texelSize * 10.0);
}

// ============================================================================
// Debug Visualization
// ============================================================================

/**
 * Visualize detail blend factor (for debugging)
 *
 * White = full detail, Black = no detail
 */
vec3 debug_visualize_detail_blend(float blendFactor)
{
    return vec3(blendFactor);
}

/**
 * Visualize detail texture tiling (for debugging)
 *
 * Shows UV seams and tiling pattern
 */
vec3 debug_visualize_detail_tiling(vec2 uv, float scale)
{
    vec2 tiled = fract(uv * scale);
    return vec3(tiled, 0.0);
}

#endif // COMMON_DETAIL_H
