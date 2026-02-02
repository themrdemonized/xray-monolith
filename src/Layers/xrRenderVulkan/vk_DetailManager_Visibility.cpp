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
// UpdateVisibleM - Main thread visibility update
// Ported from DetailManager.cpp
// ============================================================================
void CDetailManager::UpdateVisibleM()
{
    // Get camera position
    Fvector cam_pos = RDEVICE.vCameraPosition;

    // Convert to slot coordinates
    int sx = iFloor(cam_pos.x / dm_slot_size);
    int sz = iFloor(cam_pos.z / dm_slot_size);

    // Update cache (decompress up to 4 slots per frame)
    cache_Update(sx, sz, cam_pos, 4);

    // Clear visibility lists
    details_clear();

    // Get frustum
    CFrustum& frustum = RImplementation.ViewBase;

    // Iterate visible cache slots
    for (int z = 0; z < dm_cache_line; z++)
    {
        for (int x = 0; x < dm_cache_line; x++)
        {
            Slot* S = cache[z][x];
            if (!S || S->empty || S->type != stReady)
                continue;

            if (S->hidden)
                continue;

            // Frustum test slot AABB
            float wx = float(S->sx) * dm_slot_size;
            float wz = float(S->sz) * dm_slot_size;

            Fbox slot_aabb;
            slot_aabb.min.set(wx, cam_pos.y - 50.0f, wz);
            slot_aabb.max.set(wx + dm_slot_size, cam_pos.y + 50.0f, wz + dm_slot_size);

            u32 frustum_mask = 0xff;
            if (fcvNone == frustum.testAABB(slot_aabb.data(), frustum_mask))
                continue;

            // Process each object type
            for (int obj = 0; obj < dm_obj_in_slot; obj++)
            {
                SlotPart& Part = S->G[obj];

                if (Part.id == DetailSlot::ID_Empty)
                    continue;

                if (Part.id >= objects.size())
                    continue;

                // Update each instance
                for (SlotItemVecIt it = Part.items.begin(); it != Part.items.end(); ++it)
                {
                    SlotItem* Item = *it;

                    // Calculate distance to camera
                    Fvector delta;
                    delta.sub(Item->position, cam_pos);
                    Item->distance = delta.magnitude();

                    // LOD fade
                    float fade_start = fade_distance * 0.7f;
                    float fade_end = fade_distance;

                    if (Item->distance > fade_end)
                    {
                        Item->alpha_target = 0.0f;
                        continue;  // Too far
                    }
                    else if (Item->distance > fade_start)
                    {
                        // Fade out
                        float fade_factor = (fade_end - Item->distance) / (fade_end - fade_start);
                        Item->alpha_target = fade_factor;
                    }
                    else
                    {
                        Item->alpha_target = 1.0f;
                    }

                    // Smooth alpha transition
                    float alpha_speed = 2.0f;
                    if (Item->alpha < Item->alpha_target)
                    {
                        Item->alpha += RDEVICE.fTimeDelta * alpha_speed;
                        if (Item->alpha > Item->alpha_target)
                            Item->alpha = Item->alpha_target;
                    }
                    else if (Item->alpha > Item->alpha_target)
                    {
                        Item->alpha -= RDEVICE.fTimeDelta * alpha_speed;
                        if (Item->alpha < Item->alpha_target)
                            Item->alpha = Item->alpha_target;
                    }

                    // Skip if alpha too low
                    if (Item->alpha < 0.01f)
                        continue;

                    // Add to render list based on animation type
                    Part.r_items[Item->vis_ID].push_back(Item);
                }
            }
        }
    }

    // Build visibility lists for rendering (grouped by object)
    for (u32 obj_id = 0; obj_id < objects.size(); obj_id++)
    {
        // For each animation type
        for (int anim = 0; anim < 3; anim++)
        {
            // Resize visibility list
            if (m_visibles[anim].size() <= obj_id)
            {
                m_visibles[anim].resize(obj_id + 1);
            }

            xr_vector<SlotItemVec*>& vis_vec = m_visibles[anim][obj_id];
            vis_vec.clear();

            // Collect all SlotParts for this object
            for (int z = 0; z < dm_cache_line; z++)
            {
                for (int x = 0; x < dm_cache_line; x++)
                {
                    Slot* S = cache[z][x];
                    if (!S || S->empty || S->type != stReady)
                        continue;

                    for (int obj_idx = 0; obj_idx < dm_obj_in_slot; obj_idx++)
                    {
                        SlotPart& Part = S->G[obj_idx];

                        if (Part.id == obj_id && !Part.r_items[anim].empty())
                        {
                            vis_vec.push_back(&Part.r_items[anim]);
                        }
                    }
                }
            }
        }
    }
}

// ============================================================================
// UpdateVisibleS - Secondary thread visibility (unused for now)
// ============================================================================
void CDetailManager::UpdateVisibleS()
{
    // Not implemented yet - could be used for MT update
}

// ============================================================================
// MT_CALC - Multi-threaded calculation trigger
// Ported from DetailManager.cpp
// ============================================================================
void CDetailManager::MT_CALC()
{
    MT.Enter();

    if (m_frame_calc == RDEVICE.dwFrame)
    {
        MT.Leave();
        return;
    }

    m_frame_calc = RDEVICE.dwFrame;

    // Update visibility
    UpdateVisibleM();

    MT.Leave();
}

} // namespace VK
