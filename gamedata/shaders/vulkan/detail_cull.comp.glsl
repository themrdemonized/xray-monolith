#version 450
// xrRenderVulkan - Detail GPU culling compute shader
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// SPDX-License-Identifier: MIT
//
// Reads persistent SSBO of all detail instances, performs frustum culling,
// distance culling, HZB occlusion, fade computation, and stream-compacts
// visible instances into per-object-type sections using atomic counters.
// Each type gets outputCapacity/numObjTypes output slots.

layout(local_size_x = 256) in;

// ============================================================================
// Input: All decompressed instances (persistent SSBO)
// ============================================================================
struct GpuDetailInstanceExt
{
    vec4  row0;         // (m11*s, m12*s, m13*s, tx)
    vec4  row1;         // (m21*s, m22*s, m23*s, ty)
    vec4  row2;         // (m31*s, m32*s, m33*s, tz)
    vec4  color;        // (sun, sun, sun, hemi)
    uint  obj_id;       // Detail object type [0..63], 0xFFFFFFFF=dead
    float base_scale;   // Original scale before fade
    float bv_radius;    // Bounding sphere radius
    uint  _pad;
};

layout(set = 0, binding = 0) readonly buffer AllInstances {
    GpuDetailInstanceExt instances[];
} allInst;

// ============================================================================
// Output: Compacted visible instances (same layout as DetailInstance = 64 bytes)
// ============================================================================
struct DetailInstance
{
    vec4 row0;
    vec4 row1;
    vec4 row2;
    vec4 color;
};

layout(set = 0, binding = 1) writeonly buffer VisibleInstances {
    DetailInstance instances[];
} visInst;

// ============================================================================
// Atomic counters: per-object-type instance counts
// Layout: [0..63] = current count, [64..127] = base offset (set by finalize)
// ============================================================================
layout(set = 0, binding = 2) buffer AtomicCounters {
    uint counters[];
} atomics;

// ============================================================================
// Indirect draw commands (written by finalize shader, not touched here)
// ============================================================================
layout(set = 0, binding = 3) buffer IndirectCommands {
    uint cmds[];
} indirect;

// ============================================================================
// HZB texture for occlusion culling
// ============================================================================
layout(set = 0, binding = 4) uniform sampler2D uHZB;

// ============================================================================
// Push constants
// ============================================================================
layout(push_constant) uniform PushConstants
{
    mat4 viewProj;              // 64 bytes
    vec4 frustumPlanes[6];      // 96 bytes
    vec4 cameraPos;             // 16 bytes (xyz=pos, w=unused)
    vec4 fadeParams;            // 16 bytes (fadeStartSq, fadeLimitSq, fadeRangeSq, time)
    uvec4 counts;               // 16 bytes (totalInstances, numObjTypes, outputCapacity, 0)
} pc;

// ============================================================================
// Frustum test: sphere vs 6 planes
// ============================================================================
bool FrustumTestSphere(vec3 center, float radius)
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
// Main compute shader
// ============================================================================
void main()
{
    uint gid = gl_GlobalInvocationID.x;
    if (gid >= pc.counts.x)
        return;

    GpuDetailInstanceExt inst = allInst.instances[gid];

    // Skip dead instances
    if (inst.obj_id >= pc.counts.y)
        return;

    // World position from transform
    vec3 worldPos = vec3(inst.row0.w, inst.row1.w, inst.row2.w);

    // Distance culling
    vec3 delta = worldPos - pc.cameraPos.xyz;
    float distSq = dot(delta, delta);

    if (distSq > pc.fadeParams.y)  // fadeLimitSq
        return;

    // Frustum culling (sphere test)
    float radius = inst.bv_radius * inst.base_scale;
    if (!FrustumTestSphere(worldPos, radius))
        return;

    // HZB occlusion culling
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

                float hzbDepth = textureLod(uHZB, uv, mipLevel).r;
                float instanceDepth = clipPos.z / clipPos.w;

                if (instanceDepth > hzbDepth && hzbDepth > 0.0)
                    return;
            }
        }
    }

    // Compute fade alpha (scale-based fade)
    float alpha = 1.0;
    if (distSq > pc.fadeParams.x)  // fadeStartSq
    {
        alpha = 1.0 - (distSq - pc.fadeParams.x) / pc.fadeParams.z;  // fadeRangeSq
        alpha = clamp(alpha, 0.0, 1.0);
    }

    float fadedScale = inst.base_scale * alpha;
    if (fadedScale < 0.01)
        return;

    float scaleRatio = fadedScale / max(inst.base_scale, 0.001);

    // Atomic append to per-obj-type section
    uint objId = inst.obj_id;
    uint localIdx = atomicAdd(atomics.counters[objId], 1);

    // Section size from output buffer capacity (counts.z), NOT input instance count
    uint sectionSize = pc.counts.z / max(pc.counts.y, 1u);

    // Strict clamp: never write past this type's section boundary
    if (localIdx >= sectionSize)
        return;

    uint outIdx = objId * sectionSize + localIdx;
    // Safety: don't exceed output buffer
    if (outIdx >= pc.counts.z)
        return;

    // Write compacted DetailInstance with faded transform
    DetailInstance out_inst;
    out_inst.row0 = vec4(inst.row0.xyz * scaleRatio, inst.row0.w);
    out_inst.row1 = vec4(inst.row1.xyz * scaleRatio, inst.row1.w);
    out_inst.row2 = vec4(inst.row2.xyz * scaleRatio, inst.row2.w);
    out_inst.color = inst.color;

    visInst.instances[outIdx] = out_inst;
}
