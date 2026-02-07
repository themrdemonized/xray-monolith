// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// SPDX-License-Identifier: MIT

// ============================================================================
// vk_buffer_pool.cpp - Shared Buffer Pool Implementation
// ============================================================================

#include "stdafx.h"
#include "vk_buffer_pool.h"

namespace VK {

// Global instance
CBufferPool* g_BufferPool = nullptr;

// ============================================================================
// CBufferPool Implementation
// ============================================================================

CBufferPool::CBufferPool()
{
}

CBufferPool::~CBufferPool()
{
    Destroy();
}

void CBufferPool::Create()
{
    Msg("[Vulkan] BufferPool created");
}

void CBufferPool::Destroy()
{
    // Note: We don't own the buffers, just clear references
    m_VertexBuffers.clear();
    m_IndexBuffers.clear();

    Msg("[Vulkan] BufferPool destroyed");
}

void CBufferPool::RegisterVertexBuffer(u32 id, CVulkanBuffer* buffer, u32 stride, u32 tcOffset)
{
    VERIFY(buffer != nullptr);
    VERIFY(buffer->IsValid());

    // Store buffer reference (we don't own it)
    VertexBufferEntry entry;
    entry.buffer = buffer;
    entry.stride = stride;
    entry.tcOffset = tcOffset;
    m_VertexBuffers[id] = entry;

    Msg("[Vulkan] BufferPool: Registered VB[%u] - stride %u tcOffset %u", id, stride, tcOffset);
}

void CBufferPool::RegisterIndexBuffer(u32 id, CVulkanBuffer* buffer, VkIndexType indexType)
{
    VERIFY(buffer != nullptr);
    VERIFY(buffer->IsValid());

    // Store buffer reference (we don't own it)
    IndexBufferEntry entry;
    entry.buffer = buffer;
    entry.indexType = indexType;
    m_IndexBuffers[id] = entry;

    Msg("[Vulkan] BufferPool: Registered IB[%u] - type %s",
        id, indexType == VK_INDEX_TYPE_UINT16 ? "UINT16" : "UINT32");
}

CVulkanBuffer* CBufferPool::GetVertexBuffer(u32 id)
{
    auto it = m_VertexBuffers.find(id);
    if (it == m_VertexBuffers.end())
    {
        Msg("! [Vulkan] BufferPool: Vertex buffer ID %u not found", id);
        return nullptr;
    }

    return it->second.buffer;
}

CVulkanBuffer* CBufferPool::GetIndexBuffer(u32 id)
{
    auto it = m_IndexBuffers.find(id);
    if (it == m_IndexBuffers.end())
    {
        Msg("! [Vulkan] BufferPool: Index buffer ID %u not found", id);
        return nullptr;
    }

    return it->second.buffer;
}

u32 CBufferPool::GetVertexStride(u32 id)
{
    auto it = m_VertexBuffers.find(id);
    if (it == m_VertexBuffers.end())
        return 0;

    return it->second.stride;
}

VkIndexType CBufferPool::GetIndexType(u32 id)
{
    auto it = m_IndexBuffers.find(id);
    if (it == m_IndexBuffers.end())
        return VK_INDEX_TYPE_UINT16;

    return it->second.indexType;
}

u32 CBufferPool::GetTexCoordOffset(u32 id)
{
    auto it = m_VertexBuffers.find(id);
    if (it == m_VertexBuffers.end())
        return 24;  // Default: lightmapped layout

    return it->second.tcOffset;
}

} // namespace VK
