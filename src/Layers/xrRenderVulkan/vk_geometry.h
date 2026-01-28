// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// SPDX-License-Identifier: MIT

#pragma once

#include "vk_core.h"

namespace VK
{

// ============================================================================
// Geometry helper class for deferred rendering
// Manages fullscreen quad and light volumes (sphere, cone)
// ============================================================================

class CVulkanGeometry
{
public:
    CVulkanGeometry();
    ~CVulkanGeometry();

    void Create();
    void Destroy();

    // === Fullscreen quad (for deferred passes) ===
    VkBuffer m_FullscreenQuadVB;
    VkBuffer m_FullscreenQuadIB;
    VmaAllocation m_FullscreenQuadVBAlloc;
    VmaAllocation m_FullscreenQuadIBAlloc;
    u32 m_FullscreenQuadVertexCount;
    u32 m_FullscreenQuadIndexCount;

    // === Sphere volume (for point lights) ===
    VkBuffer m_SphereVB;
    VkBuffer m_SphereIB;
    VmaAllocation m_SphereVBAlloc;
    VmaAllocation m_SphereIBAlloc;
    u32 m_SphereVertexCount;
    u32 m_SphereIndexCount;

    // === Cone volume (for spot lights) ===
    VkBuffer m_ConeVB;
    VkBuffer m_ConeIB;
    VmaAllocation m_ConeVBAlloc;
    VmaAllocation m_ConeIBAlloc;
    u32 m_ConeVertexCount;
    u32 m_ConeIndexCount;

private:
    void CreateFullscreenQuad();
    void CreateSphereVolume();
    void CreateConeVolume();

    void CreateBuffer(VkBuffer& buffer, VmaAllocation& allocation,
                     VkDeviceSize size, void* data,
                     VkBufferUsageFlags usage, const char* name);
};

// Global instance
extern CVulkanGeometry* g_VulkanGeometry;

} // namespace VK
