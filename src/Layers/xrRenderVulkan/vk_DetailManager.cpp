// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// SPDX-License-Identifier: MIT

#include "stdafx.h"
#include "vk_DetailManager.h"
#include "rvk.h"
#include "HW_Vulkan.h"         // VulkanHW
#include "../xrRender/DetailFormat.h"
#include "vk_shaders.h"       // g_ShaderManager
#include "vk_pipeline.h"      // g_PipelineManager
#include "vk_swapchain.h"     // Swapchain
#include "vk_material.h"      // g_MaterialManager
#include "../../xrEngine/IGame_Persistent.h"
#include "../../xrEngine/IGame_Level.h"
#include "../../xrEngine/Environment.h"

namespace VK
{

// ============================================================================
// bwdithermap - Generate 16x16 dither matrix from 4x4 magic pattern
// Ported from DetailManager.cpp
// ============================================================================
static int magic4x4[4][4] =
{
    {0, 14, 3, 13},
    {11, 5, 8, 6},
    {12, 2, 15, 1},
    {7, 9, 4, 10}
};

void bwdithermap(int levels, int magic[16][16])
{
    float N = 255.0f / (levels - 1);
    float magicfact = (N - 1) / 16;
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            for (int k = 0; k < 4; k++)
                for (int l = 0; l < 4; l++)
                    magic[4 * k + i][4 * l + j] =
                        (int)(0.5 + magic4x4[i][j] * magicfact +
                            (magic4x4[k][l] / 16.) * magicfact);
}

// Note: DETAIL_RADIUS variables (dm_size, dm_cache1_line, etc.) and
// ps_current_detail_density/ps_current_detail_height are defined in vk_console.cpp.
// The header declares them as extern.

// ============================================================================
// CDetailManager::SSwingValue - Wind animation
// ============================================================================
void CDetailManager::SSwingValue::lerp(const SSwingValue& v1, const SSwingValue& v2, float factor)
{
    float inv = 1.0f - factor;
    rot1 = v1.rot1 * inv + v2.rot1 * factor;
    rot2 = v1.rot2 * inv + v2.rot2 * factor;
    amp1 = v1.amp1 * inv + v2.amp1 * factor;
    amp2 = v1.amp2 * inv + v2.amp2 * factor;
    speed = v1.speed * inv + v2.speed * factor;
}

// ============================================================================
// CDetailManager - Constructor/Destructor
// ============================================================================
CDetailManager::CDetailManager()
{
    // File data
    dtFS = nullptr;
    dtSlots = nullptr;
    Memory.mem_fill(&dtH, 0, sizeof(dtH));
    Memory.mem_fill(&DS_empty, 0, sizeof(DS_empty));

    // Cache
    cache_cx = 0;
    cache_cz = 0;

#ifdef DETAIL_RADIUS
    cache_level1 = nullptr;
    cache = nullptr;
    cache_pool = nullptr;
#endif

    // Wind animation
    Memory.mem_fill(&swing_desc[0], 0, sizeof(SSwingValue) * 2);
    Memory.mem_fill(&swing_current, 0, sizeof(SSwingValue));
    m_time_rot_1 = 0.0f;
    m_time_rot_2 = 0.0f;
    m_time_pos = 0.0f;
    m_global_time_old = 0.0f;

    // Initialize swing defaults
    swing_desc[0].rot1 = 0.0f;
    swing_desc[0].rot2 = 0.0f;
    swing_desc[0].amp1 = 0.1f;
    swing_desc[0].amp2 = 0.2f;
    swing_desc[0].speed = 1.0f;

    swing_desc[1].rot1 = PI_MUL_2;
    swing_desc[1].rot2 = PI_MUL_2;
    swing_desc[1].amp1 = 0.3f;
    swing_desc[1].amp2 = 0.5f;
    swing_desc[1].speed = 3.0f;

    swing_current = swing_desc[0];

    // MT
    m_frame_calc = u32(-1);
    m_frame_rendered = u32(-1);

    // Vulkan
    m_Pipeline = VK_NULL_HANDLE;
    m_bCreated = false;
    Memory.mem_fill(&m_Constants, 0, sizeof(m_Constants));

    // GPU-driven pipeline
    m_AllInstancesSSBO = nullptr;
    m_VisibleSSBO = nullptr;
    m_IndirectCmdBuf = nullptr;
    m_AtomicCounters = nullptr;
    m_TotalGpuInstances = 0;
    m_GpuDataDirty = false;
    m_ComputeLayout = VK_NULL_HANDLE;
    m_ComputeDescLayout = VK_NULL_HANDLE;
    m_ComputeDescPool = VK_NULL_HANDLE;
    m_ComputeDescSet = VK_NULL_HANDLE;
    m_HZBImage = VK_NULL_HANDLE;
    m_HZBMemory = VK_NULL_HANDLE;
    m_HZBView = VK_NULL_HANDLE;
    m_HZBSampler = VK_NULL_HANDLE;
    m_HZBWidth = 0;
    m_HZBHeight = 0;
    m_HZBMipLevels = 0;
    m_HZBBuildLayout = VK_NULL_HANDLE;
    m_HZBBuildDescLayout = VK_NULL_HANDLE;
    m_HZBBuildDescPool = VK_NULL_HANDLE;
    m_bGpuDrivenEnabled = false; // Disable legacy GPU path (replaced by GPU generation)

    // GPU generation
    m_bGpuGenerationEnabled = true; // Enable new procedural GPU generation by default
    m_HeightmapImage = VK_NULL_HANDLE;
    m_HeightmapMemory = VK_NULL_HANDLE;
    m_HeightmapView = VK_NULL_HANDLE;
    m_HeightmapSampler = VK_NULL_HANDLE;
    m_HeightmapW = 0;
    m_HeightmapH = 0;
    m_HMOriginX = 0;
    m_HMOriginZ = 0;
    m_HMWorldSizeX = 0;
    m_HMWorldSizeZ = 0;
    m_SlotDataSSBO = nullptr;
    m_ObjInfoSSBO = nullptr;
    m_GenUBO = nullptr;
    m_TotalSlots = 0;
    m_GenPipelineLayout = VK_NULL_HANDLE;
    m_GenDescLayout = VK_NULL_HANDLE;
    m_GenDescPool = VK_NULL_HANDLE;
    m_GenDescSet = VK_NULL_HANDLE;

    // Dither pattern - will be properly initialized in Load() via bwdithermap()
    Memory.mem_fill(dither, 0, sizeof(dither));

    fade_distance = 60.0f;
    light_position.set(0, 0, 0);
}

CDetailManager::~CDetailManager()
{
    Unload();
}

// ============================================================================
// Main API
// ============================================================================
void CDetailManager::Unload()
{
    // Destroy detail objects
    for (DetailIt it = objects.begin(); it != objects.end(); ++it)
    {
        VK::CDetail* obj = *it;
        obj->Unload();
        xr_delete(obj);
    }
    objects.clear();

    // Destroy cache
#ifdef DETAIL_RADIUS
    if (cache_pool)
    {
        for (u32 i = 0; i < dm_cache_size; ++i)
            cache_pool[i].~Slot();
        Memory.mem_free(cache_pool);
        cache_pool = nullptr;
    }
    if (cache)
    {
        for (u32 i = 0; i < dm_cache_line; ++i)
            Memory.mem_free(cache[i]);
        Memory.mem_free(cache);
        cache = nullptr;
    }
    if (cache_level1)
    {
        for (u32 i = 0; i < dm_cache1_line; ++i)
        {
            for (u32 j = 0; j < dm_cache1_line; ++j)
                cache_level1[i][j].~CacheSlot1();
            Memory.mem_free(cache_level1[i]);
        }
        Memory.mem_free(cache_level1);
        cache_level1 = nullptr;
    }
#endif

    // Close file
    if (dtFS)
    {
        FS.r_close(dtFS);
        dtFS = nullptr;
    }
    dtSlots = nullptr;

    // Destroy Vulkan resources
    DestroyPipeline();

    // Destroy GPU generation resources
    DestroyGpuGenPipeline();

    // Destroy GPU-driven resources (legacy)
    DestroyHZB();
    DestroyComputePipeline();
    DestroyGpuBuffers();

    m_bCreated = false;

    Msg("[Vulkan] CDetailManager unloaded");
}

// ============================================================================
// Vulkan pipeline management
// ============================================================================
void CDetailManager::CreatePipeline()
{
    if (!g_ShaderManager || !VK::g_PipelineManager)
    {
        Msg("![Vulkan] CreatePipeline: Shader/Pipeline managers not initialized");
        m_Pipeline = VK_NULL_HANDLE;
        return;
    }

    // Load shaders
    VkShaderModule vertShader = g_ShaderManager->Load("detail_vs.spv");
    VkShaderModule fragShader = g_ShaderManager->Load("detail_fs.spv");

    if (vertShader == VK_NULL_HANDLE || fragShader == VK_NULL_HANDLE)
    {
        Msg("![Vulkan] Failed to load detail shaders");
        m_Pipeline = VK_NULL_HANDLE;
        return;
    }

    // Configure pipeline
    VK::PipelineConfig config = {};
    config.vertShader = vertShader;
    config.fragShader = fragShader;
    config.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    config.cullMode = VK_CULL_MODE_NONE;  // Billboards/grass don't cull

    // Depth testing
    config.depthTest = true;
    config.depthWrite = true;
    config.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

    // Depth bias to prevent Z-fighting between grass and terrain
    config.depthBiasEnable = true;
    config.depthBiasConstant = -2.0f;  // Push grass slightly toward camera
    config.depthBiasSlope = -1.0f;

    // No blending (alpha test in shader instead)
    config.blendEnable = false;

    // Single color attachment (forward pass to rt_HDR)
    config.colorAttachmentCount = 1;
    config.colorFormats[0] = VK_FORMAT_R16G16B16A16_SFLOAT;
    config.depthFormat = Swapchain.m_DepthFormat;

    // ========================================================================
    // Custom vertex input: 2 bindings (vertex + instance)
    // ========================================================================
    config.useDefaultVertexInput = false;
    config.useCustomVertexInput = true;

    // Binding 0: per-vertex data (CDetail::Vertex = 24 bytes)
    config.customBindings[0].binding = 0;
    config.customBindings[0].stride = 24;  // vec3 pos + vec2 uv + float height
    config.customBindings[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    // Binding 1: per-instance data (DetailInstance = 64 bytes)
    config.customBindings[1].binding = 1;
    config.customBindings[1].stride = sizeof(VK::DetailInstance);  // 64 bytes
    config.customBindings[1].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

    config.customBindingCount = 2;

    // Vertex attributes:
    // location 0: vec3 aPos (binding 0, offset 0)
    config.customAttributes[0].binding = 0;
    config.customAttributes[0].location = 0;
    config.customAttributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    config.customAttributes[0].offset = 0;

    // location 1: vec2 aUV (binding 0, offset 12)
    config.customAttributes[1].binding = 0;
    config.customAttributes[1].location = 1;
    config.customAttributes[1].format = VK_FORMAT_R32G32_SFLOAT;
    config.customAttributes[1].offset = 12;

    // location 2: float aHeight (binding 0, offset 20)
    config.customAttributes[2].binding = 0;
    config.customAttributes[2].location = 2;
    config.customAttributes[2].format = VK_FORMAT_R32_SFLOAT;
    config.customAttributes[2].offset = 20;

    // location 3: vec4 aInstRow0 (binding 1, offset 0)
    config.customAttributes[3].binding = 1;
    config.customAttributes[3].location = 3;
    config.customAttributes[3].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    config.customAttributes[3].offset = 0;

    // location 4: vec4 aInstRow1 (binding 1, offset 16)
    config.customAttributes[4].binding = 1;
    config.customAttributes[4].location = 4;
    config.customAttributes[4].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    config.customAttributes[4].offset = 16;

    // location 5: vec4 aInstRow2 (binding 1, offset 32)
    config.customAttributes[5].binding = 1;
    config.customAttributes[5].location = 5;
    config.customAttributes[5].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    config.customAttributes[5].offset = 32;

    // location 6: vec4 aInstColor (binding 1, offset 48)
    config.customAttributes[6].binding = 1;
    config.customAttributes[6].location = 6;
    config.customAttributes[6].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    config.customAttributes[6].offset = 48;

    config.customAttributeCount = 7;

    // Create pipeline
    m_Pipeline = VK::g_PipelineManager->GetOrCreate(config);

    if (m_Pipeline != VK_NULL_HANDLE)
    {
        Msg("[Vulkan] Detail pipeline created successfully");
    }
    else
    {
        Msg("![Vulkan] Failed to create detail pipeline");
    }
}

void CDetailManager::DestroyPipeline()
{
    if (m_Pipeline != VK_NULL_HANDLE)
    {
        // TODO: Destroy Vulkan pipeline
        m_Pipeline = VK_NULL_HANDLE;
    }
}

// ============================================================================
// Wind animation update
// ============================================================================
void CDetailManager::UpdateWindAnimation()
{
    float new_global_time = RDEVICE.fTimeGlobal;
    float dt = new_global_time - m_global_time_old;
    m_global_time_old = new_global_time;

    // Interpolate swing parameters based on weather/wind
    float wind_power = g_pGamePersistent->Environment().wind_strength_factor;
    swing_current.lerp(swing_desc[0], swing_desc[1], wind_power);

    // Update animation time
    m_time_rot_1 += dt * swing_current.speed;
    m_time_rot_2 += dt * swing_current.speed * 0.7f;
    m_time_pos += dt;

    // Wrap times
    if (m_time_rot_1 > PI_MUL_2) m_time_rot_1 -= PI_MUL_2;
    if (m_time_rot_2 > PI_MUL_2) m_time_rot_2 -= PI_MUL_2;
    if (m_time_pos > 1000.0f) m_time_pos -= 1000.0f;

    // Fill shader constants - convert wind angle to direction vector
    float wind_angle = g_pGamePersistent->Environment().CurrentEnv->wind_direction;
    Fvector wind_dir;
    wind_dir.setHP(wind_angle, 0.f);
    wind_dir.normalize_safe();

    m_Constants.vWave.set(
        0.5f,                           // freq_x
        0.5f,                           // freq_z
        swing_current.speed,            // speed
        m_time_pos                      // time
    );

    m_Constants.vWind.set(
        wind_dir.x,                     // wind direction X
        0.0f,                           // unused
        wind_dir.z,                     // wind direction Z
        swing_current.amp1              // amplitude
    );
}

// ============================================================================
// GPU-driven pipeline: Buffer creation/destruction
// ============================================================================
void CDetailManager::CreateGpuBuffers()
{
    // Persistent SSBO: all instances (STORAGE + TRANSFER_DST for upload)
    m_AllInstancesSSBO = xr_new<CVulkanBuffer>();
    m_AllInstancesSSBO->Create(
        GPU_MAX_INSTANCES * sizeof(GpuDetailInstanceExt),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE
    );

    // Visible SSBO: compacted output (STORAGE for compute write + VERTEX_BUFFER for draw)
    // Sized at GPU_OUTPUT_CAPACITY (4x input) so each of 32 types gets ~6250 slots
    m_VisibleSSBO = xr_new<CVulkanBuffer>();
    m_VisibleSSBO->Create(
        GPU_OUTPUT_CAPACITY * sizeof(DetailInstance),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE
    );

    // Indirect draw commands: VkDrawIndexedIndirectCommand = 20 bytes each
    m_IndirectCmdBuf = xr_new<CVulkanBuffer>();
    m_IndirectCmdBuf->Create(
        GPU_MAX_OBJ_TYPES * sizeof(VkDrawIndexedIndirectCommand),
        VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE
    );

    // Atomic counters: 64 per-type counts + 64 base offsets + 1 global counter = 129 u32s
    m_AtomicCounters = xr_new<CVulkanBuffer>();
    m_AtomicCounters->Create(
        (GPU_MAX_OBJ_TYPES * 2 + 4) * sizeof(u32),  // 132 u32s, 16-byte aligned
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE
    );

    m_StagingInstances.reserve(GPU_MAX_INSTANCES);
    m_TotalGpuInstances = 0;
    m_GpuDataDirty = false;

    Msg("[Detail GPU] Buffers created: allSSBO=%.1fMB visSSBO=%.1fMB indirect=%.1fKB counters=%.1fKB sectionSize=%u",
        (GPU_MAX_INSTANCES * sizeof(GpuDetailInstanceExt)) / (1024.f * 1024.f),
        (GPU_OUTPUT_CAPACITY * sizeof(DetailInstance)) / (1024.f * 1024.f),
        (GPU_MAX_OBJ_TYPES * sizeof(VkDrawIndexedIndirectCommand)) / 1024.f,
        (GPU_MAX_OBJ_TYPES * 2 * sizeof(u32)) / 1024.f,
        GPU_OUTPUT_CAPACITY / GPU_MAX_OBJ_TYPES);
}

void CDetailManager::DestroyGpuBuffers()
{
    if (m_AllInstancesSSBO) { m_AllInstancesSSBO->Destroy(); xr_delete(m_AllInstancesSSBO); }
    if (m_VisibleSSBO) { m_VisibleSSBO->Destroy(); xr_delete(m_VisibleSSBO); }
    if (m_IndirectCmdBuf) { m_IndirectCmdBuf->Destroy(); xr_delete(m_IndirectCmdBuf); }
    if (m_AtomicCounters) { m_AtomicCounters->Destroy(); xr_delete(m_AtomicCounters); }
    m_StagingInstances.clear();
    m_GpuFreeList.clear();
    m_TotalGpuInstances = 0;
    m_GpuDataDirty = false;
}

// ============================================================================
// GPU-driven pipeline: Upload staging vector to persistent SSBO
// ============================================================================
void CDetailManager::UploadStagingToSSBO()
{
    if (!m_GpuDataDirty || !m_AllInstancesSSBO || m_StagingInstances.empty())
        return;

    u32 uploadSize = (u32)m_StagingInstances.size() * sizeof(GpuDetailInstanceExt);
    if (uploadSize > m_AllInstancesSSBO->GetSize())
    {
        Msg("![Detail GPU] SSBO upload size %u exceeds buffer %llu", uploadSize, m_AllInstancesSSBO->GetSize());
        return;
    }

    // SSBO is host-visible + persistently mapped (VMA creates storage buffers with MAPPED_BIT).
    // Write directly to mapped pointer — avoids staging buffer alloc + GPU wait every frame.
    void* mapped = m_AllInstancesSSBO->m_Mapped;
    if (mapped)
    {
        memcpy(mapped, m_StagingInstances.data(), uploadSize);
        m_AllInstancesSSBO->Flush();
    }
    else
    {
        // Fallback: buffer not mapped (shouldn't happen for storage buffers)
        m_AllInstancesSSBO->Upload(m_StagingInstances.data(), uploadSize, 0);
    }

    m_TotalGpuInstances = (u32)m_StagingInstances.size();
    m_GpuDataDirty = false;

    static u32 s_upload_log = 0;
    if (s_upload_log < 5 || (s_upload_log % 100 == 0))
    {
        Msg("[Detail GPU] Uploaded %u instances (%.1f KB) to SSBO (direct-mapped)",
            m_TotalGpuInstances, uploadSize / 1024.f);
    }
    s_upload_log++;
}

// ============================================================================
// GPU-driven pipeline: Compute pipeline creation
// ============================================================================
void CDetailManager::CreateComputePipeline()
{
    if (!g_ShaderManager)
    {
        Msg("![Detail GPU] Shader manager not initialized, skipping compute pipeline");
        return;
    }

    // Load compute shaders
    VkShaderModule cullShader = g_ShaderManager->Load("detail_cull.comp.spv");
    VkShaderModule finalizeShader = g_ShaderManager->Load("detail_cull_finalize.comp.spv");

    if (cullShader == VK_NULL_HANDLE || finalizeShader == VK_NULL_HANDLE)
    {
        Msg("![Detail GPU] Failed to load compute shaders, GPU path disabled");
        m_bGpuDrivenEnabled = false;
        return;
    }

    // ========================================================================
    // Descriptor set layout: 5 bindings
    // ========================================================================
    VkDescriptorSetLayoutBinding bindings[5] = {};

    // binding 0: AllInstances SSBO (readonly)
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // binding 1: VisibleSSBO (writeonly)
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // binding 2: AtomicCounters (read/write)
    bindings[2].binding = 2;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // binding 3: IndirectCmdBuf (writeonly)
    bindings[3].binding = 3;
    bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[3].descriptorCount = 1;
    bindings[3].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // binding 4: HZB texture (sampled, optional — nullptr initially)
    bindings[4].binding = 4;
    bindings[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[4].descriptorCount = 1;
    bindings[4].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo = {};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 5;
    layoutInfo.pBindings = bindings;

    VK_CHECK(vkCreateDescriptorSetLayout(VulkanHW.m_Device, &layoutInfo, nullptr, &m_ComputeDescLayout));

    // ========================================================================
    // Push constant range: DetailCullConstants (192 bytes) + DetailCullCounts (16 bytes)
    // ========================================================================
    VkPushConstantRange pushRange = {};
    pushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(DetailCullConstants) + sizeof(DetailCullCounts);

    VkPipelineLayoutCreateInfo pipeLayoutInfo = {};
    pipeLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeLayoutInfo.setLayoutCount = 1;
    pipeLayoutInfo.pSetLayouts = &m_ComputeDescLayout;
    pipeLayoutInfo.pushConstantRangeCount = 1;
    pipeLayoutInfo.pPushConstantRanges = &pushRange;

    VK_CHECK(vkCreatePipelineLayout(VulkanHW.m_Device, &pipeLayoutInfo, nullptr, &m_ComputeLayout));

    // ========================================================================
    // Descriptor pool (own pool, not global)
    // ========================================================================
    VkDescriptorPoolSize poolSizes[2] = {};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[0].descriptorCount = 4;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = 1;

    VkDescriptorPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 2;
    poolInfo.pPoolSizes = poolSizes;

    VK_CHECK(vkCreateDescriptorPool(VulkanHW.m_Device, &poolInfo, nullptr, &m_ComputeDescPool));

    // Allocate descriptor set
    VkDescriptorSetAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_ComputeDescPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &m_ComputeDescLayout;

    VK_CHECK(vkAllocateDescriptorSets(VulkanHW.m_Device, &allocInfo, &m_ComputeDescSet));

    // ========================================================================
    // Create compute pipelines
    // ========================================================================
    m_CullPipeline.Create(cullShader, m_ComputeLayout);
    m_FinalizePipeline.Create(finalizeShader, m_ComputeLayout);

    Msg("[Detail GPU] Compute pipelines created successfully");
}

void CDetailManager::DestroyComputePipeline()
{
    m_CullPipeline.Destroy();
    m_FinalizePipeline.Destroy();

    if (m_ComputeDescPool != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorPool(VulkanHW.m_Device, m_ComputeDescPool, nullptr);
        m_ComputeDescPool = VK_NULL_HANDLE;
        m_ComputeDescSet = VK_NULL_HANDLE;
    }
    if (m_ComputeLayout != VK_NULL_HANDLE)
    {
        vkDestroyPipelineLayout(VulkanHW.m_Device, m_ComputeLayout, nullptr);
        m_ComputeLayout = VK_NULL_HANDLE;
    }
    if (m_ComputeDescLayout != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorSetLayout(VulkanHW.m_Device, m_ComputeDescLayout, nullptr);
        m_ComputeDescLayout = VK_NULL_HANDLE;
    }
}

// ============================================================================
// GPU-driven pipeline: Frustum plane extraction from view-projection matrix
// ============================================================================
void CDetailManager::ExtractFrustumPlanes(const Fmatrix& m, Fvector4 planes[6])
{
    // Left:   row3 + row0
    planes[0].set(m._14 + m._11, m._24 + m._21, m._34 + m._31, m._44 + m._41);
    // Right:  row3 - row0
    planes[1].set(m._14 - m._11, m._24 - m._21, m._34 - m._31, m._44 - m._41);
    // Bottom: row3 + row1
    planes[2].set(m._14 + m._12, m._24 + m._22, m._34 + m._32, m._44 + m._42);
    // Top:    row3 - row1
    planes[3].set(m._14 - m._12, m._24 - m._22, m._34 - m._32, m._44 - m._42);
    // Near:   row3 + row2
    planes[4].set(m._14 + m._13, m._24 + m._23, m._34 + m._33, m._44 + m._43);
    // Far:    row3 - row2
    planes[5].set(m._14 - m._13, m._24 - m._23, m._34 - m._33, m._44 - m._43);

    // Normalize each plane
    for (int i = 0; i < 6; i++)
    {
        float len = _sqrt(planes[i].x * planes[i].x + planes[i].y * planes[i].y + planes[i].z * planes[i].z);
        if (len > 0.0001f)
        {
            float inv = 1.0f / len;
            planes[i].x *= inv;
            planes[i].y *= inv;
            planes[i].z *= inv;
            planes[i].w *= inv;
        }
    }
}

// ============================================================================
// GPU-driven pipeline: HZB creation
// ============================================================================
void CDetailManager::CreateHZB()
{
    if (!g_ShaderManager)
    {
        Msg("![Detail GPU] Shader manager not ready, skipping HZB creation");
        m_HZBImage = VK_NULL_HANDLE;
        return;
    }

    // HZB is half the depth buffer resolution
    m_HZBWidth = Swapchain.GetWidth() / 2;
    m_HZBHeight = Swapchain.GetHeight() / 2;
    if (m_HZBWidth == 0 || m_HZBHeight == 0)
    {
        Msg("![Detail GPU] Invalid swapchain size for HZB");
        m_HZBImage = VK_NULL_HANDLE;
        return;
    }

    // Calculate mip levels
    m_HZBMipLevels = 1;
    {
        u32 w = m_HZBWidth, h = m_HZBHeight;
        while (w > 1 || h > 1)
        {
            w = _max(w / 2, 1u);
            h = _max(h / 2, 1u);
            m_HZBMipLevels++;
        }
    }
    m_HZBMipLevels = _min(m_HZBMipLevels, 12u); // Cap at 12 mips

    // Create HZB image (R32_SFLOAT, STORAGE | SAMPLED)
    VkImageCreateInfo imageInfo = {};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R32_SFLOAT;
    imageInfo.extent = { m_HZBWidth, m_HZBHeight, 1 };
    imageInfo.mipLevels = m_HZBMipLevels;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

    VmaAllocation hzbAlloc;
    VkResult res = vmaCreateImage(VulkanHW.m_Allocator, &imageInfo, &allocInfo,
        &m_HZBImage, &hzbAlloc, nullptr);
    if (res != VK_SUCCESS)
    {
        Msg("![Detail GPU] Failed to create HZB image: %d", res);
        m_HZBImage = VK_NULL_HANDLE;
        return;
    }
    // We store the allocation handle in m_HZBMemory cast... but VmaAllocation is not VkDeviceMemory.
    // For proper cleanup we need to track the VmaAllocation. Store it as m_HZBMemory reinterpreted.
    // This is a simplification — in production code, store VmaAllocation separately.
    m_HZBMemory = (VkDeviceMemory)hzbAlloc;

    // Create full mip chain view
    VkImageViewCreateInfo viewInfo = {};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_HZBImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R32_SFLOAT;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = m_HZBMipLevels;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    VK_CHECK(vkCreateImageView(VulkanHW.m_Device, &viewInfo, nullptr, &m_HZBView));

    // Create per-mip views for storage image writes
    m_HZBMipViews.resize(m_HZBMipLevels);
    for (u32 mip = 0; mip < m_HZBMipLevels; mip++)
    {
        VkImageViewCreateInfo mipViewInfo = viewInfo;
        mipViewInfo.subresourceRange.baseMipLevel = mip;
        mipViewInfo.subresourceRange.levelCount = 1;
        VK_CHECK(vkCreateImageView(VulkanHW.m_Device, &mipViewInfo, nullptr, &m_HZBMipViews[mip]));
    }

    // Create sampler (nearest, clamp)
    VkSamplerCreateInfo samplerInfo = {};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_NEAREST;
    samplerInfo.minFilter = VK_FILTER_NEAREST;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.maxLod = (float)m_HZBMipLevels;

    VK_CHECK(vkCreateSampler(VulkanHW.m_Device, &samplerInfo, nullptr, &m_HZBSampler));

    // ========================================================================
    // Create HZB build compute pipeline
    // ========================================================================
    VkShaderModule hzbShader = g_ShaderManager->Load("hzb_build.comp.spv");
    if (hzbShader == VK_NULL_HANDLE)
    {
        Msg("![Detail GPU] Failed to load hzb_build.comp.spv, HZB occlusion disabled");
        // HZB image exists but won't be built — cull shader will get dummy values
        return;
    }

    // Descriptor set layout: 2 bindings (src sampler + dst storage image)
    VkDescriptorSetLayoutBinding hzbBindings[2] = {};
    hzbBindings[0].binding = 0;
    hzbBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    hzbBindings[0].descriptorCount = 1;
    hzbBindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    hzbBindings[1].binding = 1;
    hzbBindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    hzbBindings[1].descriptorCount = 1;
    hzbBindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo hzbLayoutInfo = {};
    hzbLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    hzbLayoutInfo.bindingCount = 2;
    hzbLayoutInfo.pBindings = hzbBindings;
    VK_CHECK(vkCreateDescriptorSetLayout(VulkanHW.m_Device, &hzbLayoutInfo, nullptr, &m_HZBBuildDescLayout));

    // Push constants for HZB build (32 bytes)
    VkPushConstantRange hzbPush = {};
    hzbPush.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    hzbPush.offset = 0;
    hzbPush.size = 32; // ivec2 srcSize, ivec2 dstSize, uint srcMip, uint dstMip, uint isFirstPass, uint _pad

    VkPipelineLayoutCreateInfo hzbPipeLayout = {};
    hzbPipeLayout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    hzbPipeLayout.setLayoutCount = 1;
    hzbPipeLayout.pSetLayouts = &m_HZBBuildDescLayout;
    hzbPipeLayout.pushConstantRangeCount = 1;
    hzbPipeLayout.pPushConstantRanges = &hzbPush;
    VK_CHECK(vkCreatePipelineLayout(VulkanHW.m_Device, &hzbPipeLayout, nullptr, &m_HZBBuildLayout));

    // Descriptor pool for HZB build (need multiple sets for multiple mip passes)
    VkDescriptorPoolSize hzbPoolSizes[2] = {};
    hzbPoolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    hzbPoolSizes[0].descriptorCount = m_HZBMipLevels;
    hzbPoolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    hzbPoolSizes[1].descriptorCount = m_HZBMipLevels;

    VkDescriptorPoolCreateInfo hzbPoolInfo = {};
    hzbPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    hzbPoolInfo.maxSets = m_HZBMipLevels;
    hzbPoolInfo.poolSizeCount = 2;
    hzbPoolInfo.pPoolSizes = hzbPoolSizes;
    VK_CHECK(vkCreateDescriptorPool(VulkanHW.m_Device, &hzbPoolInfo, nullptr, &m_HZBBuildDescPool));

    // Create pipeline
    m_HZBBuildPipeline.Create(hzbShader, m_HZBBuildLayout);

    Msg("[Detail GPU] HZB created: %ux%u, %u mips, %.1f KB",
        m_HZBWidth, m_HZBHeight, m_HZBMipLevels,
        (m_HZBWidth * m_HZBHeight * 4 * 4 / 3) / 1024.f);
}

void CDetailManager::DestroyHZB()
{
    for (auto& view : m_HZBMipViews)
    {
        if (view != VK_NULL_HANDLE)
            vkDestroyImageView(VulkanHW.m_Device, view, nullptr);
    }
    m_HZBMipViews.clear();

    if (m_HZBView != VK_NULL_HANDLE)
    {
        vkDestroyImageView(VulkanHW.m_Device, m_HZBView, nullptr);
        m_HZBView = VK_NULL_HANDLE;
    }
    if (m_HZBSampler != VK_NULL_HANDLE)
    {
        vkDestroySampler(VulkanHW.m_Device, m_HZBSampler, nullptr);
        m_HZBSampler = VK_NULL_HANDLE;
    }
    if (m_HZBImage != VK_NULL_HANDLE && m_HZBMemory != VK_NULL_HANDLE)
    {
        // m_HZBMemory is actually a VmaAllocation cast to VkDeviceMemory
        vmaDestroyImage(VulkanHW.m_Allocator, m_HZBImage, (VmaAllocation)m_HZBMemory);
        m_HZBImage = VK_NULL_HANDLE;
        m_HZBMemory = VK_NULL_HANDLE;
    }
    else
    {
        if (m_HZBImage != VK_NULL_HANDLE)
        {
            vkDestroyImage(VulkanHW.m_Device, m_HZBImage, nullptr);
            m_HZBImage = VK_NULL_HANDLE;
        }
        m_HZBMemory = VK_NULL_HANDLE;
    }

    m_HZBBuildPipeline.Destroy();
    if (m_HZBBuildLayout != VK_NULL_HANDLE)
    {
        vkDestroyPipelineLayout(VulkanHW.m_Device, m_HZBBuildLayout, nullptr);
        m_HZBBuildLayout = VK_NULL_HANDLE;
    }
    if (m_HZBBuildDescLayout != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorSetLayout(VulkanHW.m_Device, m_HZBBuildDescLayout, nullptr);
        m_HZBBuildDescLayout = VK_NULL_HANDLE;
    }
    if (m_HZBBuildDescPool != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorPool(VulkanHW.m_Device, m_HZBBuildDescPool, nullptr);
        m_HZBBuildDescPool = VK_NULL_HANDLE;
    }
}

void CDetailManager::BuildHZB(VkCommandBuffer cmd)
{
    if (m_HZBImage == VK_NULL_HANDLE || !m_HZBBuildPipeline.IsValid())
        return;
    if (m_HZBMipViews.empty() || m_HZBMipLevels == 0)
        return;
    if (m_HZBBuildDescPool == VK_NULL_HANDLE)
        return;

    // Reset descriptor pool — we allocate per-mip sets each frame, pool has maxSets = m_HZBMipLevels
    vkResetDescriptorPool(VulkanHW.m_Device, m_HZBBuildDescPool, 0);

    // Transition entire HZB image to GENERAL layout for compute storage writes
    {
        VkImageMemoryBarrier bar = {};
        bar.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        bar.srcAccessMask = 0;
        bar.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        bar.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        bar.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        bar.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bar.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bar.image = m_HZBImage;
        bar.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, m_HZBMipLevels, 0, 1 };

        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &bar);
    }

    m_HZBBuildPipeline.Bind(cmd);

    // Build each mip level
    u32 srcW = Swapchain.GetWidth();
    u32 srcH = Swapchain.GetHeight();

    for (u32 mip = 0; mip < m_HZBMipLevels; mip++)
    {
        u32 dstW = _max(m_HZBWidth >> mip, 1u);
        u32 dstH = _max(m_HZBHeight >> mip, 1u);

        // Allocate descriptor set for this pass
        VkDescriptorSet passSet;
        VkDescriptorSetAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = m_HZBBuildDescPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &m_HZBBuildDescLayout;
        if (vkAllocateDescriptorSets(VulkanHW.m_Device, &allocInfo, &passSet) != VK_SUCCESS)
            break;

        // Source: depth buffer for mip 0, previous HZB mip for mip > 0
        VkDescriptorImageInfo srcInfo = {};
        if (mip == 0)
        {
            // Use depth buffer as source
            srcInfo.imageView = Swapchain.m_DepthView;
            srcInfo.sampler = m_HZBSampler;
            srcInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        }
        else
        {
            // Use previous HZB mip as source
            srcInfo.imageView = m_HZBMipViews[mip - 1];
            srcInfo.sampler = m_HZBSampler;
            srcInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        }

        // Destination: current HZB mip
        VkDescriptorImageInfo dstInfo = {};
        dstInfo.imageView = m_HZBMipViews[mip];
        dstInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        VkWriteDescriptorSet writes[2] = {};
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = passSet;
        writes[0].dstBinding = 0;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[0].descriptorCount = 1;
        writes[0].pImageInfo = &srcInfo;

        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = passSet;
        writes[1].dstBinding = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[1].descriptorCount = 1;
        writes[1].pImageInfo = &dstInfo;

        vkUpdateDescriptorSets(VulkanHW.m_Device, 2, writes, 0, nullptr);

        // Push constants
        struct {
            int srcSizeX, srcSizeY;
            int dstSizeX, dstSizeY;
            u32 srcMip, dstMip, isFirstPass, _pad;
        } hzbPush;
        hzbPush.srcSizeX = (int)srcW;
        hzbPush.srcSizeY = (int)srcH;
        hzbPush.dstSizeX = (int)dstW;
        hzbPush.dstSizeY = (int)dstH;
        hzbPush.srcMip = (mip == 0) ? 0 : mip - 1;
        hzbPush.dstMip = mip;
        hzbPush.isFirstPass = (mip == 0) ? 1 : 0;
        hzbPush._pad = 0;

        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_HZBBuildLayout,
            0, 1, &passSet, 0, nullptr);
        vkCmdPushConstants(cmd, m_HZBBuildLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(hzbPush), &hzbPush);

        u32 groupsX = (dstW + 7) / 8;
        u32 groupsY = (dstH + 7) / 8;
        vkCmdDispatch(cmd, groupsX, groupsY, 1);

        // Barrier between mip levels: compute write -> compute read
        {
            VkImageMemoryBarrier mipBar = {};
            mipBar.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            mipBar.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            mipBar.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            mipBar.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
            mipBar.newLayout = VK_IMAGE_LAYOUT_GENERAL;
            mipBar.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            mipBar.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            mipBar.image = m_HZBImage;
            mipBar.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, mip, 1, 0, 1 };

            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &mipBar);
        }

        // Update source dimensions for next mip
        srcW = dstW;
        srcH = dstH;
    }
}

// ============================================================================
// GPU grass generation: Bake heightmap from collision geometry
// Rasterizes static collision triangles into a 2D R32F heightmap.
// Each texel stores the maximum terrain Y at that XZ position.
// ============================================================================
void CDetailManager::BakeHeightmap()
{
    if (!g_pGameLevel)
    {
        Msg("![Detail GPU Gen] g_pGameLevel not available for heightmap baking");
        return;
    }

    CDB::MODEL* model = g_pGameLevel->ObjectSpace.GetStaticModel();
    if (!model)
    {
        Msg("![Detail GPU Gen] Static model not available");
        return;
    }

    Fvector* verts = g_pGameLevel->ObjectSpace.GetStaticVerts();
    CDB::TRI* tris = g_pGameLevel->ObjectSpace.GetStaticTris();
    u32 triCount = model->get_tris_count();

    // World bounds from detail header
    m_HMOriginX = -(float)dtH.offs_x * dm_slot_size;
    m_HMOriginZ = -(float)dtH.offs_z * dm_slot_size;
    m_HMWorldSizeX = (float)dtH.size_x * dm_slot_size;
    m_HMWorldSizeZ = (float)dtH.size_z * dm_slot_size;

    // Heightmap resolution: ~1m per texel, capped at 2048
    m_HeightmapW = _min((u32)ceilf(m_HMWorldSizeX), 2048u);
    m_HeightmapH = _min((u32)ceilf(m_HMWorldSizeZ), 2048u);

    if (m_HeightmapW == 0 || m_HeightmapH == 0)
    {
        Msg("![Detail GPU Gen] Invalid heightmap dimensions");
        return;
    }

    float texelSizeX = m_HMWorldSizeX / (float)m_HeightmapW;
    float texelSizeZ = m_HMWorldSizeZ / (float)m_HeightmapH;

    // Allocate heightmap data (initialized to very high Y — we store MIN to get ground level)
    xr_vector<float> heightData(m_HeightmapW * m_HeightmapH, 10000.0f);

    Msg("[Detail GPU Gen] Baking heightmap %ux%u from %u triangles (%.0fx%.0f m)...",
        m_HeightmapW, m_HeightmapH, triCount, m_HMWorldSizeX, m_HMWorldSizeZ);

    u32 rasterized = 0;

    // Rasterize each triangle into the heightmap
    for (u32 t = 0; t < triCount; t++)
    {
        CDB::TRI& T = tris[t];
        Fvector v0 = verts[T.verts[0]];
        Fvector v1 = verts[T.verts[1]];
        Fvector v2 = verts[T.verts[2]];

        // Skip near-vertical surfaces (walls, ceilings) — grass doesn't grow on them
        Fvector normal;
        normal.mknormal(v0, v1, v2);
        if (normal.y < 0.3f) continue;  // Skip >73° slopes

        // Project to heightmap pixel coordinates
        float px0 = (v0.x - m_HMOriginX) / texelSizeX;
        float pz0 = (v0.z - m_HMOriginZ) / texelSizeZ;
        float px1 = (v1.x - m_HMOriginX) / texelSizeX;
        float pz1 = (v1.z - m_HMOriginZ) / texelSizeZ;
        float px2 = (v2.x - m_HMOriginX) / texelSizeX;
        float pz2 = (v2.z - m_HMOriginZ) / texelSizeZ;

        // Bounding box in pixel coords
        int minPX = _max(0, (int)floorf(_min(_min(px0, px1), px2)));
        int maxPX = _min((int)m_HeightmapW - 1, (int)ceilf(_max(_max(px0, px1), px2)));
        int minPZ = _max(0, (int)floorf(_min(_min(pz0, pz1), pz2)));
        int maxPZ = _min((int)m_HeightmapH - 1, (int)ceilf(_max(_max(pz0, pz1), pz2)));

        // Edge vectors for barycentric test
        float dx10 = px1 - px0, dz10 = pz1 - pz0;
        float dx20 = px2 - px0, dz20 = pz2 - pz0;
        float denom = dx10 * dz20 - dx20 * dz10;
        if (_abs(denom) < 1e-6f) continue; // Degenerate triangle
        float invDenom = 1.0f / denom;

        for (int pz = minPZ; pz <= maxPZ; pz++)
        {
            for (int px = minPX; px <= maxPX; px++)
            {
                float qx = (float)px + 0.5f - px0;
                float qz = (float)pz + 0.5f - pz0;

                // Barycentric coordinates
                float u = (qx * dz20 - qz * dx20) * invDenom;
                float v = (dx10 * qz - dz10 * qx) * invDenom;

                if (u < -0.01f || v < -0.01f || (u + v) > 1.01f) continue;

                // Interpolate Y
                float y = v0.y + u * (v1.y - v0.y) + v * (v2.y - v0.y);

                // Store min Y (ground level, not bridges/roofs)
                u32 idx = (u32)pz * m_HeightmapW + (u32)px;
                if (y < heightData[idx])
                    heightData[idx] = y;
            }
        }
        rasterized++;
    }

    // Fill holes (texels that no triangle covered) with neighbor average
    for (u32 z = 0; z < m_HeightmapH; z++)
    {
        for (u32 x = 0; x < m_HeightmapW; x++)
        {
            u32 idx = z * m_HeightmapW + x;
            if (heightData[idx] < 9999.0f) continue;  // Has valid data

            // Sample neighbors
            float sum = 0;
            int count = 0;
            for (int dz = -2; dz <= 2; dz++)
            {
                for (int dx = -2; dx <= 2; dx++)
                {
                    int nx = (int)x + dx, nz = (int)z + dz;
                    if (nx < 0 || nx >= (int)m_HeightmapW || nz < 0 || nz >= (int)m_HeightmapH)
                        continue;
                    float h = heightData[nz * m_HeightmapW + nx];
                    if (h < 9999.0f) { sum += h; count++; }
                }
            }
            if (count > 0) heightData[idx] = sum / (float)count;
            else heightData[idx] = 0.0f; // Fallback
        }
    }

    Msg("[Detail GPU Gen] Rasterized %u triangles, creating GPU texture...", rasterized);

    // Create VkImage (R32_SFLOAT, SAMPLED | TRANSFER_DST)
    VkImageCreateInfo imageInfo = {};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R32_SFLOAT;
    imageInfo.extent = { m_HeightmapW, m_HeightmapH, 1 };
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

    VmaAllocation hmAlloc;
    VkResult res = vmaCreateImage(VulkanHW.m_Allocator, &imageInfo, &allocInfo,
        &m_HeightmapImage, &hmAlloc, nullptr);
    if (res != VK_SUCCESS)
    {
        Msg("![Detail GPU Gen] Failed to create heightmap image: %d", res);
        m_HeightmapImage = VK_NULL_HANDLE;
        return;
    }
    m_HeightmapMemory = (VkDeviceMemory)hmAlloc;

    // Create image view
    VkImageViewCreateInfo viewInfo = {};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_HeightmapImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R32_SFLOAT;
    viewInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    VK_CHECK(vkCreateImageView(VulkanHW.m_Device, &viewInfo, nullptr, &m_HeightmapView));

    // Create sampler (bilinear, clamp)
    VkSamplerCreateInfo sampInfo = {};
    sampInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampInfo.magFilter = VK_FILTER_LINEAR;
    sampInfo.minFilter = VK_FILTER_LINEAR;
    sampInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    VK_CHECK(vkCreateSampler(VulkanHW.m_Device, &sampInfo, nullptr, &m_HeightmapSampler));

    // Upload heightmap data via staging buffer
    u32 dataSize = m_HeightmapW * m_HeightmapH * sizeof(float);
    CVulkanBuffer staging;
    staging.Create(dataSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_HOST);
    void* mapped = staging.m_Mapped;
    if (mapped)
        memcpy(mapped, heightData.data(), dataSize);
    else
        staging.Upload(heightData.data(), dataSize, 0);

    // One-shot command buffer for upload
    VkCommandBuffer uploadCmd = VulkanHW.BeginSingleTimeCommands();
    if (uploadCmd != VK_NULL_HANDLE)
    {
        // Transition to TRANSFER_DST
        VkImageMemoryBarrier bar = {};
        bar.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        bar.srcAccessMask = 0;
        bar.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        bar.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        bar.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        bar.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bar.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bar.image = m_HeightmapImage;
        bar.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        vkCmdPipelineBarrier(uploadCmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &bar);

        // Copy buffer → image
        VkBufferImageCopy region = {};
        region.bufferOffset = 0;
        region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        region.imageExtent = { m_HeightmapW, m_HeightmapH, 1 };
        vkCmdCopyBufferToImage(uploadCmd, staging.GetHandle(), m_HeightmapImage,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        // Transition to SHADER_READ_ONLY
        bar.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        bar.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        bar.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        bar.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        vkCmdPipelineBarrier(uploadCmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &bar);

        VulkanHW.EndSingleTimeCommands(uploadCmd);
    }
    staging.Destroy();

    Msg("[Detail GPU Gen] Heightmap baked: %ux%u (%.1f MB), origin=(%.0f,%.0f), size=(%.0f,%.0f)",
        m_HeightmapW, m_HeightmapH, dataSize / (1024.f * 1024.f),
        m_HMOriginX, m_HMOriginZ, m_HMWorldSizeX, m_HMWorldSizeZ);
}

// ============================================================================
// GPU grass generation: Upload slot palette data as SSBO
// ============================================================================
void CDetailManager::UploadSlotData()
{
    if (!dtSlots) return;

    m_TotalSlots = dtH.size_x * dtH.size_z;
    u32 dataSize = m_TotalSlots * sizeof(GpuSlotPacked);

    // Pack DetailSlot bitfields into GPU-friendly format
    xr_vector<GpuSlotPacked> packed(m_TotalSlots);
    for (u32 i = 0; i < m_TotalSlots; i++)
    {
        DetailSlot& ds = dtSlots[i];
        GpuSlotPacked& p = packed[i];
        p.y_base = ds.r_ybase();
        p.y_height = ds.r_yheight();
        p.ids = (u32)ds.id0 | ((u32)ds.id1 << 8) | ((u32)ds.id2 << 16) | ((u32)ds.id3 << 24);
        // Pack lighting: c_dir and c_hemi as 16-bit fixed point
        p.lighting = (u32)(ds.r_qclr(ds.c_dir, 15) * 65535.0f)
                   | ((u32)(ds.r_qclr(ds.c_hemi, 15) * 65535.0f) << 16);
        // Pack palettes: each object gets a0:8|a1:8|a2:8|a3:8
        // Alpha corners are 4-bit (0-15), scale to 0-255 for GPU
        auto packPalette = [](const DetailPalette& pal) -> u32 {
            u32 a0 = (u32)((float)pal.a0 / 15.0f * 255.0f);
            u32 a1 = (u32)((float)pal.a1 / 15.0f * 255.0f);
            u32 a2 = (u32)((float)pal.a2 / 15.0f * 255.0f);
            u32 a3 = (u32)((float)pal.a3 / 15.0f * 255.0f);
            return a0 | (a1 << 8) | (a2 << 16) | (a3 << 24);
        };
        p.palette0 = packPalette(ds.palette[0]);
        p.palette1 = packPalette(ds.palette[1]);
        p.palette2 = packPalette(ds.palette[2]);
        p.palette3 = packPalette(ds.palette[3]);
    }

    m_SlotDataSSBO = xr_new<CVulkanBuffer>();
    m_SlotDataSSBO->Create(dataSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);

    void* mapped = m_SlotDataSSBO->m_Mapped;
    if (mapped)
    {
        memcpy(mapped, packed.data(), dataSize);
        m_SlotDataSSBO->Flush();
    }
    else
    {
        m_SlotDataSSBO->Upload(packed.data(), dataSize, 0);
    }

    Msg("[Detail GPU Gen] Slot data uploaded: %u slots (%.1f KB)",
        m_TotalSlots, dataSize / 1024.f);
}

// ============================================================================
// GPU grass generation: Upload per-object-type info
// ============================================================================
void CDetailManager::UploadObjInfo()
{
    if (objects.empty()) return;

    u32 count = (u32)objects.size();
    u32 dataSize = count * sizeof(GpuDetailObjInfo);

    xr_vector<GpuDetailObjInfo> infos(count);
    for (u32 i = 0; i < count; i++)
    {
        VK::CDetail* obj = objects[i];
        if (obj)
        {
            infos[i].minScale = obj->m_MinScale;
            infos[i].maxScale = obj->m_MaxScale;
            infos[i].bvRadius = obj->bv_sphere.R;
            infos[i].flags = obj->m_Flags;
        }
        else
        {
            infos[i].minScale = 1.0f;
            infos[i].maxScale = 1.0f;
            infos[i].bvRadius = 1.0f;
            infos[i].flags = 0;
        }
    }

    m_ObjInfoSSBO = xr_new<CVulkanBuffer>();
    m_ObjInfoSSBO->Create(dataSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);

    void* mapped = m_ObjInfoSSBO->m_Mapped;
    if (mapped)
    {
        memcpy(mapped, infos.data(), dataSize);
        m_ObjInfoSSBO->Flush();
    }
    else
    {
        m_ObjInfoSSBO->Upload(infos.data(), dataSize, 0);
    }

    // Create GenUBO (uniform buffer for per-frame params)
    m_GenUBO = xr_new<CVulkanBuffer>();
    m_GenUBO->Create(sizeof(DetailGenUBO),
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VMA_MEMORY_USAGE_AUTO_PREFER_HOST);

    Msg("[Detail GPU Gen] Object info uploaded: %u types (%.1f KB)", count, dataSize / 1024.f);
}

// ============================================================================
// GPU grass generation: Create compute pipeline
// ============================================================================
void CDetailManager::CreateGpuGenPipeline()
{
    if (!g_ShaderManager)
    {
        Msg("![Detail GPU Gen] Shader manager not ready");
        m_bGpuGenerationEnabled = false;
        return;
    }

    VkShaderModule genShader = g_ShaderManager->Load("detail_generate.comp.spv");
    if (genShader == VK_NULL_HANDLE)
    {
        Msg("![Detail GPU Gen] Failed to load detail_generate.comp.spv");
        m_bGpuGenerationEnabled = false;
        return;
    }

    // Descriptor set layout: 8 bindings
    VkDescriptorSetLayoutBinding bindings[8] = {};

    // 0: Heightmap sampler
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // 1: Slot data SSBO
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // 2: Object info SSBO
    bindings[2].binding = 2;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // 3: Visible instances SSBO (output)
    bindings[3].binding = 3;
    bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[3].descriptorCount = 1;
    bindings[3].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // 4: Atomic counters
    bindings[4].binding = 4;
    bindings[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[4].descriptorCount = 1;
    bindings[4].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // 5: Indirect commands
    bindings[5].binding = 5;
    bindings[5].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[5].descriptorCount = 1;
    bindings[5].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // 6: HZB texture
    bindings[6].binding = 6;
    bindings[6].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[6].descriptorCount = 1;
    bindings[6].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // 7: Generation UBO
    bindings[7].binding = 7;
    bindings[7].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[7].descriptorCount = 1;
    bindings[7].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo = {};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 8;
    layoutInfo.pBindings = bindings;
    VK_CHECK(vkCreateDescriptorSetLayout(VulkanHW.m_Device, &layoutInfo, nullptr, &m_GenDescLayout));

    // Push constant range (208 bytes for DetailGenPushConstants)
    VkPushConstantRange pushRange = {};
    pushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(DetailGenPushConstants);

    VkPipelineLayoutCreateInfo pipeLayoutInfo = {};
    pipeLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeLayoutInfo.setLayoutCount = 1;
    pipeLayoutInfo.pSetLayouts = &m_GenDescLayout;
    pipeLayoutInfo.pushConstantRangeCount = 1;
    pipeLayoutInfo.pPushConstantRanges = &pushRange;
    VK_CHECK(vkCreatePipelineLayout(VulkanHW.m_Device, &pipeLayoutInfo, nullptr, &m_GenPipelineLayout));

    // Descriptor pool
    VkDescriptorPoolSize poolSizes[3] = {};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[0].descriptorCount = 2; // heightmap + HZB
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[1].descriptorCount = 4; // slots + objinfo + visible + atomics + indirect (5, but indirect is also storage)
    poolSizes[1].descriptorCount = 5;
    poolSizes[2].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[2].descriptorCount = 1; // GenUBO

    VkDescriptorPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 3;
    poolInfo.pPoolSizes = poolSizes;
    VK_CHECK(vkCreateDescriptorPool(VulkanHW.m_Device, &poolInfo, nullptr, &m_GenDescPool));

    // Allocate descriptor set
    VkDescriptorSetAllocateInfo dsAllocInfo = {};
    dsAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dsAllocInfo.descriptorPool = m_GenDescPool;
    dsAllocInfo.descriptorSetCount = 1;
    dsAllocInfo.pSetLayouts = &m_GenDescLayout;
    VK_CHECK(vkAllocateDescriptorSets(VulkanHW.m_Device, &dsAllocInfo, &m_GenDescSet));

    // Create compute pipeline
    m_GenPipeline.Create(genShader, m_GenPipelineLayout);

    Msg("[Detail GPU Gen] Compute pipeline created successfully");
}

// ============================================================================
// GPU grass generation: Destroy resources
// ============================================================================
void CDetailManager::DestroyGpuGenPipeline()
{
    m_GenPipeline.Destroy();

    if (m_GenDescPool != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorPool(VulkanHW.m_Device, m_GenDescPool, nullptr);
        m_GenDescPool = VK_NULL_HANDLE;
        m_GenDescSet = VK_NULL_HANDLE;
    }
    if (m_GenPipelineLayout != VK_NULL_HANDLE)
    {
        vkDestroyPipelineLayout(VulkanHW.m_Device, m_GenPipelineLayout, nullptr);
        m_GenPipelineLayout = VK_NULL_HANDLE;
    }
    if (m_GenDescLayout != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorSetLayout(VulkanHW.m_Device, m_GenDescLayout, nullptr);
        m_GenDescLayout = VK_NULL_HANDLE;
    }

    if (m_SlotDataSSBO) { m_SlotDataSSBO->Destroy(); xr_delete(m_SlotDataSSBO); }
    if (m_ObjInfoSSBO) { m_ObjInfoSSBO->Destroy(); xr_delete(m_ObjInfoSSBO); }
    if (m_GenUBO) { m_GenUBO->Destroy(); xr_delete(m_GenUBO); }

    // Heightmap
    if (m_HeightmapView != VK_NULL_HANDLE)
    {
        vkDestroyImageView(VulkanHW.m_Device, m_HeightmapView, nullptr);
        m_HeightmapView = VK_NULL_HANDLE;
    }
    if (m_HeightmapSampler != VK_NULL_HANDLE)
    {
        vkDestroySampler(VulkanHW.m_Device, m_HeightmapSampler, nullptr);
        m_HeightmapSampler = VK_NULL_HANDLE;
    }
    if (m_HeightmapImage != VK_NULL_HANDLE && m_HeightmapMemory != VK_NULL_HANDLE)
    {
        vmaDestroyImage(VulkanHW.m_Allocator, m_HeightmapImage, (VmaAllocation)m_HeightmapMemory);
        m_HeightmapImage = VK_NULL_HANDLE;
        m_HeightmapMemory = VK_NULL_HANDLE;
    }
}

} // namespace VK
