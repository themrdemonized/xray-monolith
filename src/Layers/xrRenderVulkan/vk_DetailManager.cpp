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
    m_bGpuDrivenEnabled = true; // Enable GPU path by default

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

    // Destroy GPU-driven resources
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

} // namespace VK
