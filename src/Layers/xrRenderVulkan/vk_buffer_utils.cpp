// ============================================================================
// vk_buffer_utils.cpp - Vulkan Buffer Creation Implementation
// ============================================================================

#include "stdafx.h"
#include "vk_buffer_utils.h"
#include "HW_Vulkan.h"

namespace VK {

// ============================================================================
// CreateVertexBuffer
// ============================================================================
BufferInfo BufferUtils::CreateVertexBuffer(const void* data, VkDeviceSize size)
{
    return CreateBuffer(
        data,
        size,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY
    );
}

// ============================================================================
// CreateIndexBuffer
// ============================================================================
BufferInfo BufferUtils::CreateIndexBuffer(const void* data, VkDeviceSize size)
{
    return CreateBuffer(
        data,
        size,
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY
    );
}

// ============================================================================
// CreateUniformBuffer
// ============================================================================
BufferInfo BufferUtils::CreateUniformBuffer(VkDeviceSize size)
{
    return CreateBuffer(
        nullptr,  // No initial data
        size,
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VMA_MEMORY_USAGE_CPU_TO_GPU  // Frequently updated from CPU
    );
}

// ============================================================================
// DestroyBuffer
// ============================================================================
void BufferUtils::DestroyBuffer(BufferInfo& buffer)
{
    if (!buffer.IsValid())
        return;

    vmaDestroyBuffer(VulkanHW.m_Allocator, buffer.buffer, buffer.allocation);

    buffer.buffer = VK_NULL_HANDLE;
    buffer.allocation = VK_NULL_HANDLE;
    buffer.size = 0;
}

// ============================================================================
// UpdateBuffer
// ============================================================================
void BufferUtils::UpdateBuffer(const BufferInfo& buffer, const void* data,
                               VkDeviceSize size, VkDeviceSize offset)
{
    VERIFY(buffer.IsValid());
    VERIFY(data != nullptr);
    VERIFY(offset + size <= buffer.size);

    // Map GPU memory to CPU address space
    void* mappedData = nullptr;
    VkResult result = vmaMapMemory(VulkanHW.m_Allocator, buffer.allocation, &mappedData);
    VERIFY(result == VK_SUCCESS);

    // Copy data
    memcpy(static_cast<char*>(mappedData) + offset, data, size);

    // Unmap
    vmaUnmapMemory(VulkanHW.m_Allocator, buffer.allocation);
}

// ============================================================================
// CreateBuffer (Internal)
// ============================================================================
BufferInfo BufferUtils::CreateBuffer(
    const void* data,
    VkDeviceSize size,
    VkBufferUsageFlags usage,
    VmaMemoryUsage memoryUsage)
{
    VERIFY(size > 0);

    BufferInfo bufferInfo;
    bufferInfo.size = size;

    // ========================================================================
    // 1. Create buffer
    // ========================================================================
    VkBufferCreateInfo bufferCreateInfo = {};
    bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferCreateInfo.size = size;
    bufferCreateInfo.usage = usage;
    bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = memoryUsage;

    VkResult result = vmaCreateBuffer(
        VulkanHW.m_Allocator,
        &bufferCreateInfo,
        &allocInfo,
        &bufferInfo.buffer,
        &bufferInfo.allocation,
        nullptr
    );

    if (result != VK_SUCCESS) {
        Msg("! [Vulkan] Failed to create buffer (size=%d, usage=%d): %d",
            size, usage, result);
        return BufferInfo{};
    }

    // ========================================================================
    // 2. Upload data if provided
    // ========================================================================
    if (data != nullptr) {
        // For GPU_ONLY buffers, use staging buffer
        if (memoryUsage == VMA_MEMORY_USAGE_GPU_ONLY) {
            CopyDataToBuffer(bufferInfo.buffer, data, size);
        }
        // For CPU_TO_GPU buffers, direct map/copy
        else {
            UpdateBuffer(bufferInfo, data, size, 0);
        }
    }

    return bufferInfo;
}

// ============================================================================
// CopyDataToBuffer (Internal)
// ============================================================================
void BufferUtils::CopyDataToBuffer(VkBuffer dstBuffer, const void* data, VkDeviceSize size)
{
    // ========================================================================
    // 1. Create staging buffer (CPU-visible)
    // ========================================================================
    VkBufferCreateInfo stagingBufferInfo = {};
    stagingBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    stagingBufferInfo.size = size;
    stagingBufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    stagingBufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo stagingAllocInfo = {};
    stagingAllocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;
    stagingAllocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;  // Keep mapped

    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VmaAllocation stagingAllocation = VK_NULL_HANDLE;
    VmaAllocationInfo stagingAllocInfoResult = {};

    VkResult result = vmaCreateBuffer(
        VulkanHW.m_Allocator,
        &stagingBufferInfo,
        &stagingAllocInfo,
        &stagingBuffer,
        &stagingAllocation,
        &stagingAllocInfoResult
    );

    VERIFY(result == VK_SUCCESS);

    // ========================================================================
    // 2. Copy data to staging buffer
    // ========================================================================
    memcpy(stagingAllocInfoResult.pMappedData, data, size);

    // ========================================================================
    // 3. Copy staging buffer → GPU buffer
    // ========================================================================
    ExecuteSingleTimeCommand([&](VkCommandBuffer cmd) {
        VkBufferCopy copyRegion = {};
        copyRegion.srcOffset = 0;
        copyRegion.dstOffset = 0;
        copyRegion.size = size;

        vkCmdCopyBuffer(cmd, stagingBuffer, dstBuffer, 1, &copyRegion);
    });

    // ========================================================================
    // 4. Cleanup staging buffer
    // ========================================================================
    vmaDestroyBuffer(VulkanHW.m_Allocator, stagingBuffer, stagingAllocation);
}

// ============================================================================
// ExecuteSingleTimeCommand (Internal Template)
// ============================================================================
template<typename F>
void BufferUtils::ExecuteSingleTimeCommand(F&& commandFunc)
{
    // ========================================================================
    // 1. Allocate command buffer
    // ========================================================================
    VkCommandBufferAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = VulkanHW.m_TransferCommandPool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkResult result = vkAllocateCommandBuffers(VulkanHW.m_Device, &allocInfo, &cmd);
    VERIFY(result == VK_SUCCESS);

    // ========================================================================
    // 2. Begin recording
    // ========================================================================
    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(cmd, &beginInfo);

    // ========================================================================
    // 3. Execute user command function
    // ========================================================================
    commandFunc(cmd);

    // ========================================================================
    // 4. End recording
    // ========================================================================
    vkEndCommandBuffer(cmd);

    // ========================================================================
    // 5. Submit to graphics queue
    // ========================================================================
    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;

    // Create fence for synchronization
    VkFenceCreateInfo fenceInfo = {};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;

    VkFence fence = VK_NULL_HANDLE;
    result = vkCreateFence(VulkanHW.m_Device, &fenceInfo, nullptr, &fence);
    VERIFY(result == VK_SUCCESS);

    // Submit
    result = vkQueueSubmit(VulkanHW.m_GraphicsQueue, 1, &submitInfo, fence);
    VERIFY(result == VK_SUCCESS);

    // ========================================================================
    // 6. Wait for completion
    // ========================================================================
    result = vkWaitForFences(VulkanHW.m_Device, 1, &fence, VK_TRUE, UINT64_MAX);
    VERIFY(result == VK_SUCCESS);

    // ========================================================================
    // 7. Cleanup
    // ========================================================================
    vkDestroyFence(VulkanHW.m_Device, fence, nullptr);
    vkFreeCommandBuffers(VulkanHW.m_Device, VulkanHW.m_TransferCommandPool, 1, &cmd);
}

} // namespace VK
