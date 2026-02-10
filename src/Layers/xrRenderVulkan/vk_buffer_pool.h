// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// Licensed under the same terms as X-Ray Engine (see root License.txt)

// ============================================================================
// vk_buffer_pool.h - Shared Vertex/Index Buffer Pool
// ============================================================================
//
// Phase 2.23.3: Shared Geometry Buffer Management
//
// Level geometry uses shared vertex/index buffers to reduce memory usage.
// Multiple visuals reference the same buffer with different offsets.
//
// Example:
//   Visual 1: VB[0] offset 0, count 100
//   Visual 2: VB[0] offset 100, count 50   ← Same buffer!
//   Visual 3: VB[1] offset 0, count 200    ← Different buffer
//
// ============================================================================

#pragma once

#include "vk_core.h"
#include "vk_buffer.h"

namespace VK {

// ============================================================================
// CBufferPool - Manages shared vertex/index buffers for level geometry
// ============================================================================
class CBufferPool
{
public:
    CBufferPool();
    ~CBufferPool();

    // ========================================================================
    // Lifecycle
    // ========================================================================
    void Create();
    void Destroy();

    // ========================================================================
    // Buffer Registration
    // ========================================================================
    // Register an existing vertex buffer with given ID.
    // Does NOT take ownership - buffer lifecycle managed by caller.
    //
    // Parameters:
    //   id - Buffer ID from level geometry
    //   buffer - Pointer to existing CVulkanBuffer
    //   stride - Bytes per vertex
    //
    void RegisterVertexBuffer(u32 id, CVulkanBuffer* buffer, u32 stride, u32 tcOffset = 24);

    // Register an existing index buffer with given ID.
    // Does NOT take ownership - buffer lifecycle managed by caller.
    //
    // Parameters:
    //   id - Buffer ID from level geometry
    //   buffer - Pointer to existing CVulkanBuffer
    //   indexType - VK_INDEX_TYPE_UINT16 or UINT32
    //
    void RegisterIndexBuffer(u32 id, CVulkanBuffer* buffer, VkIndexType indexType);

    // ========================================================================
    // Buffer Retrieval
    // ========================================================================
    // Get vertex buffer by ID.
    // Returns nullptr if buffer doesn't exist.
    //
    CVulkanBuffer* GetVertexBuffer(u32 id);

    // Get index buffer by ID.
    // Returns nullptr if buffer doesn't exist.
    //
    CVulkanBuffer* GetIndexBuffer(u32 id);

    // Get vertex stride for buffer ID
    u32 GetVertexStride(u32 id);

    // Get index type for buffer ID
    VkIndexType GetIndexType(u32 id);

    // Get TEXCOORD0 byte offset within vertex for buffer ID
    u32 GetTexCoordOffset(u32 id);

    // ========================================================================
    // Statistics
    // ========================================================================
    u32 GetVertexBufferCount() const { return static_cast<u32>(m_VertexBuffers.size()); }
    u32 GetIndexBufferCount() const { return static_cast<u32>(m_IndexBuffers.size()); }

private:
    // Vertex buffer entry
    struct VertexBufferEntry {
        CVulkanBuffer* buffer = nullptr;
        u32 stride = 0;
        u32 tcOffset = 24;  // TEXCOORD0 byte offset (24 for lmap, 28 for vert-lit)
    };

    // Index buffer entry
    struct IndexBufferEntry {
        CVulkanBuffer* buffer = nullptr;
        VkIndexType indexType = VK_INDEX_TYPE_UINT16;
    };

    // Buffer storage (ID → Buffer mapping)
    xr_map<u32, VertexBufferEntry> m_VertexBuffers;
    xr_map<u32, IndexBufferEntry> m_IndexBuffers;
};

// Global buffer pool instance
extern CBufferPool* g_BufferPool;

} // namespace VK
