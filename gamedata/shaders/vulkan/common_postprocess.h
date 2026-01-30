#ifndef COMMON_POSTPROCESS_H_INCLUDED
#define COMMON_POSTPROCESS_H_INCLUDED

// ============================================================================
// common_postprocess.h - Post-Processing Functions for Vulkan Renderer
// ============================================================================
//
// Ported from R3 common_functions.h + additional enhancements
//
// Contains:
// - Color grading (contrast, vibrance, saturation)
// - Ambient occlusion (colored AO)
// - Bloom combination
// - Tone mapping helpers
// - Vignette, chromatic aberration, etc.
//
// ============================================================================

// ============================================================================
// Color Grading
// ============================================================================

/**
 * Contrast adjustment (piecewise S-curve)
 *
 * Ported from R3 common_functions.h
 *
 * Applies smooth S-curve contrast that preserves black/white points.
 * More sophisticated than simple pow() - prevents clipping.
 *
 * @param Input         Input value [0..1]
 * @param ContrastPower Contrast strength (1.0 = no change, >1 = more contrast, <1 = less)
 * @return              Adjusted value [0..1]
 *
 * Example:
 *   float adjusted = Contrast(color.r, 1.2);  // Increase contrast by 20%
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
 * Contrast adjustment for RGB color
 *
 * Applies piecewise contrast to all channels.
 *
 * @param color         Input color
 * @param ContrastPower Contrast strength
 * @return              Contrast-adjusted color
 */
vec3 ContrastRGB(vec3 color, float ContrastPower)
{
    return vec3(
        Contrast(color.r, ContrastPower),
        Contrast(color.g, ContrastPower),
        Contrast(color.b, ContrastPower)
    );
}

/**
 * Vibrance adjustment (smart saturation)
 *
 * Ported from R3 common_functions.h
 *
 * Adjusts saturation while preserving luminance and skin tones.
 * More subtle than saturation - affects muted colors more than vivid ones.
 *
 * @param img Input color (RGB)
 * @param val Vibrance amount (0.0 = grayscale, 1.0 = original, >1.0 = boosted)
 * @return    Vibrance-adjusted color
 *
 * Uses Rec. 601 luminance weights (0.299, 0.587, 0.114).
 *
 * Example:
 *   vec3 vibrant = vibrance(color, 1.3);  // Boost vibrance by 30%
 */
vec3 vibrance(vec3 img, float val)
{
    const vec3 LUMINANCE_VECTOR = vec3(0.299, 0.587, 0.114);  // Rec. 601
    float luminance = dot(img.rgb, LUMINANCE_VECTOR);
    return mix(vec3(luminance), img.rgb, val);
}

/**
 * Saturation adjustment
 *
 * Simple saturation adjustment (less subtle than vibrance).
 *
 * @param color Input color
 * @param sat   Saturation amount (0.0 = grayscale, 1.0 = original, >1.0 = oversaturated)
 * @return      Saturated color
 */
vec3 saturation(vec3 color, float sat)
{
    const vec3 LUMINANCE_VECTOR = vec3(0.299, 0.587, 0.114);
    float luminance = dot(color, LUMINANCE_VECTOR);
    return mix(vec3(luminance), color, sat);
}

/**
 * Exposure adjustment (photographic exposure)
 *
 * @param color    Input HDR color
 * @param exposure Exposure value in stops (0 = no change, +1 = 2x brighter, -1 = 0.5x)
 * @return         Exposure-adjusted color
 */
vec3 exposureAdjust(vec3 color, float exposure)
{
    return color * pow(2.0, exposure);
}

/**
 * Lift-Gamma-Gain color grading
 *
 * Professional color grading controls (DaVinci Resolve style).
 *
 * @param color Input color
 * @param lift  Shadows adjustment (adds to color)
 * @param gamma Midtones adjustment (power function)
 * @param gain  Highlights adjustment (multiplies color)
 * @return      Graded color
 */
vec3 liftGammaGain(vec3 color, vec3 lift, vec3 gamma, vec3 gain)
{
    // Lift (shadows)
    color = color + lift;

    // Gamma (midtones)
    color = pow(max(color, vec3(0.0)), 1.0 / gamma);

    // Gain (highlights)
    color = color * gain;

    return color;
}

// ============================================================================
// Ambient Occlusion
// ============================================================================

/**
 * Compute colored ambient occlusion (physically-based)
 *
 * Ported from R3 common_functions.h
 * Based on: https://www.activision.com/cdn/research/s2016_pbs_activision_occlusion.pptx
 *
 * Applies physically-based colored AO that preserves albedo hue.
 * Prevents over-darkening in occluded areas while maintaining realism.
 *
 * @param ao     Ambient occlusion factor [0..1] (0 = fully occluded, 1 = no occlusion)
 * @param albedo Surface albedo color
 * @return       AO-modulated albedo
 *
 * From Activision GDC 2016 "Practical Realtime Strategies for Accurate Indirect Occlusion":
 * - Prevents AO from darkening beyond physical limits
 * - Preserves color saturation in occluded areas
 * - Avoids "muddy" look in crevices
 * - Based on measured diffuse reflectance data
 *
 * Example:
 *   vec3 finalAlbedo = compute_colored_ao(ao, baseAlbedo);
 */
vec3 compute_colored_ao(float ao, vec3 albedo)
{
    // Polynomial coefficients from Activision research
    vec3 a = 2.0404 * albedo - 0.3324;
    vec3 b = -4.7951 * albedo + 0.6417;
    vec3 c = 2.7552 * albedo + 0.6903;

    // Cubic polynomial: min(ao, polynomial(ao))
    return max(vec3(ao), ((ao * a + b) * ao + c) * ao);
}

/**
 * Simple AO application (multiply)
 *
 * Traditional AO application - just multiply by AO factor.
 * Simpler than colored AO but can look less realistic.
 *
 * @param color Input color
 * @param ao    AO factor [0..1]
 * @return      AO-darkened color
 */
vec3 apply_ao_simple(vec3 color, float ao)
{
    return color * ao;
}

// ============================================================================
// Bloom
// ============================================================================

/**
 * Combine bloom with base image (additive blend)
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
 * - high.a controls bloom strength (0 = no bloom, 1 = full bloom)
 * - Additive blend preserves bright areas
 * - Alpha is set to 1.0 (opaque)
 *
 * Example:
 *   vec4 final = combine_bloom(baseColor, vec4(bloomColor, bloomStrength));
 */
vec4 combine_bloom(vec3 low, vec4 high)
{
    return vec4(low + high.rgb * high.a, 1.0);
}

/**
 * Blend bloom with base (screen blend mode)
 *
 * Alternative bloom blending using screen mode (more subtle).
 *
 * @param base  Base color
 * @param bloom Bloom color
 * @return      Screen-blended result
 */
vec3 combine_bloom_screen(vec3 base, vec3 bloom)
{
    return 1.0 - (1.0 - base) * (1.0 - bloom);
}

// ============================================================================
// Vignette
// ============================================================================

/**
 * Vignette effect (darkens image edges)
 *
 * @param uv        Screen UV coordinates [0..1]
 * @param intensity Vignette strength (0 = none, 1 = strong)
 * @param smoothness Falloff smoothness (higher = smoother)
 * @return          Vignette factor [0..1] (multiply with color)
 */
float vignette(vec2 uv, float intensity, float smoothness)
{
    // Distance from center
    vec2 centered = uv * 2.0 - 1.0;
    float dist = length(centered);

    // Smooth falloff
    float vignette = smoothstep(1.0 - intensity, 1.0 - intensity + smoothness, 1.0 - dist);

    return vignette;
}

/**
 * Colored vignette (tints edges)
 *
 * @param uv        Screen UV
 * @param color     Edge tint color
 * @param intensity Vignette strength
 * @return          Color to blend with image
 */
vec3 vignette_colored(vec2 uv, vec3 color, float intensity)
{
    vec2 centered = uv * 2.0 - 1.0;
    float dist = length(centered);
    float factor = smoothstep(0.5, 1.0, dist) * intensity;
    return mix(vec3(1.0), color, factor);
}

// ============================================================================
// Chromatic Aberration
// ============================================================================

/**
 * Chromatic aberration offset calculation
 *
 * Calculate RGB channel offsets for chromatic aberration effect.
 *
 * @param uv        Screen UV
 * @param strength  Aberration strength (pixels)
 * @return          vec2 offset to apply to UV (direction from center)
 */
vec2 chromatic_aberration_offset(vec2 uv, float strength)
{
    vec2 centered = uv - 0.5;
    float dist = length(centered);
    vec2 direction = normalize(centered);

    return direction * strength * dist;
}

// ============================================================================
// Film Grain / Noise
// ============================================================================

/**
 * Film grain noise
 *
 * Adds film-like grain to image.
 *
 * @param uv       Screen UV
 * @param time     Time value for animation
 * @param strength Grain strength
 * @return         Noise value [-strength..strength]
 */
float film_grain(vec2 uv, float time, float strength)
{
    // Simple hash-based noise
    float noise = fract(sin(dot(uv + time, vec2(12.9898, 78.233))) * 43758.5453);
    return (noise - 0.5) * strength;
}

// ============================================================================
// Tonemapping Helpers
// ============================================================================

/**
 * Linear to sRGB conversion
 *
 * @param linear Linear color
 * @return       sRGB color
 */
vec3 linear_to_srgb(vec3 linear)
{
    vec3 a = 12.92 * linear;
    vec3 b = 1.055 * pow(linear, vec3(1.0 / 2.4)) - 0.055;
    vec3 c = step(vec3(0.0031308), linear);
    return mix(a, b, c);
}

/**
 * sRGB to linear conversion
 *
 * @param srgb sRGB color
 * @return     Linear color
 */
vec3 srgb_to_linear(vec3 srgb)
{
    vec3 a = srgb / 12.92;
    vec3 b = pow((srgb + 0.055) / 1.055, vec3(2.4));
    vec3 c = step(vec3(0.04045), srgb);
    return mix(a, b, c);
}

/**
 * Reinhard tonemapping
 *
 * @param hdr HDR color
 * @return    LDR color [0..1]
 */
vec3 tonemap_reinhard(vec3 hdr)
{
    return hdr / (1.0 + hdr);
}

/**
 * ACES filmic tonemapping (approximation)
 *
 * @param hdr HDR color
 * @return    LDR color [0..1]
 */
vec3 tonemap_aces_approx(vec3 hdr)
{
    hdr *= 0.6;
    float a = 2.51;
    float b = 0.03;
    float c = 2.43;
    float d = 0.59;
    float e = 0.14;
    return clamp((hdr * (a * hdr + b)) / (hdr * (c * hdr + d) + e), 0.0, 1.0);
}

/**
 * Uncharted 2 tonemapping
 *
 * @param x HDR color
 * @return  LDR color [0..1]
 */
vec3 tonemap_uncharted2(vec3 x)
{
    float A = 0.15;
    float B = 0.50;
    float C = 0.10;
    float D = 0.20;
    float E = 0.02;
    float F = 0.30;
    return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
}

#endif // COMMON_POSTPROCESS_H_INCLUDED
