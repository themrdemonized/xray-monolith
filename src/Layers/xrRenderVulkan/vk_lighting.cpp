// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// SPDX-License-Identifier: MIT

#include "stdafx.h"
#include "vk_lighting.h"
#include "vk_rendertarget.h"
#include "HW_Vulkan.h"
#include "vk_shaders.h"
#include "vk_pipeline.h"
#include "../../xrEngine/igame_persistent.h"  // For g_pGamePersistent and Environment access
#include "../../xrEngine/Environment.h"       // For CEnvDescriptor

// Глобальный экземпляр (VK:: prefix нужен для соответствия объявлению в namespace VK)
VK::CVulkanLighting* VK::g_VulkanLighting = nullptr;

namespace VK
{

// ============================================================================
// Constructor / Destructor
// ============================================================================

CVulkanLighting::CVulkanLighting()
{
    Msg("[Vulkan] CVulkanLighting::CVulkanLighting()");
}

CVulkanLighting::~CVulkanLighting()
{
    Destroy();
}

// ============================================================================
// Create / Destroy
// ============================================================================

void CVulkanLighting::Create()
{
    if (m_bCreated) {
        Msg("![Vulkan] CVulkanLighting already created");
        return;
    }

    Msg("[Vulkan] Creating CVulkanLighting...");

    // 1. Create sampler for G-Buffer textures
    CreateSampler();

    // 2. Create GlobalLighting UBO (Phase: Hemisphere Lighting)
    CreateGlobalLightingUBO();

    // 3. Create G-Buffer descriptor set layout
    CreateGBufferDescriptorSetLayout();

    // 4. Create descriptor pool for lighting
    VkDescriptorPoolSize poolSizes[] = {
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 16 },  // G-Buffer textures
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 4 }             // GlobalLighting UBO
    };

    VkDescriptorPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = 8;  // G-Buffer + GlobalLighting + future sets
    poolInfo.poolSizeCount = 2;
    poolInfo.pPoolSizes = poolSizes;

    VkResult result = vkCreateDescriptorPool(VulkanHW.m_Device, &poolInfo, nullptr, &m_DescriptorPool);
    if (result != VK_SUCCESS) {
        Msg("![Vulkan] Failed to create lighting descriptor pool: %d", result);
        return;
    }

    // 4. Load shaders for accum_direct_simple
    m_bShadersLoaded = LoadAccumDirectSimpleShaders();

    // 5. Create pipeline (if shaders loaded)
    if (m_bShadersLoaded) {
        m_bPipelineCreated = CreateAccumDirectSimplePipeline();
    }

    m_bCreated = true;
    Msg("[Vulkan] CVulkanLighting created successfully");
    Msg("[Vulkan]   - Shaders loaded: %s", m_bShadersLoaded ? "YES" : "NO (SPIR-V files missing)");
    Msg("[Vulkan]   - Pipeline created: %s", m_bPipelineCreated ? "YES" : "NO");
}

void CVulkanLighting::Destroy()
{
    if (!m_bCreated) {
        return;
    }

    Msg("[Vulkan] Destroying CVulkanLighting...");

    // Destroy GlobalLighting UBO (Phase: Hemisphere Lighting)
    DestroyGlobalLightingUBO();

    // Destroy pipeline
    if (m_AccumDirectSimple_Pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(VulkanHW.m_Device, m_AccumDirectSimple_Pipeline, nullptr);
        m_AccumDirectSimple_Pipeline = VK_NULL_HANDLE;
    }

    // Destroy pipeline layout
    if (m_AccumDirectSimple_PipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(VulkanHW.m_Device, m_AccumDirectSimple_PipelineLayout, nullptr);
        m_AccumDirectSimple_PipelineLayout = VK_NULL_HANDLE;
    }

    // Destroy shader modules (если они были загружены локально, а не через ShaderManager)
    // (В нашем случае они загружаются через g_ShaderManager, поэтому не нужно уничтожать)

    // Destroy descriptor pool (automatically frees all descriptor sets)
    if (m_DescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(VulkanHW.m_Device, m_DescriptorPool, nullptr);
        m_DescriptorPool = VK_NULL_HANDLE;
        m_GBufferDescriptorSet = VK_NULL_HANDLE;
    }

    // Destroy descriptor set layout
    if (m_GBufferLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(VulkanHW.m_Device, m_GBufferLayout, nullptr);
        m_GBufferLayout = VK_NULL_HANDLE;
    }

    // Destroy sampler
    if (m_GBufferSampler != VK_NULL_HANDLE) {
        vkDestroySampler(VulkanHW.m_Device, m_GBufferSampler, nullptr);
        m_GBufferSampler = VK_NULL_HANDLE;
    }

    m_bCreated = false;
    m_bShadersLoaded = false;
    m_bPipelineCreated = false;
    m_bDescriptorSetUpdated = false;

    Msg("[Vulkan] CVulkanLighting destroyed");
}

// ============================================================================
// Sampler для G-Buffer
// ============================================================================

void CVulkanLighting::CreateSampler()
{
    Msg("[Vulkan] Creating G-Buffer sampler...");

    VkSamplerCreateInfo samplerInfo = {};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;       // Linear filtering for magnification
    samplerInfo.minFilter = VK_FILTER_LINEAR;       // Linear filtering for minification
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;  // No mipmaps for G-Buffer
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 1.0f;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;

    VkResult result = vkCreateSampler(VulkanHW.m_Device, &samplerInfo, nullptr, &m_GBufferSampler);
    if (result != VK_SUCCESS) {
        Msg("![Vulkan] Failed to create G-Buffer sampler: %d", result);
        return;
    }

    Msg("[Vulkan] G-Buffer sampler created");
}

// ============================================================================
// Descriptor Set Layout для G-Buffer
// ============================================================================

void CVulkanLighting::CreateGBufferDescriptorSetLayout()
{
    Msg("[Vulkan] Creating G-Buffer descriptor set layout...");

    // Set 1: G-Buffer textures
    // Binding 0: s_position  (R32G32B32A32_SFLOAT)
    // Binding 1: s_normal    (R32G32B32A32_SFLOAT)
    // Binding 2: s_diffuse   (R8G8B8A8_SRGB)
    // Binding 3: s_material  (R8G8B8A8_UNORM)

    VkDescriptorSetLayoutBinding bindings[4] = {};

    // s_position
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    // s_normal
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    // s_diffuse
    bindings[2].binding = 2;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    // s_material
    bindings[3].binding = 3;
    bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[3].descriptorCount = 1;
    bindings[3].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo = {};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 4;
    layoutInfo.pBindings = bindings;

    VkResult result = vkCreateDescriptorSetLayout(VulkanHW.m_Device, &layoutInfo, nullptr, &m_GBufferLayout);
    if (result != VK_SUCCESS) {
        Msg("![Vulkan] Failed to create G-Buffer descriptor set layout: %d", result);
        return;
    }

    Msg("[Vulkan] G-Buffer descriptor set layout created");
}

// ============================================================================
// Загрузка шейдеров
// ============================================================================

bool CVulkanLighting::LoadAccumDirectSimpleShaders()
{
    Msg("[Vulkan] Loading accum_direct_simple shaders...");

    // Load through global ShaderManager
    if (!g_ShaderManager) {
        Msg("![Vulkan] g_ShaderManager is NULL");
        return false;
    }

    m_AccumDirectSimple_VS = g_ShaderManager->Load("vulkan/accum_sun_simple.vert.spv");
    m_AccumDirectSimple_FS = g_ShaderManager->Load("vulkan/accum_sun_simple.frag.spv");

    if (m_AccumDirectSimple_VS == VK_NULL_HANDLE || m_AccumDirectSimple_FS == VK_NULL_HANDLE) {
        Msg("![Vulkan] Failed to load accum_direct_simple shaders");
        Msg("![Vulkan]   - Make sure gamedata/shaders/vulkan/accum_sun_simple.*.spv exist");
        Msg("![Vulkan]   - Compile with: cd gamedata/shaders/vulkan && glslangValidator -V accum_sun_simple.vert && glslangValidator -V accum_sun_simple.frag");
        return false;
    }

    Msg("[Vulkan] accum_direct_simple shaders loaded successfully");
    return true;
}

// ============================================================================
// Создание Pipeline
// ============================================================================

bool CVulkanLighting::CreateAccumDirectSimplePipeline()
{
    Msg("[Vulkan] Creating accum_direct_simple pipeline...");

    // ========================================================================
    // 1. Pipeline Layout (Set 0: GlobalLighting, Set 1: G-Buffer + push constants)
    // ========================================================================

    VkPushConstantRange pushConstantRange = {};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(PushConstants);

    // Set layouts: Set 0 = GlobalLighting, Set 1 = G-Buffer
    VkDescriptorSetLayout setLayouts[] = {
        m_GlobalLightingLayout,  // Set 0: GlobalLighting UBO
        m_GBufferLayout          // Set 1: G-Buffer textures
    };

    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 2;  // Set 0 + Set 1
    pipelineLayoutInfo.pSetLayouts = setLayouts;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    VkResult result = vkCreatePipelineLayout(VulkanHW.m_Device, &pipelineLayoutInfo, nullptr, &m_AccumDirectSimple_PipelineLayout);
    if (result != VK_SUCCESS) {
        Msg("![Vulkan] Failed to create pipeline layout: %d", result);
        return false;
    }

    // ========================================================================
    // 2. Shader Stages
    // ========================================================================

    VkPipelineShaderStageCreateInfo vertShaderStageInfo = {};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = m_AccumDirectSimple_VS;
    vertShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragShaderStageInfo = {};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = m_AccumDirectSimple_FS;
    fragShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};

    // ========================================================================
    // 3. Vertex Input (fullscreen quad: position + UV)
    // ========================================================================

    VkVertexInputBindingDescription bindingDescription = {};
    bindingDescription.binding = 0;
    bindingDescription.stride = sizeof(float) * 6;  // vec4 pos + vec2 uv
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attributeDescriptions[2] = {};

    // Position (location = 0)
    attributeDescriptions[0].binding = 0;
    attributeDescriptions[0].location = 0;
    attributeDescriptions[0].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    attributeDescriptions[0].offset = 0;

    // TexCoord (location = 1)
    attributeDescriptions[1].binding = 0;
    attributeDescriptions[1].location = 1;
    attributeDescriptions[1].format = VK_FORMAT_R32G32_SFLOAT;
    attributeDescriptions[1].offset = sizeof(float) * 4;

    VkPipelineVertexInputStateCreateInfo vertexInputInfo = {};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = 2;
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions;

    // ========================================================================
    // 4. Input Assembly
    // ========================================================================

    VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    // ========================================================================
    // 5. Viewport/Scissor (dynamic)
    // ========================================================================

    VkPipelineViewportStateCreateInfo viewportState = {};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    // ========================================================================
    // 6. Rasterization (no culling for fullscreen quad)
    // ========================================================================

    VkPipelineRasterizationStateCreateInfo rasterizer = {};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = VK_CULL_MODE_NONE;  // No culling for fullscreen quad
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;
    rasterizer.lineWidth = 1.0f;

    // ========================================================================
    // 7. Multisampling (disabled)
    // ========================================================================

    VkPipelineMultisampleStateCreateInfo multisampling = {};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    multisampling.sampleShadingEnable = VK_FALSE;

    // ========================================================================
    // 8. Depth/Stencil (no depth write, stencil test only)
    // ========================================================================

    VkPipelineDepthStencilStateCreateInfo depthStencil = {};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_FALSE;   // No depth test
    depthStencil.depthWriteEnable = VK_FALSE;  // No depth write
    depthStencil.depthCompareOp = VK_COMPARE_OP_ALWAYS;

    // Stencil test: render only where stencil >= 1 (geometry exists)
    depthStencil.stencilTestEnable = VK_TRUE;
    depthStencil.front.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    depthStencil.front.compareMask = 0xFF;
    depthStencil.front.reference = 1;  // Will be set via vkCmdSetStencilReference
    depthStencil.front.failOp = VK_STENCIL_OP_KEEP;
    depthStencil.front.passOp = VK_STENCIL_OP_KEEP;
    depthStencil.front.depthFailOp = VK_STENCIL_OP_KEEP;
    depthStencil.back = depthStencil.front;

    // ========================================================================
    // 9. Blending (additive: ONE + ONE)
    // ========================================================================

    VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
    colorBlendAttachment.blendEnable = VK_TRUE;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                                          VK_COLOR_COMPONENT_G_BIT |
                                          VK_COLOR_COMPONENT_B_BIT |
                                          VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo colorBlending = {};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    // ========================================================================
    // 10. Dynamic States
    // ========================================================================

    VkDynamicState dynamicStates[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
        VK_DYNAMIC_STATE_STENCIL_REFERENCE
    };

    VkPipelineDynamicStateCreateInfo dynamicState = {};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 3;
    dynamicState.pDynamicStates = dynamicStates;

    // ========================================================================
    // 11. Rendering Info (Dynamic Rendering - Vulkan 1.3)
    // ========================================================================

    VkFormat colorAttachmentFormat = VK_FORMAT_R16G16B16A16_SFLOAT;  // rt_Accumulator

    VkPipelineRenderingCreateInfo pipelineRenderingInfo = {};
    pipelineRenderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    pipelineRenderingInfo.colorAttachmentCount = 1;
    pipelineRenderingInfo.pColorAttachmentFormats = &colorAttachmentFormat;
    pipelineRenderingInfo.depthAttachmentFormat = VK_FORMAT_D32_SFLOAT;
    pipelineRenderingInfo.stencilAttachmentFormat = VK_FORMAT_D32_SFLOAT;

    // ========================================================================
    // 12. Create Graphics Pipeline
    // ========================================================================

    VkGraphicsPipelineCreateInfo pipelineInfo = {};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.pNext = &pipelineRenderingInfo;  // Dynamic Rendering
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
    pipelineInfo.layout = m_AccumDirectSimple_PipelineLayout;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

    result = vkCreateGraphicsPipelines(VulkanHW.m_Device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_AccumDirectSimple_Pipeline);

    if (result != VK_SUCCESS) {
        Msg("![Vulkan] Failed to create pipeline: %d", result);
        return false;
    }

    Msg("[Vulkan] accum_direct_simple pipeline created successfully");
    return true;
}

// ============================================================================
// Create Descriptor Set для G-Buffer
// ============================================================================

void CVulkanLighting::CreateGBufferDescriptorSet(CRenderTarget* rt)
{
    if (!m_bCreated || !rt) {
        Msg("![Vulkan] Cannot create G-Buffer descriptor set - not created or rt is NULL");
        return;
    }

    if (m_GBufferSampler == VK_NULL_HANDLE) {
        Msg("![Vulkan] Cannot create G-Buffer descriptor set - sampler is NULL");
        return;
    }

    Msg("[Vulkan] Creating G-Buffer descriptor set...");

    // ========================================================================
    // 1. Allocate descriptor set
    // ========================================================================

    VkDescriptorSetAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_DescriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &m_GBufferLayout;

    VkResult result = vkAllocateDescriptorSets(VulkanHW.m_Device, &allocInfo, &m_GBufferDescriptorSet);
    if (result != VK_SUCCESS) {
        Msg("![Vulkan] Failed to allocate G-Buffer descriptor set: %d", result);
        return;
    }

    // ========================================================================
    // 2. Update descriptor set with G-Buffer textures
    // ========================================================================

    VkDescriptorImageInfo imageInfos[4] = {};

    // Binding 0: s_position
    imageInfos[0].sampler = m_GBufferSampler;
    imageInfos[0].imageView = rt->rt_Position.m_ImageView;
    imageInfos[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    // Binding 1: s_normal
    imageInfos[1].sampler = m_GBufferSampler;
    imageInfos[1].imageView = rt->rt_Normal.m_ImageView;
    imageInfos[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    // Binding 2: s_diffuse
    imageInfos[2].sampler = m_GBufferSampler;
    imageInfos[2].imageView = rt->rt_Color.m_ImageView;
    imageInfos[2].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    // Binding 3: s_material
    imageInfos[3].sampler = m_GBufferSampler;
    imageInfos[3].imageView = rt->rt_Material.m_ImageView;
    imageInfos[3].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet writes[4] = {};

    for (u32 i = 0; i < 4; i++) {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = m_GBufferDescriptorSet;
        writes[i].dstBinding = i;
        writes[i].dstArrayElement = 0;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[i].pImageInfo = &imageInfos[i];
    }

    vkUpdateDescriptorSets(VulkanHW.m_Device, 4, writes, 0, nullptr);

    m_bDescriptorSetUpdated = true;

    Msg("[Vulkan] G-Buffer descriptor set created and updated");
    Msg("[Vulkan]   - s_position: %p", rt->rt_Position.m_ImageView);
    Msg("[Vulkan]   - s_normal:   %p", rt->rt_Normal.m_ImageView);
    Msg("[Vulkan]   - s_diffuse:  %p", rt->rt_Color.m_ImageView);
    Msg("[Vulkan]   - s_material: %p", rt->rt_Material.m_ImageView);
}

// ============================================================================
// Bind для accum_direct_simple
// ============================================================================

bool CVulkanLighting::BindAccumDirectSimple(VkCommandBuffer cmd,
                                             CRenderTarget* rt,
                                             const Fvector& L_dir,
                                             const Fvector& L_color,
                                             float L_spec)
{
    if (!m_bPipelineCreated) {
        Msg("![Vulkan] Cannot bind accum_direct_simple - pipeline not created");
        return false;
    }

    // Create/update descriptor set if needed
    if (!m_bDescriptorSetUpdated) {
        CreateGBufferDescriptorSet(rt);
        if (!m_bDescriptorSetUpdated) {
            Msg("![Vulkan] Failed to create G-Buffer descriptor set");
            return false;
        }
    }

    // ========================================================================
    // 0. Update GlobalLighting UBO (Phase: Hemisphere Lighting)
    // ========================================================================
    UpdateGlobalLightingUBO();

    // ========================================================================
    // 1. Bind pipeline
    // ========================================================================

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_AccumDirectSimple_Pipeline);

    // ========================================================================
    // 2. Bind descriptor sets
    // ========================================================================

    // Bind Set 0: GlobalLighting UBO
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            m_AccumDirectSimple_PipelineLayout,
                            0,  // firstSet (set 0)
                            1,  // descriptorSetCount
                            &m_GlobalLightingDescriptorSet,
                            0, nullptr);  // No dynamic offsets

    // Bind Set 1: G-Buffer textures
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            m_AccumDirectSimple_PipelineLayout,
                            1,  // firstSet (set 1)
                            1,  // descriptorSetCount
                            &m_GBufferDescriptorSet,
                            0, nullptr);  // No dynamic offsets

    // ========================================================================
    // 3. Push constants (light direction + color)
    // ========================================================================

    PushConstants pushConstants;
    pushConstants.Ldynamic_dir.set(L_dir.x, L_dir.y, L_dir.z, 0.0f);
    pushConstants.Ldynamic_color.set(L_color.x, L_color.y, L_color.z, L_spec);

    vkCmdPushConstants(cmd,
                       m_AccumDirectSimple_PipelineLayout,
                       VK_SHADER_STAGE_FRAGMENT_BIT,
                       0,  // offset
                       sizeof(PushConstants),
                       &pushConstants);

    return true;
}

// ============================================================================
// Hemisphere Lighting Support (Phase: Hemisphere Lighting)
// ============================================================================

void CVulkanLighting::CreateGlobalLightingUBO()
{
    Msg("[Vulkan] Creating GlobalLighting UBO...");

    // ========================================================================
    // 1. Create Uniform Buffer
    // ========================================================================
    VkDeviceSize bufferSize = sizeof(GlobalLightingUBO);

    VkBufferCreateInfo bufferInfo = {};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = bufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkResult result = vkCreateBuffer(VulkanHW.m_Device, &bufferInfo, nullptr, &m_GlobalLightingBuffer);
    if (result != VK_SUCCESS) {
        Msg("![Vulkan] Failed to create GlobalLighting buffer: %d", result);
        return;
    }

    // ========================================================================
    // 2. Allocate Memory
    // ========================================================================
    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(VulkanHW.m_Device, m_GlobalLightingBuffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;

    // Find memory type (HOST_VISIBLE + HOST_COHERENT for easy updates)
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(VulkanHW.m_PhysicalDevice, &memProperties);

    u32 memoryTypeIndex = UINT32_MAX;
    VkMemoryPropertyFlags requiredProperties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

    for (u32 i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((memRequirements.memoryTypeBits & (1 << i)) &&
            (memProperties.memoryTypes[i].propertyFlags & requiredProperties) == requiredProperties) {
            memoryTypeIndex = i;
            break;
        }
    }

    if (memoryTypeIndex == UINT32_MAX) {
        Msg("![Vulkan] Failed to find suitable memory type for GlobalLighting UBO");
        vkDestroyBuffer(VulkanHW.m_Device, m_GlobalLightingBuffer, nullptr);
        m_GlobalLightingBuffer = VK_NULL_HANDLE;
        return;
    }

    allocInfo.memoryTypeIndex = memoryTypeIndex;

    result = vkAllocateMemory(VulkanHW.m_Device, &allocInfo, nullptr, &m_GlobalLightingMemory);
    if (result != VK_SUCCESS) {
        Msg("![Vulkan] Failed to allocate GlobalLighting memory: %d", result);
        vkDestroyBuffer(VulkanHW.m_Device, m_GlobalLightingBuffer, nullptr);
        m_GlobalLightingBuffer = VK_NULL_HANDLE;
        return;
    }

    result = vkBindBufferMemory(VulkanHW.m_Device, m_GlobalLightingBuffer, m_GlobalLightingMemory, 0);
    if (result != VK_SUCCESS) {
        Msg("![Vulkan] Failed to bind GlobalLighting buffer memory: %d", result);
        vkFreeMemory(VulkanHW.m_Device, m_GlobalLightingMemory, nullptr);
        vkDestroyBuffer(VulkanHW.m_Device, m_GlobalLightingBuffer, nullptr);
        m_GlobalLightingBuffer = VK_NULL_HANDLE;
        m_GlobalLightingMemory = VK_NULL_HANDLE;
        return;
    }

    // ========================================================================
    // 3. Map Memory (persistent mapping)
    // ========================================================================
    result = vkMapMemory(VulkanHW.m_Device, m_GlobalLightingMemory, 0, bufferSize, 0, &m_GlobalLightingMapped);
    if (result != VK_SUCCESS) {
        Msg("![Vulkan] Failed to map GlobalLighting memory: %d", result);
        vkFreeMemory(VulkanHW.m_Device, m_GlobalLightingMemory, nullptr);
        vkDestroyBuffer(VulkanHW.m_Device, m_GlobalLightingBuffer, nullptr);
        m_GlobalLightingBuffer = VK_NULL_HANDLE;
        m_GlobalLightingMemory = VK_NULL_HANDLE;
        return;
    }

    // ========================================================================
    // 4. Create Descriptor Set Layout (Set 0)
    // ========================================================================
    VkDescriptorSetLayoutBinding uboBinding = {};
    uboBinding.binding = 0;
    uboBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboBinding.descriptorCount = 1;
    uboBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    uboBinding.pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutCreateInfo layoutInfo = {};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &uboBinding;

    result = vkCreateDescriptorSetLayout(VulkanHW.m_Device, &layoutInfo, nullptr, &m_GlobalLightingLayout);
    if (result != VK_SUCCESS) {
        Msg("![Vulkan] Failed to create GlobalLighting descriptor set layout: %d", result);
        vkUnmapMemory(VulkanHW.m_Device, m_GlobalLightingMemory);
        vkFreeMemory(VulkanHW.m_Device, m_GlobalLightingMemory, nullptr);
        vkDestroyBuffer(VulkanHW.m_Device, m_GlobalLightingBuffer, nullptr);
        m_GlobalLightingBuffer = VK_NULL_HANDLE;
        m_GlobalLightingMemory = VK_NULL_HANDLE;
        m_GlobalLightingMapped = nullptr;
        return;
    }

    // ========================================================================
    // 5. Allocate Descriptor Set
    // ========================================================================
    VkDescriptorSetAllocateInfo allocDescInfo = {};
    allocDescInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocDescInfo.descriptorPool = m_DescriptorPool;
    allocDescInfo.descriptorSetCount = 1;
    allocDescInfo.pSetLayouts = &m_GlobalLightingLayout;

    result = vkAllocateDescriptorSets(VulkanHW.m_Device, &allocDescInfo, &m_GlobalLightingDescriptorSet);
    if (result != VK_SUCCESS) {
        Msg("![Vulkan] Failed to allocate GlobalLighting descriptor set: %d", result);
        vkDestroyDescriptorSetLayout(VulkanHW.m_Device, m_GlobalLightingLayout, nullptr);
        vkUnmapMemory(VulkanHW.m_Device, m_GlobalLightingMemory);
        vkFreeMemory(VulkanHW.m_Device, m_GlobalLightingMemory, nullptr);
        vkDestroyBuffer(VulkanHW.m_Device, m_GlobalLightingBuffer, nullptr);
        m_GlobalLightingBuffer = VK_NULL_HANDLE;
        m_GlobalLightingMemory = VK_NULL_HANDLE;
        m_GlobalLightingMapped = nullptr;
        m_GlobalLightingLayout = VK_NULL_HANDLE;
        return;
    }

    // ========================================================================
    // 6. Update Descriptor Set
    // ========================================================================
    VkDescriptorBufferInfo bufferDescInfo = {};
    bufferDescInfo.buffer = m_GlobalLightingBuffer;
    bufferDescInfo.offset = 0;
    bufferDescInfo.range = sizeof(GlobalLightingUBO);

    VkWriteDescriptorSet descriptorWrite = {};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = m_GlobalLightingDescriptorSet;
    descriptorWrite.dstBinding = 0;
    descriptorWrite.dstArrayElement = 0;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pBufferInfo = &bufferDescInfo;

    vkUpdateDescriptorSets(VulkanHW.m_Device, 1, &descriptorWrite, 0, nullptr);

    Msg("[Vulkan] GlobalLighting UBO created successfully");
    Msg("[Vulkan]   - Buffer: %p", m_GlobalLightingBuffer);
    Msg("[Vulkan]   - Memory: %p", m_GlobalLightingMemory);
    Msg("[Vulkan]   - Mapped: %p", m_GlobalLightingMapped);
    Msg("[Vulkan]   - Descriptor Set: %p", m_GlobalLightingDescriptorSet);
}

void CVulkanLighting::UpdateGlobalLightingUBO()
{
    if (!m_GlobalLightingMapped) {
        // UBO not created or not mapped
        return;
    }

    GlobalLightingUBO ubo = {};

    // ========================================================================
    // Get environment data
    // ========================================================================

    // Check if game persistent and environment are available
    if (g_pGamePersistent && g_pGamePersistent->Environment().CurrentEnv)
    {
        CEnvDescriptor* desc = g_pGamePersistent->Environment().CurrentEnv;

        // ====================================================================
        // Hemisphere Color (цвет неба для верхней полусферы)
        // ====================================================================
        ubo.L_hemi_color.set(
            desc->hemi_color.x,
            desc->hemi_color.y,
            desc->hemi_color.z,
            1.0f  // Alpha unused, set to 1.0
        );

        // ====================================================================
        // Ambient Color (базовый ambient)
        // ====================================================================
        ubo.L_ambient.set(
            desc->ambient.x,
            desc->ambient.y,
            desc->ambient.z,
            1.0f  // Alpha unused, set to 1.0
        );

        // ====================================================================
        // Sun Color
        // ====================================================================
        ubo.L_sun_color.set(
            desc->sun_color.x,
            desc->sun_color.y,
            desc->sun_color.z,
            1.0f  // Alpha unused, set to 1.0
        );

        // ====================================================================
        // Sun Direction (world space)
        // ====================================================================
        // NOTE: desc->sun_dir is direction FROM sun TO ground
        ubo.L_sun_dir_w.set(
            desc->sun_dir.x,
            desc->sun_dir.y,
            desc->sun_dir.z,
            0.0f  // w = 0 for direction vector
        );
    }
    else
    {
        // ====================================================================
        // Fallback to default values if environment not available
        // ====================================================================
        ubo.L_hemi_color.set(0.3f, 0.4f, 0.5f, 1.0f);  // Blue sky
        ubo.L_ambient.set(0.1f, 0.1f, 0.1f, 1.0f);     // Gray ambient
        ubo.L_sun_color.set(1.0f, 0.95f, 0.8f, 1.0f);  // Warm sunlight
        ubo.L_sun_dir_w.set(0.3f, -0.7f, 0.6f, 0.0f);  // Down and forward
    }

    // ========================================================================
    // Inverse View Matrix (eye-space -> world-space)
    // ========================================================================
    // Get current view matrix from Device
    Fmatrix viewMatrix = Device.mView;
    viewMatrix.invert(m_invV);  // Compute inverse
    ubo.m_invV = m_invV;

    // ========================================================================
    // Copy to mapped memory
    // ========================================================================
    memcpy(m_GlobalLightingMapped, &ubo, sizeof(GlobalLightingUBO));

    // Note: No need to flush if memory is HOST_COHERENT
}

void CVulkanLighting::DestroyGlobalLightingUBO()
{
    Msg("[Vulkan] Destroying GlobalLighting UBO...");

    // Unmap memory
    if (m_GlobalLightingMapped && m_GlobalLightingMemory != VK_NULL_HANDLE) {
        vkUnmapMemory(VulkanHW.m_Device, m_GlobalLightingMemory);
        m_GlobalLightingMapped = nullptr;
    }

    // Destroy buffer
    if (m_GlobalLightingBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(VulkanHW.m_Device, m_GlobalLightingBuffer, nullptr);
        m_GlobalLightingBuffer = VK_NULL_HANDLE;
    }

    // Free memory
    if (m_GlobalLightingMemory != VK_NULL_HANDLE) {
        vkFreeMemory(VulkanHW.m_Device, m_GlobalLightingMemory, nullptr);
        m_GlobalLightingMemory = VK_NULL_HANDLE;
    }

    // Destroy descriptor set layout
    if (m_GlobalLightingLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(VulkanHW.m_Device, m_GlobalLightingLayout, nullptr);
        m_GlobalLightingLayout = VK_NULL_HANDLE;
    }

    // Descriptor set automatically freed when pool is destroyed
    m_GlobalLightingDescriptorSet = VK_NULL_HANDLE;

    Msg("[Vulkan] GlobalLighting UBO destroyed");
}

} // namespace VK
