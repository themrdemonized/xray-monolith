#version 450

// ============================================================================
// gbuffer.frag - G-Buffer Fragment Shader
// ============================================================================
//
// Phase 2.21.1: G-Buffer Shaders
//
// Outputs scene geometry data to Multiple Render Targets (MRT) for deferred
// shading. This shader fills 4 render targets simultaneously in a single pass.
//
// Process:
// 1. Sample diffuse texture (albedo)
// 2. Normalize interpolated normal
// 3. Output to 4 MRT (position, normal, albedo, material)
//
// ============================================================================

// Inputs from vertex shader
layout(location = 0) in vec3 v_PositionEye;  // Eye-space position (для lighting)
layout(location = 1) in vec3 v_NormalEye;    // Eye-space normal (для lighting)
layout(location = 2) in vec2 v_TexCoord;     // Texture coordinates

// Multiple Render Targets (MRT) - 4 outputs
layout(location = 0) out vec4 o_Position;   // → rt_Position (R32G32B32A32_SFLOAT)
layout(location = 1) out vec4 o_Normal;     // → rt_Normal   (R32G32B32A32_SFLOAT)
layout(location = 2) out vec4 o_Color;      // → rt_Color    (R8G8B8A8_SRGB)
layout(location = 3) out vec4 o_Material;   // → rt_Material (R8G8B8A8_UNORM)

// Descriptor Set 1: Material textures (Phase 2.22)
// PerMaterial descriptor set from DescriptorManager
layout(set = 1, binding = 0) uniform sampler2D s_Diffuse;   // Albedo/Diffuse
layout(set = 1, binding = 1) uniform sampler2D s_Normal;    // Normal map (optional)
layout(set = 1, binding = 2) uniform sampler2D s_Specular;  // Specular/Roughness (optional)

// ============================================================================
// MVP Simplification:
// For Phase 2.21 (MVP), мы пропускаем material system и используем white color.
// Phase 2.22 добавит полноценную material system с textures.
// ============================================================================

void main()
{
    // ========================================================================
    // 1. Output Position (Eye-Space)
    // ========================================================================
    // Store eye-space position для lighting calculations в deferred pass.
    // Alpha channel stores fragment depth (optional, для debug/effects).
    o_Position = vec4(v_PositionEye, 1.0);

    // ========================================================================
    // 2. Output Normal (Eye-Space)
    // ========================================================================
    // Normalize interpolated normal (interpolation может изменить length).
    // Eye-space normals для lighting calculations.
    //
    // TODO Phase 2.22: Add normal mapping
    // vec3 normalMap = texture(s_Normal, v_TexCoord).xyz * 2.0 - 1.0;
    // vec3 N = normalize(TBN * normalMap);
    //
    vec3 N = normalize(v_NormalEye);

    // Store normal + hemi flag (alpha = 0.0 для now)
    // Hemi будет использоваться для hemisphere lighting (indirect illumination)
    o_Normal = vec4(N, 0.0);

    // ========================================================================
    // 3. Output Albedo (Diffuse Color)
    // ========================================================================
    // Phase 2.22: Sample diffuse texture for albedo
    vec3 albedo = texture(s_Diffuse, v_TexCoord).rgb;

    // For MVP: Using white texture (1x1 fallback)
    // Phase 2.23+ will load actual textures from gamedata

    // SRGB output (automatic gamma correction)
    o_Color = vec4(albedo, 1.0);

    // ========================================================================
    // 4. Output Material Properties (PBR)
    // ========================================================================
    // Material data for PBR shading:
    // R = Metallic (0.0 = dielectric, 1.0 = metal)
    // G = Roughness (0.0 = smooth/glossy, 1.0 = rough/matte)
    // B = Subsurface Scattering (SSS) strength
    // A = Ambient Occlusion (AO) factor
    //
    // TODO Phase 2.22: Sample material textures
    // float metallic = texture(s_Specular, v_TexCoord).r;
    // float roughness = texture(s_Specular, v_TexCoord).g;
    // float sss = 0.0;
    // float ao = texture(s_AO, v_TexCoord).r;
    //
    // For MVP (Phase 2.21): Default PBR values
    float metallic = 0.0;    // Dielectric (non-metal)
    float roughness = 0.5;   // Medium roughness
    float sss = 0.0;         // No subsurface scattering
    float ao = 1.0;          // No occlusion

    o_Material = vec4(metallic, roughness, sss, ao);

    // ========================================================================
    // Notes:
    // ========================================================================
    // - All outputs в eye-space (consistent coordinate system для lighting)
    // - MRT позволяет заполнить 4 buffers за один draw call (efficient!)
    // - Depth buffer заполняется автоматически (gl_FragDepth)
    // - Phase 2.22 добавит material system (textures, normal maps, PBR)
    // ========================================================================
}
