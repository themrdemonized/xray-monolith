// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// SPDX-License-Identifier: MIT

// ============================================================================
// hud_model.vert - HUD 3D model vertex shader
// ============================================================================
//
// Renders HUD objects (weapons, hands) in screen space.
// Uses HUD-specific projection matrix that keeps objects on top.
//
// ============================================================================

#version 450

// ============================================================================
// Input Attributes (from vertex buffer)
// ============================================================================

layout(location = 0) in vec3 inPosition;    // World position
layout(location = 1) in vec3 inNormal;      // Vertex normal
layout(location = 2) in vec2 inUV;          // Texture coordinates

// ============================================================================
// Output to Fragment Shader
// ============================================================================

layout(location = 0) out vec2 fragUV;
layout(location = 1) out vec3 fragNormal;

// ============================================================================
// Push Constants (Transformation Matrices)
// ============================================================================

layout(push_constant) uniform PushConstants {
    mat4 worldViewProj;     // Combined world-view-projection
    mat4 world;             // World transform only
} pc;

// ============================================================================
// Main Vertex Shader
// ============================================================================

void main()
{
    // Transform to clip space using HUD projection
    gl_Position = pc.worldViewProj * vec4(inPosition, 1.0);

    // Transform normal to world space for lighting
    fragNormal = normalize((pc.world * vec4(inNormal, 0.0)).xyz);

    // Pass texture coordinates
    fragUV = inUV;
}
