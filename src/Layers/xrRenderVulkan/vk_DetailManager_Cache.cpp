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
// cache_Initialize - Initialize cache arrays
// Ported from DetailManager.cpp
// ============================================================================
void CDetailManager::cache_Initialize()
{
#ifdef DETAIL_RADIUS
    // Variable radius: allocate dynamic arrays
    dm_current_cache1_line = dm_cache1_line;
    dm_current_cache_line = dm_cache_line;
    dm_current_cache_size = dm_cache_size;
    dm_current_fade = dm_fade;

    cache_level1 = (CacheSlot1**)xr_malloc(dm_cache1_line * sizeof(CacheSlot1*));
    for (u32 i = 0; i < dm_cache1_line; i++)
    {
        cache_level1[i] = (CacheSlot1*)xr_malloc(dm_cache1_line * sizeof(CacheSlot1));
        for (u32 j = 0; j < dm_cache1_line; j++)
        {
            new (&cache_level1[i][j]) CacheSlot1();
        }
    }

    cache = (Slot***)xr_malloc(dm_cache_line * sizeof(Slot**));
    for (u32 i = 0; i < dm_cache_line; i++)
    {
        cache[i] = (Slot**)xr_malloc(dm_cache_line * sizeof(Slot*));
        Memory.mem_fill(cache[i], 0, dm_cache_line * sizeof(Slot*));
    }

    cache_pool = (Slot*)xr_malloc(dm_cache_size * sizeof(Slot));
    for (u32 i = 0; i < dm_cache_size; i++)
    {
        new (&cache_pool[i]) Slot();
    }
#else
    // Fixed radius: use static arrays
    for (int z = 0; z < dm_cache1_line; z++)
    {
        for (int x = 0; x < dm_cache1_line; x++)
        {
            cache_level1[z][x].empty = 1;
            cache_level1[z][x].vis.clear();
        }
    }

    for (int z = 0; z < dm_cache_line; z++)
    {
        for (int x = 0; x < dm_cache_line; x++)
        {
            cache[z][x] = nullptr;
        }
    }

    for (int i = 0; i < dm_cache_size; i++)
    {
        cache_pool[i].frame = 0;
        cache_pool[i].empty = 1;
        cache_pool[i].type = stReady;
        cache_pool[i].sx = 0;
        cache_pool[i].sz = 0;
        cache_pool[i].vis.clear();
        cache_pool[i].hidden = false;
    }
#endif

    cache_cx = 0;
    cache_cz = 0;
    cache_task.clear();

    Msg("[Vulkan] Detail cache initialized: %dx%d slots", dm_cache_line, dm_cache_line);
}

// ============================================================================
// cache_Query - Get or allocate cache slot for world coordinates
// Ported from DetailManager.cpp
// ============================================================================
CDetailManager::Slot* CDetailManager::cache_Query(int sx, int sz)
{
    // Convert world → cache grid
    int gx = w2cg_X(sx);
    int gz = w2cg_Z(sz);

    // Bounds check
    if (gx < 0 || gx >= dm_cache_line || gz < 0 || gz >= dm_cache_line)
    {
        return nullptr;
    }

    // Return existing slot
    Slot* S = cache[gz][gx];
    if (S)
    {
        return S;
    }

    // Allocate new slot from pool (LRU)
    u32 oldest_frame = RDEVICE.dwFrame;
    Slot* oldest_slot = nullptr;

    for (int i = 0; i < dm_cache_size; i++)
    {
        Slot* candidate = &cache_pool[i];

        // Prefer empty slots
        if (candidate->empty)
        {
            oldest_slot = candidate;
            break;
        }

        // Find oldest used slot
        if (candidate->frame < oldest_frame)
        {
            oldest_frame = candidate->frame;
            oldest_slot = candidate;
        }
    }

    if (!oldest_slot)
    {
        Msg("![Vulkan] Detail cache exhausted");
        return nullptr;
    }

    // Evict old slot
    if (!oldest_slot->empty)
    {
        int old_gx = w2cg_X(oldest_slot->sx);
        int old_gz = w2cg_Z(oldest_slot->sz);
        if (old_gx >= 0 && old_gx < dm_cache_line && old_gz >= 0 && old_gz < dm_cache_line)
        {
            cache[old_gz][old_gx] = nullptr;
        }

        // Free SlotItems
        for (int obj = 0; obj < dm_obj_in_slot; obj++)
        {
            SlotPart& P = oldest_slot->G[obj];
            for (SlotItemVecIt it = P.items.begin(); it != P.items.end(); ++it)
            {
                poolSI.destroy(*it);
            }
            P.items.clear();
            P.r_items[0].clear();
            P.r_items[1].clear();
            P.r_items[2].clear();
        }
    }

    // Setup new slot
    oldest_slot->sx = sx;
    oldest_slot->sz = sz;
    oldest_slot->empty = 0;
    oldest_slot->type = stPending;  // Will be decompressed later
    oldest_slot->frame = RDEVICE.dwFrame;
    oldest_slot->vis.clear();
    oldest_slot->hidden = false;

    // Insert into cache
    cache[gz][gx] = oldest_slot;

    return oldest_slot;
}

// ============================================================================
// cache_Update - Update cache around camera position
// Ported from DetailManager.cpp
// ============================================================================
void CDetailManager::cache_Update(int sx, int sz, Fvector& view, int limit)
{
    // Update cache center
    cache_cx = sx;
    cache_cz = sz;

    // Collect slots that need decompression
    cache_task.clear();

    for (int z = 0; z < dm_cache_line; z++)
    {
        for (int x = 0; x < dm_cache_line; x++)
        {
            // Convert cache grid → world
            int wx = cg2w_X(x);
            int wz = cg2w_Z(z);

            // Get or create slot
            Slot* S = cache_Query(wx, wz);
            if (!S)
                continue;

            // Mark as used this frame
            S->frame = RDEVICE.dwFrame;

            // If pending decompression, add to task list
            if (S->type == stPending)
            {
                cache_Task(x, z, S);
            }
        }
    }

    // Process decompression tasks (limited per frame)
    int tasks_done = 0;
    for (u32 i = 0; i < cache_task.size() && tasks_done < limit; i++)
    {
        Slot* S = cache_task[i];
        if (S->type == stPending)
        {
            cache_Decompress(S);
            tasks_done++;
        }
    }

    if (tasks_done > 0)
    {
        Msg("[Vulkan] Detail cache: decompressed %d slots", tasks_done);
    }
}

// ============================================================================
// cache_Task - Add slot to decompression queue
// Ported from DetailManager.cpp
// ============================================================================
void CDetailManager::cache_Task(int gx, int gz, Slot* D)
{
    if (!D)
        return;

    // Add to task list if not already there
    if (std::find(cache_task.begin(), cache_task.end(), D) == cache_task.end())
    {
        cache_task.push_back(D);
    }
}

// ============================================================================
// cache_Validate - Debug validation
// ============================================================================
BOOL CDetailManager::cache_Validate()
{
    // Check for dangling pointers, overlaps, etc.
    // TODO: Implement if needed
    return TRUE;
}

} // namespace VK
