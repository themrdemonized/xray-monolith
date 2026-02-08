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
// cache_Initialize - Initialize cache arrays and assign slots
// Ported faithfully from DX11 DetailManager_CACHE.cpp
// ============================================================================
void CDetailManager::cache_Initialize()
{
    // Centroid
    cache_cx = 0;
    cache_cz = 0;

    // Initialize cache-grid: assign pool slots to grid positions
    Slot* slt = cache_pool;
    for (u32 i = 0; i < dm_cache_line; i++)
    {
        for (u32 j = 0; j < dm_cache_line; j++, slt++)
        {
            cache[i][j] = slt;
            cache_Task(j, i, slt);
        }
    }

    // Setup level1 cache (hierarchical bounding)
    for (u32 _mz1 = 0; _mz1 < dm_cache1_line; _mz1++)
    {
        for (u32 _mx1 = 0; _mx1 < dm_cache1_line; _mx1++)
        {
            CacheSlot1& MS = cache_level1[_mz1][_mx1];
            for (int _z = 0; _z < dm_cache1_count; _z++)
            {
                for (int _x = 0; _x < dm_cache1_count; _x++)
                {
                    MS.slots[_z * dm_cache1_count + _x] =
                        &cache[_mz1 * dm_cache1_count + _z][_mx1 * dm_cache1_count + _x];
                }
            }
        }
    }

    // NOTE: do NOT clear cache_task here - cache_Task() filled it with pending
    // slots that cache_Update() needs to decompress via cache_Decompress()
    Msg("[Vulkan] Detail cache initialized: %dx%d slots, %u pending tasks",
        dm_cache_line, dm_cache_line, (u32)cache_task.size());
}

// ============================================================================
// cache_Query - Get slot from cache grid
// Ported from DX11 DetailManager_CACHE.cpp
// ============================================================================
CDetailManager::Slot* CDetailManager::cache_Query(int r_x, int r_z)
{
    int gx = w2cg_X(r_x + cache_cx);
    VERIFY(gx >= 0 && gx < (int)dm_cache_line);
    int gz = w2cg_Z(r_z + cache_cz);
    VERIFY(gz >= 0 && gz < (int)dm_cache_line);
    return cache[gz][gx];
}

// ============================================================================
// cache_Task - Initialize slot data from DetailSlot DB
// Ported faithfully from DX11 DetailManager_CACHE.cpp
// This is the critical function that sets up vis.box bounds!
// ============================================================================
void CDetailManager::cache_Task(int gx, int gz, Slot* D)
{
    int sx = cg2w_X(gx);
    int sz = cg2w_Z(gz);
    DetailSlot& DS = QueryDB(sx, sz);

    D->empty = (DS.id0 == DetailSlot::ID_Empty) &&
               (DS.id1 == DetailSlot::ID_Empty) &&
               (DS.id2 == DetailSlot::ID_Empty) &&
               (DS.id3 == DetailSlot::ID_Empty);

    // Unpacking
    u32 old_type = D->type;
    D->type = stPending;
    D->sx = sx;
    D->sz = sz;

    // Initialize vis.box from slot data - this is critical for decompress!
    D->vis.box.min.set(sx * dm_slot_size, DS.r_ybase(), sz * dm_slot_size);
    D->vis.box.max.set(D->vis.box.min.x + dm_slot_size,
                       DS.r_ybase() + DS.r_yheight(),
                       D->vis.box.min.z + dm_slot_size);
    D->vis.box.grow(EPS_L);

    // Clear old items — mark their GPU instances as dead before destroying
    for (u32 i = 0; i < dm_obj_in_slot; i++)
    {
        D->G[i].id = DS.r_id(i);
        for (u32 clr = 0; clr < D->G[i].items.size(); clr++)
        {
            SlotItem* si = D->G[i].items[clr];
            // Mark GPU instance as dead so compute shader skips it
            if (si->gpu_instance_id < m_StagingInstances.size())
            {
                m_StagingInstances[si->gpu_instance_id].obj_id = 0xFFFFFFFF;
                m_GpuDataDirty = true;
            }
            poolSI.destroy(si);
        }
        D->G[i].items.clear();
    }

    if (old_type != stPending)
    {
        VERIFY(stPending == D->type);
        cache_task.push_back(D);
    }
}

// ============================================================================
// cache_Update - Update cache around camera position
// Ported faithfully from DX11 DetailManager_CACHE.cpp
// Uses grid-shifting approach (not LRU eviction)
// ============================================================================
void CDetailManager::cache_Update(int v_x, int v_z, Fvector& view, int limit)
{
    bool bNeedMegaUpdate = (cache_cx != v_x) || (cache_cz != v_z);

    // Cache shift - X axis
    while (cache_cx != v_x)
    {
        if (v_x > cache_cx)
        {
            // shift matrix to left
            cache_cx++;
            for (u32 z = 0; z < dm_cache_line; z++)
            {
                Slot* S = cache[z][0];
                for (u32 x = 1; x < dm_cache_line; x++)
                    cache[z][x - 1] = cache[z][x];
                cache[z][dm_cache_line - 1] = S;
                cache_Task(dm_cache_line - 1, z, S);
            }
        }
        else
        {
            // shift matrix to right
            cache_cx--;
            for (u32 z = 0; z < dm_cache_line; z++)
            {
                Slot* S = cache[z][dm_cache_line - 1];
                for (u32 x = dm_cache_line - 1; x > 0; x--)
                    cache[z][x] = cache[z][x - 1];
                cache[z][0] = S;
                cache_Task(0, z, S);
            }
        }
    }

    // Cache shift - Z axis
    while (cache_cz != v_z)
    {
        if (v_z > cache_cz)
        {
            // shift matrix down
            cache_cz++;
            for (u32 x = 0; x < dm_cache_line; x++)
            {
                Slot* S = cache[dm_cache_line - 1][x];
                for (u32 z = dm_cache_line - 1; z > 0; z--)
                    cache[z][x] = cache[z - 1][x];
                cache[0][x] = S;
                cache_Task(x, 0, S);
            }
        }
        else
        {
            // shift matrix up
            cache_cz--;
            for (u32 x = 0; x < dm_cache_line; x++)
            {
                Slot* S = cache[0][x];
                for (u32 z = 1; z < dm_cache_line; z++)
                    cache[z - 1][x] = cache[z][x];
                cache[dm_cache_line - 1][x] = S;
                cache_Task(x, dm_cache_line - 1, S);
            }
        }
    }

    // Task performer - decompress closest slots first
    BOOL bFullUnpack = FALSE;
    if (cache_task.size() == dm_cache_size)
    {
        limit = dm_cache_size;
        bFullUnpack = TRUE;
    }

    for (int iteration = 0; cache_task.size() && (iteration < limit); iteration++)
    {
        u32 best_id = 0;
        float best_dist = flt_max;

        if (bFullUnpack)
        {
            best_id = cache_task.size() - 1;
        }
        else
        {
            for (u32 entry = 0; entry < cache_task.size(); entry++)
            {
                Slot* S = cache_task[entry];
                VERIFY(stPending == S->type);

                Fvector C;
                S->vis.box.getcenter(C);
                float D = view.distance_to_sqr(C);

                if (D < best_dist)
                {
                    best_dist = D;
                    best_id = entry;
                }
            }
        }

        // Decompress and remove task
        cache_Decompress(cache_task[best_id]);
        cache_task.erase(best_id);
    }

    // Update level1 cache bounds after mega-update
    if (bNeedMegaUpdate)
    {
        for (u32 _mz1 = 0; _mz1 < dm_cache1_line; _mz1++)
        {
            for (u32 _mx1 = 0; _mx1 < dm_cache1_line; _mx1++)
            {
                CacheSlot1& MS = cache_level1[_mz1][_mx1];
                MS.empty = TRUE;
                MS.vis.clear();
                for (int _i = 0; _i < dm_cache1_count * dm_cache1_count; _i++)
                {
                    Slot* PS = *MS.slots[_i];
                    Slot& S = *PS;
                    MS.vis.box.merge(S.vis.box);
                    if (!S.empty)
                        MS.empty = FALSE;
                }
                MS.vis.box.getsphere(MS.vis.sphere.P, MS.vis.sphere.R);
            }
        }
    }
}

// ============================================================================
// cache_Validate - Debug validation
// Ported from DX11 DetailManager_CACHE.cpp
// ============================================================================
BOOL CDetailManager::cache_Validate()
{
    for (u32 z = 0; z < dm_cache_line; z++)
    {
        for (u32 x = 0; x < dm_cache_line; x++)
        {
            int w_x = cg2w_X(x);
            int w_z = cg2w_Z(z);
            Slot* D = cache[z][x];

            if (D->sx != w_x) return FALSE;
            if (D->sz != w_z) return FALSE;
        }
    }
    return TRUE;
}

} // namespace VK
