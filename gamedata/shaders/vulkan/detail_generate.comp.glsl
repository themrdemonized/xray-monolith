#version 450
// xrRenderVulkan - GPU grass generation compute shader
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// SPDX-License-Identifier: MIT
//
// Replaces CPU cache_Decompress + detail_cull.comp:
// Generates grass instances procedurally from slot palette data + heightmap,
// performs frustum/distance culling, and stream-compacts visible instances
// into per-object-type sections of the output SSBO.
//
// Dispatch: one thread per potential grass position in the visible slot range.
// Each thread: lookup slot → palette dither → heightmap Y → cull → output.

layout(local_size_x = 256) in;

// ============================================================================
// Bindings
// ============================================================================

// Heightmap texture (R32F, baked from collision geometry at level load)
layout(set = 0, binding = 0) uniform sampler2D u_Heightmap;

// Slot data (read-only, one entry per slot in the level)
struct GpuSlot {
    float y_base;       // terrain base Y (from DetailSlot::r_ybase())
    float y_height;     // terrain height range
    uint  ids;          // id0:8 | id1:8 | id2:8 | id3:8 (0x3F = empty)
    uint  lighting;     // c_dir:16 | c_hemi:16 (fixed-point 0..65535)
    uint  palette0;     // obj0: a0:8|a1:8|a2:8|a3:8
    uint  palette1;     // obj1: a0:8|a1:8|a2:8|a3:8
    uint  palette2;     // obj2: a0:8|a1:8|a2:8|a3:8
    uint  palette3;     // obj3: a0:8|a1:8|a2:8|a3:8
};

layout(set = 0, binding = 1) readonly buffer SlotData {
    GpuSlot slots[];
} slotBuf;

// Detail object info (read-only, one per object type)
struct DetailObjInfo {
    float minScale;
    float maxScale;
    float bvRadius;
    uint  flags;        // DO_NO_WAVING = 0x0001
};

layout(set = 0, binding = 2) readonly buffer ObjInfo {
    DetailObjInfo objs[];
} objBuf;

// Output: compacted visible instances (same as DetailInstance = 64 bytes)
struct DetailInstance {
    vec4 row0;
    vec4 row1;
    vec4 row2;
    vec4 color;
};

layout(set = 0, binding = 3) writeonly buffer VisibleInstances {
    DetailInstance instances[];
} visInst;

// Atomic counters per object type
layout(set = 0, binding = 4) buffer AtomicCounters {
    uint counters[];
} atomics;

// Indirect draw commands
layout(set = 0, binding = 5) buffer IndirectCommands {
    uint cmds[];
} indirect;

// HZB texture for occlusion culling
layout(set = 0, binding = 6) uniform sampler2D u_HZB;

// ============================================================================
// Push constants
// ============================================================================
layout(push_constant) uniform PushConstants
{
    mat4 viewProj;              // 64 bytes
    vec4 frustumPlanes[6];      // 96 bytes
    vec4 cameraPos;             // 16 bytes (xyz=pos, w=time)
    vec4 fadeParams;            // 16 bytes (fadeStartSq, fadeLimitSq, fadeRangeSq, density)
    ivec4 slotRange;            // 16 bytes (minSX, minSZ, countX, countZ)
    // Total: 208 bytes
} pc;

// ============================================================================
// Generation UBO (params that don't fit in push constants)
// ============================================================================
layout(set = 0, binding = 7) uniform GenParams
{
    vec4  hmParams;     // (originX, originZ, invScaleX, invScaleZ)
    uvec4 genCounts;    // (totalPositions, numObjTypes, outputCapacity, posPerSlot)
    vec4  dtSlotParams;  // (dtH.offs_x, dtH.offs_z, dtH.size_x, dtH.size_z)
    float slotSize;     // DETAIL_SLOT_SIZE (2.0)
    float detailHeight; // ps_current_detail_height
    uint  hmWidth;
    uint  hmHeight;
} gen;

// ============================================================================
// Hash functions (deterministic RNG replacing CRandom)
// ============================================================================
uint hash(uint x)
{
    x = ((x >> 16) ^ x) * 0x45d9f3bu;
    x = ((x >> 16) ^ x) * 0x45d9f3bu;
    x = (x >> 16) ^ x;
    return x;
}

uint hash2(uint x, uint y)
{
    // Same as CPU: hash2(u32 x, u32 y)
    uint a = (x << 16) | (y & 0xFFFFu);
    return hash(a ^ 0x12071980u);
}

float hashFloat01(uint h)
{
    return float(h & 0xFFFFu) / 65535.0;
}

// Signed float in [-1, 1]
float hashFloatS(uint h)
{
    return hashFloat01(h) * 2.0 - 1.0;
}

// ============================================================================
// Dither pattern (matches CPU bwdithermap(2, dither))
// ============================================================================
float getDither(uint col, uint row)
{
    // Hardcoded 4x4 magic pattern from DetailManager.cpp
    const int m[16] = int[16](0, 14, 3, 13, 11, 5, 8, 6, 12, 2, 15, 1, 7, 9, 4, 10);

    int i = int(col & 3u);
    int j = int(row & 3u);
    int k = int((col >> 2) & 3u);
    int l = int((row >> 2) & 3u);

    // bwdithermap formula: N=255, levels=2, magicfact=(255-1)/16=15.875
    float magicfact = 254.0 / 16.0;
    return 0.5 + float(m[i * 4 + j]) * magicfact
             + (float(m[k * 4 + l]) / 16.0) * magicfact;
}

// ============================================================================
// Bilinear interpolation of 4 palette corners
// (matches CPU Interpolate() function)
// ============================================================================
float interpolateAlpha(uint palettePacked, uint x, uint z, uint d_size)
{
    // Unpack 4 corners: a0:8|a1:8|a2:8|a3:8
    float a0 = float(palettePacked & 0xFFu);
    float a1 = float((palettePacked >> 8) & 0xFFu);
    float a2 = float((palettePacked >> 16) & 0xFFu);
    float a3 = float((palettePacked >> 24) & 0xFFu);

    float f = float(d_size);
    float fx = float(x) / f;
    float ifx = 1.0 - fx;
    float fz = float(z) / f;
    float ifz = 1.0 - fz;

    float c01 = a0 * ifx + a1 * fx;
    float c23 = a2 * ifx + a3 * fx;
    float c02 = a0 * ifz + a2 * fz;
    float c13 = a1 * ifz + a3 * fz;

    float cx = ifz * c01 + fz * c23;
    float cy = ifx * c02 + fx * c13;
    return (cx + cy) * 0.5;
}

// ============================================================================
// Frustum test: sphere vs 6 planes
// ============================================================================
bool frustumTestSphere(vec3 center, float radius)
{
    for (int i = 0; i < 6; i++)
    {
        float dist = dot(pc.frustumPlanes[i].xyz, center) + pc.frustumPlanes[i].w;
        if (dist < -radius)
            return false;
    }
    return true;
}

// ============================================================================
// Get palette data for a given object slot index (0..3)
// ============================================================================
uint getSlotPalette(GpuSlot s, uint objIdx)
{
    if (objIdx == 0u) return s.palette0;
    if (objIdx == 1u) return s.palette1;
    if (objIdx == 2u) return s.palette2;
    return s.palette3;
}

uint getSlotId(uint ids, uint objIdx)
{
    return (ids >> (objIdx * 8u)) & 0xFFu;
}

// ============================================================================
// Main: generate one grass instance per thread
// ============================================================================
void main()
{
    uint gid = gl_GlobalInvocationID.x;
    if (gid >= gen.genCounts.x) // totalPositions
        return;

    // Decode: which slot and which position within slot
    uint posPerSlot = gen.genCounts.w;
    uint slotLocal  = gid / posPerSlot;
    uint posLocal   = gid % posPerSlot;

    uint d_size = uint(sqrt(float(posPerSlot - 1u)));  // grid dimension
    // Actually: posPerSlot = (d_size+1)^2, so d_size = sqrt(posPerSlot) - 1
    // But simpler: just pass d_size in genCounts or compute from density
    // For density=0.6, slotSize=2.0: d_size = ceil(2.0/0.6) = 4, posPerSlot = 25
    // So: d_size = 4
    d_size = uint(round(sqrt(float(posPerSlot)) - 1.0));

    uint gridX = posLocal % (d_size + 1u);
    uint gridZ = posLocal / (d_size + 1u);

    // Slot world coordinates
    int sx = pc.slotRange.x + int(slotLocal % uint(pc.slotRange.z));
    int sz = pc.slotRange.y + int(slotLocal / uint(pc.slotRange.z));

    // ---- Slot lookup ----
    // The slot SSBO contains the entire level's slot grid (dtH.size_x × dtH.size_z).
    // Convert world slot coords (sx,sz) to buffer index using dtH offsets from UBO.
    int db_x = sx + int(gen.dtSlotParams.x);  // dtH.offs_x
    int db_z = sz + int(gen.dtSlotParams.y);  // dtH.offs_z
    int dbSizeX = int(gen.dtSlotParams.z);    // dtH.size_x
    int dbSizeZ = int(gen.dtSlotParams.w);    // dtH.size_z

    // Bounds check
    if (db_x < 0 || db_x >= dbSizeX || db_z < 0 || db_z >= dbSizeZ)
        return;

    uint slotIdx = uint(db_z) * uint(dbSizeX) + uint(db_x);
    GpuSlot slot = slotBuf.slots[slotIdx];

    // Check if slot is empty (all 4 IDs = 0x3F)
    uint ID_EMPTY = 0x3Fu;
    uint id0 = getSlotId(slot.ids, 0u);
    uint id1 = getSlotId(slot.ids, 1u);
    uint id2 = getSlotId(slot.ids, 2u);
    uint id3 = getSlotId(slot.ids, 3u);
    if (id0 == ID_EMPTY && id1 == ID_EMPTY && id2 == ID_EMPTY && id3 == ID_EMPTY)
        return;

    // ---- Deterministic RNG (matches CPU hash2 + CRandom) ----
    uint seed = hash2(uint(sx) & 0xFFFFu, uint(sz) & 0xFFFFu);
    // Per-position seed
    uint posSeed = hash(seed ^ (gridX * 7u + gridZ * 13u + posLocal * 31u));

    // Jitter shifts (matches CPU r_jitter.randI(16))
    uint shift_x = hash(posSeed ^ 0xA100u) & 15u;
    uint shift_z = hash(posSeed ^ 0xA200u) & 15u;

    // ---- Palette dither: select which object to place ----
    // InterpolateAndDither for each of 4 object slots
    bool sel[4];
    uint selCount = 0u;
    uint ids[4] = uint[4](id0, id1, id2, id3);

    for (int i = 0; i < 4; i++)
    {
        sel[i] = false;
        if (ids[i] == ID_EMPTY) continue;

        float alpha = interpolateAlpha(getSlotPalette(slot, uint(i)),
                                       min(gridX, d_size - 1u),
                                       min(gridZ, d_size - 1u),
                                       d_size);
        // Round to int and clamp (matches CPU: iFloor(alpha + .5f), clamp 0-255)
        int c = int(alpha + 0.5);
        c = clamp(c, 0, 255);

        uint row = (gridZ + shift_z) & 15u;
        uint col = (gridX + shift_x) & 15u;
        float threshold = getDither(col, row);

        if (float(c) > threshold)
        {
            sel[i] = true;
            selCount++;
        }
    }

    if (selCount == 0u) return;

    // Select one from candidates (matches CPU r_selection.randI)
    uint chosenSlotIdx = 0u;
    if (selCount == 1u)
    {
        for (int i = 0; i < 4; i++)
            if (sel[i]) { chosenSlotIdx = uint(i); break; }
    }
    else
    {
        uint r = hash(posSeed ^ 0xB100u) % selCount;
        uint cnt = 0u;
        for (int i = 0; i < 4; i++)
        {
            if (sel[i])
            {
                if (cnt == r) { chosenSlotIdx = uint(i); break; }
                cnt++;
            }
        }
    }

    uint objId = ids[chosenSlotIdx];
    if (objId >= gen.genCounts.y) // numObjTypes
        return;

    DetailObjInfo obj = objBuf.objs[objId];

    // ---- World position (XZ) ----
    float slotMinX = float(sx) * gen.slotSize;
    float slotMinZ = float(sz) * gen.slotSize;

    float density = pc.fadeParams.w;
    float jitter = density / 1.7;

    float rx = (float(gridX) / float(d_size)) * gen.slotSize + slotMinX;
    float rz = (float(gridZ) / float(d_size)) * gen.slotSize + slotMinZ;

    // Apply jitter
    rx += hashFloatS(hash(posSeed ^ 0xC100u)) * jitter;
    rz += hashFloatS(hash(posSeed ^ 0xC200u)) * jitter;

    // ---- Distance culling (early out) ----
    vec3 delta = vec3(rx, 0.0, rz) - pc.cameraPos.xyz;
    float distSqXZ = delta.x * delta.x + delta.z * delta.z;
    if (distSqXZ > pc.fadeParams.y * 1.5) // fadeLimitSq with some margin for Y
        return;

    // ---- Get Y position ----
    // Primary source: slot.y_base from level compiler (authoritative ground height).
    // Heightmap refines within the slot's valid range for sub-slot precision.
    // If heightmap disagrees (bridges, rooftops stored as max Y), trust the slot.
    float terrainY = slot.y_base;

    vec2 hmUV;
    hmUV.x = (rx - gen.hmParams.x) * gen.hmParams.z;
    hmUV.y = (rz - gen.hmParams.y) * gen.hmParams.w;

    if (hmUV.x >= 0.0 && hmUV.x <= 1.0 && hmUV.y >= 0.0 && hmUV.y <= 1.0)
    {
        float hmY = texture(u_Heightmap, hmUV).r;
        // Only use heightmap if it's within the slot's ground range
        if (hmY >= slot.y_base - 1.0 && hmY <= slot.y_base + slot.y_height + 1.0)
            terrainY = hmY;
    }

    vec3 worldPos = vec3(rx, terrainY, rz);

    // ---- Full distance culling with Y ----
    vec3 fullDelta = worldPos - pc.cameraPos.xyz;
    float distSq = dot(fullDelta, fullDelta);
    if (distSq > pc.fadeParams.y) // fadeLimitSq
        return;

    // ---- Scale ----
    float scale = mix(obj.minScale * 0.5, obj.maxScale * 0.9,
                      hashFloat01(hash(posSeed ^ 0xD100u)));
    scale *= gen.detailHeight; // ps_current_detail_height

    // ---- Frustum culling ----
    float radius = obj.bvRadius * scale;
    if (!frustumTestSphere(worldPos, radius))
        return;

    // ---- HZB occlusion culling ----
    {
        vec4 clipPos = pc.viewProj * vec4(worldPos, 1.0);
        if (clipPos.w > 0.0)
        {
            vec2 ndc = clipPos.xy / clipPos.w;
            vec2 uv = ndc * 0.5 + 0.5;

            if (uv.x >= 0.0 && uv.x <= 1.0 && uv.y >= 0.0 && uv.y <= 1.0)
            {
                float projRadius = radius / clipPos.w;
                float screenRadius = projRadius * 0.5;
                float mipLevel = max(0.0, log2(max(1.0, 1.0 / (screenRadius * 512.0))));

                float hzbDepth = textureLod(u_HZB, uv, mipLevel).r;
                float instanceDepth = clipPos.z / clipPos.w;

                if (instanceDepth > hzbDepth && hzbDepth > 0.0)
                    return;
            }
        }
    }

    // ---- Fade alpha (scale-based, matches detail_cull.comp) ----
    float alpha = 1.0;
    if (distSq > pc.fadeParams.x) // fadeStartSq
    {
        alpha = 1.0 - (distSq - pc.fadeParams.x) / pc.fadeParams.z; // fadeRangeSq
        alpha = clamp(alpha, 0.0, 1.0);
    }

    float fadedScale = scale * alpha;
    if (fadedScale < 0.01)
        return;

    // ---- Build transform matrix (rotation Y + scale + translate) ----
    float yaw = hashFloat01(hash(posSeed ^ 0xE100u)) * 6.2831853; // 0..2π
    float cs = cos(yaw);
    float sn = sin(yaw);

    // RotateY * Scale matrix + translate
    // [ cs*s  0  sn*s  tx ]
    // [  0    s   0    ty ]
    // [-sn*s  0  cs*s  tz ]
    float s = fadedScale;

    // ---- Lighting from slot data ----
    float c_dir  = float(slot.lighting & 0xFFFFu) / 65535.0;
    float c_hemi = float((slot.lighting >> 16) & 0xFFFFu) / 65535.0;

    // ---- Atomic append to per-obj-type section ----
    uint localIdx = atomicAdd(atomics.counters[objId], 1u);

    uint outputCapacity = gen.genCounts.z;
    uint numObjTypes = gen.genCounts.y;
    uint sectionSize = outputCapacity / max(numObjTypes, 1u);

    if (localIdx >= sectionSize)
        return;

    uint outIdx = objId * sectionSize + localIdx;
    if (outIdx >= outputCapacity)
        return;

    // ---- Write output instance ----
    DetailInstance inst;
    inst.row0 = vec4( cs * s, 0.0,    sn * s, worldPos.x);
    inst.row1 = vec4( 0.0,    s,      0.0,    worldPos.y);
    inst.row2 = vec4(-sn * s, 0.0,    cs * s, worldPos.z);
    inst.color = vec4(c_dir, c_dir, c_dir, c_hemi);

    visInst.instances[outIdx] = inst;
}
