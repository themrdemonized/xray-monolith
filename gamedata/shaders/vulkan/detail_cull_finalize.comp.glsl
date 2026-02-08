#version 450
// xrRenderVulkan - Detail cull finalize compute shader
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// SPDX-License-Identifier: MIT
//
// Reads atomic counters per object type and writes VkDrawIndexedIndirectCommand
// structs. Also stores base offsets for use by the draw phase.
// Dispatched with 1 workgroup (local_size = 64, covers all 64 possible obj types).

layout(local_size_x = 64) in;

// ============================================================================
// Buffers (same descriptor set as cull shader)
// ============================================================================

// binding 0: AllInstances (not used here)
layout(set = 0, binding = 0) readonly buffer AllInstances {
    vec4 _unused[];
} allInst;

// binding 1: VisibleInstances (not used here)
layout(set = 0, binding = 1) buffer VisibleInstances {
    vec4 _unused[];
} visInst;

// binding 2: Atomic counters
// [0..63] = per-obj-type visible count (written by cull shader)
// [64..127] = base offset in VisibleSSBO (written here)
layout(set = 0, binding = 2) buffer AtomicCounters {
    uint counters[];
} atomics;

// binding 3: Indirect draw commands
// Each VkDrawIndexedIndirectCommand = 5 uint32:
//   indexCount, instanceCount, firstIndex, vertexOffset, firstInstance
// NOT writeonly — we need to read pre-filled indexCount from CPU
layout(set = 0, binding = 3) buffer IndirectCommands {
    uint cmds[];
} indirect;

// binding 4: HZB (not used here)
layout(set = 0, binding = 4) uniform sampler2D uHZB;

// ============================================================================
// Push constants (same layout as cull shader)
// ============================================================================
layout(push_constant) uniform PushConstants
{
    mat4 viewProj;
    vec4 frustumPlanes[6];
    vec4 cameraPos;
    vec4 fadeParams;
    uvec4 counts;   // (totalInstances, numObjTypes, outputCapacity, 0)
} pc;

// ============================================================================
// Main
// ============================================================================
void main()
{
    uint objId = gl_GlobalInvocationID.x;
    if (objId >= pc.counts.y)
        return;

    // Clamp visible count to section size (matches cull shader clamping)
    uint sectionSize = pc.counts.z / max(pc.counts.y, 1u);
    uint visibleCount = min(atomics.counters[objId], sectionSize);

    // Compute base offset in VisibleSSBO for this object type
    uint baseOffset = objId * sectionSize;

    // Store base offset
    atomics.counters[64 + objId] = baseOffset;

    // Write VkDrawIndexedIndirectCommand
    // indexCount was pre-filled by CPU before the compute dispatch — preserve it
    uint cmdBase = objId * 5;
    // indirect.cmds[cmdBase + 0] is already set (indexCount, pre-filled by CPU)
    indirect.cmds[cmdBase + 1] = visibleCount;  // instanceCount
    // indirect.cmds[cmdBase + 2] is already 0 (firstIndex)
    // indirect.cmds[cmdBase + 3] is already 0 (vertexOffset)
    // indirect.cmds[cmdBase + 4] is already 0 (firstInstance)
}
