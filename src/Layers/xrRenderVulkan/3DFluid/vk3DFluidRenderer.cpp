// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// SPDX-License-Identifier: MIT

#include "stdafx.h"
#include "vk3DFluidRenderer.h"
#include "vk3DFluidData.h"
#include "vk3DFluidGrid.h"
#include "../HW_Vulkan.h"

namespace VK
{

vk3DFluidRenderer::vk3DFluidRenderer()
{
    for (int i = 0; i < RRT_NumRT; i++) {
        m_RTs[i] = nullptr;
    }
}

vk3DFluidRenderer::~vk3DFluidRenderer()
{
    Destroy();
}

void vk3DFluidRenderer::Initialize()
{
    if (m_bInitialized) {
        Msg("~[Vulkan] Fluid Renderer already initialized");
        return;
    }

    Msg("[Vulkan] Initializing 3D Fluid Renderer...");

    // Create grid geometry (для bbox и fullscreen quad)
    m_Grid = new vk3DFluidGrid();
    m_Grid->Create(128);  // Default grid size

    CreateRenderTargets();
    CreateRenderPasses();
    CreateFramebuffers();
    CreatePipelines();
    CreateGraphicsPipelines();

    // Create descriptor pool для graphics pipelines
    VkDescriptorPoolSize poolSizes[1] = {};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[0].descriptorCount = 3 * 32;  // 3 samplers × 32 sets

    VkDescriptorPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = poolSizes;
    poolInfo.maxSets = 32;

    VK_CHECK(vkCreateDescriptorPool(VulkanHW.m_Device, &poolInfo, nullptr, &m_DescriptorPool));

    // Create samplers
    VkSamplerCreateInfo samplerInfo = {};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 1.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

    // 3D sampler (для volume textures)
    VK_CHECK(vkCreateSampler(VulkanHW.m_Device, &samplerInfo, nullptr, &m_Sampler3D));

    // 2D sampler (для ray data texture)
    VK_CHECK(vkCreateSampler(VulkanHW.m_Device, &samplerInfo, nullptr, &m_Sampler2D));

    m_bInitialized = true;
    Msg("[Vulkan] 3D Fluid Renderer initialized");
}

void vk3DFluidRenderer::SetScreenSize(u32 width, u32 height)
{
    if (m_ScreenWidth == width && m_ScreenHeight == height) {
        return;
    }

    m_ScreenWidth = width;
    m_ScreenHeight = height;

    // Recreate framebuffers и RTs with new size
    DestroyFramebuffers();
    DestroyRenderTargets();
    CreateRenderTargets();
    CreateFramebuffers();

    Msg("[Vulkan] Fluid Renderer screen size updated: %dx%d", width, height);
}

void vk3DFluidRenderer::CreateRenderTargets()
{
    // RayDataTex - full resolution (stores ray entry/exit points)
    m_RTs[RRT_RayDataTex] = new CRT();
    m_RTs[RRT_RayDataTex]->Create(
        VK_FORMAT_R32G32B32A32_SFLOAT,
        m_ScreenWidth, m_ScreenHeight,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
    );

    // RayDataTexSmall - downsampled (quarter res for optimization)
    m_RTs[RRT_RayDataTexSmall] = new CRT();
    m_RTs[RRT_RayDataTexSmall]->Create(
        VK_FORMAT_R32G32B32A32_SFLOAT,
        m_ScreenWidth / 2, m_ScreenHeight / 2,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
    );

    // RayCastTex - raycast result
    m_RTs[RRT_RayCastTex] = new CRT();
    m_RTs[RRT_RayCastTex]->Create(
        VK_FORMAT_R16G16B16A16_SFLOAT,
        m_ScreenWidth, m_ScreenHeight,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
    );

    // EdgeTex - edge detection mask
    m_RTs[RRT_EdgeTex] = new CRT();
    m_RTs[RRT_EdgeTex]->Create(
        VK_FORMAT_R32_SFLOAT,
        m_ScreenWidth, m_ScreenHeight,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
    );

    Msg("[Vulkan] Fluid Renderer RTs created: %dx%d", m_ScreenWidth, m_ScreenHeight);
}

void vk3DFluidRenderer::CreateRenderPasses()
{
    // Render pass #1: RayData (ray entry/exit points)
    // Attachment: RayDataTex (R32G32B32A32_SFLOAT)
    {
        VkAttachmentDescription attachment = {};
        attachment.format = VK_FORMAT_R32G32B32A32_SFLOAT;
        attachment.samples = VK_SAMPLE_COUNT_1_BIT;
        attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkAttachmentReference colorRef = {};
        colorRef.attachment = 0;
        colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass = {};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorRef;

        VkRenderPassCreateInfo renderPassInfo = {};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassInfo.attachmentCount = 1;
        renderPassInfo.pAttachments = &attachment;
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpass;

        VK_CHECK(vkCreateRenderPass(VulkanHW.m_Device, &renderPassInfo, nullptr, &m_RenderPass_RayData));
    }

    // Render pass #2: Raycast (volumetric raymarch result)
    // Attachment: RayCastTex (R16G16B16A16_SFLOAT)
    {
        VkAttachmentDescription attachment = {};
        attachment.format = VK_FORMAT_R16G16B16A16_SFLOAT;
        attachment.samples = VK_SAMPLE_COUNT_1_BIT;
        attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkAttachmentReference colorRef = {};
        colorRef.attachment = 0;
        colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass = {};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorRef;

        VkRenderPassCreateInfo renderPassInfo = {};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassInfo.attachmentCount = 1;
        renderPassInfo.pAttachments = &attachment;
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpass;

        VK_CHECK(vkCreateRenderPass(VulkanHW.m_Device, &renderPassInfo, nullptr, &m_RenderPass_Raycast));
    }

    Msg("[Vulkan] Fluid render passes created");
}

void vk3DFluidRenderer::CreateFramebuffers()
{
    // Framebuffer для RayData render pass
    {
        VkImageView attachments[1] = { m_RTs[RRT_RayDataTex]->GetImageView() };

        VkFramebufferCreateInfo fbInfo = {};
        fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbInfo.renderPass = m_RenderPass_RayData;
        fbInfo.attachmentCount = 1;
        fbInfo.pAttachments = attachments;
        fbInfo.width = m_ScreenWidth;
        fbInfo.height = m_ScreenHeight;
        fbInfo.layers = 1;

        VK_CHECK(vkCreateFramebuffer(VulkanHW.m_Device, &fbInfo, nullptr, &m_Framebuffer_RayData));
    }

    // Framebuffer для Raycast render pass
    {
        VkImageView attachments[1] = { m_RTs[RRT_RayCastTex]->GetImageView() };

        VkFramebufferCreateInfo fbInfo = {};
        fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbInfo.renderPass = m_RenderPass_Raycast;
        fbInfo.attachmentCount = 1;
        fbInfo.pAttachments = attachments;
        fbInfo.width = m_ScreenWidth;
        fbInfo.height = m_ScreenHeight;
        fbInfo.layers = 1;

        VK_CHECK(vkCreateFramebuffer(VulkanHW.m_Device, &fbInfo, nullptr, &m_Framebuffer_Raycast));
    }

    Msg("[Vulkan] Fluid framebuffers created: %dx%d", m_ScreenWidth, m_ScreenHeight);
}

VkShaderModule vk3DFluidRenderer::LoadShaderModule(const char* filename)
{
    IReader* F = FS.r_open(filename);
    if (!F) {
        Msg("![Vulkan] Failed to open shader: %s", filename);
        return VK_NULL_HANDLE;
    }

    u32 size = F->length();
    if (size == 0 || size % 4 != 0) {
        Msg("![Vulkan] Invalid SPIR-V size: %s", filename);
        FS.r_close(F);
        return VK_NULL_HANDLE;
    }

    xr_vector<u32> code(size / 4);
    F->r(code.data(), size);
    FS.r_close(F);

    VkShaderModuleCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = size;
    createInfo.pCode = code.data();

    VkShaderModule module = VK_NULL_HANDLE;
    VK_CHECK(vkCreateShaderModule(VulkanHW.m_Device, &createInfo, nullptr, &module));

    return module;
}

void vk3DFluidRenderer::CreatePipelines()
{
    const char* shaderPath = "$game_shaders$\\vulkan\\fluid\\";

    // Загружаем shader modules
    string256 buf;
    xr_sprintf(buf, sizeof(buf), "%sraydata_back.vert.spv", shaderPath);
    m_Shader_RayDataBack_Vert = LoadShaderModule(buf);
    xr_sprintf(buf, sizeof(buf), "%sraydata_back.frag.spv", shaderPath);
    m_Shader_RayDataBack_Frag = LoadShaderModule(buf);
    xr_sprintf(buf, sizeof(buf), "%sraydata_front.vert.spv", shaderPath);
    m_Shader_RayDataFront_Vert = LoadShaderModule(buf);
    xr_sprintf(buf, sizeof(buf), "%sraydata_front.frag.spv", shaderPath);
    m_Shader_RayDataFront_Frag = LoadShaderModule(buf);
    xr_sprintf(buf, sizeof(buf), "%sfullscreen.vert.spv", shaderPath);
    m_Shader_Fullscreen_Vert = LoadShaderModule(buf);
    xr_sprintf(buf, sizeof(buf), "%sraycast_fog.frag.spv", shaderPath);
    m_Shader_RaycastFog_Frag = LoadShaderModule(buf);

    if (!m_Shader_RayDataBack_Vert || !m_Shader_RayDataBack_Frag ||
        !m_Shader_RayDataFront_Vert || !m_Shader_RayDataFront_Frag ||
        !m_Shader_Fullscreen_Vert || !m_Shader_RaycastFog_Frag)
    {
        Msg("![Vulkan] Failed to load fluid graphics shaders");
        return;
    }

    // Create descriptor set layout для graphics
    // Raycast shader нужно: sampler2D (rayData), sampler3D (density, velocity)
    VkDescriptorSetLayoutBinding bindings[3] = {};

    // Binding 0: sampler2D (ray data texture)
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    // Binding 1: sampler3D (density texture)
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    // Binding 2: sampler3D (velocity texture, optional)
    bindings[2].binding = 2;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo = {};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 3;
    layoutInfo.pBindings = bindings;

    VK_CHECK(vkCreateDescriptorSetLayout(VulkanHW.m_Device, &layoutInfo, nullptr, &m_DescriptorSetLayout));

    // Push constants для raycast parameters
    VkPushConstantRange pushRange = {};
    pushRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_VERTEX_BIT;
    pushRange.offset = 0;
    pushRange.size = 128;  // mat4 + vec4 параметры

    // Pipeline layout
    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &m_DescriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushRange;

    VK_CHECK(vkCreatePipelineLayout(VulkanHW.m_Device, &pipelineLayoutInfo, nullptr, &m_PipelineLayout));

    Msg("[Vulkan] Fluid Renderer pipeline layout created");
}

void vk3DFluidRenderer::CreateGraphicsPipelines()
{
    if (m_RenderPass_RayData == VK_NULL_HANDLE || m_RenderPass_Raycast == VK_NULL_HANDLE) {
        Msg("![Vulkan] Cannot create graphics pipelines without render passes");
        return;
    }

    // Vertex input state (для volume bbox и fullscreen quad)
    // FluidVertex: vec3 position, vec3 texCoord
    VkVertexInputBindingDescription binding = {};
    binding.binding = 0;
    binding.stride = sizeof(float) * 6;  // vec3 + vec3
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attrs[2] = {};
    // Position (location = 0)
    attrs[0].binding = 0;
    attrs[0].location = 0;
    attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attrs[0].offset = 0;
    // TexCoord (location = 1)
    attrs[1].binding = 0;
    attrs[1].location = 1;
    attrs[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attrs[1].offset = sizeof(float) * 3;

    VkPipelineVertexInputStateCreateInfo vertexInputInfo = {};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &binding;
    vertexInputInfo.vertexAttributeDescriptionCount = 2;
    vertexInputInfo.pVertexAttributeDescriptions = attrs;

    // Input assembly
    VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    // Viewport и scissor (dynamic)
    VkViewport viewport = {};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (float)m_ScreenWidth;
    viewport.height = (float)m_ScreenHeight;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor = {};
    scissor.offset = {0, 0};
    scissor.extent = {m_ScreenWidth, m_ScreenHeight};

    VkPipelineViewportStateCreateInfo viewportState = {};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    // Rasterization state
    VkPipelineRasterizationStateCreateInfo rasterizer = {};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;  // Will set per-pipeline
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    // Multisample (disabled)
    VkPipelineMultisampleStateCreateInfo multisampling = {};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // Color blend (no blending for raydata, alpha blend для raycast)
    VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending = {};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    // Depth stencil (disabled for raydata, используем для raycast)
    VkPipelineDepthStencilStateCreateInfo depthStencil = {};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_FALSE;
    depthStencil.depthWriteEnable = VK_FALSE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

    // Pipeline #1: RayData Back (render back faces)
    {
        VkPipelineShaderStageCreateInfo shaderStages[2] = {};
        shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        shaderStages[0].module = m_Shader_RayDataBack_Vert;
        shaderStages[0].pName = "main";
        shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        shaderStages[1].module = m_Shader_RayDataBack_Frag;
        shaderStages[1].pName = "main";

        rasterizer.cullMode = VK_CULL_MODE_FRONT_BIT;  // Render back faces

        VkGraphicsPipelineCreateInfo pipelineInfo = {};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.stageCount = 2;
        pipelineInfo.pStages = shaderStages;
        pipelineInfo.pVertexInputState = &vertexInputInfo;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.layout = m_PipelineLayout;
        pipelineInfo.renderPass = m_RenderPass_RayData;
        pipelineInfo.subpass = 0;

        VK_CHECK(vkCreateGraphicsPipelines(VulkanHW.m_Device, VK_NULL_HANDLE, 1,
                                           &pipelineInfo, nullptr, &m_Pipeline_RayDataBack));
    }

    // Pipeline #2: RayData Front (render front faces)
    {
        VkPipelineShaderStageCreateInfo shaderStages[2] = {};
        shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        shaderStages[0].module = m_Shader_RayDataFront_Vert;
        shaderStages[0].pName = "main";
        shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        shaderStages[1].module = m_Shader_RayDataFront_Frag;
        shaderStages[1].pName = "main";

        rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;  // Render front faces

        VkGraphicsPipelineCreateInfo pipelineInfo = {};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.stageCount = 2;
        pipelineInfo.pStages = shaderStages;
        pipelineInfo.pVertexInputState = &vertexInputInfo;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.layout = m_PipelineLayout;
        pipelineInfo.renderPass = m_RenderPass_RayData;
        pipelineInfo.subpass = 0;

        VK_CHECK(vkCreateGraphicsPipelines(VulkanHW.m_Device, VK_NULL_HANDLE, 1,
                                           &pipelineInfo, nullptr, &m_Pipeline_RayDataFront));
    }

    // Pipeline #3: Raycast (fullscreen quad raymarch)
    {
        VkPipelineShaderStageCreateInfo shaderStages[2] = {};
        shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        shaderStages[0].module = m_Shader_Fullscreen_Vert;
        shaderStages[0].pName = "main";
        shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        shaderStages[1].module = m_Shader_RaycastFog_Frag;
        shaderStages[1].pName = "main";

        rasterizer.cullMode = VK_CULL_MODE_NONE;  // Fullscreen quad

        // Enable alpha blending для raycast (прозрачный дым)
        colorBlendAttachment.blendEnable = VK_TRUE;
        colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
        colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

        VkGraphicsPipelineCreateInfo pipelineInfo = {};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.stageCount = 2;
        pipelineInfo.pStages = shaderStages;
        pipelineInfo.pVertexInputState = &vertexInputInfo;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.layout = m_PipelineLayout;
        pipelineInfo.renderPass = m_RenderPass_Raycast;
        pipelineInfo.subpass = 0;

        VK_CHECK(vkCreateGraphicsPipelines(VulkanHW.m_Device, VK_NULL_HANDLE, 1,
                                           &pipelineInfo, nullptr, &m_Pipeline_Raycast));
    }

    Msg("[Vulkan] Fluid graphics pipelines created (3 pipelines)");
}

void vk3DFluidRenderer::RenderVolume(VkCommandBuffer cmd, vk3DFluidData* volume)
{
    if (!m_bInitialized || !volume) {
        return;
    }

    // Step 1: Compute ray entry/exit points
    ComputeRayData(cmd, volume);

    // Step 2: Raymarch through volume
    Raycast(cmd, volume);

    // TODO: Step 3: Composite to screen
}

void vk3DFluidRenderer::ComputeRayData(VkCommandBuffer cmd, vk3DFluidData* volume)
{
    if (!m_Pipeline_RayDataBack || !m_Pipeline_RayDataFront || !m_Grid) {
        return;
    }

    // Begin render pass для RayDataTex
    VkRenderPassBeginInfo renderPassInfo = {};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = m_RenderPass_RayData;
    renderPassInfo.framebuffer = m_Framebuffer_RayData;
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = {m_ScreenWidth, m_ScreenHeight};

    VkClearValue clearValue = {};
    clearValue.color = {{0.0f, 0.0f, 0.0f, 0.0f}};
    renderPassInfo.clearValueCount = 1;
    renderPassInfo.pClearValues = &clearValue;

    vkCmdBeginRenderPass(cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    // Bind viewport и scissor
    VkViewport viewport = {};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (float)m_ScreenWidth;
    viewport.height = (float)m_ScreenHeight;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor = {};
    scissor.offset = {0, 0};
    scissor.extent = {m_ScreenWidth, m_ScreenHeight};
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    // Push constants: MVP matrix (64 bytes = mat4)
    // Calculate MVP = projection * view * world
    const Fmatrix& worldTransform = volume->GetTransform();

    Fmatrix viewProj;
    viewProj.mul(Device.mProject, Device.mView);

    Fmatrix mvp;
    mvp.mul(viewProj, worldTransform);

    vkCmdPushConstants(cmd, m_PipelineLayout,
                       VK_SHADER_STAGE_VERTEX_BIT,
                       0, sizeof(Fmatrix), &mvp);

    // Pass 1: Render back faces (ray entry points)
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_Pipeline_RayDataBack);
    m_Grid->RenderBoundingBox(cmd);

    // Pass 2: Render front faces (ray exit points)
    // Note: DX10 использует separate pass, но мы можем использовать тот же render pass
    // т.к. front faces перезапишут pixels где нет back faces (depth test off)
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_Pipeline_RayDataFront);
    m_Grid->RenderBoundingBox(cmd);

    vkCmdEndRenderPass(cmd);

    // Ray entry/exit points сохраняются в m_RTs[RRT_RayDataTex]
}

void vk3DFluidRenderer::Raycast(VkCommandBuffer cmd, vk3DFluidData* volume)
{
    if (!m_Pipeline_Raycast || !m_Grid) {
        return;
    }

    // Begin render pass для RayCastTex
    VkRenderPassBeginInfo renderPassInfo = {};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = m_RenderPass_Raycast;
    renderPassInfo.framebuffer = m_Framebuffer_Raycast;
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = {m_ScreenWidth, m_ScreenHeight};

    VkClearValue clearValue = {};
    clearValue.color = {{0.0f, 0.0f, 0.0f, 0.0f}};
    renderPassInfo.clearValueCount = 1;
    renderPassInfo.pClearValues = &clearValue;

    vkCmdBeginRenderPass(cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    // Bind viewport и scissor
    VkViewport viewport = {};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (float)m_ScreenWidth;
    viewport.height = (float)m_ScreenHeight;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor = {};
    scissor.offset = {0, 0};
    scissor.extent = {m_ScreenWidth, m_ScreenHeight};
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    // Bind raycast pipeline
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_Pipeline_Raycast);

    // Create/update descriptor set для этого volume
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    VkDescriptorSetAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_DescriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &m_DescriptorSetLayout;

    VK_CHECK(vkAllocateDescriptorSets(VulkanHW.m_Device, &allocInfo, &descriptorSet));

    // Bind textures: binding 0 = ray data, binding 1 = density, binding 2 = velocity (optional)
    VkDescriptorImageInfo imageInfos[3] = {};

    // Binding 0: RayDataTex (ray entry/exit points) - 2D texture
    imageInfos[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfos[0].imageView = m_RTs[RRT_RayDataTex]->GetImageView();
    imageInfos[0].sampler = m_Sampler2D;

    // Binding 1: Color/Density texture - 3D texture
    imageInfos[1].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    imageInfos[1].imageView = volume->GetColorRT()->GetImageView();
    imageInfos[1].sampler = m_Sampler3D;

    // Binding 2: Velocity texture (optional, для motion blur) - 3D texture
    imageInfos[2].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    imageInfos[2].imageView = volume->GetVelocityRT()->GetImageView();
    imageInfos[2].sampler = m_Sampler3D;

    VkWriteDescriptorSet writes[3] = {};
    for (u32 i = 0; i < 3; i++) {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = descriptorSet;
        writes[i].dstBinding = i;
        writes[i].dstArrayElement = 0;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[i].descriptorCount = 1;
        writes[i].pImageInfo = &imageInfos[i];
    }

    vkUpdateDescriptorSets(VulkanHW.m_Device, 3, writes, 0, nullptr);

    // Bind descriptor set
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            m_PipelineLayout, 0, 1, &descriptorSet, 0, nullptr);

    // Push constants: eye position + grid params (16 floats = 64 bytes)
    struct RaycastParams {
        Fvector eyePosition;    // 12 bytes
        float   padding1;       // 4 bytes (alignment)
        Fvector gridSize;       // 12 bytes (x,y,z dimensions)
        float   stepSize;       // 4 bytes
        Fvector invTransform[3]; // 36 bytes (simplified inv transform)
        float   density;        // 4 bytes
    };

    RaycastParams params = {};
    params.eyePosition = Device.vCameraPosition;
    params.gridSize.set((float)volume->GetGridSize(), (float)volume->GetGridSize(), (float)volume->GetGridSize());
    params.stepSize = 1.0f / (float)volume->GetGridSize();
    params.density = 1.0f;

    // Simplified inverse transform (для ray direction transform)
    const Fmatrix& transform = volume->GetTransform();
    Fmatrix invTransform;
    invTransform.invert(transform);
    params.invTransform[0].set(invTransform.i.x, invTransform.i.y, invTransform.i.z);
    params.invTransform[1].set(invTransform.j.x, invTransform.j.y, invTransform.j.z);
    params.invTransform[2].set(invTransform.k.x, invTransform.k.y, invTransform.k.z);

    vkCmdPushConstants(cmd, m_PipelineLayout,
                       VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(RaycastParams), &params);

    // Draw fullscreen quad (raymarching)
    m_Grid->RenderFullscreen(cmd);

    vkCmdEndRenderPass(cmd);

    // Result записывается в m_RTs[RRT_RayCastTex]
}

void vk3DFluidRenderer::DestroyRenderTargets()
{
    for (int i = 0; i < RRT_NumRT; i++) {
        if (m_RTs[i]) {
            m_RTs[i]->Destroy();
            delete m_RTs[i];
            m_RTs[i] = nullptr;
        }
    }
}

void vk3DFluidRenderer::DestroyPipelines()
{
    // Destroy shader modules
    if (m_Shader_RayDataBack_Vert != VK_NULL_HANDLE) {
        vkDestroyShaderModule(VulkanHW.m_Device, m_Shader_RayDataBack_Vert, nullptr);
        m_Shader_RayDataBack_Vert = VK_NULL_HANDLE;
    }
    if (m_Shader_RayDataBack_Frag != VK_NULL_HANDLE) {
        vkDestroyShaderModule(VulkanHW.m_Device, m_Shader_RayDataBack_Frag, nullptr);
        m_Shader_RayDataBack_Frag = VK_NULL_HANDLE;
    }
    if (m_Shader_RayDataFront_Vert != VK_NULL_HANDLE) {
        vkDestroyShaderModule(VulkanHW.m_Device, m_Shader_RayDataFront_Vert, nullptr);
        m_Shader_RayDataFront_Vert = VK_NULL_HANDLE;
    }
    if (m_Shader_RayDataFront_Frag != VK_NULL_HANDLE) {
        vkDestroyShaderModule(VulkanHW.m_Device, m_Shader_RayDataFront_Frag, nullptr);
        m_Shader_RayDataFront_Frag = VK_NULL_HANDLE;
    }
    if (m_Shader_Fullscreen_Vert != VK_NULL_HANDLE) {
        vkDestroyShaderModule(VulkanHW.m_Device, m_Shader_Fullscreen_Vert, nullptr);
        m_Shader_Fullscreen_Vert = VK_NULL_HANDLE;
    }
    if (m_Shader_RaycastFog_Frag != VK_NULL_HANDLE) {
        vkDestroyShaderModule(VulkanHW.m_Device, m_Shader_RaycastFog_Frag, nullptr);
        m_Shader_RaycastFog_Frag = VK_NULL_HANDLE;
    }

    // Destroy descriptor set layout
    if (m_DescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(VulkanHW.m_Device, m_DescriptorSetLayout, nullptr);
        m_DescriptorSetLayout = VK_NULL_HANDLE;
    }

    // Destroy pipeline layout
    if (m_PipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(VulkanHW.m_Device, m_PipelineLayout, nullptr);
        m_PipelineLayout = VK_NULL_HANDLE;
    }

    // Destroy pipelines
    if (m_Pipeline_RayDataBack != VK_NULL_HANDLE) {
        vkDestroyPipeline(VulkanHW.m_Device, m_Pipeline_RayDataBack, nullptr);
        m_Pipeline_RayDataBack = VK_NULL_HANDLE;
    }

    if (m_Pipeline_RayDataFront != VK_NULL_HANDLE) {
        vkDestroyPipeline(VulkanHW.m_Device, m_Pipeline_RayDataFront, nullptr);
        m_Pipeline_RayDataFront = VK_NULL_HANDLE;
    }

    if (m_Pipeline_Raycast != VK_NULL_HANDLE) {
        vkDestroyPipeline(VulkanHW.m_Device, m_Pipeline_Raycast, nullptr);
        m_Pipeline_Raycast = VK_NULL_HANDLE;
    }
}

void vk3DFluidRenderer::Destroy()
{
    if (!m_bInitialized) {
        return;
    }

    if (m_Sampler3D != VK_NULL_HANDLE) {
        vkDestroySampler(VulkanHW.m_Device, m_Sampler3D, nullptr);
        m_Sampler3D = VK_NULL_HANDLE;
    }

    if (m_Sampler2D != VK_NULL_HANDLE) {
        vkDestroySampler(VulkanHW.m_Device, m_Sampler2D, nullptr);
        m_Sampler2D = VK_NULL_HANDLE;
    }

    if (m_DescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(VulkanHW.m_Device, m_DescriptorPool, nullptr);
        m_DescriptorPool = VK_NULL_HANDLE;
    }

    if (m_Grid) {
        m_Grid->Destroy();
        delete m_Grid;
        m_Grid = nullptr;
    }

    DestroyFramebuffers();
    DestroyRenderTargets();
    DestroyPipelines();
    DestroyRenderPasses();

    m_bInitialized = false;
    Msg("[Vulkan] 3D Fluid Renderer destroyed");
}

void vk3DFluidRenderer::DestroyRenderPasses()
{
    if (m_RenderPass_RayData != VK_NULL_HANDLE) {
        vkDestroyRenderPass(VulkanHW.m_Device, m_RenderPass_RayData, nullptr);
        m_RenderPass_RayData = VK_NULL_HANDLE;
    }

    if (m_RenderPass_Raycast != VK_NULL_HANDLE) {
        vkDestroyRenderPass(VulkanHW.m_Device, m_RenderPass_Raycast, nullptr);
        m_RenderPass_Raycast = VK_NULL_HANDLE;
    }
}

void vk3DFluidRenderer::DestroyFramebuffers()
{
    if (m_Framebuffer_RayData != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(VulkanHW.m_Device, m_Framebuffer_RayData, nullptr);
        m_Framebuffer_RayData = VK_NULL_HANDLE;
    }

    if (m_Framebuffer_Raycast != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(VulkanHW.m_Device, m_Framebuffer_Raycast, nullptr);
        m_Framebuffer_Raycast = VK_NULL_HANDLE;
    }
}

} // namespace VK
