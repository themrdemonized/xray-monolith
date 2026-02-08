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
layout(location = 3) in vec2 v_WorldPosXZ;   // World-space XZ for terrain detail tiling

// Multiple Render Targets (MRT) - 4 outputs
layout(location = 0) out vec4 o_Position;   // → rt_Position (R32G32B32A32_SFLOAT)
layout(location = 1) out vec4 o_Normal;     // → rt_Normal   (R32G32B32A32_SFLOAT)
layout(location = 2) out vec4 o_Color;      // → rt_Color    (R8G8B8A8_SRGB)
layout(location = 3) out vec4 o_Material;   // → rt_Material (R8G8B8A8_UNORM)

// Push constants (shared with vertex shader)
// Offset 196 is u_SkinMode in skinned vertex shader, so u_AlphaRef goes at 200
layout(push_constant) uniform PushConstants
{
    layout(offset = 200) float u_AlphaRef;     // Alpha test threshold (-1.0 = disabled, 0.5 = enabled)
    layout(offset = 204) float u_DetailScale;  // Terrain detail UV multiplier (from .thm dt_params)
} pc;

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

// ============================================================================
// UV Debug: uncomment ONE to visualize UVs as colors (all disabled for release)
// ============================================================================
// #define DEBUG_UV_COLORS   // fract(UV) as RG — repeating pattern = tiling
// #define DEBUG_UV_RANGE    // abs(UV)/16 as RG — bright = large UV = excess tiling
// #define DEBUG_TEX_SIZE    // texture dimensions / 2048 — R=w/2048, G=h/2048

// ============================================================================
// Normal Mapping Helpers (derivative-based TBN, no vertex tangents needed)
// ============================================================================

// Unpack normal from texture: RGB [0,1] → XYZ [-1,1]
vec3 unpack_normal(vec3 v) { return v * 2.0 - 1.0; }

// Screen-space derivative TBN (Schüler method)
mat3 calculate_tbn(vec3 N, vec3 posEye, vec2 uv)
{
    vec3 dp1  = dFdx(posEye);
    vec3 dp2  = dFdy(posEye);
    vec2 duv1 = dFdx(uv);
    vec2 duv2 = dFdy(uv);

    vec3 dp2perp = cross(dp2, N);
    vec3 dp1perp = cross(N, dp1);

    vec3 T = dp2perp * duv1.x + dp1perp * duv2.x;
    vec3 B = dp2perp * duv1.y + dp1perp * duv2.y;

    float invmax = inversesqrt(max(dot(T, T), dot(B, B)));
    return mat3(T * invmax, B * invmax, N);
}

void main()
{
    // DEBUG: Output hardcoded values to verify fragments execute
    o_Position = vec4(v_PositionEye, 1.0);

    vec3 N = normalize(v_NormalEye);

    // ========================================================================
    // Normal mapping: perturb N if a real normal map is bound (not 1x1 fallback)
    // ========================================================================
    ivec2 normalSize = textureSize(s_Normal, 0);
    if (normalSize.x > 1 && normalSize.y > 1)
    {
        mat3 TBN = calculate_tbn(N, v_PositionEye, v_TexCoord);
        vec3 ns  = texture(s_Normal, v_TexCoord).rgb;
        vec3 nt  = unpack_normal(ns);
        N = normalize(TBN * nt);
    }

    o_Normal = vec4(N, 0.0);

    // ========================================================================
    // Terrain detection and detail texture blending
    // ========================================================================
    //
    // HOW TERRAIN WORKS IN X-RAY:
    //
    // 1. DETECTION: Terrain materials are identified by having a real mask
    //    texture (> 1x1 pixels). Non-terrain gets a 1x1 white fallback.
    //
    // 2. TEXTURES (bindings 3-7, set in CMaterial::LoadTerrainTextures):
    //    - s_Mask    (binding 3): RGBA blend weights — controls which detail
    //      material is visible at each point. Painted by the level compiler.
    //    - s_DetailR (binding 4): Detail texture for mask R channel (e.g. grass)
    //    - s_DetailG (binding 5): Detail texture for mask G channel (e.g. asphalt)
    //    - s_DetailB (binding 6): Detail texture for mask B channel (e.g. earth)
    //    - s_DetailA (binding 7): Detail texture for mask A channel (e.g. yantar)
    //
    // 3. DETAIL UV — WHY WORLD-SPACE, NOT VERTEX UV:
    //    Original X-Ray DX11 used: detailUV = v_TexCoord * dt_params (e.g. *48).
    //    In Vulkan, vertex UV (v_TexCoord) has micro-discontinuities at terrain
    //    triangle boundaries. At 1x scale these are invisible, but at 48x they
    //    get amplified into visible horizontal stripes on slopes.
    //    FIX: use world-space XZ position for detail UV. World-space is continuous
    //    across all triangles, so no stripes at any scale.
    //
    // 4. SCALE CONVERSION:
    //    u_DetailScale comes from .thm dt_params (typically ~48), designed for
    //    vertex UV multiplication. Multiplier 0.045 converts it to world-space
    //    tiles-per-meter: 48 * 0.045 ≈ 2.16 tiles/m (1 repeat every ~0.46m).
    //    TUNING: increase 0.045 → finer detail; decrease → coarser.
    //
    // 5. ANTI-TILING:
    //    A second UV octave (rotated 90 deg, slightly different scale) is blended
    //    at 12% to break visible tiling repetition. Keep blend LOW (<15%) or
    //    it washes out the contrast between different materials.
    //
    // 6. BLENDING:
    //    mask RGBA weights select which detail textures are visible.
    //    Detail color is modulated onto base: 0.5 = neutral, <0.5 = darken,
    //    >0.5 = brighten. Strength 2.0 makes the effect clearly visible.
    //    Detail fades out with distance (60-150m) to avoid shimmer at range.
    //
    // CPU SIDE (vk_material.cpp):
    //    - m_bTerrain flag set when mask texture is loaded
    //    - u_DetailScale push constant at offset 204, set in CMaterial::Bind()
    //    - Detail textures loaded from .thm associations (LoadDetail/LoadTerrainTextures)
    //
    // ========================================================================
    ivec2 maskSize = textureSize(s_Mask, 0);
    bool isTerrain = (maskSize.x > 1 && maskSize.y > 1);

    vec3 albedo;

    if (isTerrain)
    {
        // Base albedo and blend mask — sampled with original vertex UVs
        vec3 base = texture(s_Diffuse, v_TexCoord).rgb;
        vec4 mask = texture(s_Mask, v_TexCoord);

        // Detail UV from world-space XZ (no vertex UV discontinuity artifacts)
        // 0.045: converts dt_params scale (~48) to world tiles/m (~2.16)
        vec2 detailUV = v_WorldPosXZ * pc.u_DetailScale * 0.045;

        // Anti-tiling: 90-deg rotated UV at slightly offset scale, low blend
        vec2 detailUV2 = vec2(v_WorldPosXZ.y, -v_WorldPosXZ.x) * pc.u_DetailScale * 0.032;

        // Sample each detail layer with anti-tiling blend (12%)
        vec3 dR = mix(texture(s_DetailR, detailUV).rgb, texture(s_DetailR, detailUV2).rgb, 0.12);
        vec3 dG = mix(texture(s_DetailG, detailUV).rgb, texture(s_DetailG, detailUV2).rgb, 0.12);
        vec3 dB = mix(texture(s_DetailB, detailUV).rgb, texture(s_DetailB, detailUV2).rgb, 0.12);
        vec3 dA = mix(texture(s_DetailA, detailUV).rgb, texture(s_DetailA, detailUV2).rgb, 0.12);

        // Weighted blend of detail layers by mask
        float weightSum = mask.r + mask.g + mask.b + mask.a;
        vec3 detail;
        if (weightSum > 0.001)
            detail = (dR * mask.r + dG * mask.g + dB * mask.b + dA * mask.a) / weightSum;
        else
            detail = vec3(0.5); // No mask data → neutral (no detail effect)

        // Modulate base color by detail; fade out at distance
        float dist = length(v_PositionEye);
        float detailFade = 1.0 - smoothstep(60.0, 150.0, dist);
        albedo = base * (1.0 + (detail - 0.5) * 2.0 * detailFade);
    }
    else
    {
        // Non-terrain geometry — just sample diffuse
        albedo = texture(s_Diffuse, v_TexCoord).rgb;
    }

    // Alpha test (conditional via push constant)
    // pc.u_AlphaRef > 0: enabled (DX11 uses 200/255 ≈ 0.784 for def_aref)
    // pc.u_AlphaRef <= 0: disabled (solid geometry — cars, walls, etc.)
    if (pc.u_AlphaRef > 0.0)
    {
        float alpha = texture(s_Diffuse, v_TexCoord).a;
        if (alpha < pc.u_AlphaRef)
            discard;
    }

    // Output albedo
#ifdef DEBUG_UV_COLORS
    // fract() reveals repeating patterns: smooth gradient = 1x, repeating = Nx tiling
    o_Color = vec4(fract(v_TexCoord), 0.0, 1.0);
#elif defined(DEBUG_UV_RANGE)
    // Show UV magnitude: R = abs(U)/16, G = abs(V)/16, blue if negative
    // Bright = large UV range = excessive tiling. E.g. UV=10 → R=0.625
    o_Color = vec4(abs(v_TexCoord.x) / 16.0, abs(v_TexCoord.y) / 16.0,
                   float(v_TexCoord.x < 0.0 || v_TexCoord.y < 0.0), 1.0);
#elif defined(DEBUG_TEX_SIZE)
    // Show diffuse texture dimensions as color:
    //   R = width/2048, G = height/2048
    //   Bright = large texture (1024→0.5), Dim = small/fallback (1→~0)
    ivec2 diffSize = textureSize(s_Diffuse, 0);
    o_Color = vec4(float(diffSize.x) / 2048.0, float(diffSize.y) / 2048.0, 0.0, 1.0);
#else
    o_Color = vec4(albedo, 1.0);
#endif

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
