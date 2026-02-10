// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// Licensed under the same terms as X-Ray Engine (see root License.txt)

#include "stdafx.h"
#include "vk_lighting.h"
#include "vk_rendertarget.h"
#include "rvk.h"  // CRender for m_Jitter access
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

    // 2. Create descriptor pool FIRST (UBOs need it for descriptor set allocation)
    VkDescriptorPoolSize poolSizes[] = {
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 16 },  // G-Buffer textures
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 4 }             // GlobalLighting + Material UBOs
    };

    VkDescriptorPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = 8;  // G-Buffer + GlobalLighting + Material + future sets
    poolInfo.poolSizeCount = 2;
    poolInfo.pPoolSizes = poolSizes;

    VkResult result = vkCreateDescriptorPool(VulkanHW.m_Device, &poolInfo, nullptr, &m_DescriptorPool);
    if (result != VK_SUCCESS) {
        Msg("![Vulkan] Failed to create lighting descriptor pool: %d", result);
        return;
    }
    Msg("[Vulkan] Descriptor pool created");

    // 3. Create GlobalLighting UBO (Phase: Hemisphere Lighting)
    CreateGlobalLightingUBO();

    // 3b. Create MaterialConstants UBO
    CreateMaterialConstantsUBO();

    // 3c. Build constant name → offset mapping
    BuildConstantMap();

    // 4. Create G-Buffer descriptor set layout
    CreateGBufferDescriptorSetLayout();

    // 5. Load shaders for accum_direct_simple
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

    // Destroy MaterialConstants UBO
    DestroyMaterialConstantsUBO();

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
    // 0. Update UBOs
    // ========================================================================
    UpdateGlobalLightingUBO();
    UpdateMaterialConstantsUBO();

    // ========================================================================
    // 1. Bind pipeline
    // ========================================================================

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_AccumDirectSimple_Pipeline);

    // ========================================================================
    // 2. Bind descriptor sets (all 5 sets in one call for efficiency)
    // ========================================================================

    VkDescriptorSet sets[5] = {
        m_GlobalLightingDescriptorSet,      // Set 0: GlobalLighting UBO
        m_GBufferDescriptorSet,             // Set 1: G-Buffer textures
        VK_NULL_HANDLE,                     // Set 2: Per-object (not used in lighting)
        VK_NULL_HANDLE,                     // Set 3: Lighting data (not used yet)
        m_MaterialConstantsDescriptorSet    // Set 4: MaterialConstants UBO
    };

    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            m_AccumDirectSimple_PipelineLayout,
                            0,  // firstSet
                            5,  // descriptorSetCount
                            sets,
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
    uboBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
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
        CEnvDescriptorMixer* desc = g_pGamePersistent->Environment().CurrentEnv;

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
    // NEW FIELDS - Critical constants
    // ========================================================================

    if (g_pGamePersistent && g_pGamePersistent->Environment().CurrentEnv)
    {
        CEnvDescriptorMixer* desc = g_pGamePersistent->Environment().CurrentEnv;

        // Sun direction (eye space)
        Fvector sun_dir_e;
        Device.mView.transform_dir(sun_dir_e, desc->sun_dir);
        sun_dir_e.normalize();
        ubo.L_sun_dir_e.set(sun_dir_e.x, sun_dir_e.y, sun_dir_e.z, 0.0f);

        // Fog plane equation (from Blender_Recorder_StandartBinding.cpp:144-213)
        {
            Fmatrix& M = Device.mFullTransform;
            float plane_x = -(M._14 + M._13);
            float plane_y = -(M._24 + M._23);
            float plane_z = -(M._34 + M._33);
            float plane_w = -(M._44 + M._43);
            float denom = -1.0f / _sqrt(_sqr(plane_x) + _sqr(plane_y) + _sqr(plane_z));
            ubo.fog_plane.set(plane_x * denom, plane_y * denom, plane_z * denom, plane_w * denom);

            // Fog params
            float fog_near = desc->fog_near;
            float fog_far = desc->fog_far;
            float fog_range = (fog_far > fog_near) ? (1.0f / (fog_far - fog_near)) : 0.0f;
            ubo.fog_params.set(-fog_near * fog_range, fog_near, fog_far, fog_range);

            // Fog color
            ubo.fog_color.set(desc->fog_color.x, desc->fog_color.y, desc->fog_color.z, desc->fog_density);
        }

        // Wind (direction is angle in radians, convert to 2D vector)
        float wind_angle = desc->wind_direction;
        ubo.wind_params.set(_cos(wind_angle), _sin(wind_angle), desc->wind_velocity, 0.0f);

        // Rain
        float rain_density = desc->rain_density;
        float rain_wetness = g_pGamePersistent->Environment().wetness_factor;
        ubo.rain_params.set(rain_density, rain_wetness, 0.0f, 0.0f);

        // Sky
        ubo.sky_color.set(desc->sky_color.x, desc->sky_color.y, desc->sky_color.z, desc->sky_rotation);

        // Near/far planes
        ubo.near_far_plane.set(VIEWPORT_NEAR, desc->far_plane, 0.0f, 0.0f);

        // Screen params
        float fov = Device.fFOV;
        float aspect = Device.fASPECT;
        ubo.ogse_c_screen.set(fov, aspect, tan(deg2rad(fov) / 2.0f), desc->far_plane * 0.75f);
    }
    else
    {
        // Fallback values
        ubo.L_sun_dir_e.set(0.3f, -0.7f, 0.6f, 0.0f);
        ubo.fog_plane.set(0, 1, 0, 0);
        ubo.fog_params.set(0, 0, 1000, 0.001f);
        ubo.fog_color.set(0.5f, 0.5f, 0.5f, 0.01f);
        ubo.wind_params.set(0, 0, 0, 0);
        ubo.rain_params.set(0, 0, 0, 0);
        ubo.sky_color.set(0.5f, 0.6f, 0.8f, 0);
        ubo.near_far_plane.set(0.1f, 1000.0f, 0, 0);
        ubo.ogse_c_screen.set(67.5f, 1.33f, 0.7f, 750.0f);
    }

    // Eye/Camera (always from Device)
    ubo.eye_position.set(Device.vCameraPosition.x, Device.vCameraPosition.y, Device.vCameraPosition.z, 1.0f);
    ubo.eye_direction.set(Device.vCameraDirection.x, Device.vCameraDirection.y, Device.vCameraDirection.z, 0.0f);
    ubo.eye_normal.set(Device.vCameraTop.x, Device.vCameraTop.y, Device.vCameraTop.z, 0.0f);

    // Time
    float t = Device.fTimeGlobal;
    ubo.timers.set(t, t * 10.0f, t / 10.0f, _sin(t));

    // Game time
    if (g_pGamePersistent)
    {
        float gt = g_pGamePersistent->Environment().GetGameTime();
        float day_frac = gt / 86400.0f;  // 86400 seconds in a day
        float hour = gt / 3600.0f;        // 3600 seconds in an hour
        ubo.timers_game.set(gt, day_frac, hour, (float)iFloor(hour));

        // Actor data
        ubo.actor_data.set(
            g_pGamePersistent->actor_data.health,
            g_pGamePersistent->actor_data.stamina,
            g_pGamePersistent->actor_data.bleeding,
            (float)g_pGamePersistent->actor_data.helmet
        );
    }
    else
    {
        ubo.timers_game.set(0, 0, 0, 0);
        ubo.actor_data.set(1, 1, 0, 0);
    }

    // Screen resolution
    ubo.screen_res.set(
        (float)Device.dwWidth,
        (float)Device.dwHeight,
        1.0f / (float)Device.dwWidth,
        1.0f / (float)Device.dwHeight
    );

    // SSFX (extern from vk_console.cpp)
    extern Fvector4 ps_ssfx_floravariation;
    extern Fvector4 ps_ssfx_fog;
    extern Fvector4 ps_ssfx_motionblur;
    extern Fvector4 ps_ssfx_wind_grass;
    extern Fvector4 ps_ssfx_wind_trees;
    extern Fvector4 ps_ssfx_lut;
    extern Fvector3 ps_ssfx_shadow_bias;

    ubo.ssfx_floravariation = ps_ssfx_floravariation;
    ubo.ssfx_fog = ps_ssfx_fog;
    ubo.ssfx_motionblur = ps_ssfx_motionblur;
    ubo.ssfx_wind_grass = ps_ssfx_wind_grass;
    ubo.ssfx_wind_trees = ps_ssfx_wind_trees;
    ubo.ssfx_lut = ps_ssfx_lut;
    ubo.ssfx_shadow_bias.set(ps_ssfx_shadow_bias.x, ps_ssfx_shadow_bias.y, ps_ssfx_shadow_bias.z, 0.0f);

    // Wind animation (from environment)
    if (g_pGamePersistent && g_pGamePersistent->Environment().CurrentEnv) {
        ubo.ssfx_wind_anim = g_pGamePersistent->Environment().wind_anim;
    } else {
        ubo.ssfx_wind_anim.set(0, 0, 0, 0);
    }

    // ========================================================================
    // POST-PROCESSING CONSTANTS (offset 512-767)
    // ========================================================================
    extern Fvector4 ps_ssfx_bloom_1;
    extern Fvector4 ps_ssfx_bloom_2;
    extern Fvector4 ps_ssfx_ao;
    extern Fvector4 ps_ssfx_ao_setup1;
    extern Fvector4 ps_ssfx_il;
    extern Fvector4 ps_ssfx_il_setup1;
    extern Fvector4 ps_ssfx_ssr;
    extern Fvector4 ps_ssfx_ssr_2;
    extern Fvector4 ps_ssfx_volumetric;
    extern Fvector4 ps_ssfx_terrain_offset;
    extern Fvector4 ps_ssfx_florafixes_1;
    extern Fvector4 ps_ssfx_florafixes_2;
    extern Fvector4 ps_ssfx_wetsurfaces_1;
    extern Fvector4 ps_ssfx_wetsurfaces_2;
    extern float ps_ssfx_gloss_factor;
    extern Fvector3 ps_ssfx_gloss_minmax;
    extern Fvector4 ps_ssfx_lightsetup_1;

    ubo.ssfx_bloom_1 = ps_ssfx_bloom_1;
    ubo.ssfx_bloom_2 = ps_ssfx_bloom_2;
    ubo.ssfx_ao = ps_ssfx_ao;
    ubo.ssfx_ao_setup1 = ps_ssfx_ao_setup1;
    ubo.ssfx_il = ps_ssfx_il;
    ubo.ssfx_il_setup1 = ps_ssfx_il_setup1;
    ubo.ssfx_ssr = ps_ssfx_ssr;
    ubo.ssfx_ssr_2 = ps_ssfx_ssr_2;
    ubo.ssfx_volumetric = ps_ssfx_volumetric;
    ubo.ssfx_terrain_offset = ps_ssfx_terrain_offset;
    ubo.ssfx_florafixes_1 = ps_ssfx_florafixes_1;
    ubo.ssfx_florafixes_2 = ps_ssfx_florafixes_2;
    ubo.ssfx_wetsurfaces_1 = ps_ssfx_wetsurfaces_1;
    ubo.ssfx_wetsurfaces_2 = ps_ssfx_wetsurfaces_2;
    ubo.ssfx_gloss.set(ps_ssfx_gloss_minmax.x, ps_ssfx_gloss_minmax.y, ps_ssfx_gloss_factor, 0.0f);
    ubo.ssfx_lightsetup_1 = ps_ssfx_lightsetup_1;

    // ========================================================================
    // HUD/SCOPE/PDA CONSTANTS (offset 768-1023)
    // ========================================================================
    if (g_pGamePersistent && g_pGamePersistent->m_pGShaderConstants)
    {
        ubo.m_hud_params = g_pGamePersistent->m_pGShaderConstants->hud_params;
        ubo.m_hud_fov_params = g_pGamePersistent->m_pGShaderConstants->hud_fov_params;

        // m_script_params is Fmatrix, extract first row for now
        const Fmatrix& script_mtx = g_pGamePersistent->m_pGShaderConstants->m_script_params;
        ubo.m_script_params.set(script_mtx._11, script_mtx._12, script_mtx._13, script_mtx._14);

        ubo.m_blender_mode = g_pGamePersistent->m_pGShaderConstants->m_blender_mode;

        if (g_pGamePersistent->pda_shader_data.pda_display_factor > 0)
        {
            float pda_factor = g_pGamePersistent->pda_shader_data.pda_display_factor;
            float pda_psy = g_pGamePersistent->pda_shader_data.pda_psy_influence;
            float pda_brightness = g_pGamePersistent->pda_shader_data.pda_displaybrightness;
            ubo.pda_params.set(pda_factor, pda_psy, pda_brightness, 0.0f);
        }
        else
        {
            ubo.pda_params.set(0, 0, 1, 0);
        }
    }
    else
    {
        ubo.m_hud_params.set(1, 1, 1, 1);
        ubo.m_hud_fov_params.set(1, 1, 1, 1);
        ubo.m_script_params.set(0, 0, 0, 0);
        ubo.m_blender_mode.set(0, 0, 0, 0);
        ubo.pda_params.set(0, 0, 1, 0);
    }

    // Scope parameters (extern from vk_console.cpp or shared_stubs)
    extern float scope_scrollpower;
    extern float scope_innerblur;
    extern float scope_outerblur;
    extern float scope_brightness;
    extern float scope_ca;
    extern float scope_fog_attack;
    extern float scope_fog_mattack;
    extern float scope_fog_travel;
    extern float scope_radius;
    extern float scope_fog_radius;
    extern float scope_fog_sharp;

    ubo.fakescope_params1.set(scope_scrollpower, scope_innerblur, scope_outerblur, scope_brightness);
    ubo.fakescope_params2.set(scope_ca, scope_fog_attack, scope_fog_mattack, scope_fog_travel);
    ubo.fakescope_params3.set(scope_radius, scope_fog_radius, scope_fog_sharp, 0.0f);

    extern Fvector4 ps_ssfx_hud_drops_1;
    extern Fvector4 ps_ssfx_hud_drops_2;
    extern Fvector4 ps_ssfx_blood_decals;
    extern Fvector4 ps_ssfx_wpn_dof_1;
    extern float ps_ssfx_wpn_dof_2;
    extern float ps_ssfx_hud_hemi;

    ubo.ssfx_hud_drops_1 = ps_ssfx_hud_drops_1;
    ubo.ssfx_hud_drops_2 = ps_ssfx_hud_drops_2;
    ubo.ssfx_blood_decals = ps_ssfx_blood_decals;
    ubo.ssfx_wpn_dof_1 = ps_ssfx_wpn_dof_1;
    ubo.ssfx_wpn_dof_2.set(ps_ssfx_wpn_dof_2, 0, 0, 0);
    ubo.ssfx_hud_hemi.set(ps_ssfx_hud_hemi, 0, 0, 0);

    // Second viewport check
    int issvp = Device.m_SecondViewport.IsSVPFrame() ? 1 : 0;
    ubo.ssfx_issvp.set((float)issvp, 0, 0, 0);

    // Frame time delta
    ubo.ssfx_fTimeDelta.set(Device.fTimeDelta, 0, 0, 0);

    // ========================================================================
    // HEAT VISION/EFFECTS CONSTANTS (offset 1024-1279)
    // ========================================================================
    extern int ps_r2_heatvision;
    extern Fvector4 heat_vision_steps;
    extern Fvector4 heat_vision_blurring;
    extern float heat_vision_mode;
    extern Fvector4 heat_vision_args_1;
    extern Fvector4 heat_vision_args_2;

    ubo.heatvision_params1.set((float)ps_r2_heatvision, heat_vision_steps.x, heat_vision_steps.y, heat_vision_steps.z);
    ubo.heatvision_params2.set(heat_vision_blurring.x, heat_vision_blurring.y, heat_vision_blurring.z, heat_vision_mode);
    ubo.heatvision_args1 = heat_vision_args_1;
    ubo.heatvision_args2 = heat_vision_args_2;

    extern float sil_glow_max_temp;
    extern float sil_glow_shot_temp;
    extern float sil_glow_cool_temp_rate;
    extern Fvector sil_glow_color;

    ubo.silencer_glowing.set(sil_glow_max_temp, sil_glow_shot_temp, sil_glow_cool_temp_rate, sil_glow_color.x);
    ubo.silencer_glow_color.set(sil_glow_color.y, sil_glow_color.z, 0, 0);

    extern Fvector4 ps_vignette_control;
    ubo.vignette_control = ps_vignette_control;

    extern float ps_r2_img_exposure;
    extern float ps_r2_img_gamma;
    extern float ps_r2_img_saturation;
    extern Fvector3 ps_r2_img_cg;

    ubo.pp_img_corrections.set(ps_r2_img_exposure, ps_r2_img_gamma, ps_r2_img_saturation, 1.0f);
    ubo.pp_img_cg.set(ps_r2_img_cg.x, ps_r2_img_cg.y, ps_r2_img_cg.z, 1.0f);

    extern int ps_markswitch_current;
    extern int ps_markswitch_count;
    extern Fvector4 ps_markswitch_color;

    ubo.markswitch_params.set((float)ps_markswitch_current, (float)ps_markswitch_count, 0, 0);
    ubo.markswitch_color = ps_markswitch_color;

    // TAA jitter and parameters
    extern Fvector4 ps_ssfx_taa;
    // Sub-pixel jitter for DLSS/TAA: xy = current frame, zw = previous frame (NDC)
    ubo.ssfx_jitter.set(
        RImplementation.m_Jitter.current.x,
        RImplementation.m_Jitter.current.y,
        RImplementation.m_Jitter.previous.x,
        RImplementation.m_Jitter.previous.y
    );
    ubo.ssfx_taa = ps_ssfx_taa;

    extern Fvector4 ps_ssfx_rain_1;
    extern Fvector4 ps_ssfx_rain_2;
    extern Fvector4 ps_ssfx_rain_3;

    ubo.ssfx_rain_1 = ps_ssfx_rain_1;
    ubo.ssfx_rain_2 = ps_ssfx_rain_2;
    ubo.ssfx_rain_3 = ps_ssfx_rain_3;

    // ========================================================================
    // DEBUG/DEV PARAMETERS (offset 1280-1535)
    // ========================================================================
    extern Fvector4 ps_dev_param_1;
    extern Fvector4 ps_dev_param_2;
    extern Fvector4 ps_dev_param_3;
    extern Fvector4 ps_dev_param_4;
    extern Fvector4 ps_dev_param_5;
    extern Fvector4 ps_dev_param_6;
    extern Fvector4 ps_dev_param_7;
    extern Fvector4 ps_dev_param_8;

    ubo.dev_param_1 = ps_dev_param_1;
    ubo.dev_param_2 = ps_dev_param_2;
    ubo.dev_param_3 = ps_dev_param_3;
    ubo.dev_param_4 = ps_dev_param_4;
    ubo.dev_param_5 = ps_dev_param_5;
    ubo.dev_param_6 = ps_dev_param_6;
    ubo.dev_param_7 = ps_dev_param_7;
    ubo.dev_param_8 = ps_dev_param_8;

    extern Fvector4 ps_s3ds_param_1;
    extern Fvector4 ps_s3ds_param_2;
    extern Fvector4 ps_s3ds_param_3;
    extern Fvector4 ps_s3ds_param_4;

    ubo.s3ds_param_1 = ps_s3ds_param_1;
    ubo.s3ds_param_2 = ps_s3ds_param_2;
    ubo.s3ds_param_3 = ps_s3ds_param_3;
    ubo.s3ds_param_4 = ps_s3ds_param_4;

    extern Fvector4 ps_ssfx_grass_interactive;
    extern Fvector4 ps_ssfx_int_grass_params_1;
    extern Fvector4 ps_ssfx_int_grass_params_2;

    ubo.ssfx_grass_interactive = ps_ssfx_grass_interactive;
    ubo.ssfx_int_grass_params_1 = ps_ssfx_int_grass_params_1;
    ubo.ssfx_int_grass_params_2 = ps_ssfx_int_grass_params_2;

    // ========================================================================
    // MOTION VECTORS: Previous frame ViewProjection matrix (offset 1520)
    // ========================================================================
    // The previous VP must include the previous frame's jitter so that
    // motion vectors = (currNDC_jittered - prevNDC_jittered) contain only
    // actual object motion, not the jitter delta.
    {
        Fmatrix prevProj = Device.mProject_prev;
        if (RImplementation.m_Jitter.enabled)
        {
            prevProj._31 += RImplementation.m_Jitter.previous.x;
            prevProj._32 += RImplementation.m_Jitter.previous.y;
        }
        Fmatrix prevVP;
        prevVP.mul(prevProj, Device.mView_prev);
        ubo.m_prevVP = prevVP;
        ubo.m_View = Device.mView;
    }

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

// ============================================================================
// Material Constants UBO (Set 4, binding 0)
// ============================================================================

void CVulkanLighting::CreateMaterialConstantsUBO()
{
    Msg("[Vulkan] Creating MaterialConstants UBO...");

    // ========================================================================
    // 1. Create Uniform Buffer
    // ========================================================================
    VkDeviceSize bufferSize = sizeof(MaterialConstantsUBO);

    VkBufferCreateInfo bufferInfo = {};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = bufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkResult result = vkCreateBuffer(VulkanHW.m_Device, &bufferInfo, nullptr, &m_MaterialConstantsBuffer);
    if (result != VK_SUCCESS) {
        Msg("![Vulkan] Failed to create MaterialConstants buffer: %d", result);
        return;
    }

    // ========================================================================
    // 2. Allocate Memory
    // ========================================================================
    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(VulkanHW.m_Device, m_MaterialConstantsBuffer, &memRequirements);

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
        Msg("![Vulkan] Failed to find suitable memory type for MaterialConstants UBO");
        vkDestroyBuffer(VulkanHW.m_Device, m_MaterialConstantsBuffer, nullptr);
        m_MaterialConstantsBuffer = VK_NULL_HANDLE;
        return;
    }

    allocInfo.memoryTypeIndex = memoryTypeIndex;

    result = vkAllocateMemory(VulkanHW.m_Device, &allocInfo, nullptr, &m_MaterialConstantsMemory);
    if (result != VK_SUCCESS) {
        Msg("![Vulkan] Failed to allocate MaterialConstants memory: %d", result);
        vkDestroyBuffer(VulkanHW.m_Device, m_MaterialConstantsBuffer, nullptr);
        m_MaterialConstantsBuffer = VK_NULL_HANDLE;
        return;
    }

    result = vkBindBufferMemory(VulkanHW.m_Device, m_MaterialConstantsBuffer, m_MaterialConstantsMemory, 0);
    if (result != VK_SUCCESS) {
        Msg("![Vulkan] Failed to bind MaterialConstants buffer memory: %d", result);
        vkFreeMemory(VulkanHW.m_Device, m_MaterialConstantsMemory, nullptr);
        vkDestroyBuffer(VulkanHW.m_Device, m_MaterialConstantsBuffer, nullptr);
        m_MaterialConstantsBuffer = VK_NULL_HANDLE;
        m_MaterialConstantsMemory = VK_NULL_HANDLE;
        return;
    }

    // ========================================================================
    // 3. Map Memory (persistent mapping)
    // ========================================================================
    result = vkMapMemory(VulkanHW.m_Device, m_MaterialConstantsMemory, 0, bufferSize, 0, &m_MaterialConstantsMapped);
    if (result != VK_SUCCESS) {
        Msg("![Vulkan] Failed to map MaterialConstants memory: %d", result);
        vkFreeMemory(VulkanHW.m_Device, m_MaterialConstantsMemory, nullptr);
        vkDestroyBuffer(VulkanHW.m_Device, m_MaterialConstantsBuffer, nullptr);
        m_MaterialConstantsBuffer = VK_NULL_HANDLE;
        m_MaterialConstantsMemory = VK_NULL_HANDLE;
        return;
    }

    // ========================================================================
    // 4. Create Descriptor Set Layout (Set 4)
    // ========================================================================
    VkDescriptorSetLayoutBinding uboBinding = {};
    uboBinding.binding = 0;
    uboBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboBinding.descriptorCount = 1;
    uboBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_VERTEX_BIT;
    uboBinding.pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutCreateInfo layoutInfo = {};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &uboBinding;

    result = vkCreateDescriptorSetLayout(VulkanHW.m_Device, &layoutInfo, nullptr, &m_MaterialConstantsLayout);
    if (result != VK_SUCCESS) {
        Msg("![Vulkan] Failed to create MaterialConstants descriptor set layout: %d", result);
        vkUnmapMemory(VulkanHW.m_Device, m_MaterialConstantsMemory);
        vkFreeMemory(VulkanHW.m_Device, m_MaterialConstantsMemory, nullptr);
        vkDestroyBuffer(VulkanHW.m_Device, m_MaterialConstantsBuffer, nullptr);
        m_MaterialConstantsBuffer = VK_NULL_HANDLE;
        m_MaterialConstantsMemory = VK_NULL_HANDLE;
        m_MaterialConstantsMapped = nullptr;
        return;
    }

    // ========================================================================
    // 5. Allocate Descriptor Set
    // ========================================================================
    VkDescriptorSetAllocateInfo allocSetInfo = {};
    allocSetInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocSetInfo.descriptorPool = m_DescriptorPool;
    allocSetInfo.descriptorSetCount = 1;
    allocSetInfo.pSetLayouts = &m_MaterialConstantsLayout;

    result = vkAllocateDescriptorSets(VulkanHW.m_Device, &allocSetInfo, &m_MaterialConstantsDescriptorSet);
    if (result != VK_SUCCESS) {
        Msg("![Vulkan] Failed to allocate MaterialConstants descriptor set: %d", result);
        vkDestroyDescriptorSetLayout(VulkanHW.m_Device, m_MaterialConstantsLayout, nullptr);
        vkUnmapMemory(VulkanHW.m_Device, m_MaterialConstantsMemory);
        vkFreeMemory(VulkanHW.m_Device, m_MaterialConstantsMemory, nullptr);
        vkDestroyBuffer(VulkanHW.m_Device, m_MaterialConstantsBuffer, nullptr);
        m_MaterialConstantsBuffer = VK_NULL_HANDLE;
        m_MaterialConstantsMemory = VK_NULL_HANDLE;
        m_MaterialConstantsMapped = nullptr;
        m_MaterialConstantsLayout = VK_NULL_HANDLE;
        return;
    }

    // ========================================================================
    // 6. Update Descriptor Set
    // ========================================================================
    VkDescriptorBufferInfo bufferDescInfo = {};
    bufferDescInfo.buffer = m_MaterialConstantsBuffer;
    bufferDescInfo.offset = 0;
    bufferDescInfo.range = sizeof(MaterialConstantsUBO);

    VkWriteDescriptorSet descriptorWrite = {};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = m_MaterialConstantsDescriptorSet;
    descriptorWrite.dstBinding = 0;
    descriptorWrite.dstArrayElement = 0;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pBufferInfo = &bufferDescInfo;

    vkUpdateDescriptorSets(VulkanHW.m_Device, 1, &descriptorWrite, 0, nullptr);

    Msg("[Vulkan] MaterialConstants UBO created successfully");
    Msg("[Vulkan]   - Buffer: %p", m_MaterialConstantsBuffer);
    Msg("[Vulkan]   - Memory: %p", m_MaterialConstantsMemory);
    Msg("[Vulkan]   - Mapped: %p", m_MaterialConstantsMapped);
    Msg("[Vulkan]   - Descriptor Set: %p", m_MaterialConstantsDescriptorSet);
}

void CVulkanLighting::UpdateMaterialConstantsUBO()
{
    if (!m_MaterialConstantsMapped) {
        return;
    }

    MaterialConstantsUBO ubo = {};

    // ========================================================================
    // L_material (default for now, will be updated per-material later)
    // ========================================================================
    ubo.L_material.set(1.0f, 1.0f, 0.0f, 0.0f);

    // ========================================================================
    // Detail params (from r__dtex_range)
    // ========================================================================
    extern float r__dtex_range;
    float detail_scale = 1.0f;  // Default, can be adjusted per-material
    ubo.detail_params.set(detail_scale, detail_scale, detail_scale, 1.0f / r__dtex_range);

    // ========================================================================
    // Parallax/POM params (from console variables)
    // ========================================================================
    extern Fvector4 ps_ssfx_pom;
    extern Fvector4 ps_ssfx_terrain_pom;
    ubo.parallax_params = ps_ssfx_pom;
    ubo.terrain_pom_params = ps_ssfx_terrain_pom;

    // ========================================================================
    // Water params (from console variables)
    // ========================================================================
    extern Fvector4 ps_ssfx_water;
    extern Fvector4 ps_ssfx_water_setup1;
    extern Fvector4 ps_ssfx_water_setup2;
    ubo.water_params = ps_ssfx_water;
    ubo.water_setup1 = ps_ssfx_water_setup1;
    ubo.water_setup2 = ps_ssfx_water_setup2;

    // ========================================================================
    // Hemi cube (from RCache.hemi - placeholder for now)
    // ========================================================================
    // TODO: Get from RCache when available
    ubo.hemi_cube_pos_faces.set(1, 1, 1, 0);
    ubo.hemi_cube_neg_faces.set(1, 1, 1, 0);

    // ========================================================================
    // Texgen params (identity for now)
    // ========================================================================
    ubo.m_texgen_params.set(1, 1, 0, 0);

    // ========================================================================
    // HDR10 PARAMETERS (offset 160-335)
    // ========================================================================
    extern float ps_r4_hdr10_whitepoint_nits;
    extern float ps_r4_hdr10_ui_nits;
    extern float ps_r4_hdr10_pda_intensity;
    extern int ps_r4_hdr10_pda;
    extern int ps_r4_hdr10_on;
    extern int ps_r4_hdr10_colorspace;
    extern int ps_r4_hdr10_tonemapper;
    extern int ps_r4_hdr10_tonemap_mode;

    ubo.hdr10_params1.set(ps_r4_hdr10_whitepoint_nits, ps_r4_hdr10_ui_nits, ps_r4_hdr10_pda_intensity, (float)ps_r4_hdr10_pda);
    ubo.hdr10_params2.set((float)ps_r4_hdr10_on, (float)ps_r4_hdr10_colorspace, (float)ps_r4_hdr10_tonemapper, (float)ps_r4_hdr10_tonemap_mode);

    extern float ps_r4_hdr10_exposure;
    extern float ps_r4_hdr10_contrast;
    extern float ps_r4_hdr10_contrast_middle_gray;
    extern float ps_r4_hdr10_saturation;
    extern float ps_r4_hdr10_brightness;
    extern float ps_r4_hdr10_gamma;
    extern float ps_r4_hdr10_ui_saturation;

    ubo.hdr10_tonemap1.set(ps_r4_hdr10_exposure, ps_r4_hdr10_contrast, ps_r4_hdr10_contrast_middle_gray, ps_r4_hdr10_saturation);
    ubo.hdr10_tonemap2.set(ps_r4_hdr10_brightness, ps_r4_hdr10_gamma, ps_r4_hdr10_ui_saturation, 0.0f);

    extern int ps_r4_hdr10_bloom_on;
    extern int ps_r4_hdr10_bloom_blur_passes;
    extern float ps_r4_hdr10_bloom_blur_scale;
    extern float ps_r4_hdr10_bloom_intensity;

    ubo.hdr10_bloom1.set((float)ps_r4_hdr10_bloom_on, (float)ps_r4_hdr10_bloom_blur_passes, ps_r4_hdr10_bloom_blur_scale, ps_r4_hdr10_bloom_intensity);

    extern int ps_r4_hdr10_flare_on;
    extern float ps_r4_hdr10_flare_threshold;
    extern float ps_r4_hdr10_flare_power;
    extern int ps_r4_hdr10_flare_ghosts;
    extern float ps_r4_hdr10_flare_ghost_dispersal;
    extern float ps_r4_hdr10_flare_center_falloff;
    extern float ps_r4_hdr10_flare_halo_scale;
    extern float ps_r4_hdr10_flare_halo_ca;
    extern float ps_r4_hdr10_flare_ghost_ca;
    extern int ps_r4_hdr10_flare_blur_passes;
    extern float ps_r4_hdr10_flare_blur_scale;
    extern float ps_r4_hdr10_flare_ghost_intensity;
    extern float ps_r4_hdr10_flare_halo_intensity;
    extern Fvector3 ps_r4_hdr10_flare_lens_color;

    ubo.hdr10_flare1.set((float)ps_r4_hdr10_flare_on, ps_r4_hdr10_flare_threshold, ps_r4_hdr10_flare_power, (float)ps_r4_hdr10_flare_ghosts);
    ubo.hdr10_flare2.set(ps_r4_hdr10_flare_ghost_dispersal, ps_r4_hdr10_flare_center_falloff, ps_r4_hdr10_flare_halo_scale, ps_r4_hdr10_flare_halo_ca);
    ubo.hdr10_flare3.set(ps_r4_hdr10_flare_ghost_ca, (float)ps_r4_hdr10_flare_blur_passes, ps_r4_hdr10_flare_blur_scale, ps_r4_hdr10_flare_ghost_intensity);
    ubo.hdr10_flare4.set(ps_r4_hdr10_flare_halo_intensity, ps_r4_hdr10_flare_lens_color.x, ps_r4_hdr10_flare_lens_color.y, ps_r4_hdr10_flare_lens_color.z);

    extern int ps_r4_hdr10_sun_on;
    extern float ps_r4_hdr10_sun_intensity;
    extern float ps_r4_hdr10_sun_inner_radius;
    extern float ps_r4_hdr10_sun_outer_radius;
    extern float ps_r4_hdr10_sun_dawn_begin;
    extern float ps_r4_hdr10_sun_dawn_end;
    extern float ps_r4_hdr10_sun_dusk_begin;
    extern float ps_r4_hdr10_sun_dusk_end;

    ubo.hdr10_sun1.set((float)ps_r4_hdr10_sun_on, ps_r4_hdr10_sun_intensity, ps_r4_hdr10_sun_inner_radius, ps_r4_hdr10_sun_outer_radius);
    ubo.hdr10_sun2.set(ps_r4_hdr10_sun_dawn_begin, ps_r4_hdr10_sun_dawn_end, ps_r4_hdr10_sun_dusk_begin, ps_r4_hdr10_sun_dusk_end);

    // ========================================================================
    // ADDITIONAL SSFX (offset 336-511)
    // ========================================================================
    extern Fvector4 ps_ssfx_rain_drops_setup;
    extern Fvector3 ps_ssfx_shadow_cascades;
    extern Fvector4 ps_ssfx_grass_shadows;
    extern Fvector4 ps_ssfx_terrain_quality;
    extern Fvector3 ps_ssfx_water_quality;
    extern Fvector4 ps_ssfx_sss_quality;
    extern Fvector4 ps_ssfx_sss;
    extern int ps_ssfx_is_underground;
    extern float ps_ssfx_fog_scattering;

    ubo.ssfx_rain_drops = ps_ssfx_rain_drops_setup;
    ubo.ssfx_shadow_cascades.set(ps_ssfx_shadow_cascades.x, ps_ssfx_shadow_cascades.y, ps_ssfx_shadow_cascades.z, 0.0f);
    ubo.ssfx_grass_shadows = ps_ssfx_grass_shadows;
    ubo.ssfx_terrain_quality = ps_ssfx_terrain_quality;
    ubo.ssfx_water_quality.set(ps_ssfx_water_quality.x, ps_ssfx_water_quality.y, ps_ssfx_water_quality.z, 0.0f);
    ubo.ssfx_sss_quality = ps_ssfx_sss_quality;
    ubo.ssfx_sss = ps_ssfx_sss;
    ubo.ssfx_is_underground.set((float)ps_ssfx_is_underground, 0, 0, 0);

    // Wind anim previous (from Device)
    ubo.ssfx_wind_anim_prev = Device.wind_anim_prev;

    ubo.ssfx_fog_scattering.set(ps_ssfx_fog_scattering, 0, 0, 0);

    ubo.reserved_mat[0].set(0, 0, 0, 0);

    // ========================================================================
    // Copy to mapped memory
    // ========================================================================
    memcpy(m_MaterialConstantsMapped, &ubo, sizeof(MaterialConstantsUBO));
}

void CVulkanLighting::DestroyMaterialConstantsUBO()
{
    Msg("[Vulkan] Destroying MaterialConstants UBO...");

    // Unmap memory
    if (m_MaterialConstantsMapped && m_MaterialConstantsMemory != VK_NULL_HANDLE) {
        vkUnmapMemory(VulkanHW.m_Device, m_MaterialConstantsMemory);
        m_MaterialConstantsMapped = nullptr;
    }

    // Destroy buffer
    if (m_MaterialConstantsBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(VulkanHW.m_Device, m_MaterialConstantsBuffer, nullptr);
        m_MaterialConstantsBuffer = VK_NULL_HANDLE;
    }

    // Free memory
    if (m_MaterialConstantsMemory != VK_NULL_HANDLE) {
        vkFreeMemory(VulkanHW.m_Device, m_MaterialConstantsMemory, nullptr);
        m_MaterialConstantsMemory = VK_NULL_HANDLE;
    }

    // Destroy descriptor set layout
    if (m_MaterialConstantsLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(VulkanHW.m_Device, m_MaterialConstantsLayout, nullptr);
        m_MaterialConstantsLayout = VK_NULL_HANDLE;
    }

    // Descriptor set automatically freed when pool is destroyed
    m_MaterialConstantsDescriptorSet = VK_NULL_HANDLE;

    Msg("[Vulkan] MaterialConstants UBO destroyed");
}

// ============================================================================
// Constant Mapping System
// ============================================================================

void CVulkanLighting::BuildConstantMap()
{
    Msg("[Vulkan] Building constant name → UBO offset map...");

    m_ConstantMap.clear();

    // ========================================================================
    // GlobalLightingUBO (uboIndex = 0)
    // ========================================================================

    // Existing fields
    m_ConstantMap["L_hemi_color"] = {0, offsetof(GlobalLightingUBO, L_hemi_color)};
    m_ConstantMap["L_ambient"] = {0, offsetof(GlobalLightingUBO, L_ambient)};
    m_ConstantMap["L_sun_color"] = {0, offsetof(GlobalLightingUBO, L_sun_color)};
    m_ConstantMap["L_sun_dir_w"] = {0, offsetof(GlobalLightingUBO, L_sun_dir_w)};
    m_ConstantMap["m_invV"] = {0, offsetof(GlobalLightingUBO, m_invV)};

    // Critical fields
    m_ConstantMap["L_sun_dir_e"] = {0, offsetof(GlobalLightingUBO, L_sun_dir_e)};
    m_ConstantMap["eye_position"] = {0, offsetof(GlobalLightingUBO, eye_position)};
    m_ConstantMap["eye_direction"] = {0, offsetof(GlobalLightingUBO, eye_direction)};
    m_ConstantMap["eye_normal"] = {0, offsetof(GlobalLightingUBO, eye_normal)};

    m_ConstantMap["fog_plane"] = {0, offsetof(GlobalLightingUBO, fog_plane)};
    m_ConstantMap["fog_params"] = {0, offsetof(GlobalLightingUBO, fog_params)};
    m_ConstantMap["fog_color"] = {0, offsetof(GlobalLightingUBO, fog_color)};

    m_ConstantMap["timers"] = {0, offsetof(GlobalLightingUBO, timers)};
    m_ConstantMap["timers_game"] = {0, offsetof(GlobalLightingUBO, timers_game)};

    // Screen and camera
    m_ConstantMap["screen_res"] = {0, offsetof(GlobalLightingUBO, screen_res)};
    m_ConstantMap["ogse_c_screen"] = {0, offsetof(GlobalLightingUBO, ogse_c_screen)};
    m_ConstantMap["near_far_plane"] = {0, offsetof(GlobalLightingUBO, near_far_plane)};

    // Environment
    m_ConstantMap["wind_params"] = {0, offsetof(GlobalLightingUBO, wind_params)};
    m_ConstantMap["rain_params"] = {0, offsetof(GlobalLightingUBO, rain_params)};
    m_ConstantMap["sky_color"] = {0, offsetof(GlobalLightingUBO, sky_color)};
    m_ConstantMap["actor_data"] = {0, offsetof(GlobalLightingUBO, actor_data)};

    // SSFX
    m_ConstantMap["ssfx_floravariation"] = {0, offsetof(GlobalLightingUBO, ssfx_floravariation)};
    m_ConstantMap["ssfx_fog"] = {0, offsetof(GlobalLightingUBO, ssfx_fog)};
    m_ConstantMap["ssfx_motionblur"] = {0, offsetof(GlobalLightingUBO, ssfx_motionblur)};
    m_ConstantMap["ssfx_wind_grass"] = {0, offsetof(GlobalLightingUBO, ssfx_wind_grass)};
    m_ConstantMap["ssfx_wsetup_grass"] = {0, offsetof(GlobalLightingUBO, ssfx_wind_grass)};  // Alias
    m_ConstantMap["ssfx_wind_trees"] = {0, offsetof(GlobalLightingUBO, ssfx_wind_trees)};
    m_ConstantMap["ssfx_wsetup_trees"] = {0, offsetof(GlobalLightingUBO, ssfx_wind_trees)};  // Alias
    m_ConstantMap["ssfx_wind_anim"] = {0, offsetof(GlobalLightingUBO, ssfx_wind_anim)};
    m_ConstantMap["ssfx_lut"] = {0, offsetof(GlobalLightingUBO, ssfx_lut)};
    m_ConstantMap["ssfx_shadow_bias"] = {0, offsetof(GlobalLightingUBO, ssfx_shadow_bias)};

    // Post-processing
    m_ConstantMap["ssfx_bloom_1"] = {0, offsetof(GlobalLightingUBO, ssfx_bloom_1)};
    m_ConstantMap["ssfx_bloom_2"] = {0, offsetof(GlobalLightingUBO, ssfx_bloom_2)};
    m_ConstantMap["ssfx_ao"] = {0, offsetof(GlobalLightingUBO, ssfx_ao)};
    m_ConstantMap["ssfx_ao_setup1"] = {0, offsetof(GlobalLightingUBO, ssfx_ao_setup1)};
    m_ConstantMap["ssfx_il"] = {0, offsetof(GlobalLightingUBO, ssfx_il)};
    m_ConstantMap["ssfx_il_setup1"] = {0, offsetof(GlobalLightingUBO, ssfx_il_setup1)};
    m_ConstantMap["ssfx_ssr"] = {0, offsetof(GlobalLightingUBO, ssfx_ssr)};
    m_ConstantMap["ssfx_ssr_2"] = {0, offsetof(GlobalLightingUBO, ssfx_ssr_2)};
    m_ConstantMap["ssfx_volumetric"] = {0, offsetof(GlobalLightingUBO, ssfx_volumetric)};
    m_ConstantMap["ssfx_terrain_offset"] = {0, offsetof(GlobalLightingUBO, ssfx_terrain_offset)};
    m_ConstantMap["ssfx_florafixes_1"] = {0, offsetof(GlobalLightingUBO, ssfx_florafixes_1)};
    m_ConstantMap["ssfx_florafixes_2"] = {0, offsetof(GlobalLightingUBO, ssfx_florafixes_2)};
    m_ConstantMap["ssfx_wetsurfaces_1"] = {0, offsetof(GlobalLightingUBO, ssfx_wetsurfaces_1)};
    m_ConstantMap["ssfx_wetsurfaces_2"] = {0, offsetof(GlobalLightingUBO, ssfx_wetsurfaces_2)};
    m_ConstantMap["ssfx_gloss"] = {0, offsetof(GlobalLightingUBO, ssfx_gloss)};
    m_ConstantMap["ssfx_lightsetup_1"] = {0, offsetof(GlobalLightingUBO, ssfx_lightsetup_1)};

    // HUD/Scope/PDA
    m_ConstantMap["m_hud_params"] = {0, offsetof(GlobalLightingUBO, m_hud_params)};
    m_ConstantMap["m_hud_fov_params"] = {0, offsetof(GlobalLightingUBO, m_hud_fov_params)};
    m_ConstantMap["m_script_params"] = {0, offsetof(GlobalLightingUBO, m_script_params)};
    m_ConstantMap["m_blender_mode"] = {0, offsetof(GlobalLightingUBO, m_blender_mode)};
    m_ConstantMap["pda_params"] = {0, offsetof(GlobalLightingUBO, pda_params)};
    m_ConstantMap["fakescope_params1"] = {0, offsetof(GlobalLightingUBO, fakescope_params1)};
    m_ConstantMap["fakescope_params2"] = {0, offsetof(GlobalLightingUBO, fakescope_params2)};
    m_ConstantMap["fakescope_params3"] = {0, offsetof(GlobalLightingUBO, fakescope_params3)};
    m_ConstantMap["ssfx_hud_drops_1"] = {0, offsetof(GlobalLightingUBO, ssfx_hud_drops_1)};
    m_ConstantMap["ssfx_hud_drops_2"] = {0, offsetof(GlobalLightingUBO, ssfx_hud_drops_2)};
    m_ConstantMap["ssfx_blood_decals"] = {0, offsetof(GlobalLightingUBO, ssfx_blood_decals)};
    m_ConstantMap["ssfx_wpn_dof_1"] = {0, offsetof(GlobalLightingUBO, ssfx_wpn_dof_1)};
    m_ConstantMap["ssfx_wpn_dof_2"] = {0, offsetof(GlobalLightingUBO, ssfx_wpn_dof_2)};
    m_ConstantMap["ssfx_hud_hemi"] = {0, offsetof(GlobalLightingUBO, ssfx_hud_hemi)};
    m_ConstantMap["ssfx_issvp"] = {0, offsetof(GlobalLightingUBO, ssfx_issvp)};
    m_ConstantMap["ssfx_fTimeDelta"] = {0, offsetof(GlobalLightingUBO, ssfx_fTimeDelta)};

    // Heat vision/Effects
    m_ConstantMap["heatvision_params1"] = {0, offsetof(GlobalLightingUBO, heatvision_params1)};
    m_ConstantMap["heatvision_params2"] = {0, offsetof(GlobalLightingUBO, heatvision_params2)};
    m_ConstantMap["heatvision_args1"] = {0, offsetof(GlobalLightingUBO, heatvision_args1)};
    m_ConstantMap["heatvision_args2"] = {0, offsetof(GlobalLightingUBO, heatvision_args2)};
    m_ConstantMap["silencer_glowing"] = {0, offsetof(GlobalLightingUBO, silencer_glowing)};
    m_ConstantMap["silencer_glow_color"] = {0, offsetof(GlobalLightingUBO, silencer_glow_color)};
    m_ConstantMap["vignette_control"] = {0, offsetof(GlobalLightingUBO, vignette_control)};
    m_ConstantMap["pp_img_corrections"] = {0, offsetof(GlobalLightingUBO, pp_img_corrections)};
    m_ConstantMap["pp_img_cg"] = {0, offsetof(GlobalLightingUBO, pp_img_cg)};
    m_ConstantMap["markswitch_params"] = {0, offsetof(GlobalLightingUBO, markswitch_params)};
    m_ConstantMap["markswitch_color"] = {0, offsetof(GlobalLightingUBO, markswitch_color)};
    m_ConstantMap["ssfx_jitter"] = {0, offsetof(GlobalLightingUBO, ssfx_jitter)};
    m_ConstantMap["ssfx_taa"] = {0, offsetof(GlobalLightingUBO, ssfx_taa)};
    m_ConstantMap["ssfx_rain_1"] = {0, offsetof(GlobalLightingUBO, ssfx_rain_1)};
    m_ConstantMap["ssfx_rain_2"] = {0, offsetof(GlobalLightingUBO, ssfx_rain_2)};
    m_ConstantMap["ssfx_rain_3"] = {0, offsetof(GlobalLightingUBO, ssfx_rain_3)};

    // Debug/Dev
    m_ConstantMap["dev_param_1"] = {0, offsetof(GlobalLightingUBO, dev_param_1)};
    m_ConstantMap["dev_param_2"] = {0, offsetof(GlobalLightingUBO, dev_param_2)};
    m_ConstantMap["dev_param_3"] = {0, offsetof(GlobalLightingUBO, dev_param_3)};
    m_ConstantMap["dev_param_4"] = {0, offsetof(GlobalLightingUBO, dev_param_4)};
    m_ConstantMap["dev_param_5"] = {0, offsetof(GlobalLightingUBO, dev_param_5)};
    m_ConstantMap["dev_param_6"] = {0, offsetof(GlobalLightingUBO, dev_param_6)};
    m_ConstantMap["dev_param_7"] = {0, offsetof(GlobalLightingUBO, dev_param_7)};
    m_ConstantMap["dev_param_8"] = {0, offsetof(GlobalLightingUBO, dev_param_8)};
    m_ConstantMap["s3ds_param_1"] = {0, offsetof(GlobalLightingUBO, s3ds_param_1)};
    m_ConstantMap["s3ds_param_2"] = {0, offsetof(GlobalLightingUBO, s3ds_param_2)};
    m_ConstantMap["s3ds_param_3"] = {0, offsetof(GlobalLightingUBO, s3ds_param_3)};
    m_ConstantMap["s3ds_param_4"] = {0, offsetof(GlobalLightingUBO, s3ds_param_4)};
    m_ConstantMap["ssfx_grass_interactive"] = {0, offsetof(GlobalLightingUBO, ssfx_grass_interactive)};
    m_ConstantMap["ssfx_int_grass_params_1"] = {0, offsetof(GlobalLightingUBO, ssfx_int_grass_params_1)};
    m_ConstantMap["ssfx_int_grass_params_2"] = {0, offsetof(GlobalLightingUBO, ssfx_int_grass_params_2)};

    // ========================================================================
    // MaterialConstantsUBO (uboIndex = 1)
    // ========================================================================
    m_ConstantMap["L_material"] = {1, offsetof(MaterialConstantsUBO, L_material)};
    m_ConstantMap["dt_params"] = {1, offsetof(MaterialConstantsUBO, detail_params)};
    m_ConstantMap["detail_params"] = {1, offsetof(MaterialConstantsUBO, detail_params)};  // Alias
    m_ConstantMap["ssfx_pom"] = {1, offsetof(MaterialConstantsUBO, parallax_params)};
    m_ConstantMap["ssfx_terrain_pom"] = {1, offsetof(MaterialConstantsUBO, terrain_pom_params)};
    m_ConstantMap["ssfx_water"] = {1, offsetof(MaterialConstantsUBO, water_params)};
    m_ConstantMap["ssfx_water_setup1"] = {1, offsetof(MaterialConstantsUBO, water_setup1)};
    m_ConstantMap["ssfx_water_setup2"] = {1, offsetof(MaterialConstantsUBO, water_setup2)};
    m_ConstantMap["hemi_cube_pos_faces"] = {1, offsetof(MaterialConstantsUBO, hemi_cube_pos_faces)};
    m_ConstantMap["hemi_cube_neg_faces"] = {1, offsetof(MaterialConstantsUBO, hemi_cube_neg_faces)};
    m_ConstantMap["m_texgen_params"] = {1, offsetof(MaterialConstantsUBO, m_texgen_params)};

    // HDR10
    m_ConstantMap["hdr10_params1"] = {1, offsetof(MaterialConstantsUBO, hdr10_params1)};
    m_ConstantMap["hdr10_params2"] = {1, offsetof(MaterialConstantsUBO, hdr10_params2)};
    m_ConstantMap["hdr10_tonemap1"] = {1, offsetof(MaterialConstantsUBO, hdr10_tonemap1)};
    m_ConstantMap["hdr10_tonemap2"] = {1, offsetof(MaterialConstantsUBO, hdr10_tonemap2)};
    m_ConstantMap["hdr10_bloom1"] = {1, offsetof(MaterialConstantsUBO, hdr10_bloom1)};
    m_ConstantMap["hdr10_flare1"] = {1, offsetof(MaterialConstantsUBO, hdr10_flare1)};
    m_ConstantMap["hdr10_flare2"] = {1, offsetof(MaterialConstantsUBO, hdr10_flare2)};
    m_ConstantMap["hdr10_flare3"] = {1, offsetof(MaterialConstantsUBO, hdr10_flare3)};
    m_ConstantMap["hdr10_flare4"] = {1, offsetof(MaterialConstantsUBO, hdr10_flare4)};
    m_ConstantMap["hdr10_sun1"] = {1, offsetof(MaterialConstantsUBO, hdr10_sun1)};
    m_ConstantMap["hdr10_sun2"] = {1, offsetof(MaterialConstantsUBO, hdr10_sun2)};

    // Additional SSFX
    m_ConstantMap["ssfx_rain_drops"] = {1, offsetof(MaterialConstantsUBO, ssfx_rain_drops)};
    m_ConstantMap["ssfx_rain_drops_setup"] = {1, offsetof(MaterialConstantsUBO, ssfx_rain_drops)};  // Alias
    m_ConstantMap["ssfx_shadow_cascades"] = {1, offsetof(MaterialConstantsUBO, ssfx_shadow_cascades)};
    m_ConstantMap["ssfx_grass_shadows"] = {1, offsetof(MaterialConstantsUBO, ssfx_grass_shadows)};
    m_ConstantMap["ssfx_terrain_quality"] = {1, offsetof(MaterialConstantsUBO, ssfx_terrain_quality)};
    m_ConstantMap["ssfx_water_quality"] = {1, offsetof(MaterialConstantsUBO, ssfx_water_quality)};
    m_ConstantMap["ssfx_sss_quality"] = {1, offsetof(MaterialConstantsUBO, ssfx_sss_quality)};
    m_ConstantMap["ssfx_sss"] = {1, offsetof(MaterialConstantsUBO, ssfx_sss)};
    m_ConstantMap["ssfx_is_underground"] = {1, offsetof(MaterialConstantsUBO, ssfx_is_underground)};
    m_ConstantMap["ssfx_wind_anim_prev"] = {1, offsetof(MaterialConstantsUBO, ssfx_wind_anim_prev)};
    m_ConstantMap["ssfx_fog_scattering"] = {1, offsetof(MaterialConstantsUBO, ssfx_fog_scattering)};

    Msg("[Vulkan] Constant map built: %d entries", m_ConstantMap.size());
}

void CVulkanLighting::SetConstant(LPCSTR name, float x, float y, float z, float w)
{
    if (!m_GlobalLightingMapped || !m_MaterialConstantsMapped)
        return;

    auto it = m_ConstantMap.find(name);
    if (it == m_ConstantMap.end())
        return; // Unknown constant (ignore silently)

    const ConstantMapping& mapping = it->second;

    // Get target UBO mapped pointer
    void* targetMapped = nullptr;
    if (mapping.uboIndex == 0)
        targetMapped = m_GlobalLightingMapped;
    else if (mapping.uboIndex == 1)
        targetMapped = m_MaterialConstantsMapped;
    else
        return;

    // Write vec4 directly to mapped memory
    float* dst = (float*)((u8*)targetMapped + mapping.offset);
    dst[0] = x;
    dst[1] = y;
    dst[2] = z;
    dst[3] = w;

    // NOTE: HOST_COHERENT memory doesn't require flush
}

void CVulkanLighting::SetConstant(LPCSTR name, const Fmatrix& M)
{
    if (!m_GlobalLightingMapped || !m_MaterialConstantsMapped)
        return;

    auto it = m_ConstantMap.find(name);
    if (it == m_ConstantMap.end())
        return;

    const ConstantMapping& mapping = it->second;

    // Get target UBO mapped pointer
    void* targetMapped = nullptr;
    if (mapping.uboIndex == 0)
        targetMapped = m_GlobalLightingMapped;
    else if (mapping.uboIndex == 1)
        targetMapped = m_MaterialConstantsMapped;
    else
        return;

    // Copy matrix (64 bytes = 16 floats)
    memcpy((u8*)targetMapped + mapping.offset, &M, sizeof(Fmatrix));
}

void CVulkanLighting::SetConstant(LPCSTR name, const Fvector4& V)
{
    SetConstant(name, V.x, V.y, V.z, V.w);
}

} // namespace VK
