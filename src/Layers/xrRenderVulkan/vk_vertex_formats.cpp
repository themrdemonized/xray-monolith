// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// Licensed under the same terms as X-Ray Engine (see root License.txt)

// ============================================================================
// vk_vertex_formats.cpp - Vertex Format Implementation
// ============================================================================

#include "stdafx.h"
#include "vk_vertex_formats.h"

namespace VK {

// ============================================================================
// VertexStatic::GetBindingDescription
// ============================================================================
VkVertexInputBindingDescription VertexStatic::GetBindingDescription()
{
    VkVertexInputBindingDescription bindingDescription = {};

    // Binding 0: vertex buffer
    bindingDescription.binding = 0;
    bindingDescription.stride = sizeof(VertexStatic);  // 32 bytes
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;  // Per-vertex data

    return bindingDescription;
}

// ============================================================================
// VertexStatic::GetAttributeDescriptions
// ============================================================================
std::array<VkVertexInputAttributeDescription, 3> VertexStatic::GetAttributeDescriptions()
{
    std::array<VkVertexInputAttributeDescription, 3> attributeDescriptions = {};

    // ========================================================================
    // Location 0: Position (vec3)
    // ========================================================================
    // Matches shader: layout(location = 0) in vec3 i_Position;
    attributeDescriptions[0].binding = 0;
    attributeDescriptions[0].location = 0;
    attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;  // vec3 (float, float, float)
    attributeDescriptions[0].offset = offsetof(VertexStatic, position);

    // ========================================================================
    // Location 1: Normal (vec3)
    // ========================================================================
    // Matches shader: layout(location = 1) in vec3 i_Normal;
    attributeDescriptions[1].binding = 0;
    attributeDescriptions[1].location = 1;
    attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;  // vec3 (float, float, float)
    attributeDescriptions[1].offset = offsetof(VertexStatic, normal);

    // ========================================================================
    // Location 2: TexCoord (vec2)
    // ========================================================================
    // Matches shader: layout(location = 2) in vec2 i_TexCoord;
    attributeDescriptions[2].binding = 0;
    attributeDescriptions[2].location = 2;
    attributeDescriptions[2].format = VK_FORMAT_R32G32_SFLOAT;  // vec2 (float, float)
    attributeDescriptions[2].offset = offsetof(VertexStatic, uv);

    return attributeDescriptions;
}

// ============================================================================
// GetStaticVertexInputState
// ============================================================================
// Helper function to get complete vertex input state for pipelines.
// Note: Returns a static variable that persists across calls.
//
VkPipelineVertexInputStateCreateInfo GetStaticVertexInputState()
{
    // Static storage for binding/attribute descriptors
    static VkVertexInputBindingDescription bindingDescription =
        VertexStatic::GetBindingDescription();

    static auto attributeDescriptions =
        VertexStatic::GetAttributeDescriptions();

    VkPipelineVertexInputStateCreateInfo vertexInputState = {};
    vertexInputState.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    // Binding description (1 vertex buffer)
    vertexInputState.vertexBindingDescriptionCount = 1;
    vertexInputState.pVertexBindingDescriptions = &bindingDescription;

    // Attribute descriptions (3 attributes: position, normal, uv)
    vertexInputState.vertexAttributeDescriptionCount =
        static_cast<u32>(attributeDescriptions.size());
    vertexInputState.pVertexAttributeDescriptions = attributeDescriptions.data();

    return vertexInputState;
}

} // namespace VK
