#version 450
// xrRenderVulkan - Hierarchical Z-Buffer build compute shader
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// SPDX-License-Identifier: MIT
//
// Builds a mip chain for the HZB (Hierarchical Z-Buffer) for occlusion culling.
// Each mip level contains the MAX depth of the 2x2 region from the previous level.
//
// Level 0: Reads from depth buffer (sampled at half resolution)
// Level N: Reads from level N-1 storage image, writes max-depth to level N
//
// Dispatched once per mip level with appropriate push constants.

layout(local_size_x = 8, local_size_y = 8) in;

// ============================================================================
// Push constants
// ============================================================================
layout(push_constant) uniform HZBBuildConstants
{
    ivec2 srcSize;      // Source mip dimensions
    ivec2 dstSize;      // Destination mip dimensions
    uint  srcMip;       // Source mip level
    uint  dstMip;       // Destination mip level
    uint  isFirstPass;  // 1 = reading from depth buffer, 0 = reading from HZB
    uint  _pad;
} pc;

// ============================================================================
// Bindings
// ============================================================================
// Source: either depth buffer (first pass) or previous HZB mip
layout(set = 0, binding = 0) uniform sampler2D uSrcDepth;

// Destination: current HZB mip level (storage image)
layout(set = 0, binding = 1, r32f) writeonly uniform image2D uDstMip;

// ============================================================================
// Main
// ============================================================================
void main()
{
    ivec2 dstCoord = ivec2(gl_GlobalInvocationID.xy);
    if (dstCoord.x >= pc.dstSize.x || dstCoord.y >= pc.dstSize.y)
        return;

    // Map dst pixel to 2x2 region in source
    vec2 srcUV = (vec2(dstCoord) + 0.5) / vec2(pc.dstSize);

    // Sample 4 texels from source using bilinear filtering at quarter-pixel offsets
    vec2 texelSize = 1.0 / vec2(pc.srcSize);
    vec2 srcCenter = srcUV * vec2(pc.srcSize);

    // Read 4 corner samples
    float d00 = textureLod(uSrcDepth, (floor(srcCenter - 0.5) + 0.5) * texelSize, float(pc.srcMip)).r;
    float d10 = textureLod(uSrcDepth, (floor(srcCenter - 0.5) + vec2(1.5, 0.5)) * texelSize, float(pc.srcMip)).r;
    float d01 = textureLod(uSrcDepth, (floor(srcCenter - 0.5) + vec2(0.5, 1.5)) * texelSize, float(pc.srcMip)).r;
    float d11 = textureLod(uSrcDepth, (floor(srcCenter - 0.5) + vec2(1.5, 1.5)) * texelSize, float(pc.srcMip)).r;

    // Max depth (for conservative occlusion: if test depth > max, it's definitely behind)
    float maxDepth = max(max(d00, d10), max(d01, d11));

    imageStore(uDstMip, dstCoord, vec4(maxDepth, 0.0, 0.0, 0.0));
}
