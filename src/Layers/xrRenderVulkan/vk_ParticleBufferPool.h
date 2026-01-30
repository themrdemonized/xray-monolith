// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// SPDX-License-Identifier: MIT

// ============================================================================
// vk_ParticleBufferPool.h - Dynamic vertex buffer pool for particles
// ============================================================================
//
// Triple-buffered pool of dynamic vertex buffers for CPU-to-GPU transfer.
// Uses HOST_VISIBLE memory for direct CPU writes without staging.
//
// ============================================================================

#pragma once

#include "vk_buffer.h"

namespace VK
{
    class CVulkanBuffer;
    class CVulkanDevice;  // Forward declaration (placeholder for device abstraction)
}

// Forward declaration
struct VkParticleVertex;

// ============================================================================
// vkParticleBufferPool - Dynamic buffer management
// ============================================================================
class vkParticleBufferPool
{
public:
    // Initial capacity for dynamic buffers (~1.5MB)
    static constexpr u32 INITIAL_CAPACITY = 65536 * 24; // 24 = sizeof(VkParticleVertex)

    // Number of buffers for triple buffering
    static constexpr u32 BUFFER_COUNT = 3;

private:
    struct BufferFrame
    {
        VK::CVulkanBuffer* buffer = nullptr;
        u32 offset = 0;
        u32 capacity = INITIAL_CAPACITY;
        void* mappedData = nullptr; // Direct pointer to HOST_VISIBLE memory
    };

    BufferFrame m_buffers[BUFFER_COUNT];
    u32 m_currentFrameIndex = 0;

public:
    vkParticleBufferPool();
    ~vkParticleBufferPool();

    // ========================================================================
    // Initialization and Cleanup
    // ========================================================================

    // Initialize buffer pool
    bool Initialize(VK::CVulkanDevice* device);

    // Cleanup
    void Shutdown();

    // ========================================================================
    // Buffer Allocation
    // ========================================================================

    // Allocate space in current frame's buffer
    // Returns pointer to writable memory and the buffer offset
    VkParticleVertex* Allocate(u32 vertexCount, u32& outOffset, VK::CVulkanBuffer*& outBuffer);

    // Reset for next frame
    void ResetFrame(u32 frameIndex);

    // Get current buffer for rendering
    VK::CVulkanBuffer* GetCurrentBuffer() const
    {
        return m_buffers[m_currentFrameIndex].buffer;
    }

    // Get current frame index
    u32 GetCurrentFrameIndex() const { return m_currentFrameIndex; }

    // ========================================================================
    // Validation
    // ========================================================================

    bool IsValid() const;

private:
    // Create a single buffer frame
    bool CreateBufferFrame(u32 index, VK::CVulkanDevice* device);

    // Destroy a single buffer frame
    void DestroyBufferFrame(u32 index);

    // Resize buffer if needed
    bool ResizeBuffer(u32 index, u32 newCapacity, VK::CVulkanDevice* device);
};

// ============================================================================
// Global particle buffer pool instance
// ============================================================================
extern vkParticleBufferPool* g_ParticleBufferPool;
