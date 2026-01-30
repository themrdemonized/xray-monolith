// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// SPDX-License-Identifier: MIT

// ============================================================================
// hud_model.frag - HUD 3D model fragment shader
// ============================================================================
//
// Renders HUD objects with simple lighting (no shadows).
// Applies base texture with minimal lighting calculations.
//
// ============================================================================

#version 450

// ============================================================================
// Input from Vertex Shader
// ============================================================================

layout(location = 0) in vec2 fragUV;
layout(location = 1) in vec3 fragNormal;

// ============================================================================
// Texture Sampling
// ============================================================================

layout(binding = 0) uniform sampler2D texBase;

// ============================================================================
// Output
// ============================================================================

layout(location = 0) out vec4 outColor;

// ============================================================================
// Constants
// ============================================================================

const float MIN_LIGHTING = 0.3;  // Minimum light level even in shadows
const vec3 HUD_LIGHT_DIR = vec3(0.0, 0.0, 1.0);  // Light points forward in HUD space

// ============================================================================
// Main Fragment Shader
// ============================================================================

void main()
{
    // Sample base texture
    vec4 baseColor = texture(texBase, fragUV);

    // Simple directional lighting (no shadows in HUD)
    float NdotL = max(dot(fragNormal, HUD_LIGHT_DIR), 0.0);
    float lightIntensity = mix(MIN_LIGHTING, 1.0, NdotL);

    // Apply lighting to base color
    outColor = vec4(baseColor.rgb * lightIntensity, baseColor.a);
}
