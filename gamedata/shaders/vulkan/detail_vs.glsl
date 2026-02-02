#version 450
// xrRenderVulkan - Detail vertex shader (grass/debris)
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// SPDX-License-Identifier: MIT

// ============================================================================
// Input: Base mesh vertices
// ============================================================================
layout(location = 0) in vec3 aPos;         // Local position
layout(location = 1) in vec2 aUV;          // Texture coordinates
layout(location = 2) in float aHeight;     // Normalized height [0..1] for wind

// ============================================================================
// Input: Per-instance data
// ============================================================================
layout(location = 3) in vec4 aInstRow0;    // Transform matrix row 0
layout(location = 4) in vec4 aInstRow1;    // Transform matrix row 1
layout(location = 5) in vec4 aInstRow2;    // Transform matrix row 2
layout(location = 6) in vec4 aInstColor;   // (sun, sun, sun, hemi)

// ============================================================================
// Output to fragment shader
// ============================================================================
layout(location = 0) out vec2 vUV;
layout(location = 1) out vec4 vColor;
layout(location = 2) out float vHeight;

// ============================================================================
// Push constants
// ============================================================================
layout(push_constant) uniform DetailConstants
{
    mat4 mViewProj;         // View-projection matrix
    vec4 vWave;             // (freq_x, freq_z, speed, time)
    vec4 vWind;             // (dir.x, 0, dir.z, amplitude)
    vec4 vConsts;           // (scale_x, scale_y, l_aniso, l_ambient)
} pc;

// ============================================================================
// Wind animation function
// ============================================================================
vec3 ApplyWind(vec3 pos, float height)
{
    // Wind only affects upper part (based on height parameter)
    float wind_factor = height * height;  // Quadratic falloff

    // Wave motion (2 frequencies for more natural look)
    float phase1 = pos.x * pc.vWave.x + pos.z * pc.vWave.y + pc.vWave.w * pc.vWave.z;
    float phase2 = pos.x * pc.vWave.y * 0.7 + pos.z * pc.vWave.x * 1.3 - pc.vWave.w * pc.vWave.z * 0.5;

    float wave = sin(phase1) * 0.6 + sin(phase2) * 0.4;

    // Wind displacement
    vec3 wind_dir = normalize(vec3(pc.vWind.x, 0.0, pc.vWind.z));
    vec3 displacement = wind_dir * wave * pc.vWind.w * wind_factor;

    return pos + displacement;
}

// ============================================================================
// Main vertex shader
// ============================================================================
void main()
{
    // Reconstruct 3x4 instance transform matrix
    mat4x3 instTransform = mat4x3(
        aInstRow0.xyz,
        aInstRow1.xyz,
        aInstRow2.xyz,
        vec3(aInstRow0.w, aInstRow1.w, aInstRow2.w)
    );

    // Transform local vertex to world space
    vec3 worldPos = instTransform * vec4(aPos, 1.0);

    // Apply wind animation
    worldPos = ApplyWind(worldPos, aHeight);

    // Transform to clip space
    gl_Position = pc.mViewProj * vec4(worldPos, 1.0);

    // Pass UV coordinates
    vUV = aUV;

    // Calculate lighting
    // sun = directional, hemi = ambient
    float sun = aInstColor.r;
    float hemi = aInstColor.a;

    // Combine lighting (simple model for grass)
    float lighting = hemi * pc.vConsts.w + sun * (1.0 - pc.vConsts.w);

    // Anisotropic lighting (grass blades facing sun get more light)
    float aniso_factor = max(0.0, pc.vConsts.z) * 0.5 + 0.5;
    lighting *= aniso_factor;

    vColor = vec4(lighting, lighting, lighting, 1.0);

    // Pass height for debugging/effects
    vHeight = aHeight;
}
