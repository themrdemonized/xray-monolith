// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// Licensed under the same terms as X-Ray Engine (see root License.txt)

#pragma once
#include "vk_core.h"

namespace VK
{

/**
 * Vulkan Compute Pipeline
 *
 * Wrapper для compute pipeline в Vulkan.
 * Используется для GPU-driven симуляций (3D Fluid, particles, etc.)
 *
 * Usage:
 *   CVulkanComputePipeline pipeline;
 *   pipeline.Create(shaderModule, pipelineLayout);
 *   pipeline.Dispatch(cmd, groupCountX, groupCountY, groupCountZ);
 *   pipeline.Destroy();
 */
class CVulkanComputePipeline
{
public:
    CVulkanComputePipeline();
    ~CVulkanComputePipeline();

    /**
     * Создать compute pipeline
     * @param shader Compute shader module
     * @param layout Pipeline layout (descriptor sets + push constants)
     */
    void Create(VkShaderModule shader, VkPipelineLayout layout);

    /**
     * Dispatch compute работы
     * @param cmd Command buffer
     * @param groupCountX Количество workgroups по X
     * @param groupCountY Количество workgroups по Y
     * @param groupCountZ Количество workgroups по Z
     */
    void Dispatch(VkCommandBuffer cmd, u32 groupCountX, u32 groupCountY, u32 groupCountZ);

    /**
     * Bind pipeline к command buffer
     */
    void Bind(VkCommandBuffer cmd);

    /**
     * Уничтожить pipeline
     */
    void Destroy();

    // Accessors
    VkPipeline GetPipeline() const { return m_Pipeline; }
    bool IsValid() const { return m_Pipeline != VK_NULL_HANDLE; }

private:
    VkPipeline m_Pipeline = VK_NULL_HANDLE;
};

/**
 * Compute Shader Module Loader
 *
 * Загрузка SPIR-V шейдеров для compute pipelines
 */
class CVulkanComputeShader
{
public:
    CVulkanComputeShader();
    ~CVulkanComputeShader();

    /**
     * Загрузить SPIR-V compute shader из файла
     * @param filename Путь к .spv файлу
     * @return true если успешно
     */
    bool Load(const char* filename);

    /**
     * Создать shader module из SPIR-V bytecode
     * @param code SPIR-V bytecode
     * @param size Размер в байтах
     */
    void CreateFromMemory(const u32* code, size_t size);

    /**
     * Уничтожить shader module
     */
    void Destroy();

    // Accessors
    VkShaderModule GetModule() const { return m_ShaderModule; }
    bool IsValid() const { return m_ShaderModule != VK_NULL_HANDLE; }

private:
    VkShaderModule m_ShaderModule = VK_NULL_HANDLE;
};

} // namespace VK
