// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// SPDX-License-Identifier: MIT

#pragma once
#include "vk_core.h"

namespace VK
{

/**
 * Vulkan Descriptor Manager
 *
 * Управляет descriptor set layouts, descriptor pool и allocation.
 *
 * Descriptor Sets организованы по частоте обновления:
 *   Set 0 (PerFrame)    - Обновляется каждый кадр (viewProj, camera)
 *   Set 1 (PerMaterial) - Обновляется при смене материала (текстуры)
 *   Set 2 (PerObject)   - Обновляется для каждого объекта (world matrix)
 *   Set 3 (Lighting)    - Обновляется при изменении освещения
 *
 * Это соответствует лучшим практикам Vulkan для минимизации rebind.
 */
class CVulkanDescriptorManager
{
public:
    CVulkanDescriptorManager();
    ~CVulkanDescriptorManager();

    /**
     * Создать descriptor set layouts и pool
     */
    void Create();

    /**
     * Уничтожить все ресурсы
     */
    void Destroy();

    /**
     * Получить layouts для создания pipeline layout
     */
    VkDescriptorSetLayout GetPerFrameLayout() const { return m_PerFrameLayout; }
    VkDescriptorSetLayout GetPerMaterialLayout() const { return m_PerMaterialLayout; }
    VkDescriptorSetLayout GetPerObjectLayout() const { return m_PerObjectLayout; }
    VkDescriptorSetLayout GetLightingLayout() const { return m_LightingLayout; }

    /**
     * Allocate descriptor sets
     */
    VkDescriptorSet AllocatePerFrame();
    VkDescriptorSet AllocatePerMaterial();
    VkDescriptorSet AllocatePerObject();
    VkDescriptorSet AllocateLighting();

    /**
     * Update uniform buffer binding
     * @param set Descriptor set to update
     * @param binding Binding index
     * @param buffer Uniform buffer
     * @param size Buffer size
     * @param offset Offset in buffer (default 0)
     */
    void UpdateBuffer(VkDescriptorSet set, u32 binding, VkBuffer buffer,
                      VkDeviceSize size, VkDeviceSize offset = 0);

    /**
     * Update texture binding
     * @param set Descriptor set to update
     * @param binding Binding index
     * @param view Image view
     * @param sampler Sampler
     */
    void UpdateTexture(VkDescriptorSet set, u32 binding,
                       VkImageView view, VkSampler sampler);

    /**
     * Update multiple textures at once (для материалов с несколькими текстурами)
     */
    void UpdateTextures(VkDescriptorSet set, u32 firstBinding,
                        const VkImageView* views, const VkSampler* samplers, u32 count);

    /**
     * Reset pool (освобождает все allocated sets)
     * Полезно для начала нового кадра
     */
    void ResetPool();

    /**
     * Статистика
     */
    u32 GetAllocatedSets() const { return m_AllocatedSets; }

private:
    /**
     * Создать descriptor set layouts
     */
    void CreateLayouts();

    /**
     * Создать descriptor pool
     */
    void CreatePool();

    /**
     * Helper для создания layout
     */
    VkDescriptorSetLayout CreateLayout(const VkDescriptorSetLayoutBinding* bindings, u32 count);

private:
    // Descriptor Set Layouts
    VkDescriptorSetLayout m_PerFrameLayout    = VK_NULL_HANDLE;  // Set 0
    VkDescriptorSetLayout m_PerMaterialLayout = VK_NULL_HANDLE;  // Set 1
    VkDescriptorSetLayout m_PerObjectLayout   = VK_NULL_HANDLE;  // Set 2
    VkDescriptorSetLayout m_LightingLayout    = VK_NULL_HANDLE;  // Set 3

    // Descriptor Pool
    VkDescriptorPool m_Pool = VK_NULL_HANDLE;

    // Статистика
    u32 m_AllocatedSets = 0;
    bool m_bCreated = false;
};

} // namespace VK

// Глобальный экземпляр
extern VK::CVulkanDescriptorManager* g_DescriptorManager;
