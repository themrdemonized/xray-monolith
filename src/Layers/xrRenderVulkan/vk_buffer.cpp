// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// SPDX-License-Identifier: MIT

#include "stdafx.h"
#include "vk_buffer.h"
#include "HW_Vulkan.h"
#include "vk_command_buffer.h"

namespace VK
{

// Threshold для staging buffer (64 KB)
static constexpr VkDeviceSize STAGING_THRESHOLD = 64 * 1024;

// Constructor
CVulkanBuffer::CVulkanBuffer()
{
}

// Destructor
CVulkanBuffer::~CVulkanBuffer()
{
    Destroy();
}

// Создание буфера
void CVulkanBuffer::Create(VkDeviceSize size, VkBufferUsageFlags usage, VmaMemoryUsage memUsage)
{
    if (m_Buffer != VK_NULL_HANDLE) {
        Msg("![Vulkan] Buffer already created, call Destroy first");
        return;
    }

    if (size == 0) {
        Msg("![Vulkan] Cannot create buffer with size 0");
        return;
    }

    m_Size = size;
    m_Usage = usage;
    m_MemUsage = memUsage;

    // Buffer create info
    VkBufferCreateInfo bufferInfo = {};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    // VMA allocation info
    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = memUsage;

    // Для uniform buffers включаем HOST_VISIBLE для persistent mapping
    if (usage & VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT) {
        allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                          VMA_ALLOCATION_CREATE_MAPPED_BIT;
    }

    // Для staging buffers (TRANSFER_SRC) или HOST memory - нужен host access
    if ((usage & VK_BUFFER_USAGE_TRANSFER_SRC_BIT) || memUsage == VMA_MEMORY_USAGE_AUTO_PREFER_HOST) {
        allocInfo.flags |= VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
    }

    // ВАЖНО: Для VERTEX/INDEX буферов добавляем TRANSFER_DST для staging uploads
    if ((usage & (VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT)) &&
        !(usage & VK_BUFFER_USAGE_TRANSFER_DST_BIT)) {
        bufferInfo.usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    }

    VK_CHECK(vmaCreateBuffer(VulkanHW.m_Allocator, &bufferInfo, &allocInfo,
                             &m_Buffer, &m_Allocation, nullptr));

    // Если буфер создан с MAPPED_BIT, получаем mapped pointer
    if (allocInfo.flags & VMA_ALLOCATION_CREATE_MAPPED_BIT) {
        VmaAllocationInfo allocInfoResult;
        vmaGetAllocationInfo(VulkanHW.m_Allocator, m_Allocation, &allocInfoResult);
        m_Mapped = allocInfoResult.pMappedData;
    }
}

// Уничтожение буфера
void CVulkanBuffer::Destroy()
{
    if (m_Buffer == VK_NULL_HANDLE) {
        return;
    }

    // Unmap if mapped
    if (m_Mapped != nullptr && !(m_Usage & VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT)) {
        Unmap();
    }

    vmaDestroyBuffer(VulkanHW.m_Allocator, m_Buffer, m_Allocation);

    m_Buffer = VK_NULL_HANDLE;
    m_Allocation = VK_NULL_HANDLE;
    m_Size = 0;
    m_Mapped = nullptr;
}

// Upload данных
void CVulkanBuffer::Upload(const void* data, VkDeviceSize size, VkDeviceSize offset)
{
    if (!data) {
        Msg("![Vulkan] Upload: data is null");
        return;
    }

    if (offset + size > m_Size) {
        Msg("![Vulkan] Upload: offset + size exceeds buffer size");
        return;
    }

    // Проверяем, является ли буфер host-visible (можно ли Map)
    VmaAllocationInfo allocInfo;
    vmaGetAllocationInfo(VulkanHW.m_Allocator, m_Allocation, &allocInfo);

    VkMemoryPropertyFlags memFlags;
    vmaGetMemoryTypeProperties(VulkanHW.m_Allocator, allocInfo.memoryType, &memFlags);

    bool isHostVisible = (memFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0;

    // Если буфер НЕ host-visible (DEVICE_LOCAL only) - всегда используем staging
    if (!isHostVisible) {
        UploadViaStaging(data, size, offset);
        return;
    }

    // Буфер host-visible - можем мапить напрямую
    // Для небольших данных - прямой memcpy
    if (size < STAGING_THRESHOLD) {
        void* mapped = Map();
        if (!mapped) {
            Msg("![Vulkan] Failed to map buffer for upload");
            // Fallback to staging
            UploadViaStaging(data, size, offset);
            return;
        }

        memcpy((u8*)mapped + offset, data, size);
        Flush();

        // Unmap только если не persistent-mapped uniform buffer
        if (!(m_Usage & VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT)) {
            Unmap();
        }
    }
    else {
        // Для больших данных - staging buffer (быстрее для transfer)
        UploadViaStaging(data, size, offset);
    }
}

// Upload через staging buffer
void CVulkanBuffer::UploadViaStaging(const void* data, VkDeviceSize size, VkDeviceSize offset)
{
    // Создаём staging buffer
    CVulkanBuffer stagingBuffer;
    stagingBuffer.Create(
        size,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VMA_MEMORY_USAGE_AUTO_PREFER_HOST
    );

    // Копируем данные в staging
    void* mapped = stagingBuffer.Map();
    if (!mapped) {
        Msg("![Vulkan] Failed to map staging buffer");
        stagingBuffer.Destroy();
        return;
    }

    memcpy(mapped, data, size);
    stagingBuffer.Flush();
    stagingBuffer.Unmap();

    // Копируем staging → destination buffer через GPU
    VkCommandBuffer cmd = CommandManager.Begin();

    VkBufferCopy copyRegion = {};
    copyRegion.srcOffset = 0;
    copyRegion.dstOffset = offset;
    copyRegion.size = size;

    vkCmdCopyBuffer(cmd, stagingBuffer.m_Buffer, m_Buffer, 1, &copyRegion);

    // Submit и ждём завершения
    // TODO Phase 2: Использовать fence для async transfers
    CommandManager.End(cmd);

    // Submit immediately и wait
    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;

    vkQueueSubmit(VulkanHW.m_GraphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(VulkanHW.m_GraphicsQueue);

    // Cleanup staging buffer
    stagingBuffer.Destroy();
}

// Map memory
void* CVulkanBuffer::Map()
{
    if (m_Mapped != nullptr) {
        // Already mapped (persistent-mapped uniform buffer)
        return m_Mapped;
    }

    void* data = nullptr;
    VkResult result = vmaMapMemory(VulkanHW.m_Allocator, m_Allocation, &data);

    if (result != VK_SUCCESS) {
        Msg("![Vulkan] vmaMapMemory failed: %d", result);
        return nullptr;
    }

    m_Mapped = data;
    return data;
}

// Unmap memory
void CVulkanBuffer::Unmap()
{
    if (m_Mapped == nullptr) {
        return;
    }

    // Не unmap persistent-mapped uniform buffers
    if (m_Usage & VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT) {
        return;
    }

    vmaUnmapMemory(VulkanHW.m_Allocator, m_Allocation);
    m_Mapped = nullptr;
}

// Flush (для non-coherent memory)
void CVulkanBuffer::Flush()
{
    VkResult result = vmaFlushAllocation(VulkanHW.m_Allocator, m_Allocation, 0, VK_WHOLE_SIZE);
    if (result != VK_SUCCESS) {
        Msg("![Vulkan] vmaFlushAllocation failed: %d", result);
    }
}

// Invalidate (для non-coherent memory)
void CVulkanBuffer::Invalidate()
{
    VkResult result = vmaInvalidateAllocation(VulkanHW.m_Allocator, m_Allocation, 0, VK_WHOLE_SIZE);
    if (result != VK_SUCCESS) {
        Msg("![Vulkan] vmaInvalidateAllocation failed: %d", result);
    }
}

} // namespace VK
