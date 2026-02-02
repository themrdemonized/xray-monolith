// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// SPDX-License-Identifier: MIT

// ============================================================================
// vk_ParticleBufferPool.cpp - Dynamic buffer pool implementation
// ============================================================================
//
// Triple-buffered dynamic vertex buffer pool for particle rendering.
// Uses CVulkanBuffer with HOST_VISIBLE memory for direct CPU writes.
//
// ============================================================================

#include "stdafx.h"
#include "vk_ParticleBufferPool.h"
#include "vk_ParticleEffect.h"  // For VkParticleVertex definition
#include "vk_buffer.h"
#include "HW_Vulkan.h"

// Global instance
vkParticleBufferPool* g_ParticleBufferPool = nullptr;

vkParticleBufferPool::vkParticleBufferPool()
{
    for (u32 i = 0; i < BUFFER_COUNT; i++) {
        m_buffers[i].buffer = nullptr;
        m_buffers[i].offset = 0;
        m_buffers[i].capacity = INITIAL_CAPACITY;
        m_buffers[i].mappedData = nullptr;
    }
}

vkParticleBufferPool::~vkParticleBufferPool()
{
    Shutdown();
}

bool vkParticleBufferPool::Initialize(VK::CVulkanDevice* /*device*/)
{
    Msg("[Vulkan] Initializing particle buffer pool...");

    VkDevice device = VulkanHW.GetDevice();
    if (device == VK_NULL_HANDLE) {
        Msg("![Vulkan] Cannot initialize particle buffer pool: device not ready");
        return false;
    }

    // Create triple-buffered vertex buffers
    for (u32 i = 0; i < BUFFER_COUNT; i++) {
        if (!CreateBufferFrame(i, nullptr)) {
            Msg("![Vulkan] Failed to create buffer frame %u", i);
            Shutdown();
            return false;
        }
    }

    Msg("[Vulkan] Particle buffer pool initialized: %u frames x %.2f MB",
        BUFFER_COUNT, (float)INITIAL_CAPACITY / (1024.0f * 1024.0f));

    return true;
}

void vkParticleBufferPool::Shutdown()
{
    for (u32 i = 0; i < BUFFER_COUNT; i++) {
        DestroyBufferFrame(i);
    }
    m_currentFrameIndex = 0;
}

bool vkParticleBufferPool::CreateBufferFrame(u32 index, VK::CVulkanDevice* /*device*/)
{
    VERIFY(index < BUFFER_COUNT);

    VkDevice device = VulkanHW.GetDevice();
    if (device == VK_NULL_HANDLE) {
        return false;
    }

    BufferFrame& frame = m_buffers[index];

    // Create buffer
    frame.buffer = xr_new<VK::CVulkanBuffer>();

    // Create dynamic vertex buffer with HOST_VISIBLE memory
    frame.buffer->Create(
        frame.capacity,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VMA_MEMORY_USAGE_CPU_TO_GPU  // HOST_VISIBLE + DEVICE_LOCAL (if possible)
    );

    if (!frame.buffer->IsValid()) {
        Msg("![Vulkan] Failed to create particle buffer frame %u", index);
        xr_delete(frame.buffer);
        return false;
    }

    // Keep buffer persistently mapped
    frame.mappedData = frame.buffer->Map();
    if (!frame.mappedData) {
        Msg("![Vulkan] Failed to map particle buffer frame %u", index);
        frame.buffer->Destroy();
        xr_delete(frame.buffer);
        return false;
    }

    frame.offset = 0;

    Msg("[Vulkan] Particle buffer frame %u created: %.2f MB",
        index, (float)frame.capacity / (1024.0f * 1024.0f));

    return true;
}

void vkParticleBufferPool::DestroyBufferFrame(u32 index)
{
    VERIFY(index < BUFFER_COUNT);
    BufferFrame& frame = m_buffers[index];
    if (frame.buffer) {
        frame.buffer->Destroy();
        delete frame.buffer;
        frame.buffer = nullptr;
    }
    frame.offset = 0;
    frame.capacity = INITIAL_CAPACITY;
    frame.mappedData = nullptr;
}

VkParticleVertex* vkParticleBufferPool::Allocate(
    u32 vertexCount,
    u32& outOffset,
    VK::CVulkanBuffer*& outBuffer)
{
    if (vertexCount == 0) {
        outBuffer = nullptr;
        outOffset = 0;
        return nullptr;
    }

    BufferFrame& frame = m_buffers[m_currentFrameIndex];

    // Calculate required size
    u32 requiredSize = vertexCount * sizeof(VkParticleVertex);

    // Check if we need to resize buffer
    if (frame.offset + requiredSize > frame.capacity) {
        // Try to resize buffer (double capacity)
        u32 newCapacity = frame.capacity * 2;
        while (newCapacity < frame.offset + requiredSize) {
            newCapacity *= 2;
        }

        Msg("[Vulkan] Particle buffer frame %u full, resizing: %.2f MB -> %.2f MB",
            m_currentFrameIndex,
            (float)frame.capacity / (1024.0f * 1024.0f),
            (float)newCapacity / (1024.0f * 1024.0f));

        if (!ResizeBuffer(m_currentFrameIndex, newCapacity, nullptr)) {
            Msg("![Vulkan] Failed to resize particle buffer, allocation failed");
            outBuffer = nullptr;
            outOffset = 0;
            return nullptr;
        }
    }

    // Allocate from current offset
    outOffset = frame.offset;
    outBuffer = frame.buffer;

    // Calculate pointer to writable memory
    VkParticleVertex* ptr = (VkParticleVertex*)((char*)frame.mappedData + frame.offset);

    // Advance offset
    frame.offset += requiredSize;

    return ptr;
}

void vkParticleBufferPool::ResetFrame(u32 frameIndex)
{
    VERIFY(frameIndex < BUFFER_COUNT);
    m_buffers[frameIndex].offset = 0;
    m_currentFrameIndex = frameIndex;
}

bool vkParticleBufferPool::IsValid() const
{
    // Check if all buffers are created
    for (u32 i = 0; i < BUFFER_COUNT; i++) {
        if (!m_buffers[i].buffer || !m_buffers[i].mappedData) {
            return false;
        }
    }
    return true;
}

bool vkParticleBufferPool::ResizeBuffer(u32 index, u32 newCapacity, VK::CVulkanDevice* /*device*/)
{
    VERIFY(index < BUFFER_COUNT);

    BufferFrame& frame = m_buffers[index];

    // Save current offset
    u32 savedOffset = frame.offset;

    // Destroy old buffer
    if (frame.buffer) {
        // Unmap first
        if (frame.mappedData) {
            frame.buffer->Unmap();
            frame.mappedData = nullptr;
        }
        frame.buffer->Destroy();
        xr_delete(frame.buffer);
    }

    // Update capacity
    frame.capacity = newCapacity;
    frame.offset = savedOffset;

    // Create new larger buffer
    return CreateBufferFrame(index, nullptr);
}
