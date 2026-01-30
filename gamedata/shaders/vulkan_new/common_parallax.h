// ============================================================================
// common_parallax.h - Parallax Mapping System
// ============================================================================
//
// Phase 1.4: Parallax Occlusion Mapping
//
// Provides functions for parallax mapping:
// - Simple parallax mapping (fast, low quality)
// - Parallax occlusion mapping (POM) (slow, high quality)
// - Steep parallax mapping (medium quality/speed)
// - Relief mapping (highest quality, slowest)
//
// ============================================================================

#ifndef COMMON_PARALLAX_H
#define COMMON_PARALLAX_H

// ============================================================================
// Simple Parallax Mapping
// ============================================================================

/**
 * Simple parallax mapping (basic height offset)
 *
 * Fastest parallax method but least accurate.
 * Good for subtle height variations.
 *
 * @param heightMap        Height map texture (R channel = height)
 * @param uv               Base texture coordinates
 * @param viewDirTangent   View direction in tangent space
 * @param heightScale      Parallax height scale (default: 0.05)
 * @return                 Offset UV coordinates
 */
vec2 parallax_simple(
    sampler2D heightMap,
    vec2 uv,
    vec3 viewDirTangent,
    float heightScale)
{
    // Sample height (usually stored in alpha channel of normal map)
    float height = texture(heightMap, uv).a;

    // Calculate UV offset
    // viewDir.xy points in the direction of parallax offset
    vec2 offset = viewDirTangent.xy * (height * heightScale);

    // Return offset UV
    return uv - offset;
}

// ============================================================================
// Parallax Offset Mapping with Bias
// ============================================================================

/**
 * Parallax mapping with bias correction
 *
 * Better than simple parallax - adds bias to reduce "swimming" artifact.
 *
 * @param heightMap        Height map texture
 * @param uv               Base UV
 * @param viewDirTangent   View direction (tangent space)
 * @param heightScale      Height scale
 * @param bias             Height bias (default: 0.5)
 * @return                 Offset UV
 */
vec2 parallax_offset(
    sampler2D heightMap,
    vec2 uv,
    vec3 viewDirTangent,
    float heightScale,
    float bias)
{
    // Sample height and apply bias
    float height = texture(heightMap, uv).a - bias;

    // Calculate offset
    vec2 offset = viewDirTangent.xy * (height * heightScale);

    return uv - offset;
}

// ============================================================================
// Steep Parallax Mapping
// ============================================================================

/**
 * Steep parallax mapping (multi-step approximation)
 *
 * Better quality than simple parallax.
 * Uses multiple samples to approximate surface intersection.
 *
 * @param heightMap        Height map texture
 * @param uv               Base UV
 * @param viewDirTangent   View direction (tangent space)
 * @param heightScale      Height scale
 * @param numSteps         Number of steps (default: 8-16)
 * @return                 Offset UV
 */
vec2 parallax_steep(
    sampler2D heightMap,
    vec2 uv,
    vec3 viewDirTangent,
    float heightScale,
    int numSteps)
{
    // Calculate step size
    float stepSize = 1.0 / float(numSteps);

    // Calculate UV offset per step
    vec2 uvDelta = viewDirTangent.xy * heightScale / float(numSteps);

    // Initialize
    vec2 currentUV = uv;
    float currentHeight = 1.0;
    float heightFromMap = texture(heightMap, currentUV).a;

    // Raymarch until we hit surface
    for (int i = 0; i < numSteps && currentHeight > heightFromMap; i++)
    {
        currentUV -= uvDelta;
        currentHeight -= stepSize;
        heightFromMap = texture(heightMap, currentUV).a;
    }

    return currentUV;
}

// ============================================================================
// Parallax Occlusion Mapping (POM) - Full Implementation
// ============================================================================

/**
 * Parallax Occlusion Mapping (POM)
 *
 * High-quality parallax with binary search refinement.
 * This is the recommended method for best visual quality.
 *
 * Algorithm:
 * 1. Raymarch with fixed steps to find approximate intersection
 * 2. Interpolate between last two steps for smooth result
 *
 * Based on:
 * - "Parallax Occlusion Mapping in GLSL" by Fabio Policarpo
 * - "Detailed Shape Representation with Parallax Mapping" (GPU Gems 3)
 *
 * @param heightMap        Height map texture (R or A channel)
 * @param uv               Base texture coordinates
 * @param viewDirTangent   View direction in tangent space (normalized)
 * @param heightScale      Parallax depth scale (0.01 - 0.1)
 * @param minSteps         Min steps for flat viewing angles (default: 8)
 * @param maxSteps         Max steps for steep viewing angles (default: 32)
 * @return                 Offset UV coordinates
 */
vec2 parallax_occlusion_mapping(
    sampler2D heightMap,
    vec2 uv,
    vec3 viewDirTangent,
    float heightScale,
    int minSteps,
    int maxSteps)
{
    // ========================================================================
    // 1. Calculate number of steps based on viewing angle
    // ========================================================================
    // More steps needed for steep angles (glancing view)
    // Fewer steps for perpendicular view
    float numStepsFloat = mix(
        float(maxSteps),
        float(minSteps),
        abs(dot(vec3(0.0, 0.0, 1.0), viewDirTangent))
    );
    int numSteps = int(numStepsFloat);

    // ========================================================================
    // 2. Calculate step sizes
    // ========================================================================
    float layerDepth = 1.0 / float(numSteps);
    float currentLayerDepth = 0.0;

    // UV offset per layer
    vec2 uvDelta = viewDirTangent.xy * heightScale / float(numSteps);

    // ========================================================================
    // 3. Initialize raymarch
    // ========================================================================
    vec2 currentUV = uv;
    float currentDepthMapValue = texture(heightMap, currentUV).a;

    // ========================================================================
    // 4. Raymarch until intersection
    // ========================================================================
    // Walk along view ray until we go below the surface
    while (currentLayerDepth < currentDepthMapValue)
    {
        // Step along view direction
        currentUV -= uvDelta;

        // Sample height map at new position
        currentDepthMapValue = texture(heightMap, currentUV).a;

        // Move to next layer
        currentLayerDepth += layerDepth;
    }

    // ========================================================================
    // 5. Interpolate between last two samples for smooth result
    // ========================================================================
    // Get UV and depth before collision
    vec2 prevUV = currentUV + uvDelta;

    // Get depth after and before collision for linear interpolation
    float afterDepth = currentDepthMapValue - currentLayerDepth;
    float beforeDepth = texture(heightMap, prevUV).a - currentLayerDepth + layerDepth;

    // Interpolation weight
    float weight = afterDepth / (afterDepth - beforeDepth);

    // Interpolate UV coordinates
    vec2 finalUV = mix(currentUV, prevUV, weight);

    return finalUV;
}

// ============================================================================
// Parallax Occlusion Mapping with Self-Shadowing
// ============================================================================

/**
 * POM with self-shadowing
 *
 * Adds shadows cast by surface bumps on themselves.
 * Significantly enhances depth perception.
 *
 * @param heightMap        Height map texture
 * @param uv               Base UV
 * @param viewDirTangent   View direction (tangent space)
 * @param lightDirTangent  Light direction (tangent space)
 * @param heightScale      Height scale
 * @param minSteps         Min raymarch steps
 * @param maxSteps         Max raymarch steps
 * @param outShadow        Output: shadow factor (0.0 = shadow, 1.0 = lit)
 * @return                 Offset UV
 */
vec2 parallax_occlusion_mapping_shadowed(
    sampler2D heightMap,
    vec2 uv,
    vec3 viewDirTangent,
    vec3 lightDirTangent,
    float heightScale,
    int minSteps,
    int maxSteps,
    out float outShadow)
{
    // Get offset UV from standard POM
    vec2 offsetUV = parallax_occlusion_mapping(
        heightMap, uv, viewDirTangent,
        heightScale, minSteps, maxSteps
    );

    // ========================================================================
    // Calculate self-shadowing
    // ========================================================================

    // Sample height at offset position
    float currentHeight = texture(heightMap, offsetUV).a;

    // Cast ray from surface toward light
    const int shadowSteps = 10;
    float shadowStepSize = 1.0 / float(shadowSteps);

    vec2 shadowUV = offsetUV;
    vec2 shadowDelta = lightDirTangent.xy * heightScale / float(shadowSteps);

    float shadowLayerDepth = currentHeight + shadowStepSize;
    float shadowHeight = texture(heightMap, shadowUV).a;

    outShadow = 1.0;  // Start fully lit

    // Raymarch toward light
    for (int i = 0; i < shadowSteps; i++)
    {
        shadowUV += shadowDelta;
        shadowHeight = texture(heightMap, shadowUV).a;

        // If height map is above current layer, we're in shadow
        if (shadowHeight > shadowLayerDepth)
        {
            outShadow = 0.0;
            break;
        }

        shadowLayerDepth += shadowStepSize;
    }

    return offsetUV;
}

// ============================================================================
// Relief Mapping (Most Accurate)
// ============================================================================

/**
 * Relief mapping with binary search
 *
 * Most accurate parallax method but also slowest.
 * Uses binary search to refine intersection point.
 *
 * @param heightMap        Height map texture
 * @param uv               Base UV
 * @param viewDirTangent   View direction (tangent space)
 * @param heightScale      Height scale
 * @param linearSteps      Initial linear steps (default: 10)
 * @param binarySteps      Binary search steps (default: 5)
 * @return                 Offset UV
 */
vec2 parallax_relief_mapping(
    sampler2D heightMap,
    vec2 uv,
    vec3 viewDirTangent,
    float heightScale,
    int linearSteps,
    int binarySteps)
{
    // ========================================================================
    // Phase 1: Linear search to find approximate intersection
    // ========================================================================
    float stepSize = 1.0 / float(linearSteps);
    vec2 uvDelta = viewDirTangent.xy * heightScale / float(linearSteps);

    vec2 currentUV = uv;
    float currentHeight = 1.0;
    float heightFromMap = texture(heightMap, currentUV).a;

    // Linear search
    while (currentHeight > heightFromMap)
    {
        currentUV -= uvDelta;
        currentHeight -= stepSize;
        heightFromMap = texture(heightMap, currentUV).a;
    }

    // ========================================================================
    // Phase 2: Binary search to refine intersection
    // ========================================================================
    for (int i = 0; i < binarySteps; i++)
    {
        uvDelta *= 0.5;
        stepSize *= 0.5;

        heightFromMap = texture(heightMap, currentUV).a;

        if (currentHeight > heightFromMap)
        {
            // Go forward
            currentUV -= uvDelta;
            currentHeight -= stepSize;
        }
        else
        {
            // Go backward
            currentUV += uvDelta;
            currentHeight += stepSize;
        }
    }

    return currentUV;
}

// ============================================================================
// Utility Functions
// ============================================================================

/**
 * Calculate view direction in tangent space
 *
 * Helper function to convert eye-space view direction to tangent space.
 *
 * @param viewDirEye  View direction in eye-space
 * @param TBN         TBN matrix (tangent-space → eye-space)
 * @return            View direction in tangent space
 */
vec3 calculate_view_dir_tangent(vec3 viewDirEye, mat3 TBN)
{
    // TBN transforms tangent → eye
    // We need inverse: eye → tangent
    // For orthonormal matrix, inverse = transpose
    mat3 TBN_inv = transpose(TBN);
    return normalize(TBN_inv * viewDirEye);
}

/**
 * Check if parallax should be enabled
 *
 * Parallax is expensive, only use when:
 * - Close to camera
 * - Height map has actual data
 *
 * @param positionEye  Fragment position (eye-space)
 * @param maxDistance  Max distance for parallax (default: 10.0)
 * @return             true if parallax should be applied
 */
bool should_apply_parallax(vec3 positionEye, float maxDistance)
{
    float dist = length(positionEye);
    return dist < maxDistance;
}

// ============================================================================
// Debug Visualization
// ============================================================================

/**
 * Visualize height map (for debugging)
 *
 * Shows height as grayscale
 */
vec3 debug_visualize_height(float height)
{
    return vec3(height);
}

/**
 * Visualize parallax offset (for debugging)
 *
 * Shows UV offset magnitude as color
 */
vec3 debug_visualize_parallax_offset(vec2 baseUV, vec2 offsetUV)
{
    vec2 offset = offsetUV - baseUV;
    float magnitude = length(offset);
    return vec3(magnitude * 10.0);  // Scale for visibility
}

#endif // COMMON_PARALLAX_H
