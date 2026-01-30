// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// SPDX-License-Identifier: MIT

// ============================================================================
// particle.vert - Particle billboard vertex shader
// ============================================================================
//
// Renders particles as billboards with alpha blending.
// Input particles are simple quads in world space.
//
// ============================================================================

#version 450

// ============================================================================
// Input Attributes (from vertex buffer)
// ============================================================================

layout(location = 0) in vec3 inPosition;    // World position
layout(location = 1) in vec4 inColor;       // RGBA color
layout(location = 2) in vec2 inUV;          // Texture coordinates

// ============================================================================
// Output to Fragment Shader
// ============================================================================

layout(location = 0) out vec4 fragColor;
layout(location = 1) out vec2 fragUV;

// ============================================================================
// Push Constants (View-Projection Matrix)
// ============================================================================

layout(push_constant) uniform PushConstants {
    mat4 viewProj;  // Combined view-projection matrix
} pc;

// ============================================================================
// Main Vertex Shader
// ============================================================================

void main()
{
    // Transform to clip space
    gl_Position = pc.viewProj * vec4(inPosition, 1.0);

    // Pass color and UV to fragment shader
    fragColor = inColor;
    fragUV = inUV;
}
