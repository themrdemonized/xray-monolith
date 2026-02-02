// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// SPDX-License-Identifier: MIT

#include "stdafx.h"
#include "vk_R_Backend.h"
#include "HW_Vulkan.h"

// ============================================================================
// _VertexStream_vk Implementation
// ============================================================================

// Default size for dynamic vertex buffer: 4MB (matches D3D renderer)
// This is enough for ~130K vertices at 32 bytes/vertex (typical FVF::L size)
static constexpr VkDeviceSize DYNAMIC_VB_SIZE = 4 * 1024 * 1024;

_VertexStream_vk::_VertexStream_vk()
    : m_Buffer(VK_NULL_HANDLE)
    , m_Allocation(VK_NULL_HANDLE)
    , m_MappedData(nullptr)
    , m_Size(0)
    , m_Position(0)
    , m_DiscardID(0)
#ifdef DEBUG
    , dbg_lock(0)
#endif
{
}

_VertexStream_vk::~_VertexStream_vk()
{
    Destroy();
}

void _VertexStream_vk::Create()
{
    if (m_Buffer != VK_NULL_HANDLE) {
        Msg("![Vulkan] _VertexStream_vk::Create() - buffer already created");
        return;
    }

    m_Size = DYNAMIC_VB_SIZE;

    // Create buffer info
    VkBufferCreateInfo bufferInfo = {};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = m_Size;
    bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    // VMA allocation info - HOST_VISIBLE with persistent mapping
    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                      VMA_ALLOCATION_CREATE_MAPPED_BIT;

    // Create buffer
    VkResult result = vmaCreateBuffer(
        VulkanHW.m_Allocator,
        &bufferInfo,
        &allocInfo,
        &m_Buffer,
        &m_Allocation,
        nullptr
    );

    if (result != VK_SUCCESS) {
        Msg("![Vulkan] Failed to create dynamic vertex buffer: %d", result);
        return;
    }

    // Get persistent mapped pointer
    VmaAllocationInfo allocInfoResult;
    vmaGetAllocationInfo(VulkanHW.m_Allocator, m_Allocation, &allocInfoResult);
    m_MappedData = allocInfoResult.pMappedData;

    if (!m_MappedData) {
        Msg("![Vulkan] Dynamic vertex buffer mapping failed");
        vmaDestroyBuffer(VulkanHW.m_Allocator, m_Buffer, m_Allocation);
        m_Buffer = VK_NULL_HANDLE;
        m_Allocation = VK_NULL_HANDLE;
        return;
    }

    m_Position = 0;
    m_DiscardID = 0;

    Msg("[Vulkan] Dynamic vertex buffer created: %d KB (persistent mapped)", m_Size / 1024);
}

void _VertexStream_vk::Destroy()
{
    if (m_Buffer != VK_NULL_HANDLE) {
        // No need to unmap - VMA handles it automatically with MAPPED_BIT
        vmaDestroyBuffer(VulkanHW.m_Allocator, m_Buffer, m_Allocation);
        m_Buffer = VK_NULL_HANDLE;
        m_Allocation = VK_NULL_HANDLE;
        m_MappedData = nullptr;
        m_Size = 0;
        m_Position = 0;
    }
}

void _VertexStream_vk::reset_begin()
{
    // Called at frame start - nothing to do for Vulkan
    // (D3D uses this to reset old buffers)
}

void _VertexStream_vk::reset_end()
{
    // Called at frame end - nothing to do for Vulkan
}

void* _VertexStream_vk::Lock(u32 vl_Count, u32 Stride, u32& vOffset)
{
#ifdef DEBUG
    VERIFY(dbg_lock == 0);
    dbg_lock++;
#endif

    if (!m_MappedData) {
        Msg("![Vulkan] _VertexStream_vk::Lock() - buffer not created or mapping failed");
        vOffset = 0;
        return nullptr;
    }

    // Calculate bytes needed
    u32 bytes_need = vl_Count * Stride;
    R_ASSERT2(
        (bytes_need <= m_Size) && vl_Count,
        make_string("bytes_need = %d, m_Size = %d, vl_Count = %d", bytes_need, m_Size, vl_Count)
    );

    // Calculate vertex-space position
    u32 vl_mSize = m_Size / Stride;
    u32 vl_mPosition = m_Position / Stride + 1;

    // Check if we need to discard (wrap around)
    if ((vl_Count + vl_mPosition) >= vl_mSize) {
        // DISCARD: Rewind to start of buffer
        m_Position = 0;
        vOffset = 0;
        m_DiscardID++;

        // Return pointer to start of buffer
        return m_MappedData;
    }
    else {
        // NOOVERWRITE: Append to current position
        m_Position = vl_mPosition * Stride;
        vOffset = vl_mPosition;

        // Return pointer offset into buffer
        return (u8*)m_MappedData + m_Position;
    }
}

void _VertexStream_vk::Unlock(u32 Count, u32 Stride)
{
#ifdef DEBUG
    VERIFY(dbg_lock == 1);
    dbg_lock--;
#endif

    // Calculate bytes written
    u32 bytes_written = Count * Stride;

    // Flush memory range to make writes visible to GPU
    // (Required for HOST_VISIBLE memory that is not HOST_COHERENT)
    VmaAllocationInfo allocInfo;
    vmaGetAllocationInfo(VulkanHW.m_Allocator, m_Allocation, &allocInfo);

    // Check if memory is not coherent (requires manual flush)
    VkMemoryPropertyFlags memFlags;
    vmaGetMemoryTypeProperties(VulkanHW.m_Allocator, allocInfo.memoryType, &memFlags);

    if (!(memFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
        // Manual flush required
        VkResult result = vmaFlushAllocation(
            VulkanHW.m_Allocator,
            m_Allocation,
            m_Position,
            bytes_written
        );

        if (result != VK_SUCCESS) {
            Msg("![Vulkan] Failed to flush dynamic vertex buffer: %d", result);
        }
    }

    // Update position for next lock
    m_Position += bytes_written;
}
