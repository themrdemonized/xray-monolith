// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// Licensed under the same terms as X-Ray Engine (see root License.txt)

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
    hash ^= std::hash<u32>{}(tcOffset) << 25;
    hash ^= std::hash<u32>{}(customBindingCount) << 24;

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
    if (customBindingCount != other.customBindingCount) return false;
    if (vertexStride != other.vertexStride) return false;
    if (tcOffset != other.tcOffset) return false;

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
    u32 stride,
    u32 tcOffset)
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
        // Layout A: level static geometry (stride 32)
        // Two sub-layouts exist:
        //   lmap (tcOffset=24): Normal@12, TC0@24, TC1@28
        //   vert (tcOffset=28): Normal@12, COLOR@24, TC0@28
        // Normal (D3DCOLOR) @ offset 12 (same in both layouts)
        attributes[1].binding = 0;
        attributes[1].location = 1;
        attributes[1].format = VK_FORMAT_B8G8R8A8_UNORM;  // D3DCOLOR is BGRA in memory
        attributes[1].offset = 12;

        // TexCoord (SHORT2 SSCALED) @ tcOffset (24 or 28 depending on layout)
        attributes[2].binding = 0;
        attributes[2].location = 2;
        attributes[2].format = VK_FORMAT_R16G16_SSCALED;
        attributes[2].offset = tcOffset;
    }
    else
    {
        // Layout B: skinned meshes (stride 36/40/44)
        // Normal (D3DCOLOR) @ offset 16 (after FLOAT4 position)
        attributes[1].binding = 0;
        attributes[1].location = 1;
        attributes[1].format = VK_FORMAT_B8G8R8A8_UNORM;  // D3DCOLOR is BGRA in memory
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

// Skinned vertex input - includes bone data attributes for GPU skinning
//
// Stride 36 (1W):  FLOAT4 pos(0) + D3DCOLOR normal_idx(16) + D3DCOLOR tangent(20) + D3DCOLOR binormal(24) + FLOAT2 tc(28)
// Stride 44 (2W/3W): FLOAT4 pos(0) + D3DCOLOR normal_w0(16) + D3DCOLOR tangent_w1(20) + D3DCOLOR binormal_i2(24) + FLOAT4 tc_indices(28)
// Stride 40 (4W):  FLOAT4 pos(0) + D3DCOLOR normal_w0(16) + D3DCOLOR tangent_w1(20) + D3DCOLOR binormal_w2(24) + FLOAT2 tc(28) + D3DCOLOR indices(36)
//
void CVulkanPipelineManager::GetSkinnedVertexInputState(
    VkPipelineVertexInputStateCreateInfo& vertexInputInfo,
    VkVertexInputBindingDescription& binding,
    VkVertexInputAttributeDescription attributes[6],
    u32& attrCount,
    u32 stride)
{
    binding.binding = 0;
    binding.stride = stride;
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    // Attribute 0: Position (FLOAT4) @ offset 0 — all skinned formats start with FLOAT4
    attributes[0].binding = 0;
    attributes[0].location = 0;
    attributes[0].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    attributes[0].offset = 0;

    // Attribute 1: Normal + weight/index (D3DCOLOR) @ offset 16
    attributes[1].binding = 0;
    attributes[1].location = 1;
    attributes[1].format = VK_FORMAT_B8G8R8A8_UNORM;  // D3DCOLOR is BGRA in memory
    attributes[1].offset = 16;

    if (stride == 36)
    {
        // 1W: 5 attributes
        // Attr 2: TexCoord (FLOAT2) @ offset 28
        attributes[2].binding = 0;
        attributes[2].location = 2;
        attributes[2].format = VK_FORMAT_R32G32_SFLOAT;
        attributes[2].offset = 28;

        // Attr 3: Tangent (D3DCOLOR) @ offset 20
        attributes[3].binding = 0;
        attributes[3].location = 3;
        attributes[3].format = VK_FORMAT_B8G8R8A8_UNORM;
        attributes[3].offset = 20;

        // Attr 4: Binormal (D3DCOLOR) @ offset 24
        attributes[4].binding = 0;
        attributes[4].location = 4;
        attributes[4].format = VK_FORMAT_B8G8R8A8_UNORM;
        attributes[4].offset = 24;

        attrCount = 5;
    }
    else if (stride == 44)
    {
        // 2W/3W: 5 attributes, but tc is FLOAT4 (xy=tc, zw=bone indices)
        // Attr 2: TexCoord + indices (FLOAT4) @ offset 28
        attributes[2].binding = 0;
        attributes[2].location = 2;
        attributes[2].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        attributes[2].offset = 28;

        // Attr 3: Tangent (D3DCOLOR) @ offset 20
        attributes[3].binding = 0;
        attributes[3].location = 3;
        attributes[3].format = VK_FORMAT_B8G8R8A8_UNORM;
        attributes[3].offset = 20;

        // Attr 4: Binormal (D3DCOLOR) @ offset 24
        attributes[4].binding = 0;
        attributes[4].location = 4;
        attributes[4].format = VK_FORMAT_B8G8R8A8_UNORM;
        attributes[4].offset = 24;

        attrCount = 5;
    }
    else // stride == 40 (4W)
    {
        // 4W: 6 attributes
        // Attr 2: TexCoord (FLOAT2) @ offset 28
        attributes[2].binding = 0;
        attributes[2].location = 2;
        attributes[2].format = VK_FORMAT_R32G32_SFLOAT;
        attributes[2].offset = 28;

        // Attr 3: Tangent (D3DCOLOR) @ offset 20
        attributes[3].binding = 0;
        attributes[3].location = 3;
        attributes[3].format = VK_FORMAT_B8G8R8A8_UNORM;
        attributes[3].offset = 20;

        // Attr 4: Binormal (D3DCOLOR) @ offset 24
        attributes[4].binding = 0;
        attributes[4].location = 4;
        attributes[4].format = VK_FORMAT_B8G8R8A8_UNORM;
        attributes[4].offset = 24;

        // Attr 5: Bone indices (D3DCOLOR) @ offset 36
        attributes[5].binding = 0;
        attributes[5].location = 5;
        attributes[5].format = VK_FORMAT_B8G8R8A8_UNORM;
        attributes[5].offset = 36;

        attrCount = 6;
    }

    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &binding;
    vertexInputInfo.vertexAttributeDescriptionCount = attrCount;
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
    VkVertexInputBindingDescription bindings[4] = {};
    VkVertexInputAttributeDescription attributes[8] = {};

    if (config.useDefaultVertexInput) {
        GetDefaultVertexInputState(vertexInputInfo, binding, attributes, config.vertexStride, config.tcOffset);
    } else if (config.useCustomVertexInput && config.customAttributeCount > 0) {
        // Custom vertex input
        u32 bindingCount = 1;
        if (config.customBindingCount > 0) {
            // Multi-binding mode (e.g. vertex + instance data)
            bindingCount = config.customBindingCount;
            for (u32 i = 0; i < bindingCount && i < 4; ++i)
                bindings[i] = config.customBindings[i];
        } else {
            // Single binding mode (backward compatible)
            bindings[0] = config.customBinding;
        }
        for (u32 i = 0; i < config.customAttributeCount && i < 8; ++i) {
            attributes[i] = config.customAttributes[i];
        }
        vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInputInfo.vertexBindingDescriptionCount = bindingCount;
        vertexInputInfo.pVertexBindingDescriptions = bindings;
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
    rasterizer.depthBiasEnable = config.depthBiasEnable ? VK_TRUE : VK_FALSE;
    rasterizer.depthBiasConstantFactor = config.depthBiasConstant;
    rasterizer.depthBiasSlopeFactor = config.depthBiasSlope;
    rasterizer.depthBiasClamp = 0.f;

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
