// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// Licensed under the same terms as X-Ray Engine (see root License.txt)

#include "stdafx.h"
#include "vk_DetailManager.h"
#include "rvk.h"
#include "../xrRender/DetailFormat.h"

namespace VK
{

// ============================================================================
// Load - Load detail objects from level.details file
// Ported from DetailManager.cpp::CDetailManager::Load()
// ============================================================================
void CDetailManager::Load()
{
    // Check if file exists
    if (!FS.exist("$level$", "level.details"))
    {
        dtFS = nullptr;
        Msg("[Vulkan] level.details not found - details disabled");
        return;
    }

    // Open file
    string_path fn;
    FS.update_path(fn, "$level$", "level.details");
    dtFS = FS.r_open(fn);

    // ========================================================================
    // Chunk 0: Header (must use chunked reading!)
    // ========================================================================
    dtFS->r_chunk_safe(0, &dtH, sizeof(dtH));

    Msg("[Vulkan] DetailHeader:");
    Msg("  version: %d", dtH.version);
    Msg("  object_count: %d", dtH.object_count);
    Msg("  offs_x: %d, offs_z: %d", dtH.offs_x, dtH.offs_z);
    Msg("  size_x: %d, size_z: %d", dtH.size_x, dtH.size_z);

    // Validate version
    if (dtH.version != DETAIL_VERSION)
    {
        Msg("![Vulkan] Invalid level.details version: %d (expected %d)",
            dtH.version, DETAIL_VERSION);
        FS.r_close(dtFS);
        dtFS = nullptr;
        return;
    }

    u32 m_count = dtH.object_count;

    // Validate object count
    if (m_count == 0 || m_count > dm_max_objects)
    {
        Msg("![Vulkan] Invalid object_count: %d (max %d)",
            m_count, dm_max_objects);
        FS.r_close(dtFS);
        dtFS = nullptr;
        return;
    }

    // ========================================================================
    // Chunk 1: Detail model objects (each in a sub-chunk)
    // ========================================================================
    IReader* m_fs = dtFS->open_chunk(1);
    if (!m_fs)
    {
        Msg("![Vulkan] Failed to open objects chunk");
        FS.r_close(dtFS);
        dtFS = nullptr;
        return;
    }

    objects.reserve(m_count);
    for (u32 m_id = 0; m_id < m_count; m_id++)
    {
        VK::CDetail* dt = xr_new<VK::CDetail>();
        IReader* S = m_fs->open_chunk(m_id);
        if (S)
        {
            dt->Load(S);
            S->close();
        }
        else
        {
            Msg("![Vulkan] Failed to open detail object chunk %d", m_id);
        }
        objects.push_back(dt);

        Msg("[Vulkan] Detail object %d: %s", m_id, dt->m_Name.c_str());
    }
    m_fs->close();

    // ========================================================================
    // Chunk 2: Slots array (pointer into VFS, no copy needed)
    // ========================================================================
    IReader* m_slots = dtFS->open_chunk(2);
    if (!m_slots)
    {
        Msg("![Vulkan] Failed to open slots chunk");
        FS.r_close(dtFS);
        dtFS = nullptr;
        return;
    }
    dtSlots = (DetailSlot*)m_slots->pointer();
    m_slots->close();

    Msg("[Vulkan] Detail slots:");
    Msg("  size_x: %d, size_z: %d", dtH.size_x, dtH.size_z);
    Msg("  total_slots: %d", dtH.size_x * dtH.size_z);

    // ========================================================================
    // Initialize cache system
    // ========================================================================

    // Initialize empty slot template
    Memory.mem_fill(&DS_empty, 0, sizeof(DS_empty));
    DS_empty.w_id(0, DetailSlot::ID_Empty);
    DS_empty.w_id(1, DetailSlot::ID_Empty);
    DS_empty.w_id(2, DetailSlot::ID_Empty);
    DS_empty.w_id(3, DetailSlot::ID_Empty);

#ifdef DETAIL_RADIUS
    // Copy current detail radius settings (from console variables)
    dm_size = dm_current_size;
    dm_cache_line = dm_current_cache_line;
    dm_cache1_line = dm_current_cache1_line;
    dm_cache_size = dm_current_cache_size;
    dm_fade = dm_current_fade;

    // Allocate cache_level1 (2D array of CacheSlot1)
    cache_level1 = (CacheSlot1**)Memory.mem_alloc(dm_cache1_line * sizeof(CacheSlot1*));
    for (u32 i = 0; i < dm_cache1_line; ++i)
    {
        cache_level1[i] = (CacheSlot1*)Memory.mem_alloc(dm_cache1_line * sizeof(CacheSlot1));
        for (u32 j = 0; j < dm_cache1_line; ++j)
            new(&(cache_level1[i][j])) CacheSlot1();
    }

    // Allocate cache grid (2D array of Slot*)
    cache = (Slot***)Memory.mem_alloc(dm_cache_line * sizeof(Slot**));
    for (u32 i = 0; i < dm_cache_line; ++i)
        cache[i] = (Slot**)Memory.mem_alloc(dm_cache_line * sizeof(Slot*));

    // Allocate cache pool (flat array of Slots)
    cache_pool = (Slot*)Memory.mem_alloc(dm_cache_size * sizeof(Slot));
    for (u32 i = 0; i < dm_cache_size; ++i)
        new(&(cache_pool[i])) Slot();
#endif

    cache_Initialize();

    // Make dither matrix (same as DX11)
    bwdithermap(2, dither);

    // ========================================================================
    // Create GPU-driven pipeline buffers and compute pipelines
    // ========================================================================
    CreateGpuBuffers();
    CreateComputePipeline();
    CreateHZB();

    // ========================================================================
    // GPU procedural grass generation (replaces CPU cache + GPU cull)
    // ========================================================================
    if (m_bGpuGenerationEnabled)
    {
        BakeHeightmap();
        UploadSlotData();
        UploadObjInfo();
        CreateGpuGenPipeline();
        CreateTrailMap();
    }

    // ========================================================================
    // Create Vulkan graphics pipeline
    // ========================================================================
    CreatePipeline();

    // ========================================================================
    // Load swing parameters from config (same as DX11)
    // ========================================================================
    // normal
    swing_desc[0].amp1 = pSettings->r_float("details", "swing_normal_amp1");
    swing_desc[0].amp2 = pSettings->r_float("details", "swing_normal_amp2");
    swing_desc[0].rot1 = pSettings->r_float("details", "swing_normal_rot1");
    swing_desc[0].rot2 = pSettings->r_float("details", "swing_normal_rot2");
    swing_desc[0].speed = pSettings->r_float("details", "swing_normal_speed");
    // fast
    swing_desc[1].amp1 = pSettings->r_float("details", "swing_fast_amp1");
    swing_desc[1].amp2 = pSettings->r_float("details", "swing_fast_amp2");
    swing_desc[1].rot1 = pSettings->r_float("details", "swing_fast_rot1");
    swing_desc[1].rot2 = pSettings->r_float("details", "swing_fast_rot2");
    swing_desc[1].speed = pSettings->r_float("details", "swing_fast_speed");

    m_bCreated = true;

    Msg("[Vulkan] CDetailManager loaded: %d objects, %dx%d slots",
        objects.size(), dtH.size_x, dtH.size_z);
}

// ============================================================================
// QueryDB - Get DetailSlot from world coordinates
// ported from DetailManager.cpp
// ============================================================================
DetailSlot& CDetailManager::QueryDB(int sx, int sz)
{
    // Convert from world slot coords to array index
    // DX11: db_x = sx + dtH.offs_x (offs are stored as positive offsets to add)
    int db_x = sx + dtH.offs_x;
    int db_z = sz + dtH.offs_z;

    // Bounds check
    if ((db_x >= 0) && (db_x < int(dtH.size_x)) && (db_z >= 0) && (db_z < int(dtH.size_z)))
    {
        u32 linear_id = db_z * dtH.size_x + db_x;
        return dtSlots[linear_id];
    }
    else
    {
        // Empty slot
        DS_empty.w_id(0, DetailSlot::ID_Empty);
        DS_empty.w_id(1, DetailSlot::ID_Empty);
        DS_empty.w_id(2, DetailSlot::ID_Empty);
        DS_empty.w_id(3, DetailSlot::ID_Empty);
        return DS_empty;
    }
}

} // namespace VK
