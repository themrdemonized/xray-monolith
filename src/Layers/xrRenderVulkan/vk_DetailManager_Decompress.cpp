// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// SPDX-License-Identifier: MIT

#include "stdafx.h"
#include "vk_DetailManager.h"
#include "rvk.h"
#include "../xrRender/DetailFormat.h"
#include "../../xrEngine/GameMtlLib.h"
#include "../../xrEngine/IGame_Level.h"  // g_pGameLevel

namespace VK
{

// ============================================================================
// cache_Decompress - Decompress DetailSlot into cache Slot
// Ported from DetailManager.cpp
// ============================================================================
void CDetailManager::cache_Decompress(Slot* D)
{
    if (!D || D->type != stPending)
        return;

    // Get compressed slot data from file
    DetailSlot& DS = QueryDB(D->sx, D->sz);

    // Calculate slot world position
    float sx_world = float(D->sx) * dm_slot_size;
    float sz_world = float(D->sz) * dm_slot_size;

    // Decompress each object type
    for (int obj_idx = 0; obj_idx < dm_obj_in_slot; obj_idx++)
    {
        SlotPart& Part = D->G[obj_idx];
        Part.id = DS.r_id(obj_idx);
        Part.items.clear();
        Part.r_items[0].clear();
        Part.r_items[1].clear();
        Part.r_items[2].clear();

        // Skip empty slots
        if (DS.r_id(obj_idx) == DetailSlot::ID_Empty)
            continue;

        // Validate object ID
        if (DS.r_id(obj_idx) >= objects.size())
        {
            Msg("![Vulkan] Invalid detail object ID: %d (max %d)",
                DS.r_id(obj_idx), objects.size());
            continue;
        }

        VK::CDetail* obj = objects[DS.r_id(obj_idx)];
        if (!obj)
            continue;

        // Get density from palette data (4-bit per object, 0-15)
        u32 density = DS.palette[obj_idx].a0;  // Use palette density
        if (density == 0)
            continue;

        // Spawn detail instances
        u32 base_seed = D->sx * 17 + D->sz * 31 + obj_idx;

        for (u32 i = 0; i < density; i++)
        {
            // Random position within slot
            CRandom rnd(base_seed + i);

            float rx = rnd.randF(0.0f, dm_slot_size);
            float rz = rnd.randF(0.0f, dm_slot_size);

            Fvector pos;
            pos.x = sx_world + rx;
            pos.z = sz_world + rz;

            // Raycast down to find terrain height
            Fvector ray_start = pos;
            ray_start.y = 1000.0f;  // High above terrain

            Fvector ray_dir;
            ray_dir.set(0, -1, 0);

            collide::rq_result RQ;
            if (g_pGameLevel->ObjectSpace.RayPick(ray_start, ray_dir, 2000.0f, collide::rqtBoth, RQ, nullptr))
            {
                pos.y = RQ.range;  // Hit point
            }
            else
            {
                // No hit - skip this instance
                continue;
            }

            // Check material (skip if non-grass)
            if (RQ.O)
            {
                // Hit dynamic object - skip
                continue;
            }

            CDB::TRI* T = g_pGameLevel->ObjectSpace.GetStaticTris() + RQ.element;
            if (T)
            {
                u16 mtl_id = (u16)T->material;
                if (mtl_id != GMLib.GetMaterialIdx("default"))
                {
                    // Check if material supports grass
                    SGameMtl* mtl = GMLib.GetMaterialByIdx(mtl_id);
                    if (!mtl || !(mtl->Flags.test(SGameMtl::flActorObstacle)))
                    {
                        // Not a grass-friendly material
                        continue;
                    }
                }
            }

            // Create SlotItem
            SlotItem* Item = poolSI.create();

            Item->position = pos;
            Item->scale = rnd.randF(obj->m_MinScale, obj->m_MaxScale);
            Item->scale_calculated = Item->scale;

            // Random Y rotation
            float rot_y = rnd.randF(0.0f, PI_MUL_2);
            Item->mRotY.rotateY(rot_y);

            // Animation type (0=still, 1=wave1, 2=wave2)
            Item->vis_ID = rnd.randI(0, 3);
            if (Item->vis_ID > 2)
                Item->vis_ID = 2;

            // Lighting (will be calculated later)
            Item->c_hemi = 0.5f;
            Item->c_sun = 0.5f;

            // Distance/alpha
            Item->distance = 0.0f;
            Item->alpha = 0.0f;
            Item->alpha_target = 0.0f;

            // Normal (up)
            Item->normal.set(0, 1, 0);

            // Add to lists
            Part.items.push_back(Item);
        }

        Msg("[Vulkan] Decompressed slot (%d,%d) obj %d: %d instances",
            D->sx, D->sz, obj_idx, Part.items.size());
    }

    // Mark slot as ready
    D->type = stReady;
}

} // namespace VK
