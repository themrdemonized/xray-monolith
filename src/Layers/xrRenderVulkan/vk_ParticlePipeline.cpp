// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// SPDX-License-Identifier: MIT

// ============================================================================
// vk_ParticlePipeline.cpp - Particle pipeline implementation
// ============================================================================

#include "stdafx.h"
#include "vk_ParticlePipeline.h"
#include "vk_ParticleEffect.h"
#include <array>
#include <vector>

// ============================================================================
// Destructor
// ============================================================================
vkParticlePipeline::~vkParticlePipeline()
{
    // Note: Device must be passed to Destroy() before destruction
    // Otherwise we have a resource leak
}

// ============================================================================
// Create - Main Pipeline Creation (Dynamic Rendering)
// ============================================================================
bool vkParticlePipeline::Create(
    VkDevice device,
    const ParticlePipelineConfig& config,
    VkDescriptorSetLayout descriptorSetLayout)
{
    // Load shader modules
    ShaderModules shaders;
    if (!LoadShaders(device, shaders)) {
        Msg("![Vulkan] Failed to load particle shaders");
        return false;
    }

    // Create pipeline layout
    if (!CreatePipelineLayout(device, descriptorSetLayout)) {
        Msg("![Vulkan] Failed to create particle pipeline layout");
        vkDestroyShaderModule(device, shaders.vertexModule, nullptr);
        vkDestroyShaderModule(device, shaders.fragmentModule, nullptr);
        return false;
    }

    // Create graphics pipeline (with dynamic rendering support)
    if (!CreateGraphicsPipeline(device, config, shaders)) {
        Msg("![Vulkan] Failed to create particle graphics pipeline");
        vkDestroyShaderModule(device, shaders.vertexModule, nullptr);
        vkDestroyShaderModule(device, shaders.fragmentModule, nullptr);
        return false;
    }

    // Clean up shader modules (they're copied into pipeline)
    vkDestroyShaderModule(device, shaders.vertexModule, nullptr);
    vkDestroyShaderModule(device, shaders.fragmentModule, nullptr);

    return true;
}

// ============================================================================
// Destroy - Pipeline Cleanup
// ============================================================================
void vkParticlePipeline::Destroy(VkDevice device)
{
    if (pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, pipeline, nullptr);
        pipeline = VK_NULL_HANDLE;
    }

    if (pipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
        pipelineLayout = VK_NULL_HANDLE;
    }
}

// ============================================================================
// LoadShaderModule - Load SPIR-V binary
// ============================================================================
VkShaderModule vkParticlePipeline::LoadShaderModule(
    VkDevice device,
    const char* filename)
{
    // Read binary file via VFS (supports .db archives)
    IReader* reader = FS.r_open(filename);
    if (!reader) {
        Msg("![Vulkan] Failed to open shader file: %s", filename);
        return VK_NULL_HANDLE;
    }

    size_t fileSize = (size_t)reader->length();
    std::vector<char> buffer(fileSize);
    reader->r(buffer.data(), (int)fileSize);
    FS.r_close(reader);

    // Create shader module
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = buffer.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(buffer.data());

    VkShaderModule shaderModule;
    if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        Msg("![Vulkan] Failed to create shader module from %s", filename);
        return VK_NULL_HANDLE;
    }

    return shaderModule;
}

// ============================================================================
// LoadShaders - Load both vertex and fragment shaders
// ============================================================================
bool vkParticlePipeline::LoadShaders(
    VkDevice device,
    ShaderModules& modules)
{
    // Construct proper paths using FS.update_path
    string_path vertShaderPath, fragShaderPath;

    FS.update_path(vertShaderPath, "$game_shaders$", "vulkan\\particle.vert.spv");
    FS.update_path(fragShaderPath, "$game_shaders$", "vulkan\\particle.frag.spv");

    modules.vertexModule = LoadShaderModule(device, vertShaderPath);
    if (modules.vertexModule == VK_NULL_HANDLE) {
        Msg("![Vulkan] Failed to load vertex shader: %s", vertShaderPath);
        return false;
    }

    modules.fragmentModule = LoadShaderModule(device, fragShaderPath);
    if (modules.fragmentModule == VK_NULL_HANDLE) {
        vkDestroyShaderModule(device, modules.vertexModule, nullptr);
        Msg("![Vulkan] Failed to load fragment shader: %s", fragShaderPath);
        return false;
    }

    Msg("[Vulkan] Particle shaders loaded successfully");
    return true;
}

// ============================================================================
// CreatePipelineLayout - Create VkPipelineLayout
// ============================================================================
bool vkParticlePipeline::CreatePipelineLayout(
    VkDevice device,
    VkDescriptorSetLayout descriptorSetLayout)
{
    // Push constants for view-projection matrix
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(Fmatrix); // 64 bytes

    // Pipeline layout with descriptor set and push constants
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
        Msg("![Vulkan] Failed to create particle pipeline layout");
        return false;
    }

    return true;
}

// ============================================================================
// CreateGraphicsPipeline - Create graphics pipeline (with dynamic rendering)
// ============================================================================
bool vkParticlePipeline::CreateGraphicsPipeline(
    VkDevice device,
    const ParticlePipelineConfig& config,
    const ShaderModules& shaders)
{
    // Shader stages
    VkPipelineShaderStageCreateInfo shaderStages[2];

    shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[0].pNext = nullptr;
    shaderStages[0].flags = 0;
    shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    shaderStages[0].module = shaders.vertexModule;
    shaderStages[0].pName = "main";
    shaderStages[0].pSpecializationInfo = nullptr;

    shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[1].pNext = nullptr;
    shaderStages[1].flags = 0;
    shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    shaderStages[1].module = shaders.fragmentModule;
    shaderStages[1].pName = "main";
    shaderStages[1].pSpecializationInfo = nullptr;

    // Vertex input
    auto bindingDescription = VkParticleVertex::GetBindingDescription();
    auto attributeDescriptions = VkParticleVertex::GetAttributeDescriptions();

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

    // Input assembly
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    // Viewport and scissor (dynamic)
    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = nullptr;
    viewportState.scissorCount = 1;
    viewportState.pScissors = nullptr;

    // Dynamic state
    VkDynamicState dynamicStates[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    // Rasterizer
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = config.wireframe ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = config.cullMode;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;
    rasterizer.lineWidth = 1.0f;

    // Multisampling (MSAA)
    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // Depth stencil
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = config.depthTest ? VK_TRUE : VK_FALSE;
    depthStencil.depthWriteEnable = config.depthWrite ? VK_TRUE : VK_FALSE;
    depthStencil.depthCompareOp = config.depthOp;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    // Color blending
    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_TRUE;
    colorBlendAttachment.srcColorBlendFactor = config.srcBlend;
    colorBlendAttachment.dstColorBlendFactor = config.dstBlend;
    colorBlendAttachment.colorBlendOp = config.blendOp;
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    // ========================================================================
    // Dynamic Rendering Support (VK_KHR_dynamic_rendering)
    // ========================================================================
    VkPipelineRenderingCreateInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachmentFormats = &config.colorFormat;
    renderingInfo.depthAttachmentFormat = config.depthFormat;
    renderingInfo.stencilAttachmentFormat = VK_FORMAT_UNDEFINED;

    // Create graphics pipeline
    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.pNext = &renderingInfo;  // Dynamic rendering
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
    pipelineInfo.layout = pipelineLayout;
    pipelineInfo.renderPass = VK_NULL_HANDLE;  // Not used with dynamic rendering
    pipelineInfo.subpass = 0;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline) != VK_SUCCESS) {
        Msg("![Vulkan] Failed to create particle graphics pipeline");
        return false;
    }

    return true;
}

// ============================================================================
// Bind - Bind pipeline to command buffer
// ============================================================================
void vkParticlePipeline::Bind(VkCommandBuffer commandBuffer) const
{
    if (pipeline != VK_NULL_HANDLE) {
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    }
}
