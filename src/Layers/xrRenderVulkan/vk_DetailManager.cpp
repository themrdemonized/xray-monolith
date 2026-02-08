// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// SPDX-License-Identifier: MIT

#include "stdafx.h"
#include "vk_DetailManager.h"
#include "rvk.h"
#include "../xrRender/DetailFormat.h"
#include "vk_shaders.h"       // g_ShaderManager
#include "vk_pipeline.h"      // g_PipelineManager
#include "vk_swapchain.h"     // Swapchain
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
// CDetailInstanceBuffer - GPU instance buffer manager
// ============================================================================
CDetailInstanceBuffer::CDetailInstanceBuffer()
{
    m_Buffer = nullptr;
    m_Mapped = nullptr;
    m_Capacity = 0;
    m_FrameOffset = 0;
    m_BatchStart = 0;
    m_BatchCount = 0;
}

CDetailInstanceBuffer::~CDetailInstanceBuffer()
{
    Destroy();
}

void CDetailInstanceBuffer::Create(u32 capacity)
{
    m_Capacity = capacity;
    m_FrameOffset = 0;
    m_BatchStart = 0;
    m_BatchCount = 0;

    // Create persistent mapped buffer for instance data
    u32 bufferSize = capacity * sizeof(DetailInstance);
    m_Buffer = xr_new<VK::CVulkanBuffer>();
    m_Buffer->Create(
        bufferSize,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VMA_MEMORY_USAGE_AUTO_PREFER_HOST
    );

    // Get persistent mapping
    m_Mapped = (DetailInstance*)m_Buffer->Map();

    Msg("[Vulkan] DetailInstanceBuffer created: %d instances (%d KB)",
        capacity, bufferSize / 1024);
}

void CDetailInstanceBuffer::Destroy()
{
    if (m_Buffer)
    {
        m_Buffer->Unmap();
        m_Buffer->Destroy();
        xr_delete(m_Buffer);
        m_Mapped = nullptr;
    }
    m_Capacity = 0;
    m_FrameOffset = 0;
    m_BatchStart = 0;
    m_BatchCount = 0;
}

void CDetailInstanceBuffer::BeginFrame()
{
    m_FrameOffset = 0;
}

void CDetailInstanceBuffer::BeginUpdate()
{
    m_BatchStart = m_FrameOffset;
    m_BatchCount = 0;
}

void CDetailInstanceBuffer::AddInstance(const Fmatrix& transform, float sun, float hemi, float scale)
{
    u32 pos = m_FrameOffset + m_BatchCount;
    if (pos >= m_Capacity)
    {
        static u32 s_overflow_log = 0;
        if (s_overflow_log < 5)
        {
            Msg("![Vulkan] DetailInstanceBuffer overflow: %d >= %d", pos, m_Capacity);
            s_overflow_log++;
        }
        return;
    }

    DetailInstance& inst = m_Mapped[pos];

    // Pack 3x4 transform matrix
    // Apply scale to rotation/scale part (upper 3x3)
    inst.row0.set(transform._11 * scale, transform._12 * scale, transform._13 * scale, transform._41);
    inst.row1.set(transform._21 * scale, transform._22 * scale, transform._23 * scale, transform._42);
    inst.row2.set(transform._31 * scale, transform._32 * scale, transform._33 * scale, transform._43);

    // Lighting (sun, sun, sun, hemi)
    inst.color.set(sun, sun, sun, hemi);

    m_BatchCount++;
}

u32 CDetailInstanceBuffer::EndUpdate()
{
    // Advance frame offset past this batch
    m_FrameOffset += m_BatchCount;

    // Flush CPU writes to GPU
    if (m_Buffer && m_BatchCount > 0)
    {
        m_Buffer->Flush();
    }
    return m_BatchCount;
}

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
    // Clear visibility lists
    details_clear();

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
    m_InstanceBuffer.Destroy();

    m_bCreated = false;

    Msg("[Vulkan] CDetailManager unloaded");
}

// ============================================================================
// Clear visibility lists
// ============================================================================
void CDetailManager::details_clear()
{
    for (int i = 0; i < 3; i++)
    {
        m_visibles[i].clear();
    }
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

    // Single color attachment (forward pass to swapchain)
    config.colorAttachmentCount = 1;
    config.colorFormats[0] = Swapchain.GetFormat();
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
// Build instance data from visible slots (stub)
// ============================================================================
void CDetailManager::BuildInstanceData()
{
    // TODO: Implement in vk_DetailManager_Instance.cpp
    // - Iterate over m_visibles[0/1/2]
    // - For each visible SlotItem, call m_InstanceBuffer.AddInstance()
    // - Group by detail object ID for batched draws
}

} // namespace VK
