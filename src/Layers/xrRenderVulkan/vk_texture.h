// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// SPDX-License-Identifier: MIT

#pragma once
#include "vk_core.h"

namespace VK
{

/**
 * Vulkan Texture Wrapper
 *
 * Класс для работы с текстурами в Vulkan.
 * Включает VkImage, VkImageView и VkSampler.
 *
 * Поддерживаемые форматы:
 * - RGBA8 (VK_FORMAT_R8G8B8A8_UNORM)
 * - BC1/DXT1 (VK_FORMAT_BC1_RGBA_UNORM_BLOCK)
 * - BC3/DXT5 (VK_FORMAT_BC3_UNORM_BLOCK)
 * - BC5 (VK_FORMAT_BC5_UNORM_BLOCK)
 *
 * Usage:
 *   CVulkanTexture tex;
 *   tex.CreateFromData(pixels, 256, 256, VK_FORMAT_R8G8B8A8_UNORM);
 *   // ... use tex.GetView() and tex.GetSampler() for binding
 *   tex.Destroy();
 */
class CVulkanTexture
{
public:
    CVulkanTexture();
    ~CVulkanTexture();

    /**
     * Создать пустую текстуру
     * @param width Ширина
     * @param height Высота
     * @param format VkFormat
     * @param mipLevels Количество mip уровней (1 = без mips)
     * @param usage Image usage flags
     */
    void Create(u32 width, u32 height, VkFormat format, u32 mipLevels = 1,
                VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);

    /**
     * Создать текстуру из RGBA данных
     * @param data Указатель на пиксели (RGBA или compressed)
     * @param width Ширина
     * @param height Высота
     * @param format VkFormat
     * @param dataSize Размер данных в байтах (для compressed текстур)
     */
    void CreateFromData(const void* data, u32 width, u32 height, VkFormat format,
                        VkDeviceSize dataSize = 0);

    /**
     * Загрузить текстуру из DDS файла
     * @param filename Путь к файлу
     * @return true если успешно
     */
    bool LoadDDS(const char* filename);

    /**
     * Уничтожить текстуру
     */
    void Destroy();

    /**
     * Transition image layout
     * Используется для подготовки текстуры к transfer или sampling
     */
    void TransitionLayout(VkCommandBuffer cmd, VkImageLayout oldLayout, VkImageLayout newLayout);

    /**
     * Transition layout используя временный command buffer
     */
    void TransitionLayoutImmediate(VkImageLayout oldLayout, VkImageLayout newLayout);

    // Getters
    VkImage GetImage() const { return m_Image; }
    VkImageView GetView() const { return m_ImageView; }
    VkSampler GetSampler() const { return m_Sampler; }
    u32 GetWidth() const { return m_Width; }
    u32 GetHeight() const { return m_Height; }
    u32 GetMipLevels() const { return m_MipLevels; }
    VkFormat GetFormat() const { return m_Format; }
    VkImageLayout GetCurrentLayout() const { return m_CurrentLayout; }
    bool IsValid() const { return m_Image != VK_NULL_HANDLE; }

private:
    /**
     * Создать VkImageView
     */
    void CreateImageView();

    /**
     * Создать VkSampler
     */
    void CreateSampler();

    /**
     * Upload данных через staging buffer
     */
    void UploadData(const void* data, VkDeviceSize size);

    /**
     * Рассчитать количество mip levels
     */
    static u32 CalculateMipLevels(u32 width, u32 height);

    /**
     * Проверить является ли формат compressed (BC/DXT)
     */
    static bool IsCompressedFormat(VkFormat format);

    /**
     * Получить размер блока для compressed формата
     */
    static u32 GetBlockSize(VkFormat format);

private:
    VkImage         m_Image       = VK_NULL_HANDLE;
    VmaAllocation   m_Allocation  = VK_NULL_HANDLE;
    VkImageView     m_ImageView   = VK_NULL_HANDLE;
    VkSampler       m_Sampler     = VK_NULL_HANDLE;

    u32             m_Width       = 0;
    u32             m_Height      = 0;
    u32             m_MipLevels   = 1;
    VkFormat        m_Format      = VK_FORMAT_R8G8B8A8_UNORM;
    VkImageLayout   m_CurrentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    bool            m_bAlphaSwizzle = false; // For alpha-only textures (fonts): swizzle R→A, RGB→ONE
    bool            m_bBCSwizzle = false;    // For BC/DXT textures: swizzle R<->B for DirectX compatibility
};

} // namespace VK
