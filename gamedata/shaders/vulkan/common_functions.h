#ifndef COMMON_FUNCTIONS_H_INCLUDED
#define COMMON_FUNCTIONS_H_INCLUDED

#include "constants.h"  // Uniform buffer definitions

// ============================================================================
// common_functions.h - Common Lighting Functions for Vulkan Renderer
// ============================================================================
//
// Ported from R3/R4 rendering pipeline (HLSL -> GLSL)
//
// Contains:
// - calc_model_hemi_r1() - Hemisphere lighting calculation
// - calc_sun_r1() - Directional sun lighting
// - v_hemi() - Vertex hemisphere lighting
// - calc_fogging() - Fog calculation
// - unpack functions - Normal/color unpacking
//
// ============================================================================

// ============================================================================
// Post-Processing Functions
// ============================================================================

/**
 * Contrast adjustment (piecewise contrast function)
 *
 * Ported from R3 common_functions.h
 *
 * Applies S-curve contrast adjustment preserving luminance range.
 * More sophisticated than simple pow() - uses piecewise function.
 *
 * @param Input         Input value [0..1]
 * @param ContrastPower Contrast strength (1.0 = no change, >1 = more contrast)
 * @return              Adjusted value [0..1]
 */
float Contrast(float Input, float ContrastPower)
{
    // Piecewise contrast function
    bool IsAboveHalf = Input > 0.5;
    float ToRaise = clamp(2.0 * (IsAboveHalf ? 1.0 - Input : Input), 0.0, 1.0);
    float Output = 0.5 * pow(ToRaise, ContrastPower);
    Output = IsAboveHalf ? 1.0 - Output : Output;
    return Output;
}

/**
 * Vibrance adjustment (saturation with luminance preservation)
 *
 * Ported from R3 common_functions.h
 *
 * Adjusts color saturation while preserving perceived brightness.
 * More subtle than saturation - affects less-saturated colors more.
 *
 * @param img Input color (RGB)
 * @param val Vibrance amount (0.0 = grayscale, 1.0 = original, >1.0 = boosted)
 * @return    Adjusted color
 *
 * Note: Requires LUMINANCE_VECTOR constant (e.g., vec3(0.299, 0.587, 0.114))
 */
vec3 vibrance(vec3 img, float val)
{
    const vec3 LUMINANCE_VECTOR = vec3(0.299, 0.587, 0.114);  // Rec. 601
    float luminance = dot(img.rgb, LUMINANCE_VECTOR);
    return mix(vec3(luminance), img.rgb, val);
}

/**
 * Compute colored ambient occlusion
 *
 * Ported from R3 common_functions.h
 * Based on: https://www.activision.com/cdn/research/s2016_pbs_activision_occlusion.pptx
 *
 * Applies physically-based colored AO that preserves albedo hue.
 * This prevents over-darkening in occluded areas while maintaining realism.
 *
 * @param ao     Ambient occlusion factor [0..1] (0 = fully occluded, 1 = no occlusion)
 * @param albedo Surface albedo color
 * @return       AO-modulated albedo (min is ao, preserves color)
 *
 * Formula from Activision GDC 2016:
 * - Prevents AO from darkening beyond physical limits
 * - Preserves albedo color relationships
 * - Avoids "muddy" look in crevices
 */
vec3 compute_colored_ao(float ao, vec3 albedo)
{
    vec3 a = 2.0404 * albedo - 0.3324;
    vec3 b = -4.7951 * albedo + 0.6417;
    vec3 c = 2.7552 * albedo + 0.6903;

    return max(vec3(ao), ((ao * a + b) * ao + c) * ao);
}

/**
 * Combine bloom with base image
 *
 * Ported from R3 common_functions.h
 *
 * Additively blends bloom on top of base color.
 * Bloom intensity is controlled by high.a (alpha channel).
 *
 * @param low  Base image color (LDR or tonemapped HDR)
 * @param high Bloom color (RGB) + intensity (A)
 * @return     Combined color with bloom applied
 *
 * Formula: result = low + high.rgb * high.a
 * - high.a controls bloom strength
 * - Additive blend preserves bright areas
 */
vec4 combine_bloom(vec3 low, vec4 high)
{
    return vec4(low + high.rgb * high.a, 1.0);
}

// ============================================================================
// Lighting Constants (должны быть определены через uniforms)
// ============================================================================
// vec3 L_hemi_color;    // Hemisphere sky color
// vec3 L_sun_color;     // Sun light color
// vec3 L_sun_dir_w;     // Sun direction (world space)
// vec3 L_ambient;       // Ambient light color
// vec2 L_material;      // Material properties (hemi factor, sun factor)

// ============================================================================
// calc_model_hemi_r1() - Hemisphere Lighting (R1 version)
// ============================================================================
//
// Вычисляет ambient освещение от неба на основе Y-компонента нормали.
// Верхняя полусфера освещена (norm.y > 0), нижняя - нет.
//
// Parameters:
//   norm_w - Normal in world space (normalized)
//
// Returns:
//   vec3 - Hemisphere lighting contribution (sky color * norm.y)
//
// Notes:
//   - Более реалистично чем flat ambient
//   - Используется для ambient в accumulation shaders
//   - max(0, norm.y) дает 0 для нижней полусферы, [0..1] для верхней
//
// ============================================================================
vec3 calc_model_hemi_r1(vec3 norm_w)
{
    return max(0.0, norm_w.y) * L_hemi_color;
}

// ============================================================================
// v_hemi() - Vertex Hemisphere Lighting
// ============================================================================
//
// Более продвинутая версия hemisphere lighting с offset 0.5.
// Дает более мягкий переход между верхней и нижней полусферами.
//
// Parameters:
//   n - Normal (world space, normalized)
//
// Returns:
//   vec3 - Hemisphere lighting (L_hemi_color scaled by [0.5 .. 1.0])
//
// Notes:
//   - Используется для vertex lighting
//   - (.5 + .5*n.y) дает диапазон [0.0 .. 1.0] вместо [0.0 .. 1.0]
//   - Нижняя полусфера получает 50% освещения вместо 0%
//
// ============================================================================
vec3 v_hemi(vec3 n)
{
    return L_hemi_color * (0.5 + 0.5 * n.y);
}

// ============================================================================
// calc_sun_r1() - Directional Sun Lighting (R1 version)
// ============================================================================
//
// Вычисляет освещение от солнца (directional light).
// Lambertian diffuse: max(0, dot(N, -L))
//
// Parameters:
//   norm_w - Normal in world space (normalized)
//
// Returns:
//   vec3 - Sun lighting contribution
//
// Notes:
//   - L_sun_dir_w указывает ОТ солнца, поэтому используем -L_sun_dir_w
//   - saturate(dot) эквивалентно max(0, dot)
//
// ============================================================================
vec3 calc_sun_r1(vec3 norm_w)
{
    return L_sun_color * max(0.0, dot(norm_w, -L_sun_dir_w));
}

// ============================================================================
// calc_model_lq_lighting() - Low-Quality Model Lighting
// ============================================================================
//
// Комбинирует hemisphere + ambient + sun lighting для low-quality режима.
//
// Parameters:
//   norm_w - Normal in world space (normalized)
//
// Returns:
//   vec3 - Combined lighting (hemi + ambient + sun)
//
// Notes:
//   - L_material.x - hemisphere factor
//   - L_material.y - sun factor
//   - Используется для vertex lighting в low-quality режиме
//
// ============================================================================
vec3 calc_model_lq_lighting(vec3 norm_w)
{
    return L_material.x * calc_model_hemi_r1(norm_w) +
           L_ambient +
           L_material.y * calc_sun_r1(norm_w);
}

// ============================================================================
// calc_fogging() - Fog Calculation
// ============================================================================
//
// Вычисляет fog factor на основе world position.
//
// Parameters:
//   w_pos - World position (vec4)
//
// Returns:
//   float - Fog factor
//
// Notes:
//   - Требует uniform vec4 fog_plane
//
// ============================================================================
float calc_fogging(vec4 w_pos)
{
    return dot(w_pos, fog_plane);
}

// ============================================================================
// Unpacking Functions
// ============================================================================

// Unpack normal from [0..1] to [-1..1]
vec3 unpack_normal(vec3 v)
{
    return 2.0 * v - 1.0;
}

// Unpack bx2 format (alias for unpack_normal, R3 compatibility)
vec3 unpack_bx2(vec3 v)
{
    return 2.0 * v - 1.0;
}

// Unpack bx4 format for detail normals (extended range [-2..2])
vec3 unpack_bx4(vec3 v)
{
    return 4.0 * v - 2.0;
}

// Unpack color (BGRA -> RGBA swizzle for D3D compatibility)
vec4 unpack_color(vec4 c)
{
    return c.bgra;
}

vec3 unpack_D3DCOLOR(vec3 c)
{
    return c.bgr;
}

// Unpack texture coordinates
vec2 unpack_tc_base(vec2 tc, float du, float dv)
{
    return (tc.xy + vec2(du, dv)) * (32.0 / 32768.0);
}

// ============================================================================
// p_hemi() - Sample Hemi from Lightmap
// ============================================================================
//
// Samples hemisphere lighting from lightmap texture.
//
// Parameters:
//   tc - Texture coordinates
//
// Returns:
//   vec3 - Hemisphere lighting from lightmap (alpha channel)
//
// Notes:
//   - Требует sampler2D s_hemi
//   - Возвращает .a канал (hemi factor)
//
// ============================================================================
// vec3 p_hemi(vec2 tc)
// {
//     vec4 t_lmh = texture(s_hemi, tc);
//     return vec3(t_lmh.a);
// }

// Extract hemi from lightmap vec4
float get_hemi(vec4 lmh)
{
    return lmh.a;
}

// Extract sun from lightmap vec4
float get_sun(vec4 lmh)
{
    return lmh.g;
}

// ============================================================================
// calc_reflection() - Reflection Vector
// ============================================================================
//
// Вычисляет reflection vector для environment mapping.
//
// Parameters:
//   pos_w - World position
//   norm_w - World normal (normalized)
//
// Returns:
//   vec3 - Reflection vector
//
// Notes:
//   - Требует vec3 eye_position (camera world position)
//
// ============================================================================
vec3 calc_reflection(vec3 pos_w, vec3 norm_w)
{
    return reflect(normalize(pos_w - eye_position), norm_w);
}

#endif // COMMON_FUNCTIONS_H_INCLUDED
