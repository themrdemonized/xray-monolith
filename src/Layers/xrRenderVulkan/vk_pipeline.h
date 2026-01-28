#pragma once
#include "vk_core.h"
#include <functional>

namespace VK
{

/**
 * Pipeline Configuration
 *
 * Описывает все параметры graphics pipeline для создания.
 * Используется для hash-based caching - pipelines с одинаковой
 * конфигурацией переиспользуются.
 */
struct PipelineConfig
{
    // Shaders
    VkShaderModule vertShader = VK_NULL_HANDLE;
    VkShaderModule fragShader = VK_NULL_HANDLE;

    // Primitive topology
    VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    // Rasterization
    VkCullModeFlags cullMode = VK_CULL_MODE_BACK_BIT;
    VkFrontFace frontFace = VK_FRONT_FACE_CLOCKWISE;
    VkPolygonMode polygonMode = VK_POLYGON_MODE_FILL;
    float lineWidth = 1.0f;

    // Depth/Stencil
    bool depthTest = true;
    bool depthWrite = true;
    VkCompareOp depthCompareOp = VK_COMPARE_OP_LESS;

    // Color attachments (для Dynamic Rendering)
    u32 colorAttachmentCount = 1;
    VkFormat colorFormats[8] = { VK_FORMAT_UNDEFINED };
    VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;

    // Blending (по умолчанию - disabled для G-Buffer)
    bool blendEnable = false;
    VkBlendFactor srcColorBlend = VK_BLEND_FACTOR_ONE;
    VkBlendFactor dstColorBlend = VK_BLEND_FACTOR_ZERO;
    VkBlendFactor srcAlphaBlend = VK_BLEND_FACTOR_ONE;
    VkBlendFactor dstAlphaBlend = VK_BLEND_FACTOR_ZERO;
    VkCompareOp depthCompare = VK_COMPARE_OP_LESS;  // Alias for depthCompareOp

    // Vertex input (по умолчанию - position, texcoord, normal)
    bool useDefaultVertexInput = true;

    /**
     * Calculate hash для caching
     * Pipelines с одинаковым hash переиспользуются
     */
    size_t Hash() const;

    /**
     * Equality operator для hash map
     */
    bool operator==(const PipelineConfig& other) const;
};

/**
 * Vulkan Pipeline Manager
 *
 * Управляет pipeline layout, pipeline cache и graphics pipelines.
 *
 * Features:
 * - Hash-based pipeline caching (избегает создания дубликатов)
 * - VkPipelineCache для ускорения компиляции шейдеров
 * - Pipeline layout с 4 descriptor sets
 * - Dynamic Rendering (Vulkan 1.3)
 * - Сохранение/загрузка pipeline cache на диск
 */
class CVulkanPipelineManager
{
public:
    CVulkanPipelineManager();
    ~CVulkanPipelineManager();

    /**
     * Создать pipeline layout и cache
     * Должен вызываться после создания DescriptorManager
     */
    void Create();

    /**
     * Уничтожить все ресурсы
     */
    void Destroy();

    /**
     * Получить pipeline layout (для vkCmdBindDescriptorSets)
     */
    VkPipelineLayout GetLayout() const { return m_Layout; }

    /**
     * Получить или создать pipeline по конфигурации
     * @param config Конфигурация pipeline
     * @return VkPipeline или VK_NULL_HANDLE при ошибке
     *
     * Если pipeline с таким hash уже существует - вернёт кэшированный.
     * Иначе создаст новый и закэширует.
     */
    VkPipeline GetOrCreate(const PipelineConfig& config);

    /**
     * Сохранить pipeline cache на диск
     * @param filename Имя файла (относительно gamedata/)
     *
     * Ускоряет последующие запуски - Vulkan driver
     * может переиспользовать скомпилированные pipelines.
     */
    void SaveCache(const char* filename);

    /**
     * Загрузить pipeline cache с диска
     * @param filename Имя файла (относительно gamedata/)
     */
    void LoadCache(const char* filename);

    /**
     * Статистика
     */
    u32 GetCachedPipelineCount() const { return (u32)m_Pipelines.size(); }

private:
    /**
     * Создать pipeline layout из descriptor set layouts
     */
    void CreatePipelineLayout();

    /**
     * Создать pipeline cache
     */
    void CreatePipelineCache();

    /**
     * Создать graphics pipeline из конфигурации
     */
    VkPipeline CreateGraphicsPipeline(const PipelineConfig& config);

    /**
     * Получить default vertex input state (position, texcoord, normal)
     */
    void GetDefaultVertexInputState(
        VkPipelineVertexInputStateCreateInfo& vertexInputInfo,
        VkVertexInputBindingDescription& binding,
        VkVertexInputAttributeDescription attributes[3]);

private:
    // Pipeline layout (4 descriptor sets)
    VkPipelineLayout m_Layout = VK_NULL_HANDLE;

    // Pipeline cache (для ускорения компиляции)
    VkPipelineCache m_Cache = VK_NULL_HANDLE;

    // Кэш pipelines: hash -> VkPipeline
    xr_map<size_t, VkPipeline> m_Pipelines;

    bool m_bCreated = false;
};

// Глобальный экземпляр
extern CVulkanPipelineManager* g_PipelineManager;

} // namespace VK

// Hash function для PipelineConfig (для xr_map)
namespace std {
    template<>
    struct hash<VK::PipelineConfig> {
        size_t operator()(const VK::PipelineConfig& config) const {
            return config.Hash();
        }
    };
}
