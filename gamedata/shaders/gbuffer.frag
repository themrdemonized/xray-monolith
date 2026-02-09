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
layout(location = 3) in vec3 v_WorldPos;     // World-space position (XYZ for triplanar)
layout(location = 4) in vec3 v_WorldNormal;  // World-space normal (for triplanar blend weights)
layout(location = 5) in vec4 v_CurrClipPos;  // Current clip-space position (for motion vectors)
layout(location = 6) in vec4 v_PrevClipPos;  // Previous clip-space position (for motion vectors)

// Multiple Render Targets (MRT) - 5 outputs
layout(location = 0) out vec4 o_Position;    // → rt_Position    (R32G32B32A32_SFLOAT)
layout(location = 1) out vec4 o_Normal;      // → rt_Normal      (R32G32B32A32_SFLOAT)
layout(location = 2) out vec4 o_Color;       // → rt_Color       (R8G8B8A8_SRGB)
layout(location = 3) out vec4 o_Material;    // → rt_Material    (R8G8B8A8_UNORM)
layout(location = 4) out vec2 o_MotionVec;   // → rt_MotionVector (R16G16_SFLOAT)

// Push constants (shared with vertex shader)
// Offset 196 is u_SkinMode in skinned vertex shader, so u_AlphaRef goes at 200
layout(push_constant) uniform PushConstants
{
    layout(offset = 200) float u_AlphaRef;     // Alpha test threshold (-1.0 = disabled, 0.5 = enabled)
    layout(offset = 204) float u_DetailScale;  // Terrain detail UV multiplier (from .thm dt_params)
} pc;

// Descriptor Set 0: GlobalLighting UBO — only jitter needed here
layout(std140, set = 0, binding = 0) uniform GlobalLighting
{
    vec4 _pad_frag[75];    // offsets 0-1199 (75 * 16 bytes, not used in this shader)
    vec4 ssfx_jitter;      // offset 1200: (currJitterX, currJitterY, prevJitterX, prevJitterY)
} uGlobal;

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

// ============================================================================
// Hex Tiling: eliminates visible texture repetition
// ============================================================================
//
// Problem: detail textures tile every ~0.5m. From above (helicopter view on
// Cordon) the repeating pattern is obvious — a checkerboard of identical
// grass/dirt patches that kills the sense of scale.
//
// Solution: divide UV space into hexagonal cells (via equilateral triangle
// grid). Each cell randomly rotates the texture. Three overlapping cells
// are blended using smooth barycentric weights. The eye sees no repeating
// pattern because each cell has a unique orientation.
//
// Cost: 3 texture samples per call (vs 1 for plain texture, 2 for old
// anti-tiling). Uses textureGrad with original-UV gradients to prevent
// mip-level seams at hex cell boundaries.
//
// hexCellSize controls how many texture tiles fit in one hex cell.
// Larger = less visible hex edges, but repetition can show within a cell.
// Smaller = more variation, but hex grid pattern may become visible.
// Default 8.0 ≈ 3.7m per cell at 2.16 tiles/m — good balance.
//
// FUTURE: Current blend is naive linear (s0*w0 + s1*w1 + s2*w2) which
// averages out contrast — directional textures (grass) lose detail.
// Workaround: rotation restricted to 90-deg multiples.
// Proper fix: Heitz 2019 "Histogram-Preserving Blending" — replaces
// linear blend with a histogram-preserving operator that keeps the
// original color distribution intact. Needs a 1D LUT texture per
// detail texture (precomputed CDF). Same GPU cost (3 samples + 1 LUT),
// but requires CPU-side LUT generation + extra binding in descriptor set.
//
// ============================================================================

// Hash: deterministic pseudo-random vec2 from grid cell ID
vec2 hex_hash(vec2 p)
{
    return fract(sin(vec2(dot(p, vec2(127.1, 311.7)),
                          dot(p, vec2(269.5, 183.3)))) * 43758.5453);
}

// Sample texture with hex tiling (3 samples, no visible repetition)
vec3 sampleHex(sampler2D tex, vec2 uv)
{
    const float hexCellSize = 8.0; // texture tiles per hex cell
    float invCell = 1.0 / hexCellSize;

    // Transform UV to equilateral triangle grid
    vec2 p = vec2(uv.x * invCell + uv.y * invCell * 0.57735027,
                  uv.y * invCell * 1.15470054);

    vec2 pi = floor(p);
    vec2 pf = fract(p);

    // Which triangle half (lower-left or upper-right of the quad cell)
    float tri = step(1.0, pf.x + pf.y);

    // Three vertices of the current triangle
    vec2 v0 = pi + mix(vec2(0.0), vec2(1.0), tri);
    vec2 v1 = pi + vec2(1.0, 0.0);
    vec2 v2 = pi + vec2(0.0, 1.0);

    // Barycentric weights
    float b0, b1, b2;
    if (tri < 0.5) {
        b0 = 1.0 - pf.x - pf.y;
        b1 = pf.x;
        b2 = pf.y;
    } else {
        b0 = pf.x + pf.y - 1.0;
        b1 = 1.0 - pf.y;
        b2 = 1.0 - pf.x;
    }

    // Smooth weights for seamless blending between hex cells
    b0 = smoothstep(0.0, 0.5, b0);
    b1 = smoothstep(0.0, 0.5, b1);
    b2 = smoothstep(0.0, 0.5, b2);
    float bsum = b0 + b1 + b2;
    b0 /= bsum; b1 /= bsum; b2 /= bsum;

    // Random 90-degree rotation + offset per vertex.
    // Full 360 deg rotation washes out directional textures (grass blades
    // cancel out when blended at arbitrary angles). 90-deg multiples
    // (0, 90, 180, 270) preserve texture structure while still breaking
    // the visible tiling pattern with 4 distinct orientations.
    vec2 r0 = hex_hash(v0), r1 = hex_hash(v1), r2 = hex_hash(v2);

    float a0 = floor(r0.x * 4.0) * 1.5707963; // 0, 90, 180, or 270 deg
    float a1 = floor(r1.x * 4.0) * 1.5707963;
    float a2 = floor(r2.x * 4.0) * 1.5707963;

    vec2 uv0 = mat2(cos(a0), -sin(a0), sin(a0), cos(a0)) * uv + r0;
    vec2 uv1 = mat2(cos(a1), -sin(a1), sin(a1), cos(a1)) * uv + r1;
    vec2 uv2 = mat2(cos(a2), -sin(a2), sin(a2), cos(a2)) * uv + r2;

    // Use original-UV gradients to avoid mip-level discontinuities
    // at hex cell boundaries (where rotation changes abruptly).
    // Rotation preserves gradient magnitude → correct mip selection.
    vec2 dx = dFdx(uv);
    vec2 dy = dFdy(uv);

    vec3 s0 = textureGrad(tex, uv0, dx, dy).rgb;
    vec3 s1 = textureGrad(tex, uv1, dx, dy).rgb;
    vec3 s2 = textureGrad(tex, uv2, dx, dy).rgb;

    return s0 * b0 + s1 * b1 + s2 * b2;
}

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
    // 5. HEX TILING (replaces old 2-octave anti-tiling):
    //    Divides UV space into hexagonal cells via equilateral triangle grid.
    //    Each cell randomly rotates the texture UV (90-deg multiples). Three
    //    overlapping cells are blended with smooth barycentric weights.
    //    Eliminates visible repetition completely. Uses textureGrad with
    //    original-UV derivatives to prevent mip seams at cell boundaries.
    //    Cost: 3 samples per detail texture (was 2 with old anti-tiling).
    //    hexCellSize (8.0) = tiles per hex cell. See sampleHex() above main().
    //
    // 5b. TRIPLANAR MAPPING:
    //    On flat terrain (worldNormal.y > 0.85) only XZ projection is used
    //    (cheapest path, same as before). On steep slopes (cliffs, ravines)
    //    blends XZ + XY + YZ projections weighted by world normal (pow=4
    //    for sharp transitions). Prevents texture stretching on slopes.
    //    Hex tiling only on dominant XZ axis; side projections use plain
    //    sampling (tiling not visible at oblique angles).
    //    Cost: flat = 12 samples, steep = 20 samples (4 detail * 5 each).
    //
    // 6. HEIGHT-BASED BLENDING:
    //    Instead of simple linear mix by mask weights, uses detail luminance
    //    as a "height" value. Materials with higher (mask_weight + luminance)
    //    win at transitions — grass blades grow over soil, stone ridges poke
    //    through dirt. blendDepth (0.2) controls sharpness: lower = sharper.
    //    Falls back to standard blend when all heights are similar.
    //    Detail modulated onto base: 0.5 = neutral, <0.5 = darken, >0.5 = brighten.
    //    Strength 2.0, fades out at distance (60-150m).
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

        // Detail UV scale: converts dt_params (~48) to world tiles/m (~2.16)
        float uvScale = pc.u_DetailScale * 0.045;

        // Triplanar mapping: on flat terrain (normal.y ≈ 1) use only top-down
        // XZ projection. On steep slopes, blend in front (XY) and side (YZ)
        // projections so the texture doesn't stretch.
        // Hex tiling only on dominant axis (XZ); plain sampling on secondary
        // axes where tiling isn't noticeable at oblique viewing angles.
        vec3 wN = normalize(v_WorldNormal);
        float flatness = abs(wN.y);

        // Top-down projection (XZ) — always computed, uses hex tiling
        vec2 uvTop = v_WorldPos.xz * uvScale;
        vec3 dR, dG, dB, dA;

        if (flatness > 0.85)
        {
            // Flat terrain (majority of surface): XZ only, cheapest path
            dR = sampleHex(s_DetailR, uvTop);
            dG = sampleHex(s_DetailG, uvTop);
            dB = sampleHex(s_DetailB, uvTop);
            dA = sampleHex(s_DetailA, uvTop);
        }
        else
        {
            // Steep slope: triplanar blend of 3 projections
            // Blend weights from world normal (sharpened with pow)
            vec3 tw = pow(abs(wN), vec3(4.0));
            tw /= dot(tw, vec3(1.0));

            // Front (XY) and side (YZ) projections — plain sampling (no hex)
            vec2 uvFront = v_WorldPos.xy * uvScale;
            vec2 uvSide  = v_WorldPos.yz * uvScale;

            dR = sampleHex(s_DetailR, uvTop) * tw.y
               + texture(s_DetailR, uvFront).rgb * tw.z
               + texture(s_DetailR, uvSide).rgb  * tw.x;
            dG = sampleHex(s_DetailG, uvTop) * tw.y
               + texture(s_DetailG, uvFront).rgb * tw.z
               + texture(s_DetailG, uvSide).rgb  * tw.x;
            dB = sampleHex(s_DetailB, uvTop) * tw.y
               + texture(s_DetailB, uvFront).rgb * tw.z
               + texture(s_DetailB, uvSide).rgb  * tw.x;
            dA = sampleHex(s_DetailA, uvTop) * tw.y
               + texture(s_DetailA, uvFront).rgb * tw.z
               + texture(s_DetailA, uvSide).rgb  * tw.x;
        }

        // Height-based blending: instead of simple linear mix by mask weights,
        // use detail luminance as "height" — brighter texels (grass blades, stone
        // ridges) win over darker ones (soil between blades, cracks). This gives
        // natural sharp transitions: dirt fills crevices, grass grows on top.
        // Luminance is used as height proxy because X-Ray detail textures may not
        // have a dedicated height channel in alpha (often DXT1/BC1).
        //
        // LIMITATION: luminance-as-height breaks on dark materials that should
        // be "on top" (e.g. wet/dark rocks sink under bright sand). When moving
        // to PBR materials, replace this with one of:
        //   a) real height in alpha channel (requires BC3/DXT5 detail textures)
        //   b) per-material height constant via push constant / UBO
        //   c) separate height texture per material (extra bindings + samples)
        float hR = dot(dR, vec3(0.299, 0.587, 0.114));
        float hG = dot(dG, vec3(0.299, 0.587, 0.114));
        float hB = dot(dB, vec3(0.299, 0.587, 0.114));
        float hA = dot(dA, vec3(0.299, 0.587, 0.114));

        // Blend sharpness: lower = sharper transitions, higher = softer.
        // 0.2 gives a good balance — visible material edges without hard cutoff.
        const float blendDepth = 0.2;
        float ma = max(max(hR + mask.r, hG + mask.g),
                       max(hB + mask.b, hA + mask.a)) - blendDepth;

        float bR = max(hR + mask.r - ma, 0.0);
        float bG = max(hG + mask.g - ma, 0.0);
        float bB = max(hB + mask.b - ma, 0.0);
        float bA = max(hA + mask.a - ma, 0.0);

        float bSum = bR + bG + bB + bA;
        vec3 detail;
        if (bSum > 0.001)
            detail = (dR * bR + dG * bG + dB * bB + dA * bA) / bSum;
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
    // 5. Output Motion Vectors (NDC-space velocity)
    // ========================================================================
    // Camera-only motion vectors: same worldPos, different VP matrix
    // Remove sub-pixel jitter from both frames for clean motion vectors (DLSS requirement)
    vec2 currNDC = v_CurrClipPos.xy / v_CurrClipPos.w;
    vec2 prevNDC = v_PrevClipPos.xy / v_PrevClipPos.w;
    currNDC -= uGlobal.ssfx_jitter.xy;
    prevNDC -= uGlobal.ssfx_jitter.zw;
    o_MotionVec = (currNDC - prevNDC) * 0.5;  // [-1,1] → [-0.5,0.5] range

    // ========================================================================
    // Notes:
    // ========================================================================
    // - All outputs в eye-space (consistent coordinate system для lighting)
    // - MRT позволяет заполнить 4 buffers за один draw call (efficient!)
    // - Depth buffer заполняется автоматически (gl_FragDepth)
    // - Phase 2.22 добавит material system (textures, normal maps, PBR)
    // ========================================================================
}
