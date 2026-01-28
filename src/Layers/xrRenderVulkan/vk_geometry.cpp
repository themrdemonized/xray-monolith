// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// SPDX-License-Identifier: MIT

#include "stdafx.h"
#include "vk_geometry.h"
#include "HW_Vulkan.h"
#include "../xrRender/du_sphere.h"
#include "../xrRender/du_cone.h"

namespace VK
{

CVulkanGeometry* g_VulkanGeometry = nullptr;

// ============================================================================
// Constructor / Destructor
// ============================================================================

CVulkanGeometry::CVulkanGeometry()
    : m_FullscreenQuadVB(VK_NULL_HANDLE)
    , m_FullscreenQuadIB(VK_NULL_HANDLE)
    , m_SphereVB(VK_NULL_HANDLE)
    , m_SphereIB(VK_NULL_HANDLE)
    , m_ConeVB(VK_NULL_HANDLE)
    , m_ConeIB(VK_NULL_HANDLE)
    , m_FullscreenQuadVertexCount(0)
    , m_FullscreenQuadIndexCount(0)
    , m_SphereVertexCount(0)
    , m_SphereIndexCount(0)
    , m_ConeVertexCount(0)
    , m_ConeIndexCount(0)
{
    Msg("[Vulkan] CVulkanGeometry::CVulkanGeometry()");
}

CVulkanGeometry::~CVulkanGeometry()
{
    Msg("[Vulkan] CVulkanGeometry::~CVulkanGeometry()");
    Destroy();
}

// ============================================================================
// Create / Destroy
// ============================================================================

void CVulkanGeometry::Create()
{
    Msg("[Vulkan] Creating geometry for deferred rendering...");

    CreateFullscreenQuad();
    CreateSphereVolume();
    CreateConeVolume();

    Msg("[Vulkan] Geometry created successfully");
}

void CVulkanGeometry::Destroy()
{
    if (!VulkanHW.m_Device) return;

    Msg("[Vulkan] Destroying geometry...");

    // Fullscreen quad
    if (m_FullscreenQuadVB != VK_NULL_HANDLE) {
        vmaDestroyBuffer(VulkanHW.m_Allocator, m_FullscreenQuadVB, m_FullscreenQuadVBAlloc);
        m_FullscreenQuadVB = VK_NULL_HANDLE;
    }
    if (m_FullscreenQuadIB != VK_NULL_HANDLE) {
        vmaDestroyBuffer(VulkanHW.m_Allocator, m_FullscreenQuadIB, m_FullscreenQuadIBAlloc);
        m_FullscreenQuadIB = VK_NULL_HANDLE;
    }

    // Sphere
    if (m_SphereVB != VK_NULL_HANDLE) {
        vmaDestroyBuffer(VulkanHW.m_Allocator, m_SphereVB, m_SphereVBAlloc);
        m_SphereVB = VK_NULL_HANDLE;
    }
    if (m_SphereIB != VK_NULL_HANDLE) {
        vmaDestroyBuffer(VulkanHW.m_Allocator, m_SphereIB, m_SphereIBAlloc);
        m_SphereIB = VK_NULL_HANDLE;
    }

    // Cone
    if (m_ConeVB != VK_NULL_HANDLE) {
        vmaDestroyBuffer(VulkanHW.m_Allocator, m_ConeVB, m_ConeVBAlloc);
        m_ConeVB = VK_NULL_HANDLE;
    }
    if (m_ConeIB != VK_NULL_HANDLE) {
        vmaDestroyBuffer(VulkanHW.m_Allocator, m_ConeIB, m_ConeIBAlloc);
        m_ConeIB = VK_NULL_HANDLE;
    }

    Msg("[Vulkan] Geometry destroyed");
}

// ============================================================================
// Fullscreen Quad
// ============================================================================

void CVulkanGeometry::CreateFullscreenQuad()
{
    Msg("[Vulkan]   Creating fullscreen quad...");

    // Vertex format: position (XYZW clip space) + UV
    struct Vertex {
        float x, y, z, w;
        float u, v;
    };

    // Fullscreen quad vertices (clip space: -1..1)
    Vertex vertices[4] = {
        {-1.0f, -1.0f, 0.0f, 1.0f,  0.0f, 1.0f},  // Bottom-left
        {-1.0f,  1.0f, 0.0f, 1.0f,  0.0f, 0.0f},  // Top-left
        { 1.0f, -1.0f, 0.0f, 1.0f,  1.0f, 1.0f},  // Bottom-right
        { 1.0f,  1.0f, 0.0f, 1.0f,  1.0f, 0.0f}   // Top-right
    };

    // Indices (two triangles)
    u16 indices[6] = {
        0, 1, 2,  // First triangle
        2, 1, 3   // Second triangle
    };

    m_FullscreenQuadVertexCount = 4;
    m_FullscreenQuadIndexCount = 6;

    // Create vertex buffer
    CreateBuffer(m_FullscreenQuadVB, m_FullscreenQuadVBAlloc,
                sizeof(vertices), vertices,
                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                "FullscreenQuad_VB");

    // Create index buffer
    CreateBuffer(m_FullscreenQuadIB, m_FullscreenQuadIBAlloc,
                sizeof(indices), indices,
                VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                "FullscreenQuad_IB");

    Msg("[Vulkan]   Fullscreen quad created: %d vertices, %d indices",
        m_FullscreenQuadVertexCount, m_FullscreenQuadIndexCount);
}

// ============================================================================
// Sphere Volume (for point lights)
// ============================================================================

void CVulkanGeometry::CreateSphereVolume()
{
    Msg("[Vulkan]   Creating sphere volume...");

    // Use du_sphere vertices/indices from xrRender
    m_SphereVertexCount = DU_SPHERE_NUMVERTEX;
    m_SphereIndexCount = DU_SPHERE_NUMFACES * 3;

    // Vertex buffer (Fvector = 3 floats)
    CreateBuffer(m_SphereVB, m_SphereVBAlloc,
                m_SphereVertexCount * sizeof(Fvector),
                (void*)du_sphere_vertices,
                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                "Sphere_VB");

    // Index buffer (u16)
    CreateBuffer(m_SphereIB, m_SphereIBAlloc,
                m_SphereIndexCount * sizeof(u16),
                (void*)du_sphere_faces,
                VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                "Sphere_IB");

    Msg("[Vulkan]   Sphere volume created: %d vertices, %d indices",
        m_SphereVertexCount, m_SphereIndexCount);
}

// ============================================================================
// Cone Volume (for spot lights)
// ============================================================================

void CVulkanGeometry::CreateConeVolume()
{
    Msg("[Vulkan]   Creating cone volume...");

    // Use du_cone vertices/indices from xrRender
    m_ConeVertexCount = DU_CONE_NUMVERTEX;
    m_ConeIndexCount = DU_CONE_NUMFACES * 3;

    // Vertex buffer
    CreateBuffer(m_ConeVB, m_ConeVBAlloc,
                m_ConeVertexCount * sizeof(Fvector),
                (void*)du_cone_vertices,
                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                "Cone_VB");

    // Index buffer
    CreateBuffer(m_ConeIB, m_ConeIBAlloc,
                m_ConeIndexCount * sizeof(u16),
                (void*)du_cone_faces,
                VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                "Cone_IB");

    Msg("[Vulkan]   Cone volume created: %d vertices, %d indices",
        m_ConeVertexCount, m_ConeIndexCount);
}

// ============================================================================
// Helper: Create Buffer
// ============================================================================

void CVulkanGeometry::CreateBuffer(VkBuffer& buffer, VmaAllocation& allocation,
                                   VkDeviceSize size, void* data,
                                   VkBufferUsageFlags usage, const char* name)
{
    // Create staging buffer
    VkBuffer stagingBuffer;
    VmaAllocation stagingAlloc;

    VkBufferCreateInfo stagingBufferInfo = {};
    stagingBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    stagingBufferInfo.size = size;
    stagingBufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    stagingBufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo stagingAllocInfo = {};
    stagingAllocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                             VMA_ALLOCATION_CREATE_MAPPED_BIT;
    stagingAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;

    VmaAllocationInfo allocInfo;
    VK_CHECK(vmaCreateBuffer(VulkanHW.m_Allocator, &stagingBufferInfo,
                             &stagingAllocInfo, &stagingBuffer,
                             &stagingAlloc, &allocInfo));

    // Copy data to staging buffer
    memcpy(allocInfo.pMappedData, data, size);

    // Create device-local buffer
    VkBufferCreateInfo bufferInfo = {};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocCreateInfo = {};
    allocCreateInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

    VK_CHECK(vmaCreateBuffer(VulkanHW.m_Allocator, &bufferInfo,
                             &allocCreateInfo, &buffer,
                             &allocation, nullptr));

    // Set debug name (disabled - requires loading extension function pointer)
    // TODO: Load vkSetDebugUtilsObjectNameEXT via vkGetDeviceProcAddr when needed
    (void)name;  // Suppress unused parameter warning

    // Copy staging → device-local
    VkCommandBuffer cmd = VulkanHW.BeginSingleTimeCommands();

    VkBufferCopy copyRegion = {};
    copyRegion.srcOffset = 0;
    copyRegion.dstOffset = 0;
    copyRegion.size = size;
    vkCmdCopyBuffer(cmd, stagingBuffer, buffer, 1, &copyRegion);

    VulkanHW.EndSingleTimeCommands(cmd);

    // Cleanup staging buffer
    vmaDestroyBuffer(VulkanHW.m_Allocator, stagingBuffer, stagingAlloc);
}

} // namespace VK
