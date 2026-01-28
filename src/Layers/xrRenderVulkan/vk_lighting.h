// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// SPDX-License-Identifier: MIT

#pragma once
#include "vk_core.h"

namespace VK
{

// Forward declarations
class CRenderTarget;

/**
 * CVulkanLighting - Управление lighting pipelines и descriptor sets
 *
 * Отвечает за:
 * - Создание pipelines для различных типов освещения
 * - Управление descriptor sets для G-Buffer textures
 * - Binding shaders и resources для lighting passes
 */
class CVulkanLighting
{
public:
    CVulkanLighting();
    ~CVulkanLighting();

    /**
     * Создать lighting resources
     * Вызывается после создания G-Buffer
     */
    void Create();

    /**
     * Уничтожить resources
     */
    void Destroy();

    /**
     * Setup для directional light pass (simple, без теней)
     * @param cmd Command buffer
     * @param rt Render target (для доступа к G-Buffer)
     * @param L_dir Light direction (view space)
     * @param L_color Light color (RGB) + specular (A)
     * @return true если успешно, false если нет шейдеров/pipeline
     */
    bool BindAccumDirectSimple(VkCommandBuffer cmd,
                                CRenderTarget* rt,
                                const Fvector& L_dir,
                                const Fvector& L_color,
                                float L_spec);

    /**
     * Создать descriptor set для G-Buffer textures
     * Вызывается при создании или изменении G-Buffer
     */
    void CreateGBufferDescriptorSet(CRenderTarget* rt);

private:
    /**
     * Загрузить шейдеры для accum_direct_simple
     */
    bool LoadAccumDirectSimpleShaders();

    /**
     * Создать pipeline для accum_direct_simple
     */
    bool CreateAccumDirectSimplePipeline();

    /**
     * Создать descriptor set layout для G-Buffer
     */
    void CreateGBufferDescriptorSetLayout();

private:
    /**
     * Создать sampler для G-Buffer текстур
     */
    void CreateSampler();

private:
    // === Accum Direct Simple (sun без теней) ===
    VkShaderModule m_AccumDirectSimple_VS = VK_NULL_HANDLE;
    VkShaderModule m_AccumDirectSimple_FS = VK_NULL_HANDLE;
    VkPipeline m_AccumDirectSimple_Pipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_AccumDirectSimple_PipelineLayout = VK_NULL_HANDLE;

    // === Descriptor Sets ===
    VkDescriptorSetLayout m_GBufferLayout = VK_NULL_HANDLE;  // Layout для G-Buffer textures
    VkDescriptorSet m_GBufferDescriptorSet = VK_NULL_HANDLE; // Set с G-Buffer textures
    VkDescriptorPool m_DescriptorPool = VK_NULL_HANDLE;      // Pool для lighting descriptor sets

    // === Sampler ===
    VkSampler m_GBufferSampler = VK_NULL_HANDLE;  // Linear sampler для G-Buffer

    // === Push Constants ===
    struct PushConstants {
        Fvector4 Ldynamic_dir;    // Light direction (view space) + unused w
        Fvector4 Ldynamic_color;  // RGB + specular
    };

    bool m_bCreated = false;
    bool m_bShadersLoaded = false;
    bool m_bPipelineCreated = false;
    bool m_bDescriptorSetUpdated = false;
};

// Глобальный экземпляр
extern CVulkanLighting* g_VulkanLighting;

} // namespace VK
