// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// SPDX-License-Identifier: MIT

#include "stdafx.h"
#include "vk3DFluidManager.h"
#include "vk3DFluidRenderer.h"
#include "vk3DFluidEmitters.h"
#include "../HW_Vulkan.h"

namespace VK
{

vk3DFluidManager::vk3DFluidManager()
{
}

vk3DFluidManager::~vk3DFluidManager()
{
    Destroy();
}

void vk3DFluidManager::Initialize()
{
    Msg("[Vulkan] Initializing 3D Fluid Manager...");

    // Создаём grid
    m_Grid = new vk3DFluidGrid();
    m_Grid->Create(m_SharedGridSize);

    // Создаём renderer
    m_Renderer = new vk3DFluidRenderer();
    // m_Renderer->Initialize();  // TODO: implement

    // Создаём shared RTs
    CreateSharedRenderTargets(m_SharedGridSize);

    // Создаём compute pipelines
    CreatePipelineLayout();
    CreateComputePipelines();

    // Создаём synchronization objects
    CreateSyncObjects();

    // Создаём query pool для profiling
    CreateQueryPool();

    Msg("[Vulkan] 3D Fluid Manager initialized");
}

void vk3DFluidManager::CreateSharedRenderTargets(u32 gridSize)
{
    // 6 глобальных RT, которые переиспользуются между volumes
    VkFormat format = VK_FORMAT_R16G16B16A16_SFLOAT;
    VkImageUsageFlags usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

    m_RT_Velocity1 = new CRT();
    m_RT_Velocity1->Create3D(format, gridSize, gridSize, gridSize, usage);

    m_RT_ColorTemp = new CRT();
    m_RT_ColorTemp->Create3D(format, gridSize, gridSize, gridSize, usage);

    m_RT_Obstacles = new CRT();
    m_RT_Obstacles->Create3D(format, gridSize, gridSize, gridSize, usage);

    m_RT_TempScalar = new CRT();
    m_RT_TempScalar->Create3D(format, gridSize, gridSize, gridSize, usage);

    m_RT_TempVector = new CRT();
    m_RT_TempVector->Create3D(format, gridSize, gridSize, gridSize, usage);

    m_RT_ObstVelocity = new CRT();
    m_RT_ObstVelocity->Create3D(format, gridSize, gridSize, gridSize, usage);

    Msg("[Vulkan] Fluid shared RTs created: %dx%dx%d (~%.1f MB)",
        gridSize, gridSize, gridSize,
        (6 * gridSize * gridSize * gridSize * 8) / (1024.0f * 1024.0f));
}

void vk3DFluidManager::CreatePipelineLayout()
{
    // Descriptor set layout для compute шейдеров
    // Универсальный layout покрывает все 11 compute shaders:
    // - Bindings 0-3: sampler3D (input textures - velocity, density, pressure, etc.)
    // - Bindings 4-5: image3D storage (output textures)

    VkDescriptorSetLayoutBinding bindings[6] = {};

    // Bindings 0-3: Combined image samplers (sampler3D)
    for (u32 i = 0; i < 4; i++) {
        bindings[i].binding = i;
        bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        bindings[i].pImmutableSamplers = nullptr;
    }

    // Bindings 4-5: Storage images (image3D)
    for (u32 i = 4; i < 6; i++) {
        bindings[i].binding = i;
        bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        bindings[i].pImmutableSamplers = nullptr;
    }

    VkDescriptorSetLayoutCreateInfo layoutInfo = {};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 6;
    layoutInfo.pBindings = bindings;

    VkResult result = vkCreateDescriptorSetLayout(VulkanHW.m_Device, &layoutInfo, nullptr, &m_DescriptorSetLayout);
    if (result != VK_SUCCESS) {
        Msg("![Vulkan] Failed to create fluid descriptor set layout: %d", result);
        return;
    }

    // Push constant range для parameters (64 bytes покрывает все шейдеры)
    VkPushConstantRange pushConstantRange = {};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = 64;  // mat4 (64 bytes) максимум

    // Pipeline layout
    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &m_DescriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    result = vkCreatePipelineLayout(VulkanHW.m_Device, &pipelineLayoutInfo, nullptr, &m_PipelineLayout);
    if (result != VK_SUCCESS) {
        Msg("![Vulkan] Failed to create fluid pipeline layout: %d", result);
        return;
    }

    // Descriptor pool для allocation (поддержка до 32 volumes)
    VkDescriptorPoolSize poolSizes[2] = {};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[0].descriptorCount = 4 * 32;  // 4 samplers × 32 sets
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    poolSizes[1].descriptorCount = 2 * 32;  // 2 storage images × 32 sets

    VkDescriptorPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 2;
    poolInfo.pPoolSizes = poolSizes;
    poolInfo.maxSets = 32;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;

    result = vkCreateDescriptorPool(VulkanHW.m_Device, &poolInfo, nullptr, &m_DescriptorPool);
    if (result != VK_SUCCESS) {
        Msg("![Vulkan] Failed to create fluid descriptor pool: %d", result);
        return;
    }

    Msg("[Vulkan] Fluid pipeline layout and descriptor pool created");
}

void vk3DFluidManager::CreateComputePipelines()
{
    const char* shaderPath = "$game_shaders$\\vulkan\\compute\\fluid\\";

    // Загружаем compute shaders (11 штук)
    m_Shader_Advect = new CVulkanComputeShader();
    m_Shader_AdvectBFECC = new CVulkanComputeShader();
    m_Shader_AdvectVelocity = new CVulkanComputeShader();
    m_Shader_Vorticity = new CVulkanComputeShader();
    m_Shader_Confinement = new CVulkanComputeShader();
    m_Shader_Divergence = new CVulkanComputeShader();
    m_Shader_Jacobi = new CVulkanComputeShader();
    m_Shader_Project = new CVulkanComputeShader();
    m_Shader_Obstacles = new CVulkanComputeShader();

    // Load SPIR-V bytecode
    bool success = true;
    string256 buf;
    xr_sprintf(buf, sizeof(buf), "%sadvect.comp.spv", shaderPath);
    success &= m_Shader_Advect->Load(buf);
    xr_sprintf(buf, sizeof(buf), "%sadvect_bfecc.comp.spv", shaderPath);
    success &= m_Shader_AdvectBFECC->Load(buf);
    xr_sprintf(buf, sizeof(buf), "%sadvect_velocity.comp.spv", shaderPath);
    success &= m_Shader_AdvectVelocity->Load(buf);
    xr_sprintf(buf, sizeof(buf), "%svorticity.comp.spv", shaderPath);
    success &= m_Shader_Vorticity->Load(buf);
    xr_sprintf(buf, sizeof(buf), "%sconfinement.comp.spv", shaderPath);
    success &= m_Shader_Confinement->Load(buf);
    xr_sprintf(buf, sizeof(buf), "%sdivergence.comp.spv", shaderPath);
    success &= m_Shader_Divergence->Load(buf);
    xr_sprintf(buf, sizeof(buf), "%sjacobi.comp.spv", shaderPath);
    success &= m_Shader_Jacobi->Load(buf);
    xr_sprintf(buf, sizeof(buf), "%sproject.comp.spv", shaderPath);
    success &= m_Shader_Project->Load(buf);
    xr_sprintf(buf, sizeof(buf), "%sobstacles.comp.spv", shaderPath);
    success &= m_Shader_Obstacles->Load(buf);

    if (!success) {
        Msg("![Vulkan] Failed to load one or more fluid compute shaders");
        return;
    }

    // Создаём pipelines
    m_Pipeline_Advect = new CVulkanComputePipeline();
    m_Pipeline_AdvectBFECC = new CVulkanComputePipeline();
    m_Pipeline_AdvectVelocity = new CVulkanComputePipeline();
    m_Pipeline_Vorticity = new CVulkanComputePipeline();
    m_Pipeline_Confinement = new CVulkanComputePipeline();
    m_Pipeline_Divergence = new CVulkanComputePipeline();
    m_Pipeline_Jacobi = new CVulkanComputePipeline();
    m_Pipeline_Project = new CVulkanComputePipeline();
    m_Pipeline_Obstacles = new CVulkanComputePipeline();

    // Create pipelines с shader modules
    m_Pipeline_Advect->Create(m_Shader_Advect->GetModule(), m_PipelineLayout);
    m_Pipeline_AdvectBFECC->Create(m_Shader_AdvectBFECC->GetModule(), m_PipelineLayout);
    m_Pipeline_AdvectVelocity->Create(m_Shader_AdvectVelocity->GetModule(), m_PipelineLayout);
    m_Pipeline_Vorticity->Create(m_Shader_Vorticity->GetModule(), m_PipelineLayout);
    m_Pipeline_Confinement->Create(m_Shader_Confinement->GetModule(), m_PipelineLayout);
    m_Pipeline_Divergence->Create(m_Shader_Divergence->GetModule(), m_PipelineLayout);
    m_Pipeline_Jacobi->Create(m_Shader_Jacobi->GetModule(), m_PipelineLayout);
    m_Pipeline_Project->Create(m_Shader_Project->GetModule(), m_PipelineLayout);
    m_Pipeline_Obstacles->Create(m_Shader_Obstacles->GetModule(), m_PipelineLayout);

    Msg("[Vulkan] Fluid compute pipelines created (9 pipelines)");
}

void vk3DFluidManager::AddVolume(vk3DFluidData* volume)
{
    if (!volume) {
        return;
    }

    // Проверяем дубликаты
    for (auto v : m_Volumes) {
        if (v == volume) {
            Msg("~[Vulkan] Volume already added to fluid manager");
            return;
        }
    }

    m_Volumes.push_back(volume);
    Msg("[Vulkan] Fluid volume added (total: %d)", m_Volumes.size());
}

void vk3DFluidManager::RemoveVolume(vk3DFluidData* volume)
{
    auto it = std::find(m_Volumes.begin(), m_Volumes.end(), volume);
    if (it != m_Volumes.end()) {
        m_Volumes.erase(it);
        Msg("[Vulkan] Fluid volume removed (total: %d)", m_Volumes.size());
    }
}

void vk3DFluidManager::Update(float dt)
{
    if (m_Volumes.empty()) {
        return;
    }

    // TODO: Получить command buffer для compute
    VkCommandBuffer cmd = VK_NULL_HANDLE;  // Placeholder

    // Update каждого volume
    for (auto volume : m_Volumes) {
        if (volume->IsEnabled()) {
            SimulateVolume(cmd, volume, dt);
        }
    }
}

void vk3DFluidManager::SimulateVolume(VkCommandBuffer cmd, vk3DFluidData* volume, float dt)
{
    if (!cmd || !volume) {
        return;
    }

    BeginProfile(cmd, SLOT_TOTAL_START);

    // Navier-Stokes simulation steps:
    // 1. Advection
    BeginProfile(cmd, SLOT_ADVECT_START);
    AdvectStep(cmd, volume);
    EndProfile(cmd, SLOT_ADVECT_END);
    InsertComputeBarrier(cmd);  // Wait for advection to complete

    // 2. External forces (emitters)
    if (volume->GetEmitters()) {
        volume->GetEmitters()->Render(cmd);
        InsertComputeBarrier(cmd);  // Wait for emitter injection
    }

    // 3. Vorticity confinement
    if (volume->GetSettings().EnableVorticity) {
        BeginProfile(cmd, SLOT_VORTICITY_START);
        VorticityStep(cmd, volume);
        EndProfile(cmd, SLOT_VORTICITY_END);
        InsertComputeBarrier(cmd);  // Wait for vorticity calculation
    }

    // 4. Pressure projection
    BeginProfile(cmd, SLOT_PRESSURE_START);
    PressureStep(cmd, volume);
    EndProfile(cmd, SLOT_PRESSURE_END);
    InsertComputeBarrier(cmd);  // Wait for pressure solve

    // 5. Apply decay
    // TODO: Decay shader dispatch

    EndProfile(cmd, SLOT_TOTAL_END);

    // Final barrier: compute write → graphics read
    // Необходимо перед raycasting в RenderFluid()
    InsertComputeToGraphicsBarrier(cmd);
}

void vk3DFluidManager::AdvectStep(VkCommandBuffer cmd, vk3DFluidData* volume)
{
    u32 gridSize = volume->GetGridSize();
    u32 workgroupSize = 8;  // Shader uses 8x8x8 local size
    u32 numGroups = (gridSize + workgroupSize - 1) / workgroupSize;

    const FluidSettings& settings = volume->GetSettings();

    // Get descriptor set для volume
    VkDescriptorSet descSet = GetVolumeDescriptorSet(volume);
    if (descSet == VK_NULL_HANDLE) {
        return;
    }

    // 1. Advect velocity (self-advection)
    {
        // Update descriptor bindings: velocityTex(input) → velocity1(output)
        CRT* inputs[1] = { volume->GetVelocityRT() };
        UpdateComputeDescriptorSet(descSet, inputs, m_RT_Velocity1, 1);

        // Bind descriptor set
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                                m_PipelineLayout, 0, 1, &descSet, 0, nullptr);

        // Push constants (timeStep, dissipation, gravityBuoyancy, padding)
        struct { float timeStep, dissipation, gravityBuoyancy, padding; } params;
        params.timeStep = settings.TimeStep;
        params.dissipation = 1.0f - settings.Decay;  // Convert decay to dissipation
        params.gravityBuoyancy = settings.Buoyancy;
        params.padding = 0.0f;

        vkCmdPushConstants(cmd, m_PipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT,
                           0, sizeof(params), &params);

        // Dispatch
        if (m_Pipeline_AdvectVelocity && m_Pipeline_AdvectVelocity->IsValid()) {
            m_Pipeline_AdvectVelocity->Dispatch(cmd, numGroups, numGroups, numGroups);
        }

        // Copy result: velocity1 → velocity0
        InsertComputeBarrier(cmd);
        CopyRT3D(cmd, m_RT_Velocity1, volume->GetVelocityRT());
    }

    InsertComputeBarrier(cmd);

    // 2. Advect density/color
    {
        // Update descriptor bindings: velocityTex, colorTex(input) → colorTemp(output)
        CRT* inputs[2] = { volume->GetVelocityRT(), volume->GetColorRT() };
        UpdateComputeDescriptorSet(descSet, inputs, m_RT_ColorTemp, 2);

        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                                m_PipelineLayout, 0, 1, &descSet, 0, nullptr);

        // Push constants
        struct { float timeStep, dissipation, forward, padding; } params;
        params.timeStep = settings.TimeStep;
        params.dissipation = settings.Decay;
        params.forward = 1.0f;  // Forward advection
        params.padding = 0.0f;

        vkCmdPushConstants(cmd, m_PipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT,
                           0, sizeof(params), &params);

        if (settings.EnableBFECC) {
            // BFECC требует 3 прохода
            // Simplified: используем один проход
            if (m_Pipeline_AdvectBFECC && m_Pipeline_AdvectBFECC->IsValid()) {
                m_Pipeline_AdvectBFECC->Dispatch(cmd, numGroups, numGroups, numGroups);
            }
        } else {
            // Simple Semi-Lagrangian
            if (m_Pipeline_Advect && m_Pipeline_Advect->IsValid()) {
                m_Pipeline_Advect->Dispatch(cmd, numGroups, numGroups, numGroups);
            }
        }

        // Copy result: colorTemp → color
        InsertComputeBarrier(cmd);
        CopyRT3D(cmd, m_RT_ColorTemp, volume->GetColorRT());
    }
}

void vk3DFluidManager::VorticityStep(VkCommandBuffer cmd, vk3DFluidData* volume)
{
    u32 gridSize = volume->GetGridSize();
    u32 workgroupSize = 8;
    u32 numGroups = (gridSize + workgroupSize - 1) / workgroupSize;

    VkDescriptorSet descSet = GetVolumeDescriptorSet(volume);
    if (descSet == VK_NULL_HANDLE) {
        return;
    }

    const FluidSettings& settings = volume->GetSettings();

    // 1. Compute vorticity (curl of velocity)
    {
        // Bind: velocityTex(input) → vorticityOut(tempVector)
        CRT* inputs[1] = { volume->GetVelocityRT() };
        UpdateComputeDescriptorSet(descSet, inputs, m_RT_TempVector, 1);

        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                                m_PipelineLayout, 0, 1, &descSet, 0, nullptr);

        if (m_Pipeline_Vorticity && m_Pipeline_Vorticity->IsValid()) {
            m_Pipeline_Vorticity->Dispatch(cmd, numGroups, numGroups, numGroups);
        }
    }

    InsertComputeBarrier(cmd);

    // 2. Apply confinement force
    {
        // Bind: velocityTex, vorticityTex(tempVector) → velocityOut(velocity1)
        CRT* inputs[2] = { volume->GetVelocityRT(), m_RT_TempVector };
        UpdateComputeDescriptorSet(descSet, inputs, m_RT_Velocity1, 2);

        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                                m_PipelineLayout, 0, 1, &descSet, 0, nullptr);

        // Push constants (timeStep, epsilon, padding)
        struct { float timeStep, epsilon, padding1, padding2; } params;
        params.timeStep = settings.TimeStep;
        params.epsilon = settings.VorticityStrength;
        params.padding1 = 0.0f;
        params.padding2 = 0.0f;

        vkCmdPushConstants(cmd, m_PipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT,
                           0, sizeof(params), &params);

        if (m_Pipeline_Confinement && m_Pipeline_Confinement->IsValid()) {
            m_Pipeline_Confinement->Dispatch(cmd, numGroups, numGroups, numGroups);
        }

        // Copy result: velocity1 → velocity
        InsertComputeBarrier(cmd);
        CopyRT3D(cmd, m_RT_Velocity1, volume->GetVelocityRT());
    }
}

void vk3DFluidManager::PressureStep(VkCommandBuffer cmd, vk3DFluidData* volume)
{
    u32 gridSize = volume->GetGridSize();
    u32 workgroupSize = 8;
    u32 numGroups = (gridSize + workgroupSize - 1) / workgroupSize;

    VkDescriptorSet descSet = GetVolumeDescriptorSet(volume);
    if (descSet == VK_NULL_HANDLE) {
        return;
    }

    const FluidSettings& settings = volume->GetSettings();

    // 1. Compute divergence of velocity
    {
        // Bind: velocityTex → divergenceOut(tempScalar)
        CRT* inputs[1] = { volume->GetVelocityRT() };
        UpdateComputeDescriptorSet(descSet, inputs, m_RT_TempScalar, 1);

        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                                m_PipelineLayout, 0, 1, &descSet, 0, nullptr);

        if (m_Pipeline_Divergence && m_Pipeline_Divergence->IsValid()) {
            m_Pipeline_Divergence->Dispatch(cmd, numGroups, numGroups, numGroups);
        }
    }
    InsertComputeBarrier(cmd);

    // 2. Jacobi iterations для Poisson equation (∇²p = div(v))
    {
        // Push constants для Jacobi
        struct { float alpha, rBeta, padding1, padding2; } params;
        params.alpha = -1.0f;        // Laplacian coefficient
        params.rBeta = 1.0f / 6.0f;  // 1/(2*numDimensions)
        params.padding1 = 0.0f;
        params.padding2 = 0.0f;

        u32 iterations = settings.JacobiIterations;
        for (u32 i = 0; i < iterations; i++) {
            // Ping-pong: read from pressureRT, write to tempScalar
            // Then swap for next iteration
            CRT* pressureRead = (i % 2 == 0) ? volume->GetPressureRT() : m_RT_TempScalar;
            CRT* pressureWrite = (i % 2 == 0) ? m_RT_TempScalar : volume->GetPressureRT();

            // Bind: pressureTex, divergenceTex → pressureOut
            CRT* inputs[2] = { pressureRead, m_RT_TempScalar /* divergence */ };
            UpdateComputeDescriptorSet(descSet, inputs, pressureWrite, 2);

            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                                    m_PipelineLayout, 0, 1, &descSet, 0, nullptr);

            vkCmdPushConstants(cmd, m_PipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT,
                               0, sizeof(params), &params);

            if (m_Pipeline_Jacobi && m_Pipeline_Jacobi->IsValid()) {
                m_Pipeline_Jacobi->Dispatch(cmd, numGroups, numGroups, numGroups);
            }

            // CRITICAL: Barrier после каждой итерации
            InsertComputeBarrier(cmd);
        }
    }

    // 3. Subtract pressure gradient from velocity (projection)
    {
        // Bind: velocityTex, pressureTex → velocityOut(velocity1)
        CRT* inputs[2] = { volume->GetVelocityRT(), volume->GetPressureRT() };
        UpdateComputeDescriptorSet(descSet, inputs, m_RT_Velocity1, 2);

        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                                m_PipelineLayout, 0, 1, &descSet, 0, nullptr);

        if (m_Pipeline_Project && m_Pipeline_Project->IsValid()) {
            m_Pipeline_Project->Dispatch(cmd, numGroups, numGroups, numGroups);
        }

        // Copy result: velocity1 → velocity (финальная projected velocity)
        InsertComputeBarrier(cmd);
        CopyRT3D(cmd, m_RT_Velocity1, volume->GetVelocityRT());
    }
}

void vk3DFluidManager::RenderFluid(VkCommandBuffer cmd)
{
    if (m_Volumes.empty() || !m_Renderer) {
        return;
    }

    // Рендерим каждый volume через renderer
    for (auto volume : m_Volumes) {
        if (volume->IsEnabled()) {
            // m_Renderer->RenderVolume(cmd, volume);  // TODO: implement
        }
    }
}

void vk3DFluidManager::CreateSyncObjects()
{
    // Create semaphore for compute → graphics synchronization
    VkSemaphoreCreateInfo semaphoreInfo = {};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkResult result = vkCreateSemaphore(VulkanHW.m_Device, &semaphoreInfo, nullptr, &m_ComputeFinishedSemaphore);
    if (result != VK_SUCCESS) {
        Msg("![Vulkan] Failed to create compute semaphore: %d", result);
        return;
    }

    // Create fence for CPU/GPU synchronization
    VkFenceCreateInfo fenceInfo = {};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;  // Start signaled

    result = vkCreateFence(VulkanHW.m_Device, &fenceInfo, nullptr, &m_ComputeFence);
    if (result != VK_SUCCESS) {
        Msg("![Vulkan] Failed to create compute fence: %d", result);
        return;
    }

    Msg("[Vulkan] Fluid sync objects created");
}

void vk3DFluidManager::DestroySyncObjects()
{
    if (m_ComputeFinishedSemaphore != VK_NULL_HANDLE) {
        vkDestroySemaphore(VulkanHW.m_Device, m_ComputeFinishedSemaphore, nullptr);
        m_ComputeFinishedSemaphore = VK_NULL_HANDLE;
    }

    if (m_ComputeFence != VK_NULL_HANDLE) {
        vkDestroyFence(VulkanHW.m_Device, m_ComputeFence, nullptr);
        m_ComputeFence = VK_NULL_HANDLE;
    }
}

void vk3DFluidManager::InsertComputeBarrier(VkCommandBuffer cmd)
{
    // Memory barrier для compute shader write → compute shader read
    // Используется между этапами симуляции (например, advect → vorticity)
    VkMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(
        cmd,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,  // srcStageMask
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,  // dstStageMask
        0,                                      // dependencyFlags
        1, &barrier,                            // memoryBarriers
        0, nullptr,                             // bufferMemoryBarriers
        0, nullptr                              // imageMemoryBarriers
    );
}

void vk3DFluidManager::InsertComputeToGraphicsBarrier(VkCommandBuffer cmd)
{
    // Memory barrier для compute shader write → fragment shader read
    // Используется после завершения симуляции перед raycasting
    VkMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(
        cmd,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,   // srcStageMask
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,  // dstStageMask
        0,                                       // dependencyFlags
        1, &barrier,                             // memoryBarriers
        0, nullptr,                              // bufferMemoryBarriers
        0, nullptr                               // imageMemoryBarriers
    );
}

void vk3DFluidManager::Destroy()
{
    // Clear RT pool first
    ClearRTPool();

    // Destroy query pool
    DestroyQueryPool();

    // Destroy sync objects
    DestroySyncObjects();

    // Destroy pipelines
    if (m_Pipeline_Advect) { m_Pipeline_Advect->Destroy(); delete m_Pipeline_Advect; m_Pipeline_Advect = nullptr; }
    if (m_Pipeline_AdvectBFECC) { m_Pipeline_AdvectBFECC->Destroy(); delete m_Pipeline_AdvectBFECC; m_Pipeline_AdvectBFECC = nullptr; }
    if (m_Pipeline_AdvectVelocity) { m_Pipeline_AdvectVelocity->Destroy(); delete m_Pipeline_AdvectVelocity; m_Pipeline_AdvectVelocity = nullptr; }
    if (m_Pipeline_Vorticity) { m_Pipeline_Vorticity->Destroy(); delete m_Pipeline_Vorticity; m_Pipeline_Vorticity = nullptr; }
    if (m_Pipeline_Confinement) { m_Pipeline_Confinement->Destroy(); delete m_Pipeline_Confinement; m_Pipeline_Confinement = nullptr; }
    if (m_Pipeline_Divergence) { m_Pipeline_Divergence->Destroy(); delete m_Pipeline_Divergence; m_Pipeline_Divergence = nullptr; }
    if (m_Pipeline_Jacobi) { m_Pipeline_Jacobi->Destroy(); delete m_Pipeline_Jacobi; m_Pipeline_Jacobi = nullptr; }
    if (m_Pipeline_Project) { m_Pipeline_Project->Destroy(); delete m_Pipeline_Project; m_Pipeline_Project = nullptr; }
    if (m_Pipeline_Obstacles) { m_Pipeline_Obstacles->Destroy(); delete m_Pipeline_Obstacles; m_Pipeline_Obstacles = nullptr; }

    // Destroy shaders
    if (m_Shader_Advect) { m_Shader_Advect->Destroy(); delete m_Shader_Advect; m_Shader_Advect = nullptr; }
    if (m_Shader_AdvectBFECC) { m_Shader_AdvectBFECC->Destroy(); delete m_Shader_AdvectBFECC; m_Shader_AdvectBFECC = nullptr; }
    if (m_Shader_AdvectVelocity) { m_Shader_AdvectVelocity->Destroy(); delete m_Shader_AdvectVelocity; m_Shader_AdvectVelocity = nullptr; }
    if (m_Shader_Vorticity) { m_Shader_Vorticity->Destroy(); delete m_Shader_Vorticity; m_Shader_Vorticity = nullptr; }
    if (m_Shader_Confinement) { m_Shader_Confinement->Destroy(); delete m_Shader_Confinement; m_Shader_Confinement = nullptr; }
    if (m_Shader_Divergence) { m_Shader_Divergence->Destroy(); delete m_Shader_Divergence; m_Shader_Divergence = nullptr; }
    if (m_Shader_Jacobi) { m_Shader_Jacobi->Destroy(); delete m_Shader_Jacobi; m_Shader_Jacobi = nullptr; }
    if (m_Shader_Project) { m_Shader_Project->Destroy(); delete m_Shader_Project; m_Shader_Project = nullptr; }
    if (m_Shader_Obstacles) { m_Shader_Obstacles->Destroy(); delete m_Shader_Obstacles; m_Shader_Obstacles = nullptr; }

    // Destroy descriptor pool (automatically frees all descriptor sets)
    if (m_DescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(VulkanHW.m_Device, m_DescriptorPool, nullptr);
        m_DescriptorPool = VK_NULL_HANDLE;
    }
    m_VolumeDescriptorSets.clear();

    // Destroy descriptor set layout и pipeline layout
    if (m_DescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(VulkanHW.m_Device, m_DescriptorSetLayout, nullptr);
        m_DescriptorSetLayout = VK_NULL_HANDLE;
    }

    if (m_PipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(VulkanHW.m_Device, m_PipelineLayout, nullptr);
        m_PipelineLayout = VK_NULL_HANDLE;
    }

    // Destroy shared RTs
    if (m_RT_Velocity1) { m_RT_Velocity1->Destroy(); delete m_RT_Velocity1; m_RT_Velocity1 = nullptr; }
    if (m_RT_ColorTemp) { m_RT_ColorTemp->Destroy(); delete m_RT_ColorTemp; m_RT_ColorTemp = nullptr; }
    if (m_RT_Obstacles) { m_RT_Obstacles->Destroy(); delete m_RT_Obstacles; m_RT_Obstacles = nullptr; }
    if (m_RT_TempScalar) { m_RT_TempScalar->Destroy(); delete m_RT_TempScalar; m_RT_TempScalar = nullptr; }
    if (m_RT_TempVector) { m_RT_TempVector->Destroy(); delete m_RT_TempVector; m_RT_TempVector = nullptr; }
    if (m_RT_ObstVelocity) { m_RT_ObstVelocity->Destroy(); delete m_RT_ObstVelocity; m_RT_ObstVelocity = nullptr; }

    // Destroy grid
    if (m_Grid) {
        m_Grid->Destroy();
        delete m_Grid;
        m_Grid = nullptr;
    }

    // Destroy renderer
    if (m_Renderer) {
        // m_Renderer->Destroy();  // TODO: implement
        delete m_Renderer;
        m_Renderer = nullptr;
    }

    m_Volumes.clear();

    Msg("[Vulkan] 3D Fluid Manager destroyed");
}

CRT* vk3DFluidManager::AllocateRT(VkFormat format, u32 w, u32 h, u32 d)
{
    // Поиск подходящего RT в pool
    for (auto& entry : m_RTPool) {
        if (!entry.inUse &&
            entry.format == format &&
            entry.width == w &&
            entry.height == h &&
            entry.depth == d)
        {
            // Нашли подходящий RT
            entry.inUse = true;
            return entry.rt;
        }
    }

    // Pool пуст или нет подходящего RT - создаём новый
    CRT* newRT = new CRT();
    newRT->Create3D(format, w, h, d, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);

    RTPoolEntry entry;
    entry.rt = newRT;
    entry.inUse = true;
    entry.format = format;
    entry.width = w;
    entry.height = h;
    entry.depth = d;

    m_RTPool.push_back(entry);

    Msg("[Vulkan] RT allocated from pool (total pool size: %d)", m_RTPool.size());
    return newRT;
}

void vk3DFluidManager::ReleaseRT(CRT* rt)
{
    if (!rt) return;

    // Находим RT в pool и помечаем как свободный
    for (auto& entry : m_RTPool) {
        if (entry.rt == rt) {
            entry.inUse = false;
            return;
        }
    }

    // RT не из pool - это ошибка
    Msg("![Vulkan] Attempted to release RT not from pool");
}

void vk3DFluidManager::ClearRTPool()
{
    // Уничтожаем все RT в pool
    for (auto& entry : m_RTPool) {
        if (entry.rt) {
            entry.rt->Destroy();
            delete entry.rt;
        }
    }

    m_RTPool.clear();
    Msg("[Vulkan] RT pool cleared");
}

void vk3DFluidManager::CreateQueryPool()
{
    VkQueryPoolCreateInfo queryPoolInfo = {};
    queryPoolInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    queryPoolInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
    queryPoolInfo.queryCount = MAX_QUERY_SLOTS;

    VkResult result = vkCreateQueryPool(VulkanHW.m_Device, &queryPoolInfo, nullptr, &m_QueryPool);
    if (result != VK_SUCCESS) {
        Msg("![Vulkan] Failed to create query pool: %d", result);
        return;
    }

    Msg("[Vulkan] Query pool created (%d slots)", MAX_QUERY_SLOTS);
}

void vk3DFluidManager::DestroyQueryPool()
{
    if (m_QueryPool != VK_NULL_HANDLE) {
        vkDestroyQueryPool(VulkanHW.m_Device, m_QueryPool, nullptr);
        m_QueryPool = VK_NULL_HANDLE;
    }
}

void vk3DFluidManager::BeginProfile(VkCommandBuffer cmd, u32 slot)
{
    if (m_QueryPool == VK_NULL_HANDLE || slot >= MAX_QUERY_SLOTS) {
        return;
    }

    // Reset query перед использованием
    vkCmdResetQueryPool(cmd, m_QueryPool, slot, 1);

    // Write timestamp
    vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, m_QueryPool, slot);
}

void vk3DFluidManager::EndProfile(VkCommandBuffer cmd, u32 slot)
{
    if (m_QueryPool == VK_NULL_HANDLE || slot >= MAX_QUERY_SLOTS) {
        return;
    }

    // Write timestamp
    vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, m_QueryPool, slot);
}

void vk3DFluidManager::UpdateProfilingStats()
{
    if (m_QueryPool == VK_NULL_HANDLE) {
        return;
    }

    // Read timestamp query results
    u64 timestamps[8];
    VkResult result = vkGetQueryPoolResults(
        VulkanHW.m_Device,
        m_QueryPool,
        0, 8,  // First 8 slots
        sizeof(timestamps),
        timestamps,
        sizeof(u64),
        VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT
    );

    if (result != VK_SUCCESS) {
        return;
    }

    // Get GPU timestamp period (nanoseconds per tick)
    VkPhysicalDeviceProperties deviceProps;
    vkGetPhysicalDeviceProperties(VulkanHW.m_PhysicalDevice, &deviceProps);
    float timestampPeriod = deviceProps.limits.timestampPeriod;

    // Вычисляем время в миллисекундах
    m_Stats.advectTime = (timestamps[SLOT_ADVECT_END] - timestamps[SLOT_ADVECT_START]) * timestampPeriod / 1000000.0f;
    m_Stats.vorticityTime = (timestamps[SLOT_VORTICITY_END] - timestamps[SLOT_VORTICITY_START]) * timestampPeriod / 1000000.0f;
    m_Stats.pressureTime = (timestamps[SLOT_PRESSURE_END] - timestamps[SLOT_PRESSURE_START]) * timestampPeriod / 1000000.0f;
    m_Stats.totalSimTime = (timestamps[SLOT_TOTAL_END] - timestamps[SLOT_TOTAL_START]) * timestampPeriod / 1000000.0f;
}

VkDescriptorSet vk3DFluidManager::GetVolumeDescriptorSet(vk3DFluidData* volume)
{
    // Проверяем есть ли уже descriptor set для этого volume
    auto it = m_VolumeDescriptorSets.find(volume);
    if (it != m_VolumeDescriptorSets.end()) {
        return it->second;
    }

    // Allocate новый descriptor set
    VkDescriptorSetAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_DescriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &m_DescriptorSetLayout;

    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    VkResult result = vkAllocateDescriptorSets(VulkanHW.m_Device, &allocInfo, &descriptorSet);
    if (result != VK_SUCCESS) {
        Msg("![Vulkan] Failed to allocate descriptor set for fluid volume: %d", result);
        return VK_NULL_HANDLE;
    }

    // Сохраняем в map
    m_VolumeDescriptorSets[volume] = descriptorSet;

    return descriptorSet;
}

void vk3DFluidManager::UpdateComputeDescriptorSet(VkDescriptorSet descriptorSet,
                                                   CRT** inputTextures,
                                                   CRT* outputTexture,
                                                   u32 numInputs)
{
    if (descriptorSet == VK_NULL_HANDLE || !inputTextures) {
        return;
    }

    xr_vector<VkDescriptorImageInfo> imageInfos;
    xr_vector<VkWriteDescriptorSet> writes;

    // Bind input sampler3D textures (bindings 0-3)
    for (u32 i = 0; i < numInputs && i < 4; i++) {
        if (!inputTextures[i]) continue;

        VkDescriptorImageInfo imageInfo = {};
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        imageInfo.imageView = inputTextures[i]->GetImageView();
        imageInfo.sampler = inputTextures[i]->GetSampler();
        imageInfos.push_back(imageInfo);

        VkWriteDescriptorSet write = {};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = descriptorSet;
        write.dstBinding = i;
        write.dstArrayElement = 0;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.descriptorCount = 1;
        write.pImageInfo = &imageInfos[imageInfos.size() - 1];
        writes.push_back(write);
    }

    // Bind output storage image3D (binding 4)
    if (outputTexture) {
        VkDescriptorImageInfo imageInfo = {};
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        imageInfo.imageView = outputTexture->GetImageView();
        imageInfo.sampler = VK_NULL_HANDLE;  // Storage images don't need sampler
        imageInfos.push_back(imageInfo);

        VkWriteDescriptorSet write = {};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = descriptorSet;
        write.dstBinding = 4;
        write.dstArrayElement = 0;
        write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        write.descriptorCount = 1;
        write.pImageInfo = &imageInfos[imageInfos.size() - 1];
        writes.push_back(write);
    }

    // Update descriptor sets
    vkUpdateDescriptorSets(VulkanHW.m_Device, writes.size(), writes.data(), 0, nullptr);
}

void vk3DFluidManager::CopyRT3D(VkCommandBuffer cmd, CRT* src, CRT* dst)
{
    if (!cmd || !src || !dst) {
        return;
    }

    // 3D texture copy region
    VkImageCopy copyRegion = {};
    copyRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copyRegion.srcSubresource.mipLevel = 0;
    copyRegion.srcSubresource.baseArrayLayer = 0;
    copyRegion.srcSubresource.layerCount = 1;
    copyRegion.srcOffset = {0, 0, 0};

    copyRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copyRegion.dstSubresource.mipLevel = 0;
    copyRegion.dstSubresource.baseArrayLayer = 0;
    copyRegion.dstSubresource.layerCount = 1;
    copyRegion.dstOffset = {0, 0, 0};

    copyRegion.extent.width = src->GetWidth();
    copyRegion.extent.height = src->GetHeight();
    copyRegion.extent.depth = src->GetDepth();

    // Copy image (both should be in VK_IMAGE_LAYOUT_GENERAL)
    vkCmdCopyImage(
        cmd,
        src->GetImage(),
        VK_IMAGE_LAYOUT_GENERAL,
        dst->GetImage(),
        VK_IMAGE_LAYOUT_GENERAL,
        1,
        &copyRegion
    );
}

// Глобальный instance
vk3DFluidManager g_FluidManager;

} // namespace VK
