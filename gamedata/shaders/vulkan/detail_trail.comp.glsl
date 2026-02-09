#version 450
// xrRenderVulkan - Trail map update compute shader
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// SPDX-License-Identifier: MIT
//
// Updates the grass trail map (R16F) each frame:
// 1. Fades existing trail values toward 0 (recovery over ~30 seconds)
// 2. Stamps circles at character positions (player + NPCs)
//
// Single image, in-place read-modify-write (each thread owns its texel).

layout(local_size_x = 16, local_size_y = 16) in;

// Trail map image (R16F, same resolution/coverage as heightmap)
layout(set = 0, binding = 0, r16f) uniform image2D u_TrailMap;

layout(push_constant) uniform TrailPC
{
    vec4  interactors[4]; // 64B — xyz=worldPos, w=radius (0=unused)
    float originX;        // World X of texel [0,0]
    float originZ;        // World Z of texel [0,0]
    float texelSizeX;     // World meters per texel X
    float texelSizeZ;     // World meters per texel Z
    float fadeRate;        // Multiply factor per frame: pow(0.5, dt/halfLife)
    uint  mapW;           // Trail map width in texels
    uint  mapH;           // Trail map height in texels
    float _pad;
} pc; // Total: 96 bytes

void main()
{
    ivec2 texel = ivec2(gl_GlobalInvocationID.xy);
    if (texel.x >= int(pc.mapW) || texel.y >= int(pc.mapH))
        return;

    // Current trail value
    float value = imageLoad(u_TrailMap, texel).r;

    // Fade toward 0 (grass recovery)
    value *= pc.fadeRate;

    // World position of this texel center
    float worldX = pc.originX + (float(texel.x) + 0.5) * pc.texelSizeX;
    float worldZ = pc.originZ + (float(texel.y) + 0.5) * pc.texelSizeZ;

    // Stamp interactor circles
    for (int i = 0; i < 4; i++)
    {
        float r = pc.interactors[i].w;
        if (r < 0.01) continue; // Unused slot

        float dx = worldX - pc.interactors[i].x;
        float dz = worldZ - pc.interactors[i].z;
        float distSq = dx * dx + dz * dz;
        float rSq = r * r;

        if (distSq < rSq)
        {
            // Smooth circular stamp with quadratic falloff at edges
            float t = 1.0 - sqrt(distSq) / r;
            float stamp = t * t; // Quadratic: strong center, soft edges
            value = max(value, stamp);
        }
    }

    value = clamp(value, 0.0, 1.0);
    imageStore(u_TrailMap, texel, vec4(value, 0.0, 0.0, 0.0));
}
