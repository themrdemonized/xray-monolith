#ifndef COMMON_UTILS_H_INCLUDED
#define COMMON_UTILS_H_INCLUDED

#include "constants.h"  // Uniform buffer definitions

// ============================================================================
// common_utils.h - Utility Functions for Vulkan Renderer
// ============================================================================
//
// Ported from R3 common_functions.h
//
// Contains:
// - Fog calculation
// - Reflection calculation
// - Texture coordinate utilities
// - Screen-space helpers
// - Depth utilities
//
// ============================================================================

// ============================================================================
// Fog Utilities
// ============================================================================

/**
 * calc_fogging() - Calculate fog factor
 *
 * Ported from R3 common_functions.h
 *
 * Calculates fog factor based on world position and fog plane.
 * Uses planar fog equation: fog = dot(pos, plane).
 *
 * @param w_pos World position (vec4, w should be 1.0)
 * @return      Fog factor (distance-based, can be used with exp/exp2 fog)
 *
 * Requires uniform:
 *   vec4 fog_plane - Fog plane equation (xyz = normal, w = distance)
 *
 * Usage:
 *   float fogFactor = calc_fogging(worldPos);
 *   float fogAmount = exp2(-fogFactor * fogDensity);
 *   color = mix(fogColor, color, fogAmount);
 */
float calc_fogging(vec4 w_pos)
{
    return dot(w_pos, fog_plane);
}

/**
 * Apply exponential fog
 *
 * @param fogFactor Fog factor from calc_fogging()
 * @param density   Fog density
 * @return          Fog blend amount [0..1] (0 = full fog, 1 = no fog)
 */
float apply_fog_exp(float fogFactor, float density)
{
    return exp(-fogFactor * density);
}

/**
 * Apply exponential squared fog (smoother falloff)
 *
 * @param fogFactor Fog factor from calc_fogging()
 * @param density   Fog density
 * @return          Fog blend amount [0..1]
 */
float apply_fog_exp2(float fogFactor, float density)
{
    float exponent = fogFactor * density;
    return exp(-(exponent * exponent));
}

/**
 * Apply linear fog
 *
 * @param fogFactor Fog factor from calc_fogging()
 * @param fogStart  Fog start distance
 * @param fogEnd    Fog end distance
 * @return          Fog blend amount [0..1]
 */
float apply_fog_linear(float fogFactor, float fogStart, float fogEnd)
{
    return clamp((fogEnd - fogFactor) / (fogEnd - fogStart), 0.0, 1.0);
}

/**
 * Blend color with fog
 *
 * @param color     Original color
 * @param fogColor  Fog color (usually sky color)
 * @param fogAmount Fog blend factor [0..1] (from apply_fog_*)
 * @return          Fogged color
 */
vec3 blend_fog(vec3 color, vec3 fogColor, float fogAmount)
{
    return mix(fogColor, color, fogAmount);
}

// ============================================================================
// Reflection Utilities
// ============================================================================

/**
 * calc_reflection() - Calculate reflection vector
 *
 * Ported from R3 common_functions.h
 *
 * Calculates reflection vector for environment mapping/reflections.
 * Used for cubemap sampling, SSR, planar reflections.
 *
 * @param pos_w  Fragment world position
 * @param norm_w Surface normal (world space, normalized)
 * @return       Reflection vector (world space, for cubemap lookup)
 *
 * Requires uniform:
 *   vec3 eye_position - Camera world position
 *
 * Usage:
 *   vec3 refl = calc_reflection(worldPos, worldNormal);
 *   vec3 envColor = texture(envCubemap, refl).rgb;
 */
vec3 calc_reflection(vec3 pos_w, vec3 norm_w)
{
    return reflect(normalize(pos_w - eye_position), norm_w);
}

/**
 * Calculate reflection vector (eye-space version)
 *
 * Alternative version for eye-space calculations.
 *
 * @param pos_e  Fragment position (eye-space)
 * @param norm_e Surface normal (eye-space, normalized)
 * @return       Reflection vector (eye-space)
 */
vec3 calc_reflection_eye(vec3 pos_e, vec3 norm_e)
{
    // In eye-space, camera is at origin
    vec3 V = normalize(-pos_e);  // View direction
    return reflect(-V, norm_e);   // Reflection
}

/**
 * Fresnel approximation (Schlick)
 *
 * Calculate Fresnel term for reflections (view-dependent).
 *
 * @param V         View direction (normalized)
 * @param N         Surface normal (normalized)
 * @param F0        Fresnel at normal incidence (0.04 for dielectrics, higher for metals)
 * @return          Fresnel factor [F0..1]
 */
float fresnel_schlick(vec3 V, vec3 N, float F0)
{
    float cosTheta = max(dot(V, N), 0.0);
    return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}

/**
 * Fresnel approximation for RGB (colored F0)
 *
 * @param V  View direction
 * @param N  Surface normal
 * @param F0 Base reflectance (vec3 for colored metals)
 * @return   Fresnel factor (vec3)
 */
vec3 fresnel_schlick_vec3(vec3 V, vec3 N, vec3 F0)
{
    float cosTheta = max(dot(V, N), 0.0);
    return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}

// ============================================================================
// Texture Coordinate Utilities
// ============================================================================

/**
 * unpack_tc_base() - Unpack base texture coordinates
 *
 * Ported from R3 common_functions.h
 *
 * Unpacks compressed texture coordinates with detail offset.
 *
 * @param tc Base texture coordinates
 * @param du Detail U offset
 * @param dv Detail V offset
 * @return   Unpacked and offset texture coordinates
 */
vec2 unpack_tc_base(vec2 tc, float du, float dv)
{
    return (tc.xy + vec2(du, dv)) * (32.0 / 32768.0);
}

/**
 * unpack_tc_lmap() - Unpack lightmap texture coordinates
 *
 * Ported from R3 common_functions.h
 *
 * @param tc Lightmap texture coordinates
 * @return   Unpacked coordinates in range [-1..1]
 */
vec2 unpack_tc_lmap(vec2 tc)
{
    return tc * (1.0 / 32768.0);
}

/**
 * Screen-space to texture coordinates
 *
 * Convert from screen-space position to texture UV [0..1].
 *
 * @param screenPos Screen position (clip space [-1..1])
 * @return          Texture UV [0..1]
 */
vec2 screen_to_uv(vec2 screenPos)
{
    return screenPos * 0.5 + 0.5;
}

/**
 * Texture UV to screen-space
 *
 * Convert from texture UV [0..1] to screen-space [-1..1].
 *
 * @param uv Texture coordinates [0..1]
 * @return   Screen position [-1..1]
 */
vec2 uv_to_screen(vec2 uv)
{
    return uv * 2.0 - 1.0;
}

// ============================================================================
// Screen-Space Utilities
// ============================================================================

/**
 * screen_to_proj() - Convert screen UV to projection space
 *
 * Ported from R3 common_functions.h
 *
 * @param screen Screen UV [0..1]
 * @param z      Depth value
 * @return       Projection-space coordinates
 */
vec4 screen_to_proj(vec2 screen, float z)
{
    vec4 proj;
    proj.w = 1.0;
    proj.z = z;
    proj.x = screen.x * 2.0 - proj.w;
    proj.y = -screen.y * 2.0 + proj.w;
    return proj;
}

/**
 * proj_to_screen() - Convert projection to screen UV
 *
 * Ported from R3 common_functions.h
 *
 * @param proj Projection-space coordinates
 * @return     Screen UV [0..1]
 */
vec4 proj_to_screen(vec4 proj)
{
    vec4 screen = proj;
    screen.x = (proj.x + proj.w);
    screen.y = (proj.w - proj.y);
    screen.xy *= 0.5;
    return screen;
}

/**
 * convert_to_screen_space() - Convert projection to screen (alternative)
 *
 * Ported from R3 common_functions.h
 *
 * @param proj Projection-space coordinates
 * @return     Screen-space coordinates
 */
vec4 convert_to_screen_space(vec4 proj)
{
    vec4 screen;
    screen.x = (proj.x + proj.w) * 0.5;
    screen.y = (proj.w - proj.y) * 0.5;
    screen.z = proj.z;
    screen.w = proj.w;
    return screen;
}

// ============================================================================
// Depth Utilities
// ============================================================================

/**
 * normalize_depth() - Normalize depth to [0..1]
 *
 * Ported from R3 common_functions.h
 *
 * Normalizes depth from [0..100] to [0..1] for visualization.
 *
 * @param depth Raw depth value
 * @return      Normalized depth [0..1]
 */
float normalize_depth(float depth)
{
    return clamp(depth / 100.0, 0.0, 1.0);
}

/**
 * Linearize depth buffer value
 *
 * Convert non-linear depth [0..1] to linear eye-space depth.
 *
 * @param depth    Non-linear depth from depth buffer
 * @param nearPlane Near plane distance
 * @param farPlane  Far plane distance
 * @return          Linear eye-space depth
 */
float linearize_depth(float depth, float nearPlane, float farPlane)
{
    float z_n = 2.0 * depth - 1.0;  // To NDC [-1..1]
    return 2.0 * nearPlane * farPlane / (farPlane + nearPlane - z_n * (farPlane - nearPlane));
}

/**
 * is_sky() - Check if depth represents sky
 *
 * Ported from R3 common_functions.h (with SKY_EPS check)
 *
 * @param depth Depth value
 * @return      1.0 if sky, 0.0 if geometry
 */
float is_sky(float depth)
{
    const float SKY_EPS = 0.001;
    return step(depth, SKY_EPS);
}

/**
 * is_not_sky() - Check if depth represents geometry
 *
 * @param depth Depth value
 * @return      1.0 if geometry, 0.0 if sky
 */
float is_not_sky(float depth)
{
    const float SKY_EPS = 0.001;
    return step(SKY_EPS, depth);
}

// ============================================================================
// Hash and Noise Utilities
// ============================================================================

/**
 * hash() - Simple 2D hash function
 *
 * Ported from R3 common_functions.h
 *
 * @param intro Input vec2
 * @return      Pseudo-random value [0..1]
 */
float hash(vec2 intro)
{
    return fract(1.0e4 * sin(17.0 * intro.x + 0.1 * intro.y) *
                 (0.1 + abs(sin(13.0 * intro.y + intro.x))));
}

/**
 * hash3D() - 3D hash function
 *
 * Ported from R3 common_functions.h
 *
 * @param intro Input vec3
 * @return      Pseudo-random value [0..1]
 */
float hash3D(vec3 intro)
{
    return hash(vec2(hash(intro.xy), intro.z));
}

/**
 * hash12() - Improved 2D hash
 *
 * Better quality hash function (more random distribution).
 *
 * @param p Input vec2
 * @return  Pseudo-random value [0..1]
 */
float hash12(vec2 p)
{
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 19.19);
    return fract((p3.x + p3.y) * p3.z);
}

/**
 * hash22() - 2D to 2D hash
 *
 * Hash that returns vec2 output.
 *
 * @param p Input vec2
 * @return  Pseudo-random vec2 [0..1]
 */
vec2 hash22(vec2 p)
{
    vec3 p3 = fract(vec3(p.xyx) * vec3(0.1031, 0.1030, 0.0973));
    p3 += dot(p3, p3.yzx + 19.19);
    return fract((p3.xx + p3.yz) * p3.zy);
}

/**
 * rand() - Simple 1D random
 *
 * @param n Seed value
 * @return  Pseudo-random value [0..1]
 */
float rand(float n)
{
    return fract(cos(n) * 343.42);
}

/**
 * noise() - Animated noise function
 *
 * Time-varying noise for effects.
 *
 * @param tc   Texture coordinates
 * @param time Time value (e.g., from uniform)
 * @return     Noise value [0..0.25]
 */
float noise(vec2 tc, float time)
{
    return fract(sin(dot(tc, vec2(12.0, 78.0) + time)) * 43758.0) * 0.25;
}

// ============================================================================
// Blend Utilities
// ============================================================================

/**
 * blend_soft() - Soft light blend mode
 *
 * Ported from R3 common_functions.h (custom)
 *
 * Soft light blending (like Photoshop soft light).
 *
 * @param a Base color
 * @param b Blend color
 * @return  Blended color
 */
vec3 blend_soft(vec3 a, vec3 b)
{
    return 1.0 - (1.0 - a) * (1.0 - b);
}

/**
 * Overlay blend mode
 *
 * @param base Base color
 * @param blend Blend color
 * @return Overlaid color
 */
vec3 blend_overlay(vec3 base, vec3 blend)
{
    vec3 result;
    result.r = base.r < 0.5 ? 2.0 * base.r * blend.r : 1.0 - 2.0 * (1.0 - base.r) * (1.0 - blend.r);
    result.g = base.g < 0.5 ? 2.0 * base.g * blend.g : 1.0 - 2.0 * (1.0 - base.g) * (1.0 - blend.g);
    result.b = base.b < 0.5 ? 2.0 * base.b * blend.b : 1.0 - 2.0 * (1.0 - base.b) * (1.0 - blend.b);
    return result;
}

// ============================================================================
// Reconstruction Utilities (for deferred rendering)
// ============================================================================

/**
 * Reconstruct world position from depth
 *
 * @param uv          Screen UV [0..1]
 * @param depth       Non-linear depth [0..1]
 * @param invViewProj Inverse view-projection matrix
 * @return            World position
 */
vec3 reconstruct_world_pos(vec2 uv, float depth, mat4 invViewProj)
{
    // Screen to NDC
    vec4 ndc;
    ndc.xy = uv * 2.0 - 1.0;
    ndc.z = depth;
    ndc.w = 1.0;

    // NDC to world
    vec4 worldPos = invViewProj * ndc;
    worldPos /= worldPos.w;  // Perspective divide

    return worldPos.xyz;
}

/**
 * Reconstruct eye-space position from depth
 *
 * @param uv       Screen UV [0..1]
 * @param depth    Non-linear depth [0..1]
 * @param invProj  Inverse projection matrix
 * @return         Eye-space position
 */
vec3 reconstruct_eye_pos(vec2 uv, float depth, mat4 invProj)
{
    // Screen to clip space
    vec4 clip;
    clip.xy = uv * 2.0 - 1.0;
    clip.z = depth;
    clip.w = 1.0;

    // Clip to eye space
    vec4 eyePos = invProj * clip;
    eyePos /= eyePos.w;

    return eyePos.xyz;
}

// ============================================================================
// Miscellaneous Utilities
// ============================================================================

/**
 * Calculate distance attenuation (inverse square)
 *
 * @param distance Light distance
 * @param radius   Light radius
 * @return         Attenuation factor [0..1]
 */
float calc_attenuation(float distance, float radius)
{
    float attenuation = 1.0 / (distance * distance);
    float cutoff = 1.0 - smoothstep(0.0, radius, distance);
    return attenuation * cutoff;
}

/**
 * Safe normalize (avoids division by zero)
 *
 * @param v Input vector
 * @return  Normalized vector (or zero if input is zero)
 */
vec3 safe_normalize(vec3 v)
{
    float len = length(v);
    return len > 0.0001 ? v / len : vec3(0.0);
}

/**
 * Luminance calculation (Rec. 709)
 *
 * @param color Input color
 * @return      Luminance [0..1]
 */
float luminance(vec3 color)
{
    const vec3 LUMINANCE_VECTOR = vec3(0.2126, 0.7152, 0.0722);  // Rec. 709
    return dot(color, LUMINANCE_VECTOR);
}

/**
 * Luminance calculation (Rec. 601 - for compatibility with R3)
 *
 * @param color Input color
 * @return      Luminance [0..1]
 */
float luminance_601(vec3 color)
{
    const vec3 LUMINANCE_VECTOR = vec3(0.299, 0.587, 0.114);  // Rec. 601
    return dot(color, LUMINANCE_VECTOR);
}

#endif // COMMON_UTILS_H_INCLUDED
