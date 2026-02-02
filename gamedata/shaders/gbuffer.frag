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

// Terrain textures (bindings 3-7) — only meaningful for terrain materials
layout(set = 1, binding = 3) uniform sampler2D s_Mask;      // Terrain mask (RGBA = blend weights)
layout(set = 1, binding = 4) uniform sampler2D s_DetailR;   // Detail for R channel (grass)
layout(set = 1, binding = 5) uniform sampler2D s_DetailG;   // Detail for G channel (asphalt)
layout(set = 1, binding = 6) uniform sampler2D s_DetailB;   // Detail for B channel (earth)
layout(set = 1, binding = 7) uniform sampler2D s_DetailA;   // Detail for A channel (yantar)

// ============================================================================
// MVP Simplification:
// For Phase 2.21 (MVP), мы пропускаем material system и используем white color.
// Phase 2.22 добавит полноценную material system с textures.
// ============================================================================

void main()
{
    // DEBUG: Output hardcoded values to verify fragments execute
    o_Position = vec4(v_PositionEye, 1.0);

    vec3 N = normalize(v_NormalEye);
    o_Normal = vec4(N, 0.0);

    // ========================================================================
    // Terrain detection and blending
    // ========================================================================
    // Terrain materials have a valid mask texture (> 1x1).
    // Non-terrain materials use a 1x1 white fallback for s_Mask.
    ivec2 maskSize = textureSize(s_Mask, 0);
    bool isTerrain = (maskSize.x > 1 && maskSize.y > 1);

    vec3 albedo;

    if (isTerrain)
    {
        // Base terrain color (low-res pre-blended texture)
        vec3 base = texture(s_Diffuse, v_TexCoord).rgb;

        // Sample terrain mask — RGBA channels contain blend weights
        vec4 mask = texture(s_Mask, v_TexCoord);

        // Detail textures tile at higher frequency than base UV
        vec2 detailUV = v_TexCoord * 48.0;

        // Sample 4 detail layers
        vec3 dR = texture(s_DetailR, detailUV).rgb;
        vec3 dG = texture(s_DetailG, detailUV).rgb;
        vec3 dB = texture(s_DetailB, detailUV).rgb;
        vec3 dA = texture(s_DetailA, detailUV).rgb;

        // Blend detail textures by mask weights
        float weightSum = mask.r + mask.g + mask.b + mask.a;
        vec3 detail;
        if (weightSum > 0.001)
            detail = (dR * mask.r + dG * mask.g + dB * mask.b + dA * mask.a) / weightSum;
        else
            detail = vec3(1.0); // No mask data — neutral detail

        // X-Ray terrain: base_color * detail (multiply-2x blend)
        // base provides overall color, detail adds high-frequency texture
        albedo = base * detail * 2.0;
    }
    else
    {
        // Regular geometry — sample diffuse texture
        albedo = texture(s_Diffuse, v_TexCoord).rgb;
    }

    // Alpha test for foliage/tree leaves (cutout transparency)
    // Only for non-terrain geometry — terrain diffuse alpha may be < 0.5
    if (!isTerrain)
    {
        float alpha = texture(s_Diffuse, v_TexCoord).a;
        if (alpha < 0.5)
            discard;
    }

    // Output albedo
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
