// ============================================================================
// common_tbn.h - TBN Matrix and Normal Mapping Utilities
// ============================================================================
//
// Phase 1.1: Normal Mapping
//
// Provides functions for tangent-space normal mapping:
// - TBN matrix calculation from derivatives
// - Normal map sampling and unpacking
// - Normal transformation to eye-space
//
// ============================================================================

#ifndef COMMON_TBN_H
#define COMMON_TBN_H

// ============================================================================
// Normal Map Unpacking
// ============================================================================

/**
 * Unpack normal from texture (RGB [0,1] → XYZ [-1,1])
 *
 * Ported from R3 common_functions.h:
 *   float3 unpack_normal(float3 v) { return 2.0*v - 1.0; }
 *
 * Normal maps store normals in tangent space with:
 * - R channel = Tangent X (left/right)
 * - G channel = Tangent Y (up/down)
 * - B channel = Tangent Z (forward, usually ~1.0 for surface-aligned normals)
 */
vec3 unpack_normal(vec3 normalSample)
{
    return normalSample * 2.0 - 1.0;
}

/**
 * Unpack bx2 format (alias for unpack_normal)
 *
 * Ported from R3 common_functions.h:
 *   float3 unpack_bx2(float3 v) { return 2.0*v - 1.0; }
 *
 * Identical to unpack_normal(), provided for R3 compatibility.
 * Some shaders use bx2 naming convention for clarity.
 */
vec3 unpack_bx2(vec3 normalSample)
{
    return normalSample * 2.0 - 1.0;
}

/**
 * Alternative unpacking for high-range detail normals
 *
 * Ported from R3 common_functions.h:
 *   float3 unpack_bx4(float3 v) { return 4.0*v - 2.0; }
 *
 * Use for detail normal maps that need more range.
 * Provides extended range [-2..2] instead of [-1..1].
 *
 * Note: Reduces precision (stretching from 4*v-2) but increases range.
 * Useful for high-frequency detail normals.
 */
vec3 unpack_bx4(vec3 normalSample)
{
    return normalSample * 4.0 - 2.0;
}

// ============================================================================
// TBN Matrix Calculation (from derivatives)
// ============================================================================

/**
 * Calculate TBN matrix using screen-space derivatives
 *
 * This method calculates tangent and bitangent from position and UV
 * derivatives, avoiding the need for explicit tangent vertex attributes.
 *
 * Based on:
 * - http://www.thetenthplanet.de/archives/1180
 * - "Followup: Normal Mapping Without Precomputed Tangents" by Christian Schüler
 *
 * @param N         Surface normal (eye-space, normalized)
 * @param posEye    Fragment position (eye-space)
 * @param uv        Texture coordinates
 * @return          TBN matrix (tangent-space → eye-space)
 */
mat3 calculate_tbn(vec3 N, vec3 posEye, vec2 uv)
{
    // Calculate edge vectors (position derivatives)
    vec3 dp1 = dFdx(posEye);
    vec3 dp2 = dFdy(posEye);

    // Calculate UV derivatives
    vec2 duv1 = dFdx(uv);
    vec2 duv2 = dFdy(uv);

    // Solve the linear system:
    // dp1 = duv1.x * T + duv1.y * B
    // dp2 = duv2.x * T + duv2.y * B
    //
    // Solution (using cross products):
    vec3 dp2perp = cross(dp2, N);
    vec3 dp1perp = cross(N, dp1);

    vec3 T = dp2perp * duv1.x + dp1perp * duv2.x;
    vec3 B = dp2perp * duv1.y + dp1perp * duv2.y;

    // Construct TBN matrix (orthonormalize)
    float invmax = inversesqrt(max(dot(T, T), dot(B, B)));

    return mat3(
        T * invmax,  // Tangent
        B * invmax,  // Bitangent
        N            // Normal
    );
}

/**
 * Alternative TBN calculation with explicit tangent/bitangent
 *
 * Use this if vertex data includes tangent and bitangent attributes.
 * This is more accurate than derivative-based calculation but requires
 * more vertex data.
 *
 * @param N         Surface normal (eye-space, normalized)
 * @param T         Tangent vector (eye-space, normalized)
 * @param B         Bitangent vector (eye-space, normalized)
 * @return          TBN matrix (tangent-space → eye-space)
 */
mat3 calculate_tbn_explicit(vec3 N, vec3 T, vec3 B)
{
    // Gram-Schmidt orthogonalization
    T = normalize(T - dot(T, N) * N);
    B = cross(N, T);

    return mat3(T, B, N);
}

// ============================================================================
// Normal Map Application
// ============================================================================

/**
 * Apply normal map to surface
 *
 * Samples normal map texture, unpacks it, and transforms from tangent-space
 * to eye-space using TBN matrix.
 *
 * @param normalMap Normal map texture (RGB = tangent-space normal)
 * @param uv        Texture coordinates
 * @param TBN       TBN matrix (tangent-space → eye-space)
 * @return          Eye-space normal (normalized)
 */
vec3 apply_normal_map(sampler2D normalMap, vec2 uv, mat3 TBN)
{
    // Sample normal map (RGB in [0, 1])
    vec3 normalSample = texture(normalMap, uv).rgb;

    // Unpack to tangent-space normal ([-1, 1])
    vec3 normalTangent = unpack_normal(normalSample);

    // Transform from tangent-space to eye-space
    vec3 normalEye = TBN * normalTangent;

    // Normalize (important after matrix multiplication)
    return normalize(normalEye);
}

/**
 * Apply normal map with detail normal blending
 *
 * Blends base normal map with detail normal map for enhanced surface detail.
 * Commonly used for close-up surfaces (walls, ground).
 *
 * @param normalMap       Base normal map
 * @param detailMap       Detail normal map (high-frequency details)
 * @param uv              Texture coordinates (for base)
 * @param uvDetail        Detail texture coordinates (usually tiled)
 * @param TBN             TBN matrix
 * @param detailStrength  Detail blend strength (0.0 = no detail, 1.0 = full)
 * @return                Blended eye-space normal
 */
vec3 apply_normal_map_with_detail(
    sampler2D normalMap,
    sampler2D detailMap,
    vec2 uv,
    vec2 uvDetail,
    mat3 TBN,
    float detailStrength)
{
    // Sample base normal
    vec3 normalBase = unpack_normal(texture(normalMap, uv).rgb);

    // Sample detail normal
    vec3 normalDetail = unpack_normal(texture(detailMap, uvDetail).rgb);

    // Blend normals (Reoriented Normal Mapping - UDN blending)
    // This preserves detail better than simple lerp
    vec3 normalBlended = vec3(
        normalBase.xy + normalDetail.xy * detailStrength,
        normalBase.z
    );

    // Transform to eye-space and normalize
    return normalize(TBN * normalBlended);
}

// ============================================================================
// Utility Functions
// ============================================================================

/**
 * Check if normal map is default (flat surface)
 *
 * Default normal maps are usually (0.5, 0.5, 1.0) in RGB,
 * which unpacks to (0, 0, 1) in tangent space (surface-aligned normal).
 *
 * @param normalSample  Normal map sample (RGB [0,1])
 * @param threshold     Difference threshold (default: 0.01)
 * @return              true if normal map is NOT default (has actual data)
 */
bool has_normal_map_data(vec3 normalSample, float threshold)
{
    const vec3 DEFAULT_NORMAL = vec3(0.5, 0.5, 1.0);
    return length(normalSample - DEFAULT_NORMAL) > threshold;
}

/**
 * Flip normal map Y channel (for different normal map conventions)
 *
 * Some engines use Y+ = down, others use Y+ = up.
 * This function flips the Y channel if needed.
 *
 * @param normalSample  Normal map sample (RGB [0,1])
 * @return              Flipped normal sample
 */
vec3 flip_normal_y(vec3 normalSample)
{
    return vec3(normalSample.r, 1.0 - normalSample.g, normalSample.b);
}

// ============================================================================
// Advanced: Parallax Offset Calculation
// ============================================================================

/**
 * Calculate UV offset for parallax mapping
 *
 * Simple parallax mapping (height-based UV offset).
 * For full parallax occlusion mapping, see common_parallax.h.
 *
 * @param heightMap      Height map texture (R channel = height)
 * @param uv             Base texture coordinates
 * @param viewDirTangent View direction in tangent space
 * @param heightScale    Parallax height scale (default: 0.05)
 * @return               Offset UV coordinates
 */
vec2 parallax_offset_simple(
    sampler2D heightMap,
    vec2 uv,
    vec3 viewDirTangent,
    float heightScale)
{
    // Sample height (usually stored in alpha channel of normal map)
    float height = texture(heightMap, uv).a;

    // Calculate offset (simple parallax)
    vec2 offset = viewDirTangent.xy * (height * heightScale);

    return uv - offset;
}

#endif // COMMON_TBN_H
