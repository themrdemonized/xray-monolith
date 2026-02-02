// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// SPDX-License-Identifier: MIT

#pragma once
#include "../vk_core.h"
#include "../vk_compute.h"
#include "../SH_RT_Vulkan.h"
#include "vk3DFluidData.h"
#include "vk3DFluidGrid.h"

namespace VK
{

// Forward declaration
class vk3DFluidRenderer;

/**
 * 3D Fluid Manager
 *
 * Главный оркестратор fluid симуляции:
 * - Управление глобальными RT (shared между всеми volumes)
 * - 9 compute pipelines для Navier-Stokes симуляции
 * - Update() - выполняет симуляцию для всех активных volumes
 * - RenderFluid() - вызывает renderer для всех volumes
 *
 * Алгоритм Navier-Stokes:
 * 1. Advection (перенос velocity и density)
 * 2. External forces (emitters)
 * 3. Vorticity confinement (завихрения)
 * 4. Divergence → Jacobi solver → Projection (несжимаемость)
 */
class vk3DFluidManager
{
public:
    vk3DFluidManager();
    ~vk3DFluidManager();

    /**
     * Инициализация manager
     */
    void Initialize();

    /**
     * Добавить volume для симуляции
     */
    void AddVolume(vk3DFluidData* volume);

    /**
     * Удалить volume
     */
    void RemoveVolume(vk3DFluidData* volume);

    /**
     * Update симуляции для всех volumes
     * @param dt Delta time
     */
    void Update(float dt);

    /**
     * Рендерить все volumes
     */
    void RenderFluid(VkCommandBuffer cmd);

    /**
     * Уничтожить manager
     */
    void Destroy();

    struct ProfilingStats {
        float advectTime;      // ms
        float vorticityTime;   // ms
        float pressureTime;    // ms
        float totalSimTime;    // ms
    };

    // Accessors
    vk3DFluidGrid* GetGrid() { return m_Grid; }
    vk3DFluidRenderer* GetRenderer() { return m_Renderer; }
    const ProfilingStats& GetProfilingStats() const { return m_Stats; }

private:
    /**
     * Создать глобальные shared RTs
     */
    void CreateSharedRenderTargets(u32 gridSize);

    /**
     * Создать compute pipelines для симуляции
     */
    void CreateComputePipelines();

    /**
     * Создать pipeline layout для compute шейдеров
     */
    void CreatePipelineLayout();

    /**
     * Выполнить один шаг симуляции для volume
     */
    void SimulateVolume(VkCommandBuffer cmd, vk3DFluidData* volume, float dt);

    /**
     * Advection step (Semi-Lagrangian или BFECC)
     */
    void AdvectStep(VkCommandBuffer cmd, vk3DFluidData* volume);

    /**
     * Vorticity confinement step
     */
    void VorticityStep(VkCommandBuffer cmd, vk3DFluidData* volume);

    /**
     * Pressure projection step (Jacobi solver)
     */
    void PressureStep(VkCommandBuffer cmd, vk3DFluidData* volume);

private:
    // Volumes под управлением
    xr_vector<vk3DFluidData*> m_Volumes;

    // Shared components
    vk3DFluidGrid*     m_Grid = nullptr;
    vk3DFluidRenderer* m_Renderer = nullptr;

    // Глобальные shared render targets (переиспользуются между volumes)
    CRT* m_RT_Velocity1 = nullptr;      // Ping-pong с volume's velocity0
    CRT* m_RT_ColorTemp = nullptr;      // Temp для advection
    CRT* m_RT_Obstacles = nullptr;      // Obstacle geometry
    CRT* m_RT_TempScalar = nullptr;     // Temp для divergence/pressure
    CRT* m_RT_TempVector = nullptr;     // Temp для vorticity
    CRT* m_RT_ObstVelocity = nullptr;   // Obstacle velocity

    // Compute pipelines (9 штук)
    CVulkanComputePipeline* m_Pipeline_Advect = nullptr;
    CVulkanComputePipeline* m_Pipeline_AdvectBFECC = nullptr;
    CVulkanComputePipeline* m_Pipeline_AdvectVelocity = nullptr;
    CVulkanComputePipeline* m_Pipeline_Vorticity = nullptr;
    CVulkanComputePipeline* m_Pipeline_Confinement = nullptr;
    CVulkanComputePipeline* m_Pipeline_Divergence = nullptr;
    CVulkanComputePipeline* m_Pipeline_Jacobi = nullptr;
    CVulkanComputePipeline* m_Pipeline_Project = nullptr;
    CVulkanComputePipeline* m_Pipeline_Obstacles = nullptr;

    // Compute shaders
    CVulkanComputeShader* m_Shader_Advect = nullptr;
    CVulkanComputeShader* m_Shader_AdvectBFECC = nullptr;
    CVulkanComputeShader* m_Shader_AdvectVelocity = nullptr;
    CVulkanComputeShader* m_Shader_Vorticity = nullptr;
    CVulkanComputeShader* m_Shader_Confinement = nullptr;
    CVulkanComputeShader* m_Shader_Divergence = nullptr;
    CVulkanComputeShader* m_Shader_Jacobi = nullptr;
    CVulkanComputeShader* m_Shader_Project = nullptr;
    CVulkanComputeShader* m_Shader_Obstacles = nullptr;

    // Pipeline layout
    VkPipelineLayout m_PipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_DescriptorSetLayout = VK_NULL_HANDLE;

    // Descriptor pool для allocation
    VkDescriptorPool m_DescriptorPool = VK_NULL_HANDLE;

    // Descriptor sets для каждого volume (allocated on demand)
    xr_map<vk3DFluidData*, VkDescriptorSet> m_VolumeDescriptorSets;

    // Grid size for shared RTs
    u32 m_SharedGridSize = 128;  // По умолчанию 128³

    // Synchronization primitives
    VkSemaphore m_ComputeFinishedSemaphore = VK_NULL_HANDLE;  // Signals compute completion
    VkFence m_ComputeFence = VK_NULL_HANDLE;                  // Fence for CPU/GPU sync

    /**
     * Create synchronization objects
     */
    void CreateSyncObjects();

    /**
     * Destroy synchronization objects
     */
    void DestroySyncObjects();

    /**
     * Insert memory barrier for compute shader write → compute shader read
     */
    void InsertComputeBarrier(VkCommandBuffer cmd);

    /**
     * Insert memory barrier for compute write → graphics read
     */
    void InsertComputeToGraphicsBarrier(VkCommandBuffer cmd);

    // RT Memory Pooling (Phase 5.2)
    struct RTPoolEntry {
        CRT* rt;
        bool inUse;
        VkFormat format;
        u32 width, height, depth;
    };

    xr_vector<RTPoolEntry> m_RTPool;

    /**
     * Allocate RT from pool (creates new if pool empty)
     * @param format Texture format
     * @param w Width
     * @param h Height
     * @param d Depth
     * @return Allocated RT
     */
    CRT* AllocateRT(VkFormat format, u32 w, u32 h, u32 d);

    /**
     * Release RT back to pool
     * @param rt RT to release
     */
    void ReleaseRT(CRT* rt);

    /**
     * Clear RT pool (destroy all pooled RTs)
     */
    void ClearRTPool();

    // Performance Profiling (Phase 5.3)
    VkQueryPool m_QueryPool = VK_NULL_HANDLE;
    static const u32 MAX_QUERY_SLOTS = 32;  // Slots for GPU timestamps

    enum ProfilerSlots {
        SLOT_ADVECT_START = 0,
        SLOT_ADVECT_END = 1,
        SLOT_VORTICITY_START = 2,
        SLOT_VORTICITY_END = 3,
        SLOT_PRESSURE_START = 4,
        SLOT_PRESSURE_END = 5,
        SLOT_TOTAL_START = 6,
        SLOT_TOTAL_END = 7
    };

    ProfilingStats m_Stats;

    /**
     * Create query pool for GPU profiling
     */
    void CreateQueryPool();

    /**
     * Destroy query pool
     */
    void DestroyQueryPool();

    /**
     * Begin profiling section
     */
    void BeginProfile(VkCommandBuffer cmd, u32 slot);

    /**
     * End profiling section
     */
    void EndProfile(VkCommandBuffer cmd, u32 slot);

    /**
     * Retrieve profiling results
     */
    void UpdateProfilingStats();

    /**
     * Get or create descriptor set для volume
     */
    VkDescriptorSet GetVolumeDescriptorSet(vk3DFluidData* volume);

    /**
     * Update descriptor set bindings для compute shader
     * @param descriptorSet Descriptor set to update
     * @param inputTextures Array of input sampler3D textures (bindings 0-3)
     * @param outputTexture Output storage image3D texture (binding 4)
     * @param numInputs Number of input textures (1-4)
     */
    void UpdateComputeDescriptorSet(VkDescriptorSet descriptorSet,
                                     CRT** inputTextures,
                                     CRT* outputTexture,
                                     u32 numInputs);

    /**
     * Copy 3D texture (для ping-pong buffering)
     * @param cmd Command buffer
     * @param src Source RT
     * @param dst Destination RT
     */
    void CopyRT3D(VkCommandBuffer cmd, CRT* src, CRT* dst);
};

// Глобальный instance
extern vk3DFluidManager g_FluidManager;

} // namespace VK
