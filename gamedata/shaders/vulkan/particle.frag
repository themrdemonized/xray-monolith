// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// SPDX-License-Identifier: MIT

// ============================================================================
// particle.frag - Particle billboard fragment shader
// ============================================================================
//
// Samples particle texture and applies vertex color with alpha blending.
// Supports alpha test for texture cutout (no semi-transparent edges).
//
// ============================================================================

#version 450

// ============================================================================
// Input from Vertex Shader
// ============================================================================

layout(location = 0) in vec4 fragColor;     // Vertex color (RGBA)
layout(location = 1) in vec2 fragUV;        // Texture coordinates

// ============================================================================
// Texture Sampling
// ============================================================================

layout(binding = 0) uniform sampler2D texSampler;

// ============================================================================
// Output
// ============================================================================

layout(location = 0) out vec4 outColor;

// ============================================================================
// Constants
// ============================================================================

const float ALPHA_TEST_THRESHOLD = 0.01;

// ============================================================================
// Main Fragment Shader
// ============================================================================

void main()
{
    // Sample texture
    vec4 texColor = texture(texSampler, fragUV);

    // Combine texture color with vertex color
    outColor = texColor * fragColor;

    // Alpha test - discard fully transparent pixels
    if (outColor.a < ALPHA_TEST_THRESHOLD) {
        discard;
    }
}
