#version 450
#extension GL_EXT_nonuniform_qualifier : require
// xrRenderVulkan - Detail fragment shader (bindless multi-draw variant)
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// SPDX-License-Identifier: MIT

// ============================================================================
// Input from vertex shader
// ============================================================================
layout(location = 0) in vec2 vUV;
layout(location = 1) in vec4 vColor;
layout(location = 2) in float vHeight;
layout(location = 3) in flat float vTexIdx;

// ============================================================================
// Output
// ============================================================================
layout(location = 0) out vec4 outColor;

// ============================================================================
// Bindless texture array (all detail object textures)
// ============================================================================
layout(set = 1, binding = 0) uniform sampler2D uTextures[64];

// ============================================================================
// Push constants
// ============================================================================
layout(push_constant) uniform DetailConstants
{
    mat4 mViewProj;
    vec4 vWave;
    vec4 vWind;
    vec4 vConsts;
} pc;

// ============================================================================
// Main fragment shader
// ============================================================================
void main()
{
    // Index into texture array from instance data
    int idx = int(vTexIdx + 0.5);

    // Sample diffuse texture using nonuniform indexing
    vec4 diffuse = texture(uTextures[nonuniformEXT(idx)], vUV);

    // Alpha test (grass billboards need hard cutoff)
    const float alphaRef = 0.5;
    if (diffuse.a < alphaRef)
    {
        discard;
    }

    // Apply lighting
    vec3 color = diffuse.rgb * vColor.rgb;

    // Output final color
    outColor = vec4(color, 1.0);
}
