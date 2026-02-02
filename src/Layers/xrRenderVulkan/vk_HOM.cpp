// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// SPDX-License-Identifier: MIT

#include "stdafx.h"
#include "vk_HOM.h"
#include "vk_occRasterizer.h"
#include "vk_R_Backend.h"
#include "../../xrEngine/GameFont.h"
#include "../../xrEngine/device.h"
#include "../../xrEngine/IGame_Persistent.h"
#include "../xrRender/fvf.h"

#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>

float psOSSR = .001f;

#ifdef DEBUG
// Debug flags (defined in engine)
extern Flags32 psDeviceFlags;
#endif

void __stdcall vkCHOM::MT_RENDER()
{
    MT.Enter();
    bool b_main_menu_is_active = (g_pGamePersistent->m_pMainMenu && g_pGamePersistent->m_pMainMenu->IsActive());
    if (MT_frame_rendered != Device.dwFrame && !b_main_menu_is_active)
    {
        CFrustum ViewBase;
        ViewBase.CreateFromMatrix(Device.mFullTransform, FRUSTUM_P_LRTB + FRUSTUM_P_FAR);
        Enable();
        Render(ViewBase);
    }
    MT.Leave();
}

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

vkCHOM::vkCHOM()
{
    bEnabled = FALSE;
    m_pModel = nullptr;
    m_pTris = nullptr;
    MT_frame_rendered = 0;
}

vkCHOM::~vkCHOM()
{
}

#pragma pack(push,4)
struct HOM_poly
{
    Fvector v1, v2, v3;
    u32 flags;
};
#pragma pack(pop)

IC float Area(Fvector& v0, Fvector& v1, Fvector& v2)
{
    float e1 = v0.distance_to(v1);
    float e2 = v0.distance_to(v2);
    float e3 = v1.distance_to(v2);

    float p = (e1 + e2 + e3) / 2.f;
    return _sqrt(p * (p - e1) * (p - e2) * (p - e3));
}

void vkCHOM::Load()
{
    // Find and open file
    string_path fName;
    FS.update_path(fName, "$level$", "level.hom");
    if (!FS.exist(fName))
    {
        Msg(" WARNING: Occlusion map '%s' not found.", fName);
        return;
    }
    Msg("* Loading HOM: %s", fName);

    IReader* fs = FS.r_open(fName);
    IReader* S = fs->open_chunk(1);

    // Load tris and merge them
    CDB::Collector CL;
    while (!S->eof())
    {
        HOM_poly P;
        S->r(&P, sizeof(P));
        CL.add_face_packed_D(P.v1, P.v2, P.v3, P.flags, 0.01f);
    }

    // Determine adjacency
    xr_vector<u32> adjacency;
    CL.calc_adjacency(adjacency);

    // Create RASTER-triangles
    m_pTris = xr_alloc<occTri>(u32(CL.getTS()));
    for (u32 it = 0; it < CL.getTS(); it++)
    {
        CDB::TRI& clT = CL.getT()[it];
        occTri& rT = m_pTris[it];

        Fvector& v0 = CL.getV()[clT.verts[0]];
        Fvector& v1 = CL.getV()[clT.verts[1]];
        Fvector& v2 = CL.getV()[clT.verts[2]];

        rT.adjacent[0] = (0xffffffff == adjacency[3 * it + 0]) ? ((occTri*)(-1)) : (m_pTris + adjacency[3 * it + 0]);
        rT.adjacent[1] = (0xffffffff == adjacency[3 * it + 1]) ? ((occTri*)(-1)) : (m_pTris + adjacency[3 * it + 1]);
        rT.adjacent[2] = (0xffffffff == adjacency[3 * it + 2]) ? ((occTri*)(-1)) : (m_pTris + adjacency[3 * it + 2]);
        rT.flags = clT.dummy;
        rT.area = Area(v0, v1, v2);

        if (rT.area < EPS_L)
        {
            Msg("! Invalid HOM triangle (%f,%f,%f)-(%f,%f,%f)-(%f,%f,%f)", VPUSH(v0), VPUSH(v1), VPUSH(v2));
        }

        rT.plane.build(v0, v1, v2);
        rT.skip = 0;
        rT.center.add(v0, v1).add(v2).div(3.f);
    }

    // Create AABB-tree
    m_pModel = xr_new<CDB::MODEL>();
    m_pModel->build(CL.getV(), int(CL.getVS()), CL.getT(), int(CL.getTS()));
    bEnabled = TRUE;
    S->close();
    FS.r_close(fs);
}

void vkCHOM::Unload()
{
    xr_delete(m_pModel);
    xr_free(m_pTris);
    bEnabled = FALSE;
}

class pred_fb
{
public:
    occTri* m_pTris;
    Fvector camera;
public:
    pred_fb(occTri* _t) : m_pTris(_t)
    {
    }

    pred_fb(occTri* _t, Fvector& _c) : m_pTris(_t), camera(_c)
    {
    }

    ICF bool operator()(const CDB::RESULT& _1, const CDB::RESULT& _2) const
    {
        occTri& t0 = m_pTris[_1.id];
        occTri& t1 = m_pTris[_2.id];
        return camera.distance_to_sqr(t0.center) < camera.distance_to_sqr(t1.center);
    }

    ICF bool operator()(const CDB::RESULT& _1) const
    {
        occTri& T = m_pTris[_1.id];
        return T.skip > Device.dwFrame;
    }
};

void vkCHOM::Render_DB(CFrustum& base)
{
    // Safety: Verify HOM data is loaded
    if (!m_pModel || !m_pTris)
    {
        return;
    }

    // Update projection matrices on every frame to ensure valid HOM culling
    float view_dim = occ_dim_0;
    Fmatrix m_viewport = {
        view_dim / 2.f, 0.0f, 0.0f, 0.0f,
        0.0f, -view_dim / 2.f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        view_dim / 2.f + 0 + 0, view_dim / 2.f + 0 + 0, 0.0f, 1.0f
    };
    Fmatrix m_viewport_01 = {
        1.f / 2.f, 0.0f, 0.0f, 0.0f,
        0.0f, -1.f / 2.f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        1.f / 2.f + 0 + 0, 1.f / 2.f + 0 + 0, 0.0f, 1.0f
    };
    m_xform.mul(m_viewport, Device.mFullTransform);
    m_xform_01.mul(m_viewport_01, Device.mFullTransform);

    // Query DB
    xrc.frustum_options(0);
    xrc.frustum_query(m_pModel, base);
    if (0 == xrc.r_count()) return;

    // Prepare
    CDB::RESULT* it = xrc.r_begin();
    CDB::RESULT* end = xrc.r_end();

    Fvector COP = Device.vCameraPosition;
    end = std::remove_if(it, end, pred_fb(m_pTris));
    std::sort(it, end, pred_fb(m_pTris, COP));

    // Build frustum with near plane only
    CFrustum clip;
    clip.CreateFromMatrix(Device.mFullTransform, FRUSTUM_P_NEAR);
    sPoly src, dst;
    u32 _frame = Device.dwFrame;

    // Perform selection, sorting, culling
    for (; it != end; it++)
    {
        // Control skipping
        occTri& T = m_pTris[it->id];
        u32 next = _frame + ::Random.randI(3, 10);

        // Test for good occluder - should be improved :)
        if (!(T.flags || (T.plane.classify(COP) > 0)))
        {
            T.skip = next;
            continue;
        }

        // Access to triangle vertices
        CDB::TRI& t = m_pModel->get_tris()[it->id];
        Fvector* v = m_pModel->get_verts();
        src.clear();
        dst.clear();
        src.push_back(v[t.verts[0]]);
        src.push_back(v[t.verts[1]]);
        src.push_back(v[t.verts[2]]);
        sPoly* P = clip.ClipPoly(src, dst);
        if (0 == P)
        {
            T.skip = next;
            continue;
        }

        // XForm and Rasterize
        u32 pixels = 0;
        int limit = int(P->size()) - 1;
        for (int v = 1; v < limit; v++)
        {
            m_xform.transform(T.raster[0], (*P)[0]);
            m_xform.transform(T.raster[1], (*P)[v + 0]);
            m_xform.transform(T.raster[2], (*P)[v + 1]);
            pixels += Raster.rasterize(&T);
        }
        if (0 == pixels)
        {
            T.skip = next;
            continue;
        }
    }
}

void vkCHOM::Render(CFrustum& base)
{
    if (!bEnabled) return;

    if (Device.Statistic)
        Device.Statistic->RenderCALC_HOM.Begin();

    Raster.clear();
    Render_DB(base);
    Raster.propagade();
    MT_frame_rendered = Device.dwFrame;

    if (Device.Statistic)
        Device.Statistic->RenderCALC_HOM.End();
}

ICF BOOL xform_b0(Fvector2& min, Fvector2& max, float& minz, Fmatrix& X, float _x, float _y, float _z)
{
    float z = _x * X._13 + _y * X._23 + _z * X._33 + X._43;
    if (z < EPS) return TRUE;
    float iw = 1.f / (_x * X._14 + _y * X._24 + _z * X._34 + X._44);
    min.x = max.x = (_x * X._11 + _y * X._21 + _z * X._31 + X._41) * iw;
    min.y = max.y = (_x * X._12 + _y * X._22 + _z * X._32 + X._42) * iw;
    minz = 0.f + z * iw;
    return FALSE;
}

ICF BOOL xform_b1(Fvector2& min, Fvector2& max, float& minz, Fmatrix& X, float _x, float _y, float _z)
{
    float t;
    float z = _x * X._13 + _y * X._23 + _z * X._33 + X._43;
    if (z < EPS) return TRUE;
    float iw = 1.f / (_x * X._14 + _y * X._24 + _z * X._34 + X._44);
    t = (_x * X._11 + _y * X._21 + _z * X._31 + X._41) * iw;
    if (t < min.x) min.x = t;
    else if (t > max.x) max.x = t;
    t = (_x * X._12 + _y * X._22 + _z * X._32 + X._42) * iw;
    if (t < min.y) min.y = t;
    else if (t > max.y) max.y = t;
    t = 0.f + z * iw;
    if (t < minz) minz = t;
    return FALSE;
}

IC BOOL _visible(Fbox& B, Fmatrix& m_xform_01, occRasterizer& Raster)
{
    // Find min/max points of xformed-box
    Fvector2 min, max;
    float z;
    if (xform_b0(min, max, z, m_xform_01, B.min.x, B.min.y, B.min.z)) return TRUE;
    if (xform_b1(min, max, z, m_xform_01, B.min.x, B.min.y, B.max.z)) return TRUE;
    if (xform_b1(min, max, z, m_xform_01, B.max.x, B.min.y, B.max.z)) return TRUE;
    if (xform_b1(min, max, z, m_xform_01, B.max.x, B.min.y, B.min.z)) return TRUE;
    if (xform_b1(min, max, z, m_xform_01, B.min.x, B.max.y, B.min.z)) return TRUE;
    if (xform_b1(min, max, z, m_xform_01, B.min.x, B.max.y, B.max.z)) return TRUE;
    if (xform_b1(min, max, z, m_xform_01, B.max.x, B.max.y, B.max.z)) return TRUE;
    if (xform_b1(min, max, z, m_xform_01, B.max.x, B.max.y, B.min.z)) return TRUE;
    return Raster.test(min.x, min.y, max.x, max.y, z);
}

BOOL vkCHOM::visible(Fbox3& B)
{
    if (!bEnabled) return TRUE;
    if (B.contains(Device.vCameraPosition)) return TRUE;
    MT_SYNC();
    return _visible(B, m_xform_01, Raster);
}

BOOL vkCHOM::visible(Fbox2& B, float depth)
{
    if (!bEnabled) return TRUE;
    MT_SYNC();
    return Raster.test(B.min.x, B.min.y, B.max.x, B.max.y, depth);
}

BOOL vkCHOM::visible(vis_data& vis)
{
    if (Device.dwFrame < vis.hom_frame) return TRUE; // not at this time :)
    if (!bEnabled) return TRUE; // return - everything visible

    u32 frame_current = Device.dwFrame;

#ifdef DEBUG
    if (Device.Statistic)
        Device.Statistic->RenderCALC_HOM.Begin();
#endif

    MT_SYNC();
    BOOL result = _visible(vis.box, m_xform_01, Raster);
    u32 delay = 1;
    if (result)
    {
        // visible - delay next test
        delay = ::Random.randI(5 * 2, 5 * 5);
    }
    else
    {
        // invisible - test next frame
        delay = 1;
    }
    vis.hom_frame = frame_current + delay;
    vis.hom_res = result;

#ifdef DEBUG
    if (Device.Statistic)
        Device.Statistic->RenderCALC_HOM.End();
#endif

    return result;
}

BOOL vkCHOM::visible(sPoly& P)
{
    if (!bEnabled) return TRUE;
    MT_SYNC();

    // Project poly into screen space and test against depth buffer
    Fvector2 min, max;
    min.set(flt_max, flt_max);
    max.set(-flt_max, -flt_max);
    float minz = flt_max;

    for (u32 v = 0; v < P.size(); v++)
    {
        Fvector4 t;
        Fmatrix& M = m_xform_01;
        Fvector& vert = P[v];

        t.x = vert.x * M._11 + vert.y * M._21 + vert.z * M._31 + M._41;
        t.y = vert.x * M._12 + vert.y * M._22 + vert.z * M._32 + M._42;
        t.z = vert.x * M._13 + vert.y * M._23 + vert.z * M._33 + M._43;
        t.w = vert.x * M._14 + vert.y * M._24 + vert.z * M._34 + M._44;

        if (t.w < EPS) return TRUE; // Behind camera

        t.mul(1.f / t.w);

        if (t.x < min.x) min.x = t.x;
        if (t.x > max.x) max.x = t.x;
        if (t.y < min.y) min.y = t.y;
        if (t.y > max.y) max.y = t.y;
        if (t.z < minz) minz = t.z;
    }

    return Raster.test(min.x, min.y, max.x, max.y, minz);
}

void vkCHOM::Disable()
{
    bEnabled = FALSE;
}

void vkCHOM::Enable()
{
    // Only enable if HOM data is actually loaded
    if (m_pModel && m_pTris)
    {
        bEnabled = TRUE;
    }
    else
    {
        bEnabled = FALSE;
    }
}

void vkCHOM::occlude(Fbox2& space)
{
    // Empty - this is for occlusion query accumulation
}

#ifdef DEBUG

// Console variable for HOM rendering toggle (defined externally)
extern Flags32 psDeviceFlags;
const u32 rsOcclusionDraw = (1 << 5); // Flag bit for occlusion draw

void vkCHOM::OnRender()
{
    // Render HOM depth buffer visualization
    Raster.on_dbg_render();

    // Render HOM occluder triangles (if enabled)
    if (psDeviceFlags.is(rsOcclusionDraw))
    {
        if (m_pModel)
        {
            // Build vertex arrays for solid and wireframe rendering
            DEFINE_VECTOR(FVF::L, LVec, LVecIt);
            static LVec poly;
            poly.resize(m_pModel->get_tris_count() * 3);
            static LVec line;
            line.resize(m_pModel->get_tris_count() * 6);

            for (int it = 0; it < m_pModel->get_tris_count(); it++)
            {
                CDB::TRI* T = m_pModel->get_tris() + it;
                Fvector* verts = m_pModel->get_verts();

                // Solid triangles (semi-transparent white)
                poly[it * 3 + 0].set(*(verts + T->verts[0]), 0x80FFFFFF);
                poly[it * 3 + 1].set(*(verts + T->verts[1]), 0x80FFFFFF);
                poly[it * 3 + 2].set(*(verts + T->verts[2]), 0x80FFFFFF);

                // Wireframe edges (opaque white)
                line[it * 6 + 0].set(*(verts + T->verts[0]), 0xFFFFFFFF);
                line[it * 6 + 1].set(*(verts + T->verts[1]), 0xFFFFFFFF);
                line[it * 6 + 2].set(*(verts + T->verts[1]), 0xFFFFFFFF);
                line[it * 6 + 3].set(*(verts + T->verts[2]), 0xFFFFFFFF);
                line[it * 6 + 4].set(*(verts + T->verts[2]), 0xFFFFFFFF);
                line[it * 6 + 5].set(*(verts + T->verts[0]), 0xFFFFFFFF);
            }

            RCache.set_xform_world(Fidentity);

            // Draw solid triangles (semi-transparent)
            // TODO: Device.SetNearer(TRUE) - need to implement depth bias
            // TODO: Set selection shader
            RCache.dbg_Draw(D3DPT_TRIANGLELIST, &*poly.begin(), poly.size() / 3);
            // TODO: Device.SetNearer(FALSE)

            // Draw wireframe
            // TODO: RImplementation.rmNear() - set near render mode
            // TODO: Set editor shader
            RCache.dbg_Draw(D3DPT_LINELIST, &*line.begin(), line.size() / 2);
            // TODO: RImplementation.rmNormal() - restore normal render mode
        }
    }
}

void vkCHOM::stats()
{
    // Display HOM statistics on screen
    if (m_pModel)
    {
        CGameFont& F = *Device.Statistic->Font();
        F.OutNext(" **** HOM-occ ****");
        // TODO: Track tris_in_frame_visible and tris_in_frame counters
        // F.OutNext("  visible:  %2d", tris_in_frame_visible);
        // F.OutNext("  frustum:  %2d", tris_in_frame);
        F.OutNext("    total:  %2d", m_pModel->get_tris_count());
    }
}
#endif
