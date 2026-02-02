// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// SPDX-License-Identifier: MIT

#include "stdafx.h"
#include "vk_pipeline.h"
#include "HW_Vulkan.h"
#include "vk_descriptors.h"
#include "vk_lighting.h"
#include <fstream>

namespace VK
{

// ============================================================================
// PipelineConfig
// ============================================================================

// Hash calculation для pipeline caching
size_t PipelineConfig::Hash() const
{
    size_t hash = 0;

    // Combine all parameters
    hash ^= std::hash<VkShaderModule>{}(vertShader);
    hash ^= std::hash<VkShaderModule>{}(fragShader) << 1;
    hash ^= std::hash<u32>{}(topology) << 2;
    hash ^= std::hash<u32>{}(cullMode) << 3;
    hash ^= std::hash<u32>{}(frontFace) << 4;
    hash ^= std::hash<u32>{}(polygonMode) << 5;
    hash ^= std::hash<bool>{}(depthTest) << 6;
    hash ^= std::hash<bool>{}(depthWrite) << 7;
    hash ^= std::hash<u32>{}(depthCompareOp) << 8;
    hash ^= std::hash<u32>{}(colorAttachmentCount) << 9;
    hash ^= std::hash<u32>{}(depthFormat) << 10;
    hash ^= std::hash<bool>{}(blendEnable) << 11;
    hash ^= std::hash<bool>{}(useDefaultVertexInput) << 20;
    hash ^= std::hash<bool>{}(useCustomVertexInput) << 21;
    hash ^= std::hash<u32>{}(customAttributeCount) << 22;
    hash ^= std::hash<u32>{}(vertexStride) << 23;

    // Hash color formats
    for (u32 i = 0; i < colorAttachmentCount && i < 8; ++i) {
        hash ^= std::hash<u32>{}(colorFormats[i]) << (12 + i);
    }

    return hash;
}

// Equality operator
bool PipelineConfig::operator==(const PipelineConfig& other) const
{
    if (vertShader != other.vertShader) return false;
    if (fragShader != other.fragShader) return false;
    if (topology != other.topology) return false;
    if (cullMode != other.cullMode) return false;
    if (frontFace != other.frontFace) return false;
    if (polygonMode != other.polygonMode) return false;
    if (depthTest != other.depthTest) return false;
    if (depthWrite != other.depthWrite) return false;
    if (depthCompareOp != other.depthCompareOp) return false;
    if (colorAttachmentCount != other.colorAttachmentCount) return false;
    if (depthFormat != other.depthFormat) return false;
    if (blendEnable != other.blendEnable) return false;
    if (useDefaultVertexInput != other.useDefaultVertexInput) return false;
    if (useCustomVertexInput != other.useCustomVertexInput) return false;
    if (customAttributeCount != other.customAttributeCount) return false;
    if (vertexStride != other.vertexStride) return false;

    for (u32 i = 0; i < colorAttachmentCount && i < 8; ++i) {
        if (colorFormats[i] != other.colorFormats[i]) return false;
    }

    return true;
}

// ============================================================================
// CVulkanPipelineManager
// ============================================================================

// Constructor
CVulkanPipelineManager::CVulkanPipelineManager()
{
    Msg("[Vulkan] CVulkanPipelineManager::CVulkanPipelineManager()");
}

// Destructor
CVulkanPipelineManager::~CVulkanPipelineManager()
{
    Msg("[Vulkan] CVulkanPipelineManager::~CVulkanPipelineManager()");
    Destroy();
}

// Создание
void CVulkanPipelineManager::Create()
{
    if (m_bCreated) {
        Msg("![Vulkan] PipelineManager already created");
        return;
    }

    Msg("[Vulkan] Creating Pipeline Manager...");

    CreatePipelineLayout();
    CreatePipelineCache();

    m_bCreated = true;

    Msg("[Vulkan] Pipeline Manager created successfully");
}

// Уничтожение
void CVulkanPipelineManager::Destroy()
{
    if (!m_bCreated) {
        return;
    }

    Msg("[Vulkan] Destroying Pipeline Manager...");

    // Destroy all cached pipelines
    for (auto& [hash, pipeline] : m_Pipelines) {
        vkDestroyPipeline(VulkanHW.m_Device, pipeline, nullptr);
    }
    m_Pipelines.clear();

    // Destroy pipeline cache
    if (m_Cache != VK_NULL_HANDLE) {
        vkDestroyPipelineCache(VulkanHW.m_Device, m_Cache, nullptr);
        m_Cache = VK_NULL_HANDLE;
    }

    // Destroy pipeline layout
    if (m_Layout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(VulkanHW.m_Device, m_Layout, nullptr);
        m_Layout = VK_NULL_HANDLE;
    }

    m_bCreated = false;

    Msg("[Vulkan] Pipeline Manager destroyed");
}

// Создание pipeline layout
void CVulkanPipelineManager::CreatePipelineLayout()
{
    Msg("[Vulkan] Creating pipeline layout...");

    // Получаем descriptor set layouts из DescriptorManager и VulkanLighting
    if (!g_DescriptorManager) {
        Msg("![Vulkan] DescriptorManager not initialized");
        return;
    }

    if (!g_VulkanLighting) {
        Msg("![Vulkan] VulkanLighting not initialized");
        return;
    }

    VkDescriptorSetLayout layouts[5] = {
        g_VulkanLighting->GetGlobalLightingLayout(),      // Set 0 (GlobalLighting UBO)
        g_DescriptorManager->GetPerMaterialLayout(),      // Set 1 (Textures)
        g_DescriptorManager->GetPerObjectLayout(),        // Set 2 (Object data)
        g_DescriptorManager->GetLightingLayout(),         // Set 3 (Light data)
        g_VulkanLighting->GetMaterialConstantsLayout()    // Set 4 (MaterialConstants UBO)
    };

    // Push constants для shadow MVP matrix и других per-draw данных
    // Range: 256 bytes (достаточно для 3x Fmatrix 4x4 = 192 bytes + запас)
    // Used by:
    // - G-Buffer pass: Model/View/Projection matrices (192 bytes)
    // - Shadow pass: MVP matrix (64 bytes)
    // - Lighting pass: various parameters
    VkPushConstantRange pushConstantRange = {};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = 256;  // 256 bytes для flexibility

    VkPipelineLayoutCreateInfo layoutInfo = {};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 5;
    layoutInfo.pSetLayouts = layouts;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushConstantRange;

    VK_CHECK(vkCreatePipelineLayout(VulkanHW.m_Device, &layoutInfo, nullptr, &m_Layout));

    Msg("[Vulkan] Pipeline layout created (5 descriptor sets + 256 byte push constants)");
}

// Создание pipeline cache
void CVulkanPipelineManager::CreatePipelineCache()
{
    Msg("[Vulkan] Creating pipeline cache...");

    VkPipelineCacheCreateInfo cacheInfo = {};
    cacheInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    cacheInfo.initialDataSize = 0;
    cacheInfo.pInitialData = nullptr;

    VK_CHECK(vkCreatePipelineCache(VulkanHW.m_Device, &cacheInfo, nullptr, &m_Cache));

    Msg("[Vulkan] Pipeline cache created");
}

// Get or create pipeline
VkPipeline CVulkanPipelineManager::GetOrCreate(const PipelineConfig& config)
{
    // Calculate hash
    size_t hash = config.Hash();

    // Check cache
    auto it = m_Pipelines.find(hash);
    if (it != m_Pipelines.end()) {
        return it->second;
    }

    // Create new pipeline
    VkPipeline pipeline = CreateGraphicsPipeline(config);
    if (pipeline == VK_NULL_HANDLE) {
        Msg("![Vulkan] Failed to create graphics pipeline");
        return VK_NULL_HANDLE;
    }

    // Cache it
    m_Pipelines[hash] = pipeline;

    Msg("[Vulkan] Graphics pipeline created and cached (hash: 0x%zX, total: %u)",
        hash, (u32)m_Pipelines.size());

    return pipeline;
}

// Default vertex input - parametric by stride:
//
// Layout A (stride 32 - level static geometry):
//   Position (FLOAT3)    @ offset 0
//   Normal   (D3DCOLOR)  @ offset 12
//   TexCoord (SHORT2)    @ offset 24  (need /1024 in shader)
//
// Layout B (stride 36/40/44 - skinned meshes):
//   Position (FLOAT3 from FLOAT4, w ignored) @ offset 0
//   Normal   (D3DCOLOR)  @ offset 16
//   TexCoord (FLOAT2)    @ offset 28  (native float UVs)
//
void CVulkanPipelineManager::GetDefaultVertexInputState(
    VkPipelineVertexInputStateCreateInfo& vertexInputInfo,
    VkVertexInputBindingDescription& binding,
    VkVertexInputAttributeDescription attributes[3],
    u32 stride)
{
    // Binding description
    binding.binding = 0;
    binding.stride = stride;
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    // Attribute 0: Position (vec3) @ offset 0 (always FLOAT3, reads xyz)
    attributes[0].binding = 0;
    attributes[0].location = 0;
    attributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributes[0].offset = 0;

    if (stride == 32)
    {
        // Layout A: level static geometry
        // Normal (D3DCOLOR) @ offset 12
        attributes[1].binding = 0;
        attributes[1].location = 1;
        attributes[1].format = VK_FORMAT_R8G8B8A8_UNORM;
        attributes[1].offset = 12;

        // TexCoord (SHORT2 SSCALED) @ offset 24
        attributes[2].binding = 0;
        attributes[2].location = 2;
        attributes[2].format = VK_FORMAT_R16G16_SSCALED;
        attributes[2].offset = 24;
    }
    else
    {
        // Layout B: skinned meshes (stride 36/40/44)
        // Normal (D3DCOLOR) @ offset 16 (after FLOAT4 position)
        attributes[1].binding = 0;
        attributes[1].location = 1;
        attributes[1].format = VK_FORMAT_R8G8B8A8_UNORM;
        attributes[1].offset = 16;

        // TexCoord (FLOAT2) @ offset 28
        attributes[2].binding = 0;
        attributes[2].location = 2;
        attributes[2].format = VK_FORMAT_R32G32_SFLOAT;
        attributes[2].offset = 28;
    }

    // Vertex input state
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &binding;
    vertexInputInfo.vertexAttributeDescriptionCount = 3;
    vertexInputInfo.pVertexAttributeDescriptions = attributes;
}

// Create graphics pipeline
VkPipeline CVulkanPipelineManager::CreateGraphicsPipeline(const PipelineConfig& config)
{
    // ========================================================================
    // Shader stages
    // ========================================================================
    VkPipelineShaderStageCreateInfo shaderStages[2] = {};

    shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    shaderStages[0].module = config.vertShader;
    shaderStages[0].pName = "main";

    shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    shaderStages[1].module = config.fragShader;
    shaderStages[1].pName = "main";

    // ========================================================================
    // Vertex input state
    // ========================================================================
    VkPipelineVertexInputStateCreateInfo vertexInputInfo = {};
    VkVertexInputBindingDescription binding = {};
    VkVertexInputAttributeDescription attributes[3] = {};

    if (config.useDefaultVertexInput) {
        GetDefaultVertexInputState(vertexInputInfo, binding, attributes, config.vertexStride);
    } else if (config.useCustomVertexInput && config.customAttributeCount > 0) {
        // Custom vertex input (e.g. sky box: vec3 position only)
        binding = config.customBinding;
        for (u32 i = 0; i < config.customAttributeCount && i < 4; ++i) {
            attributes[i] = config.customAttributes[i];
        }
        vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInputInfo.vertexBindingDescriptionCount = 1;
        vertexInputInfo.pVertexBindingDescriptions = &binding;
        vertexInputInfo.vertexAttributeDescriptionCount = config.customAttributeCount;
        vertexInputInfo.pVertexAttributeDescriptions = attributes;
    } else {
        // No vertex input (для fullscreen quad shader)
        vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInputInfo.vertexBindingDescriptionCount = 0;
        vertexInputInfo.vertexAttributeDescriptionCount = 0;
    }

    // ========================================================================
    // Input assembly
    // ========================================================================
    VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = config.topology;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    // ========================================================================
    // Viewport state (dynamic - устанавливается в command buffer)
    // ========================================================================
    VkPipelineViewportStateCreateInfo viewportState = {};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    // ========================================================================
    // Rasterization state
    // ========================================================================
    VkPipelineRasterizationStateCreateInfo rasterizer = {};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = config.polygonMode;
    rasterizer.lineWidth = config.lineWidth;
    rasterizer.cullMode = config.cullMode;
    rasterizer.frontFace = config.frontFace;
    rasterizer.depthBiasEnable = VK_FALSE;

    // ========================================================================
    // Multisample state (MSAA disabled for now)
    // ========================================================================
    VkPipelineMultisampleStateCreateInfo multisampling = {};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // ========================================================================
    // Depth/Stencil state
    // ========================================================================
    VkPipelineDepthStencilStateCreateInfo depthStencil = {};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = config.depthTest ? VK_TRUE : VK_FALSE;
    depthStencil.depthWriteEnable = config.depthWrite ? VK_TRUE : VK_FALSE;
    depthStencil.depthCompareOp = config.depthCompareOp;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    // ========================================================================
    // Color blend state
    // ========================================================================
    VkPipelineColorBlendAttachmentState colorBlendAttachments[8] = {};

    for (u32 i = 0; i < config.colorAttachmentCount && i < 8; ++i) {
        colorBlendAttachments[i].colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

        if (config.blendEnable) {
            // Alpha blending: srcColor * srcAlpha + dstColor * (1 - srcAlpha)
            colorBlendAttachments[i].blendEnable = VK_TRUE;
            colorBlendAttachments[i].srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            colorBlendAttachments[i].dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            colorBlendAttachments[i].colorBlendOp = VK_BLEND_OP_ADD;
            colorBlendAttachments[i].srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            colorBlendAttachments[i].dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
            colorBlendAttachments[i].alphaBlendOp = VK_BLEND_OP_ADD;
        } else {
            // No blending (для G-Buffer)
            colorBlendAttachments[i].blendEnable = VK_FALSE;
        }
    }

    VkPipelineColorBlendStateCreateInfo colorBlending = {};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = config.colorAttachmentCount;
    colorBlending.pAttachments = colorBlendAttachments;

    // ========================================================================
    // Dynamic state (viewport и scissor устанавливаются в runtime)
    // ========================================================================
    VkDynamicState dynamicStates[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };

    VkPipelineDynamicStateCreateInfo dynamicState = {};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    // ========================================================================
    // Dynamic Rendering info (Vulkan 1.3)
    // ========================================================================
    VkPipelineRenderingCreateInfo renderingInfo = {};
    renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    renderingInfo.colorAttachmentCount = config.colorAttachmentCount;
    renderingInfo.pColorAttachmentFormats = config.colorFormats;
    renderingInfo.depthAttachmentFormat = config.depthFormat;
    renderingInfo.stencilAttachmentFormat = VK_FORMAT_UNDEFINED;

    // ========================================================================
    // Create graphics pipeline
    // ========================================================================
    VkGraphicsPipelineCreateInfo pipelineInfo = {};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.pNext = &renderingInfo;  // Dynamic Rendering
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = m_Layout;
    pipelineInfo.renderPass = VK_NULL_HANDLE;  // Для Dynamic Rendering
    pipelineInfo.subpass = 0;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

    VkPipeline pipeline = VK_NULL_HANDLE;
    VkResult result = vkCreateGraphicsPipelines(VulkanHW.m_Device, m_Cache, 1, &pipelineInfo, nullptr, &pipeline);

    if (result != VK_SUCCESS) {
        Msg("![Vulkan] vkCreateGraphicsPipelines failed: %d", result);
        return VK_NULL_HANDLE;
    }

    return pipeline;
}

// Save cache to disk
void CVulkanPipelineManager::SaveCache(const char* filename)
{
    if (!m_bCreated || m_Cache == VK_NULL_HANDLE) {
        return;
    }

    Msg("[Vulkan] Saving pipeline cache to: %s", filename);

    // Get cache size
    size_t cacheSize = 0;
    vkGetPipelineCacheData(VulkanHW.m_Device, m_Cache, &cacheSize, nullptr);

    if (cacheSize == 0) {
        Msg("[Vulkan] Pipeline cache is empty, nothing to save");
        return;
    }

    // Get cache data
    xr_vector<u8> cacheData(cacheSize);
    vkGetPipelineCacheData(VulkanHW.m_Device, m_Cache, &cacheSize, cacheData.data());

    // Write to file
    xr_string fullPath = "gamedata\\";
    fullPath += filename;

    std::ofstream file(fullPath.c_str(), std::ios::binary);
    if (!file.is_open()) {
        Msg("![Vulkan] Failed to open file for writing: %s", fullPath.c_str());
        return;
    }

    file.write((const char*)cacheData.data(), cacheSize);
    file.close();

    Msg("[Vulkan] Pipeline cache saved (%zu bytes)", cacheSize);
}

// Load cache from disk
void CVulkanPipelineManager::LoadCache(const char* filename)
{
    if (!m_bCreated) {
        return;
    }

    Msg("[Vulkan] Loading pipeline cache from: %s", filename);

    xr_string fullPath = "gamedata\\";
    fullPath += filename;

    // Read file
    std::ifstream file(fullPath.c_str(), std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        Msg("[Vulkan] Pipeline cache file not found: %s", fullPath.c_str());
        return;
    }

    size_t fileSize = (size_t)file.tellg();
    xr_vector<u8> cacheData(fileSize);

    file.seekg(0);
    file.read((char*)cacheData.data(), fileSize);
    file.close();

    // Destroy old cache
    if (m_Cache != VK_NULL_HANDLE) {
        vkDestroyPipelineCache(VulkanHW.m_Device, m_Cache, nullptr);
        m_Cache = VK_NULL_HANDLE;
    }

    // Create cache with loaded data
    VkPipelineCacheCreateInfo cacheInfo = {};
    cacheInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    cacheInfo.initialDataSize = fileSize;
    cacheInfo.pInitialData = cacheData.data();

    VkResult result = vkCreatePipelineCache(VulkanHW.m_Device, &cacheInfo, nullptr, &m_Cache);
    if (result != VK_SUCCESS) {
        Msg("![Vulkan] Failed to create pipeline cache from file: %d", result);
        // Recreate empty cache
        CreatePipelineCache();
        return;
    }

    Msg("[Vulkan] Pipeline cache loaded (%zu bytes)", fileSize);
}

} // namespace VK

// Глобальный экземпляр (VK:: prefix нужен для соответствия объявлению в namespace VK)
VK::CVulkanPipelineManager* VK::g_PipelineManager = nullptr;
