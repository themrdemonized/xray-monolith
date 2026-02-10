// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// Licensed under the same terms as X-Ray Engine (see root License.txt)

#include "stdafx.h"
#include "vk_compute.h"
#include "HW_Vulkan.h"

namespace VK
{

//------------------------------------------------------------------------------
// CVulkanComputePipeline
//------------------------------------------------------------------------------

CVulkanComputePipeline::CVulkanComputePipeline()
{
}

CVulkanComputePipeline::~CVulkanComputePipeline()
{
    Destroy();
}

void CVulkanComputePipeline::Create(VkShaderModule shader, VkPipelineLayout layout)
{
    if (m_Pipeline != VK_NULL_HANDLE) {
        Msg("![Vulkan] Compute pipeline already created, call Destroy first");
        return;
    }

    if (shader == VK_NULL_HANDLE) {
        Msg("![Vulkan] Cannot create compute pipeline with null shader");
        return;
    }

    if (layout == VK_NULL_HANDLE) {
        Msg("![Vulkan] Cannot create compute pipeline with null layout");
        return;
    }

    // Compute pipeline create info
    VkComputePipelineCreateInfo pipelineInfo = {};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pipelineInfo.stage.module = shader;
    pipelineInfo.stage.pName = "main";  // Entry point
    pipelineInfo.layout = layout;

    VK_CHECK(vkCreateComputePipelines(VulkanHW.m_Device, VK_NULL_HANDLE, 1,
                                      &pipelineInfo, nullptr, &m_Pipeline));

    Msg("[Vulkan] Compute pipeline created");
}

void CVulkanComputePipeline::Bind(VkCommandBuffer cmd)
{
    if (m_Pipeline == VK_NULL_HANDLE) {
        Msg("![Vulkan] Cannot bind null compute pipeline");
        return;
    }

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_Pipeline);
}

void CVulkanComputePipeline::Dispatch(VkCommandBuffer cmd, u32 groupCountX, u32 groupCountY, u32 groupCountZ)
{
    if (m_Pipeline == VK_NULL_HANDLE) {
        Msg("![Vulkan] Cannot dispatch null compute pipeline");
        return;
    }

    // Bind pipeline first
    Bind(cmd);

    // Dispatch compute work
    vkCmdDispatch(cmd, groupCountX, groupCountY, groupCountZ);
}

void CVulkanComputePipeline::Destroy()
{
    if (m_Pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(VulkanHW.m_Device, m_Pipeline, nullptr);
        m_Pipeline = VK_NULL_HANDLE;
    }
}

//------------------------------------------------------------------------------
// CVulkanComputeShader
//------------------------------------------------------------------------------

CVulkanComputeShader::CVulkanComputeShader()
{
}

CVulkanComputeShader::~CVulkanComputeShader()
{
    Destroy();
}

bool CVulkanComputeShader::Load(const char* filename)
{
    if (!filename) {
        Msg("![Vulkan] Compute shader filename is null");
        return false;
    }

    // Открываем файл
    IReader* F = FS.r_open(filename);
    if (!F) {
        Msg("![Vulkan] Failed to open compute shader: %s", filename);
        return false;
    }

    // Читаем SPIR-V bytecode
    u32 size = F->length();
    if (size == 0 || size % 4 != 0) {
        Msg("![Vulkan] Invalid SPIR-V file size: %s", filename);
        FS.r_close(F);
        return false;
    }

    // Загружаем в память
    xr_vector<u32> code(size / 4);
    F->r(code.data(), size);
    FS.r_close(F);

    // Создаём shader module
    CreateFromMemory(code.data(), size);

    if (m_ShaderModule != VK_NULL_HANDLE) {
        Msg("[Vulkan] Compute shader loaded: %s", filename);
        return true;
    }

    return false;
}

void CVulkanComputeShader::CreateFromMemory(const u32* code, size_t size)
{
    if (m_ShaderModule != VK_NULL_HANDLE) {
        Msg("![Vulkan] Shader module already created, call Destroy first");
        return;
    }

    if (!code || size == 0) {
        Msg("![Vulkan] Invalid SPIR-V code");
        return;
    }

    VkShaderModuleCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = size;
    createInfo.pCode = code;

    VK_CHECK(vkCreateShaderModule(VulkanHW.m_Device, &createInfo, nullptr, &m_ShaderModule));
}

void CVulkanComputeShader::Destroy()
{
    if (m_ShaderModule != VK_NULL_HANDLE) {
        vkDestroyShaderModule(VulkanHW.m_Device, m_ShaderModule, nullptr);
        m_ShaderModule = VK_NULL_HANDLE;
    }
}

} // namespace VK
