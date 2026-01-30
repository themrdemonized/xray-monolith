#version 450

// ============================================================================
// gbuffer.frag - G-Buffer Fragment Shader
// ============================================================================
//
// Phase 1.3: PBR Materials from Textures (UPDATED)
//
// Outputs scene geometry data to Multiple Render Targets (MRT) for deferred
// shading. This shader fills 4 render targets simultaneously in a single pass.
//
// Process:
// 1. Sample diffuse texture (albedo)
// 2. Apply normal mapping (if available)
// 3. Sample PBR materials from textures (metallic, roughness, AO)
// 4. Output to 4 MRT (position, normal, albedo, material)
//
// ============================================================================

// Include utilities for normal mapping, PBR materials, detail textures, and parallax
#include "common_tbn.h"
#include "common_pbr.h"
#include "common_detail.h"
#include "common_parallax.h"

// Inputs from vertex shader
layout(location = 0) in vec3 v_PositionEye;  // Eye-space position (для lighting)
layout(location = 1) in vec3 v_NormalEye;    // Eye-space normal (для lighting)
layout(location = 2) in vec2 v_TexCoord;     // Texture coordinates

// Multiple Render Targets (MRT) - 4 outputs
layout(location = 0) out vec4 o_Position;   // → rt_Position (R32G32B32A32_SFLOAT)
layout(location = 1) out vec4 o_Normal;     // → rt_Normal   (R32G32B32A32_SFLOAT)
layout(location = 2) out vec4 o_Color;      // → rt_Color    (R8G8B8A8_SRGB)
layout(location = 3) out vec4 o_Material;   // → rt_Material (R8G8B8A8_UNORM)

// Descriptor Set 1: Material textures (Phase 1.2 + 1.3)
// PerMaterial descriptor set from DescriptorManager
layout(set = 1, binding = 0) uniform sampler2D s_Diffuse;   // Albedo/Diffuse
layout(set = 1, binding = 1) uniform sampler2D s_Normal;    // Normal map (optional)
layout(set = 1, binding = 2) uniform sampler2D s_Specular;  // PBR: R=Metallic, G=Roughness, A=AO
layout(set = 1, binding = 3) uniform sampler2D s_Detail;    // Detail texture (optional)
layout(set = 1, binding = 5) uniform sampler2D s_AO;        // AO map (optional, separate)

// ============================================================================
// MVP Simplification:
// For Phase 2.21 (MVP), мы пропускаем material system и используем white color.
// Phase 2.22 добавит полноценную material system с textures.
// ============================================================================

void main()
{
    // ========================================================================
    // 0. Parallax Mapping (UV Offset) - MUST BE FIRST
    // ========================================================================
    // Phase 1.4: Parallax occlusion mapping
    //
    // Calculate UV offset based on height map (stored in normal map alpha)
    // This must happen BEFORE any texture sampling!

    vec2 uv = v_TexCoord;  // Start with base UV

    // Check if we should apply parallax (close to camera)
    if (should_apply_parallax(v_PositionEye, 15.0))
    {
        // Calculate view direction in tangent space
        vec3 N = normalize(v_NormalEye);
        mat3 TBN = calculate_tbn(N, v_PositionEye, v_TexCoord);

        vec3 viewDirEye = normalize(-v_PositionEye);  // Camera at origin in eye-space
        vec3 viewDirTangent = calculate_view_dir_tangent(viewDirEye, TBN);

        // Apply parallax occlusion mapping
        // Height map is in normal map alpha channel
        const float heightScale = 0.08;  // Parallax depth (0.05-0.1)
        const int minSteps = 8;          // Min raymarch steps
        const int maxSteps = 32;         // Max raymarch steps

        uv = parallax_occlusion_mapping(
            s_Normal,           // Height in alpha channel
            uv,
            viewDirTangent,
            heightScale,
            minSteps,
            maxSteps
        );
    }

    // From now on, use offset 'uv' for ALL texture sampling!

    // ========================================================================
    // 1. Output Position (Eye-Space)
    // ========================================================================
    // Store eye-space position для lighting calculations в deferred pass.
    // Alpha channel stores fragment depth (optional, для debug/effects).
    o_Position = vec4(v_PositionEye, 1.0);

    // ========================================================================
    // 2. Output Normal (Eye-Space) - WITH NORMAL MAPPING
    // ========================================================================
    // Phase 1.1: Normal mapping implementation
    //
    // Start with interpolated vertex normal
    vec3 N = normalize(v_NormalEye);

    // Check if normal map has actual data (not default flat normal)
    // NOTE: Use offset 'uv' from parallax mapping!
    vec3 normalMapSample = texture(s_Normal, uv).rgb;

    if (has_normal_map_data(normalMapSample, 0.01))
    {
        // Calculate TBN matrix using screen-space derivatives
        // This avoids needing explicit tangent/bitangent vertex attributes
        mat3 TBN = calculate_tbn(N, v_PositionEye, uv);  // Use offset UV

        // Apply normal map (sample → unpack → transform to eye-space)
        N = apply_normal_map(s_Normal, uv, TBN);  // Use offset UV
    }

    // Store normal + hemi flag (alpha = 0.0 для now)
    // Hemi будет использоваться для hemisphere lighting (indirect illumination)
    o_Normal = vec4(N, 0.0);

    // ========================================================================
    // 3. Output Albedo (Diffuse Color) - WITH DETAIL TEXTURES
    // ========================================================================
    // Phase 1.2: Detail texture implementation

    // Sample base diffuse texture (use offset UV from parallax)
    vec3 albedo = texture(s_Diffuse, uv).rgb;

    // Apply detail texture if available and close enough
    vec3 detailSample = texture(s_Detail, uv).rgb;

    if (has_detail_data(detailSample))
    {
        // Detail texture parameters
        const float detailScale = 10.0;     // Tiling scale (higher = more detail)
        const float detailStart = 5.0;      // Start fading at 5 units
        const float detailEnd = 20.0;       // Fully faded at 20 units
        const float detailStrength = 0.5;   // Detail intensity (0.0-1.0)

        // Apply detail texture with distance fade
        albedo = apply_detail_texture(
            albedo,
            s_Detail,
            uv,  // Use offset UV from parallax
            detailScale,
            v_PositionEye,
            detailStart,
            detailEnd,
            detailStrength
        );
    }

    // SRGB output (automatic gamma correction)
    o_Color = vec4(albedo, 1.0);

    // ========================================================================
    // 4. Output Material Properties (PBR) - FROM TEXTURES
    // ========================================================================
    // Phase 1.3: Sample PBR material from textures
    //
    // Material data for PBR shading:
    // R = Metallic (0.0 = dielectric, 1.0 = metal)
    // G = Roughness (0.0 = smooth/glossy, 1.0 = rough/matte)
    // B = Subsurface Scattering (SSS) strength
    // A = Ambient Occlusion (AO) factor

    // Sample PBR material from textures
    // Default values used if textures are missing/default
    PBRMaterial mat = sample_pbr_material(
        s_Specular,           // PBR texture (R=metallic, G=roughness, A=AO)
        s_AO,                 // Separate AO texture (optional)
        uv,                   // UV coordinates (offset by parallax!)
        0.0,                  // Default metallic (dielectric)
        0.5                   // Default roughness (medium)
    );

    // Validate and clamp to safe ranges
    mat = validate_material(mat);

    // Remap roughness for better visual results
    // Quadratic curve makes mid-range materials look better
    mat.roughness = remap_roughness(mat.roughness);

    // Output material properties
    o_Material = vec4(mat.metallic, mat.roughness, mat.sss, mat.ao);

    // ========================================================================
    // Notes:
    // ========================================================================
    // - All outputs в eye-space (consistent coordinate system для lighting)
    // - MRT позволяет заполнить 4 buffers за один draw call (efficient!)
    // - Depth buffer заполняется автоматически (gl_FragDepth)
    // - Phase 2.22 добавит material system (textures, normal maps, PBR)
    // ========================================================================
}
