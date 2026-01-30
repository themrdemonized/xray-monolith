// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// SPDX-License-Identifier: MIT

// ============================================================================
// vk_ParticleBufferPool.cpp - Dynamic buffer pool implementation
// ============================================================================
// TODO: Rewrite to use CVulkanBuffer's VMA-based API instead of raw Vulkan

#include "stdafx.h"
#include "vk_ParticleBufferPool.h"
#include "vk_ParticleEffect.h"  // For VkParticleVertex definition
#include "vk_buffer.h"

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
    // TODO: Implement using CVulkanBuffer VMA API
    Msg("[Vulkan] Particle buffer pool: stub initialization (not yet functional)");
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
    // TODO: Implement using CVulkanBuffer VMA API
    return false;
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
    // TODO: Implement when buffer pool is functional
    outBuffer = nullptr;
    outOffset = 0;
    return nullptr;
}

void vkParticleBufferPool::ResetFrame(u32 frameIndex)
{
    VERIFY(frameIndex < BUFFER_COUNT);
    m_buffers[frameIndex].offset = 0;
    m_currentFrameIndex = frameIndex;
}

bool vkParticleBufferPool::IsValid() const
{
    // Stub: always returns false until properly implemented
    return false;
}

bool vkParticleBufferPool::ResizeBuffer(u32 index, u32 newCapacity, VK::CVulkanDevice* /*device*/)
{
    // TODO: Implement buffer resize
    return false;
}
