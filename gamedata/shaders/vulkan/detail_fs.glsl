#version 450
// xrRenderVulkan - Detail fragment shader (grass/debris)
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// SPDX-License-Identifier: MIT

// ============================================================================
// Input from vertex shader
// ============================================================================
layout(location = 0) in vec2 vUV;
layout(location = 1) in vec4 vColor;
layout(location = 2) in float vHeight;

// ============================================================================
// Output
// ============================================================================
layout(location = 0) out vec4 outColor;

// ============================================================================
// Textures
// ============================================================================
layout(set = 0, binding = 0) uniform sampler2D uDiffuse;

// ============================================================================
// Push constants (if needed for alpha ref)
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
    // Sample diffuse texture
    vec4 diffuse = texture(uDiffuse, vUV);

    // Alpha test (grass billboards need hard cutoff)
    const float alphaRef = 0.5;
    if (diffuse.a < alphaRef)
    {
        discard;
    }

    // Apply lighting
    vec3 color = diffuse.rgb * vColor.rgb;

    // Optionally add height-based tinting (darker at base)
    // color *= mix(0.8, 1.0, vHeight);

    // Output final color
    outColor = vec4(color, 1.0);
}
