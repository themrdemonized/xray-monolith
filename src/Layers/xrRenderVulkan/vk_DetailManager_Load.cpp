// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// SPDX-License-Identifier: MIT

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

    // Read header
    dtFS->r(&dtH, sizeof(dtH));

    Msg("[Vulkan] DetailHeader:");
    Msg("  version: %d", dtH.version);
    Msg("  object_count: %d", dtH.object_count);
    Msg("  slots_x: %d", dtH.size_x);
    Msg("  slots_z: %d", dtH.size_z);

    // Validate version
    if (dtH.version != DETAIL_VERSION)
    {
        Msg("![Vulkan] Invalid level.details version: %d (expected %d)",
            dtH.version, DETAIL_VERSION);
        FS.r_close(dtFS);
        dtFS = nullptr;
        return;
    }

    // Validate object count
    if (dtH.object_count == 0 || dtH.object_count > dm_max_objects)
    {
        Msg("![Vulkan] Invalid object_count: %d (max %d)",
            dtH.object_count, dm_max_objects);
        FS.r_close(dtFS);
        dtFS = nullptr;
        return;
    }

    // Load detail objects
    objects.reserve(dtH.object_count);

    for (u32 i = 0; i < dtH.object_count; i++)
    {
        VK::CDetail* obj = xr_new<VK::CDetail>();
        obj->Load(dtFS);
        objects.push_back(obj);

        Msg("[Vulkan] Detail object %d: %s", i, obj->m_Name.c_str());
    }

    // Calculate slots data offset
    u32 slot_data_offset = dtFS->tell();

    // Map slots array (pointer into VFS, no copy needed)
    dtSlots = (DetailSlot*)(dtFS->pointer());

    Msg("[Vulkan] Detail slots:");
    Msg("  slots_x: %d", dtH.size_x);
    Msg("  slots_z: %d", dtH.size_z);
    Msg("  total_slots: %d", dtH.size_x * dtH.size_z);
    Msg("  slot_data_offset: 0x%X", slot_data_offset);

    // Initialize empty slot template
    Memory.mem_fill(&DS_empty, 0, sizeof(DS_empty));
    DS_empty.w_id(0, DetailSlot::ID_Empty);
    DS_empty.w_id(1, DetailSlot::ID_Empty);
    DS_empty.w_id(2, DetailSlot::ID_Empty);
    DS_empty.w_id(3, DetailSlot::ID_Empty);

    // Initialize cache system
    cache_Initialize();

    // Create instance buffer (estimate: 50K instances max)
    m_InstanceBuffer.Create(50000);

    // Create Vulkan pipeline
    CreatePipeline();

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
    // Bounds check
    if (sx < 0 || sx >= (int)dtH.size_x || sz < 0 || sz >= (int)dtH.size_z)
    {
        return DS_empty;
    }

    // Return slot from array
    int idx = sz * dtH.size_x + sx;
    return dtSlots[idx];
}

} // namespace VK
