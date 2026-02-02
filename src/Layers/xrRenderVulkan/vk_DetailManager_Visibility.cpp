// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// SPDX-License-Identifier: MIT

#include "stdafx.h"
#include "vk_DetailManager.h"
#include "rvk.h"
#include "../xrRender/DetailFormat.h"

extern ECORE_API float r_ssaDISCARD;

namespace VK
{

// ============================================================================
// UpdateVisibleM - Main thread visibility update
// Ported faithfully from DX11 DetailManager.cpp::UpdateVisibleM()
// ============================================================================
void CDetailManager::UpdateVisibleM()
{
    // Diagnostic: log cache state periodically
    {
        static u32 s_vis_diag = 0;
        if (RDEVICE.dwFrame > s_vis_diag + 120)
        {
            s_vis_diag = RDEVICE.dwFrame;
            u32 non_empty_l1 = 0, non_empty_slots = 0, slots_with_items = 0;
            u32 total_slot_items = 0;
            for (u32 mz = 0; mz < dm_cache1_line; mz++)
                for (u32 mx = 0; mx < dm_cache1_line; mx++)
                {
                    CacheSlot1& MS = cache_level1[mz][mx];
                    if (!MS.empty) non_empty_l1++;
                    for (int si = 0; si < dm_cache1_count * dm_cache1_count; si++)
                    {
                        Slot* PS = *MS.slots[si];
                        if (!PS->empty)
                        {
                            non_empty_slots++;
                            for (int g = 0; g < dm_obj_in_slot; g++)
                            {
                                if (!PS->G[g].items.empty())
                                {
                                    slots_with_items++;
                                    total_slot_items += (u32)PS->G[g].items.size();
                                }
                            }
                        }
                    }
                }
            Msg("[Detail Vis] cache_level1: %u/%u non-empty, slots: %u non-empty, %u with items, %u total items, pending=%u",
                non_empty_l1, dm_cache1_line * dm_cache1_line,
                non_empty_slots, slots_with_items, total_slot_items, (u32)cache_task.size());
        }
    }

    Fvector EYE = RDEVICE.vCameraPosition_saved;

    CFrustum View;
    View.CreateFromMatrix(RDEVICE.mFullTransform_saved, FRUSTUM_P_LRTB + FRUSTUM_P_FAR);

    float fade_limit = dm_fade;
    fade_limit = fade_limit * fade_limit;
    float fade_start = 1.f;
    fade_start = fade_start * fade_start;
    float fade_range = fade_limit - fade_start;
    float r_ssaCHEAP = 16 * r_ssaDISCARD;

    // Clear visibility lists before repopulating
    for (u32 i = 0; i < 3; ++i)
    {
        for (u32 j = 0; j < m_visibles[i].size(); j++)
            m_visibles[i][j].clear_not_free();
    }

    // Collect objects for rendering
    for (u32 _mz = 0; _mz < dm_cache1_line; _mz++)
    {
        for (u32 _mx = 0; _mx < dm_cache1_line; _mx++)
        {
            CacheSlot1& MS = cache_level1[_mz][_mx];
            if (MS.empty)
                continue;

            u32 mask = 0xff;
            u32 res = View.testSphere(MS.vis.sphere.P, MS.vis.sphere.R, mask);
            if (fcvNone == res)
                continue;  // invisible-view frustum

            u32 dwCC = dm_cache1_count * dm_cache1_count;

            for (u32 _i = 0; _i < dwCC; _i++)
            {
                Slot* PS = *MS.slots[_i];
                Slot& S = *PS;

                // if slot empty - continue
                if (S.empty)
                    continue;

                // if upper test = fcvPartial - test inner slots
                if (fcvPartial == res)
                {
                    u32 _mask = mask;
                    u32 _res = View.testSphere(S.vis.sphere.P, S.vis.sphere.R, _mask);
                    if (fcvNone == _res)
                        continue;  // invisible-view frustum
                }

                // HOM test
                if (!RImplementation.HOM->visible(S.vis))
                    continue;  // invisible-occlusion

                // Add to visibility structures
                if (RDEVICE.dwFrame > S.frame)
                {
                    // Calc fade factor (per slot)
                    float dist_sq = EYE.distance_to_sqr(S.vis.sphere.P);
                    if (dist_sq > fade_limit)
                    {
                        S.hidden = true;
                        continue;
                    }
                    float alpha = (dist_sq < fade_start) ? 0.f : (dist_sq - fade_start) / fade_range;
                    float alpha_i = 1.f - alpha;
                    float dist_sq_rcp = 1.f / dist_sq;

                    S.frame = RDEVICE.dwFrame + Random.randI(15, 30);
                    for (int sp_id = 0; sp_id < dm_obj_in_slot; sp_id++)
                    {
                        SlotPart& sp = S.G[sp_id];
                        if (sp.id == DetailSlot::ID_Empty) continue;

                        sp.r_items[0].clear_not_free();
                        sp.r_items[1].clear_not_free();
                        sp.r_items[2].clear_not_free();

                        float R = objects[sp.id]->bv_sphere.R;
                        float Rq_drcp = R * R * dist_sq_rcp;

                        SlotItem** siIT = &(*sp.items.begin()), **siEND = &(*sp.items.end());
                        for (; siIT != siEND; siIT++)
                        {
                            SlotItem& Item = *(*siIT);
                            float scale = Item.scale_calculated = Item.scale * alpha_i;
                            float ssa = scale * scale * Rq_drcp;
                            if (ssa < r_ssaDISCARD)
                            {
                                Item.alpha_target = 0;
                                continue;
                            }
                            u32 vis_id = 0;
                            if (ssa > r_ssaCHEAP) vis_id = Item.vis_ID;

                            sp.r_items[vis_id].push_back(*siIT);

                            if (S.hidden)
                            {
                                Item.alpha = 0;
                                S.hidden = false;
                            }
                            Item.alpha_target = 1;
                            Item.distance = dist_sq;
                            Item.position = S.vis.sphere.P;
                        }
                    }
                }
                for (int sp_id = 0; sp_id < dm_obj_in_slot; sp_id++)
                {
                    SlotPart& sp = S.G[sp_id];
                    if (sp.id == DetailSlot::ID_Empty) continue;
                    if (!sp.r_items[0].empty())
                        m_visibles[0][sp.id].push_back(&sp.r_items[0]);
                    if (!sp.r_items[1].empty())
                        m_visibles[1][sp.id].push_back(&sp.r_items[1]);
                    if (!sp.r_items[2].empty())
                        m_visibles[2][sp.id].push_back(&sp.r_items[2]);
                }
            }
        }
    }
}

// ============================================================================
// UpdateVisibleS - Secondary thread visibility (unused)
// ============================================================================
void CDetailManager::UpdateVisibleS()
{
    // Not implemented - could be used for MT update
}

// ============================================================================
// MT_CALC - Multi-threaded calculation trigger
// Ported from DX11 DetailManager.cpp::MT_CALC()
// ============================================================================
void CDetailManager::MT_CALC()
{
    if (!dtFS) return;

    MT.Enter();

    if (m_frame_calc != RDEVICE.dwFrame)
    {
        if ((m_frame_rendered + 1) == RDEVICE.dwFrame)
        {
            Fvector EYE = RDEVICE.vCameraPosition_saved;

            int s_x = iFloor(EYE.x / dm_slot_size + .5f);
            int s_z = iFloor(EYE.z / dm_slot_size + .5f);

            cache_Update(s_x, s_z, EYE, dm_max_decompress);

            UpdateVisibleM();
            m_frame_calc = RDEVICE.dwFrame;
        }
    }

    MT.Leave();
}

} // namespace VK
