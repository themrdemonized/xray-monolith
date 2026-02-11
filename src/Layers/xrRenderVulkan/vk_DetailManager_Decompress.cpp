// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// Licensed under the same terms as X-Ray Engine (see root License.txt)

#include "stdafx.h"
#include "vk_DetailManager.h"
#include "rvk.h"
#include "../xrRender/DetailFormat.h"
#include "../../xrEngine/GameMtlLib.h"
#include "../../xrEngine/IGame_Level.h"

#include "../xrRender/cl_intersect.h"

namespace VK
{

// ============================================================================
// Interpolate - Bilinear interpolation of 4 corner values
// Ported from DetailManager_Decompress.cpp
// ============================================================================
static IC float Interpolate(float* base, u32 x, u32 y, u32 size)
{
    float f = float(size);
    float fx = float(x) / f;
    float ifx = 1.f - fx;
    float fy = float(y) / f;
    float ify = 1.f - fy;

    float c01 = base[0] * ifx + base[1] * fx;
    float c23 = base[2] * ifx + base[3] * fx;

    float c02 = base[0] * ify + base[2] * fy;
    float c13 = base[1] * ify + base[3] * fy;

    float cx = ify * c01 + fy * c23;
    float cy = ifx * c02 + fx * c13;
    return (cx + cy) / 2;
}

// ============================================================================
// InterpolateAndDither - Check if detail should be placed at this position
// Ported from DetailManager_Decompress.cpp
// ============================================================================
static IC bool InterpolateAndDither(float* alpha255, u32 x, u32 y, u32 sx, u32 sy, u32 size, int dither[16][16])
{
    clamp(x, (u32)0, size - 1);
    clamp(y, (u32)0, size - 1);
    int c = iFloor(Interpolate(alpha255, x, y, size) + .5f);
    clamp(c, 0, 255);

    u32 row = (y + sy) % 16;
    u32 col = (x + sx) % 16;
    return c > dither[col][row];
}

// ============================================================================
// hash2 - Hash function for deterministic randomization
// ============================================================================
static inline u32 hash2(u32 x, u32 y)
{
    u64 a = ((u64)x << 32) | (u64)y;
    u64 r = 9199940308585234877ull * a + 9199940308585234877ull;
    return (u32)(r >> 32);
}

// ============================================================================
// cache_Decompress - Decompress DetailSlot into cache Slot
// Ported faithfully from DX11 DetailManager_Decompress.cpp
// ============================================================================
void CDetailManager::cache_Decompress(Slot* S)
{
    VERIFY(S);
    Slot& D = *S;

    // Diagnostic: count decompress calls
    {
        static u32 s_total_calls = 0;
        static u32 s_empty_calls = 0;
        s_total_calls++;
        if (D.empty) s_empty_calls++;
        if (s_total_calls <= 3 || (s_total_calls % 500 == 0))
        {
            Msg("[Detail Decompress] call #%u empty=%d sx=%d sz=%d type=%u (empty_total=%u)",
                s_total_calls, (int)D.empty, D.sx, D.sz, D.type, s_empty_calls);
        }
    }

    D.type = stReady;
    if (D.empty) return;

    DetailSlot& DS = QueryDB(D.sx, D.sz);

    // Select polygons via box query against static collision model
    Fvector bC, bD_vec;
    D.vis.box.get_CD(bC, bD_vec);

    if (!g_pGameLevel)
    {
        static bool s_log1 = false;
        if (!s_log1) { Msg("![Detail Decompress] g_pGameLevel is null!"); s_log1 = true; }
        return;
    }

    CDB::MODEL* staticModel = g_pGameLevel->ObjectSpace.GetStaticModel();
    if (!staticModel)
    {
        static bool s_log2 = false;
        if (!s_log2) { Msg("![Detail Decompress] Static model is null!"); s_log2 = true; }
        return;
    }

    xrc.box_options(CDB::OPT_FULL_TEST);
    xrc.box_query(staticModel, bC, bD_vec);
    u32 triCount = xrc.r_count();
    CDB::TRI* tris = g_pGameLevel->ObjectSpace.GetStaticTris();
    Fvector* verts = g_pGameLevel->ObjectSpace.GetStaticVerts();

    // Diagnostic: log decompress results
    {
        static u32 s_decomp_log = 0;
        if (s_decomp_log < 5)
        {
            s_decomp_log++;
            Msg("[Detail Decompress] slot(%d,%d) box_center=(%.1f,%.1f,%.1f) box_dim=(%.1f,%.1f,%.1f) triCount=%u ids=(%d,%d,%d,%d)",
                D.sx, D.sz, bC.x, bC.y, bC.z, bD_vec.x, bD_vec.y, bD_vec.z, triCount,
                DS.r_id(0), DS.r_id(1), DS.r_id(2), DS.r_id(3));
        }
    }

    if (0 == triCount) return;

    // Build shading table from palette data
    float alpha255[dm_obj_in_slot][4];
    for (int i = 0; i < dm_obj_in_slot; i++)
    {
        alpha255[i][0] = 255.f * float(DS.palette[i].a0) / 15.f;
        alpha255[i][1] = 255.f * float(DS.palette[i].a1) / 15.f;
        alpha255[i][2] = 255.f * float(DS.palette[i].a2) / 15.f;
        alpha255[i][3] = 255.f * float(DS.palette[i].a3) / 15.f;
    }

    // Prepare to selection
    float density = ps_r__Detail_density;
    float jitter = density / 1.7f;
    u32 d_size = iCeil(dm_slot_size / density);
    svector<int, dm_obj_in_slot> selected;

    u32 p_rnd = hash2((u32)D.sx, (u32)D.sz);
    CRandom r_selection(0x12071980 ^ p_rnd);
    CRandom r_jitter(0x12071980 ^ p_rnd);
    CRandom r_yaw(0x12071980 ^ p_rnd);
    CRandom r_scale(0x12071980 ^ p_rnd);

    // Prepare actual-bounds-calculations
    Fbox Bounds;
    Bounds.invalidate();

    // Decompressing
    for (u32 z = 0; z <= d_size; z++)
    {
        for (u32 x = 0; x <= d_size; x++)
        {
            // Jitter shift
            u32 shift_x = r_jitter.randI(16);
            u32 shift_z = r_jitter.randI(16);

            // Interpolate and dither palette
            selected.clear();

            if ((DS.id0 != DetailSlot::ID_Empty) && InterpolateAndDither(
                alpha255[0], x, z, shift_x, shift_z, d_size, dither))
                selected.push_back(0);
            if ((DS.id1 != DetailSlot::ID_Empty) && InterpolateAndDither(
                alpha255[1], x, z, shift_x, shift_z, d_size, dither))
                selected.push_back(1);
            if ((DS.id2 != DetailSlot::ID_Empty) && InterpolateAndDither(
                alpha255[2], x, z, shift_x, shift_z, d_size, dither))
                selected.push_back(2);
            if ((DS.id3 != DetailSlot::ID_Empty) && InterpolateAndDither(
                alpha255[3], x, z, shift_x, shift_z, d_size, dither))
                selected.push_back(3);

            // Select one from candidates
            if (selected.empty()) continue;
            u32 index;
            if (selected.size() == 1) index = selected[0];
            else index = selected[r_selection.randI(selected.size())];

            VK::CDetail* Dobj = objects[DS.r_id(index)];
            SlotItem* ItemP = poolSI.create();
            SlotItem& Item = *ItemP;

            // Position (XZ)
            float rx = (float(x) / float(d_size)) * dm_slot_size + D.vis.box.min.x;
            float rz = (float(z) / float(d_size)) * dm_slot_size + D.vis.box.min.z;
            Fvector Item_P;
            Item_P.set(rx + r_jitter.randFs(jitter), D.vis.box.max.y, rz + r_jitter.randFs(jitter));

            // Position (Y) - raycast down through triangles
            float y_found = D.vis.box.min.y - 5;
            Fvector dir;
            dir.set(0, -1, 0);
            Fvector3 terrain_normal;
            terrain_normal.set(0, 1, 0);

            float r_u, r_v, r_range;
            for (u32 tid = 0; tid < triCount; tid++)
            {
                CDB::TRI& T = tris[xrc.r_begin()[tid].id];
                SGameMtl* mtl = GMLib.GetMaterialByIdx(T.material);
                if (mtl->Flags.test(SGameMtl::flPassable))
                    continue;

                Fvector Tv[3] = {verts[T.verts[0]], verts[T.verts[1]], verts[T.verts[2]]};
                if (CDB::TestRayTri(Item_P, dir, Tv, r_u, r_v, r_range, TRUE))
                {
                    if (r_range >= 0)
                    {
                        float y_test = Item_P.y - r_range;
                        if (y_test > y_found)
                        {
                            y_found = y_test;
                            terrain_normal.mknormal(Tv[0], Tv[1], Tv[2]);
                        }
                    }
                }
            }

            if (y_found < D.vis.box.min.y)
            {
                poolSI.destroy(ItemP);
                continue;
            }
            Item_P.y = y_found;
            Item.normal = terrain_normal;

            // Scale
            Item.scale = r_scale.randF(Dobj->m_MinScale * 0.5f, Dobj->m_MaxScale * 0.9f);
            Item.scale *= ps_current_detail_height;

            // Rotation
            Item.mRotY.rotateY(r_yaw.randF(0, PI_MUL_2));
            Item.mRotY.translate_over(Item_P);

            // X-Form BBox
            Fmatrix mScale, mXform;
            Fbox ItemBB;
            mScale.scale(Item.scale, Item.scale, Item.scale);
            mXform.mul_43(Item.mRotY, mScale);
            ItemBB.xform(Dobj->bv_bb, mXform);
            Bounds.merge(ItemBB);

            // Position
            Item.position = Item_P;

            // Lighting from slot data
            Item.c_hemi = DS.r_qclr(DS.c_hemi, 15);
            Item.c_sun = DS.r_qclr(DS.c_dir, 15);

            // Vis-sorting
            if (Dobj->m_Flags & DO_NO_WAVING) Item.vis_ID = 0;
            else
            {
                if (r_scale.randI(0, 3) == 0) Item.vis_ID = 2;
                else Item.vis_ID = 1;
            }

            // Distance/alpha (will be updated by visibility system)
            Item.distance = 0.0f;
            Item.alpha = 0.0f;
            Item.alpha_target = 0.0f;
            Item.scale_calculated = Item.scale;

            // Save it
            D.G[index].items.push_back(ItemP);
        }
    }

    // Update bounds to more tight and real ones
    if (Bounds.is_valid())
    {
        D.vis.clear();
        D.vis.box.set(Bounds);
        D.vis.box.getsphere(D.vis.sphere.P, D.vis.sphere.R);
    }
}

} // namespace VK
