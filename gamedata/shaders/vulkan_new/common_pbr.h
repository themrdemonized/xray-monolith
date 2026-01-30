// ============================================================================
// common_pbr.h - PBR Material System
// ============================================================================
//
// Phase 1.3: PBR Materials from Textures
//
// Provides functions for PBR (Physically-Based Rendering) material sampling:
// - Material properties extraction from textures
// - Metallic/Roughness workflows
// - Material type handling (from X-Ray .thm files)
// - Roughness remapping for better visual results
//
// ============================================================================

#ifndef COMMON_PBR_H
#define COMMON_PBR_H

// ============================================================================
// PBR Material Structure
// ============================================================================

/**
 * PBR Material Properties
 *
 * Stores all material parameters needed for physically-based shading.
 */
struct PBRMaterial
{
    float metallic;   // 0.0 = dielectric (non-metal), 1.0 = metal
    float roughness;  // 0.0 = smooth/glossy, 1.0 = rough/matte
    float ao;         // Ambient occlusion (0.0 = fully occluded, 1.0 = no occlusion)
    float sss;        // Subsurface scattering strength (for skin, wax, etc.)
};

// ============================================================================
// Material Property Sampling
// ============================================================================

/**
 * Sample PBR material from textures (Metallic/Roughness workflow)
 *
 * Texture format:
 * - s_Specular: R = Metallic, G = Roughness, B = unused, A = AO (optional)
 * - s_AO: R = Ambient Occlusion (if separate texture)
 *
 * @param specularMap      Specular/PBR map (metallic/roughness)
 * @param aoMap            AO map (optional, can be same as specularMap.a)
 * @param uv               Texture coordinates
 * @param defaultMetallic  Fallback metallic if texture is default
 * @param defaultRoughness Fallback roughness if texture is default
 * @return                 PBR material properties
 */
PBRMaterial sample_pbr_material(
    sampler2D specularMap,
    sampler2D aoMap,
    vec2 uv,
    float defaultMetallic,
    float defaultRoughness)
{
    PBRMaterial mat;

    // ========================================================================
    // Sample Specular/PBR Map
    // ========================================================================
    vec4 specular = texture(specularMap, uv);

    // Check if specular map has actual data (not default gray)
    // Default specular map is usually (0.5, 0.5, 0.5, 1.0)
    const vec3 DEFAULT_SPECULAR = vec3(0.5);
    const float THRESHOLD = 0.01;

    if (length(specular.rgb - DEFAULT_SPECULAR) > THRESHOLD)
    {
        // Texture has real PBR data
        mat.metallic = specular.r;   // Red channel = Metallic
        mat.roughness = specular.g;  // Green channel = Roughness

        // Blue channel is unused in metallic/roughness workflow
        // Alpha channel can contain AO
        mat.ao = specular.a;
    }
    else
    {
        // Use default values (texture is missing or default)
        mat.metallic = defaultMetallic;
        mat.roughness = defaultRoughness;
        mat.ao = 1.0;  // No occlusion by default
    }

    // ========================================================================
    // Sample Separate AO Map (if available)
    // ========================================================================
    // If AO is in separate texture (not in specular.a)
    vec4 aoSample = texture(aoMap, uv);

    // Check if AO map has data (not white default)
    if (aoSample.r < 0.99)
    {
        mat.ao = aoSample.r;  // Override with separate AO map
    }

    // ========================================================================
    // SSS (Subsurface Scattering)
    // ========================================================================
    // Currently not used, will be added later for skin/organic materials
    mat.sss = 0.0;

    return mat;
}

/**
 * Sample PBR material with material type modulation
 *
 * X-Ray material types (from .thm files):
 * - 0 = OrenNayar-Blin (rough diffuse surfaces: cloth, concrete)
 * - 1 = Blin-Phong (standard specular: plastic, painted surfaces)
 * - 2 = Phong-Metal (metallic surfaces: metal, chrome)
 * - 3 = Metal-OrenNayar (rough metal: rusty metal, worn metal)
 *
 * @param specularMap      Specular/PBR map
 * @param aoMap            AO map
 * @param uv               Texture coordinates
 * @param materialID       Material type + weight (e.g., 2.5 = type 2 + 50% weight)
 * @return                 PBR material with type modulation applied
 */
PBRMaterial sample_pbr_material_typed(
    sampler2D specularMap,
    sampler2D aoMap,
    vec2 uv,
    float materialID)
{
    // Extract material type and weight
    float materialType = floor(materialID);       // 0, 1, 2, or 3
    float materialWeight = fract(materialID);     // 0.0 to 1.0

    // Default values based on material type
    float defaultMetallic = 0.0;
    float defaultRoughness = 0.5;

    if (materialType >= 2.0)  // Metal types (2 = Phong-Metal, 3 = Metal-OrenNayar)
    {
        defaultMetallic = 0.8;   // Force metallic for metal types
        defaultRoughness = (materialType >= 3.0) ? 0.7 : 0.3;  // Rough vs smooth metal
    }
    else if (materialType >= 1.0)  // Blin-Phong (standard specular)
    {
        defaultMetallic = 0.0;
        defaultRoughness = 0.4;
    }
    else  // OrenNayar-Blin (rough diffuse)
    {
        defaultMetallic = 0.0;
        defaultRoughness = 0.8;
    }

    // Sample material from textures
    PBRMaterial mat = sample_pbr_material(
        specularMap, aoMap, uv,
        defaultMetallic, defaultRoughness
    );

    // Apply material weight (blend between texture and type defaults)
    // Higher weight = more influence from material type
    if (materialWeight > 0.01)
    {
        mat.metallic = mix(mat.metallic, defaultMetallic, materialWeight);
        mat.roughness = mix(mat.roughness, defaultRoughness, materialWeight);
    }

    // Enforce constraints for metal types
    if (materialType >= 2.0)
    {
        mat.metallic = max(mat.metallic, 0.5);  // Force at least 50% metallic
    }

    return mat;
}

// ============================================================================
// Roughness Remapping
// ============================================================================

/**
 * Remap roughness for more intuitive control
 *
 * Linear roughness in [0, 1] doesn't look good visually.
 * Most materials look too shiny with linear mapping.
 *
 * This function applies perceptual remapping:
 * - 0.0 → 0.0 (mirror smooth)
 * - 0.5 → 0.25 (medium roughness looks smoother)
 * - 1.0 → 1.0 (fully rough)
 *
 * @param roughness  Linear roughness [0, 1]
 * @return           Perceptually-remapped roughness
 */
float remap_roughness(float roughness)
{
    // Quadratic curve for better distribution
    // More glossy materials in mid-range
    return roughness * roughness;
}

/**
 * Remap roughness with custom curve
 *
 * Allows different remapping curves for different material types.
 *
 * @param roughness  Linear roughness [0, 1]
 * @param power      Curve power (1.0 = linear, 2.0 = quadratic, etc.)
 * @return           Remapped roughness
 */
float remap_roughness_power(float roughness, float power)
{
    return pow(roughness, power);
}

// ============================================================================
// Material Validation & Clamping
// ============================================================================

/**
 * Validate and clamp material properties to safe ranges
 *
 * Prevents extreme values that can cause rendering artifacts.
 *
 * @param mat  Input material
 * @return     Clamped material
 */
PBRMaterial validate_material(PBRMaterial mat)
{
    PBRMaterial result;

    // Clamp to valid ranges
    result.metallic = clamp(mat.metallic, 0.0, 1.0);
    result.roughness = clamp(mat.roughness, 0.04, 1.0);  // Min 0.04 to avoid divide-by-zero
    result.ao = clamp(mat.ao, 0.0, 1.0);
    result.sss = clamp(mat.sss, 0.0, 1.0);

    return result;
}

// ============================================================================
// F0 (Fresnel at 0 degrees) Calculation
// ============================================================================

/**
 * Calculate F0 (surface reflectance at 0 degrees)
 *
 * F0 determines how much light is reflected at perpendicular viewing angle.
 * - Dielectrics (non-metals): F0 ~= 0.04 (4% reflection)
 * - Metals: F0 = albedo color (colored reflection)
 *
 * @param albedo    Surface albedo color
 * @param metallic  Metallic value [0, 1]
 * @return          F0 for Fresnel calculation
 */
vec3 calculate_f0(vec3 albedo, float metallic)
{
    // Dielectric F0 (plastic, glass, etc.)
    const vec3 DIELECTRIC_F0 = vec3(0.04);

    // Blend between dielectric and metal F0
    // Metals use albedo as F0 (colored reflection)
    // Non-metals use constant 0.04 (white reflection)
    return mix(DIELECTRIC_F0, albedo, metallic);
}

/**
 * Calculate F0 with custom dielectric reflectance
 *
 * Some materials have different base reflectance:
 * - Water: 0.02
 * - Plastic: 0.04
 * - Glass: 0.04-0.05
 * - Diamond: 0.17
 *
 * @param albedo              Surface albedo
 * @param metallic            Metallic value
 * @param dielectricF0        Custom dielectric F0 (default: 0.04)
 * @return                    F0 for Fresnel calculation
 */
vec3 calculate_f0_custom(vec3 albedo, float metallic, float dielectricF0)
{
    vec3 F0_dielectric = vec3(dielectricF0);
    return mix(F0_dielectric, albedo, metallic);
}

// ============================================================================
// Utility Functions
// ============================================================================

/**
 * Check if material is metal
 *
 * @param metallic  Metallic value
 * @return          true if material is considered metal
 */
bool is_metal(float metallic)
{
    return metallic > 0.5;
}

/**
 * Get diffuse contribution factor
 *
 * Metals have no diffuse contribution (all reflection).
 * Non-metals have diffuse contribution.
 *
 * @param metallic  Metallic value
 * @return          Diffuse factor (0.0 for metals, 1.0 for dielectrics)
 */
float get_diffuse_factor(float metallic)
{
    return 1.0 - metallic;
}

/**
 * Apply energy conservation
 *
 * Total reflected + diffuse must not exceed 1.0.
 * This is automatically handled in PBR BRDF but useful for debugging.
 *
 * @param diffuse   Diffuse component
 * @param specular  Specular component
 * @return          Energy-conserved sum
 */
vec3 apply_energy_conservation(vec3 diffuse, vec3 specular)
{
    return diffuse + specular;  // PBR BRDF already conserves energy
}

// ============================================================================
// Debug Visualization
// ============================================================================

/**
 * Visualize metallic as color (for debugging)
 *
 * Blue = dielectric, Yellow = metal
 */
vec3 debug_visualize_metallic(float metallic)
{
    vec3 dielectricColor = vec3(0.0, 0.0, 1.0);  // Blue
    vec3 metallicColor = vec3(1.0, 1.0, 0.0);    // Yellow
    return mix(dielectricColor, metallicColor, metallic);
}

/**
 * Visualize roughness as grayscale (for debugging)
 *
 * Black = smooth, White = rough
 */
vec3 debug_visualize_roughness(float roughness)
{
    return vec3(roughness);
}

/**
 * Visualize AO as grayscale (for debugging)
 *
 * Black = fully occluded, White = no occlusion
 */
vec3 debug_visualize_ao(float ao)
{
    return vec3(ao);
}

#endif // COMMON_PBR_H
