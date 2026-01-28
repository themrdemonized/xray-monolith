// ============================================================================
// vk_vertex_formats.h - Vulkan Vertex Format Definitions
// ============================================================================
//
// Phase 2.23.2: Vertex Format Definition
//
// Defines vertex structures and their Vulkan input state descriptors.
// These structures must match the vertex shader input layouts.
//
// ============================================================================

#pragma once

#include <vulkan/vulkan.h>
#include <array>

namespace VK {

// ============================================================================
// VertexStatic - Standard vertex format for static geometry
// ============================================================================
// Used for:
// - Level geometry (walls, floors, ceilings)
// - Static props and models
// - Any non-animated meshes
//
// Matches gbuffer.vert shader inputs:
//   layout(location = 0) in vec3 i_Position;
//   layout(location = 1) in vec3 i_Normal;
//   layout(location = 2) in vec2 i_TexCoord;
//
struct VertexStatic {
    Fvector position;   // 12 bytes (x, y, z)
    Fvector normal;     // 12 bytes (nx, ny, nz)
    Fvector2 uv;        // 8 bytes (u, v)

    // Total: 32 bytes per vertex

    // ========================================================================
    // Vulkan Vertex Input State
    // ========================================================================

    // Get binding description (how vertices are stored in buffer)
    static VkVertexInputBindingDescription GetBindingDescription();

    // Get attribute descriptions (how to extract each attribute)
    static std::array<VkVertexInputAttributeDescription, 3> GetAttributeDescriptions();
};

// ============================================================================
// VertexSkinned - Vertex format for skinned/animated geometry
// ============================================================================
// TODO Phase 2.24: Add for character models
// Will include:
// - Bone indices (4 bones per vertex)
// - Bone weights (4 weights per vertex)
//
// Total: 56 bytes per vertex
//
struct VertexSkinned {
    Fvector position;
    Fvector normal;
    Fvector2 uv;
    u8 boneIndices[4];  // 4 bytes
    float boneWeights[4]; // 16 bytes

    // Total: 56 bytes per vertex

    // TODO: Implement when needed
    // static VkVertexInputBindingDescription GetBindingDescription();
    // static std::array<VkVertexInputAttributeDescription, 5> GetAttributeDescriptions();
};

// ============================================================================
// Helper Functions
// ============================================================================

// Get vertex input state for static geometry (used in pipeline creation)
VkPipelineVertexInputStateCreateInfo GetStaticVertexInputState();

} // namespace VK
