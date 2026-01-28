#pragma once
#include "vk_core.h"

namespace VK
{

/**
 * Vulkan Buffer Wrapper
 *
 * Универсальный класс для работы с буферами (vertex, index, uniform).
 * Использует VMA (Vulkan Memory Allocator) для эффективного управления памятью.
 *
 * Типы буферов:
 * - Vertex Buffer: геометрия (позиции, нормали, UV)
 * - Index Buffer: индексы треугольников
 * - Uniform Buffer: константы для шейдеров (матрицы, параметры)
 * - Staging Buffer: временный буфер для CPU→GPU transfer
 *
 * Usage:
 *   CVulkanBuffer vb;
 *   vb.Create(size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VMA_MEMORY_USAGE_AUTO);
 *   vb.Upload(vertexData, size);
 *   vb.Destroy();
 */
class CVulkanBuffer
{
public:
    CVulkanBuffer();
    ~CVulkanBuffer();

    /**
     * Создать буфер
     * @param size Размер буфера в байтах
     * @param usage Usage flags (VERTEX_BUFFER, INDEX_BUFFER, UNIFORM_BUFFER, etc.)
     * @param memUsage VMA memory usage hint:
     *                 - VMA_MEMORY_USAGE_AUTO (рекомендуется)
     *                 - VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE (GPU-only, быстро)
     *                 - VMA_MEMORY_USAGE_AUTO_PREFER_HOST (CPU-доступ, медленно)
     */
    void Create(VkDeviceSize size, VkBufferUsageFlags usage, VmaMemoryUsage memUsage);

    /**
     * Уничтожить буфер
     */
    void Destroy();

    /**
     * Загрузить данные в буфер (CPU → GPU)
     * @param data Указатель на данные
     * @param size Размер данных в байтах
     * @param offset Смещение в буфере (default: 0)
     *
     * Для небольших данных (<65KB) использует прямой memcpy.
     * Для больших данных использует staging buffer + vkCmdCopyBuffer.
     */
    void Upload(const void* data, VkDeviceSize size, VkDeviceSize offset = 0);

    /**
     * Map buffer memory (получить CPU-доступный указатель)
     * @return Указатель на mapped память или nullptr при ошибке
     *
     * ВАЖНО: После Map() необходимо вызвать Unmap()!
     * Используйте для persistent-mapped uniform buffers.
     */
    void* Map();

    /**
     * Unmap buffer memory
     */
    void Unmap();

    /**
     * Flush mapped memory (для non-coherent memory)
     * Гарантирует видимость изменений на GPU стороне.
     */
    void Flush();

    /**
     * Invalidate mapped memory (для non-coherent memory)
     * Гарантирует видимость изменений от GPU на CPU стороне.
     */
    void Invalidate();

    /**
     * Получить VkBuffer handle
     */
    VkBuffer GetHandle() const { return m_Buffer; }

    /**
     * Получить размер буфера
     */
    VkDeviceSize GetSize() const { return m_Size; }

    /**
     * Проверить создан ли буфер
     */
    bool IsValid() const { return m_Buffer != VK_NULL_HANDLE; }

    /**
     * Проверить mapped ли буфер
     */
    bool IsMapped() const { return m_Mapped != nullptr; }

private:
    /**
     * Upload с использованием staging buffer (для больших данных)
     */
    void UploadViaStaging(const void* data, VkDeviceSize size, VkDeviceSize offset);

public:
    VkBuffer      m_Buffer     = VK_NULL_HANDLE;  // Vulkan buffer handle
    VmaAllocation m_Allocation = VK_NULL_HANDLE;  // VMA allocation handle
    VkDeviceSize  m_Size       = 0;                // Buffer size
    void*         m_Mapped     = nullptr;          // Mapped pointer (если mapped)

private:
    VkBufferUsageFlags m_Usage    = 0;     // Usage flags
    VmaMemoryUsage     m_MemUsage = VMA_MEMORY_USAGE_AUTO;  // Memory usage
};

} // namespace VK
