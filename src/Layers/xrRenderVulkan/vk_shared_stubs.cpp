// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// Licensed under the same terms as X-Ray Engine (see root License.txt)

// Vulkan Renderer - Shared code stubs
// These are temporary stubs for shared xrRender code that the Vulkan renderer
// references but doesn't have full implementations for yet.
// This allows the project to link while the full Vulkan implementations are developed.

#include "stdafx.h"
#pragma hdrstop

#include "../xrRender/r__dsgraph_structure.h"
#include "../xrRender/r__dsgraph_types.h"
#include "../xrRender/FBasicVisual.h"
#include "../xrRender/Shader.h"
#include "rvk.h"
#include "vk_Visual.h"
#include "vk_sector.h"
#include "vk_R_Backend.h"
#include "vk_pipeline.h"
#include "vk_rendertarget.h"
#include "vk_swapchain.h"
#include "vk_material.h"
#include "../../xrEngine/device.h"
#include "../../xrEngine/IGame_Persistent.h"
#include "../../xrEngine/Environment.h"
#include "../../xrEngine/customhud.h"
#include "../../Include/xrRender/Kinematics.h"

// Include for sorting
#include <algorithm>

using namespace R_dsgraph;

// CalcSSA - compute screen-space area for LOD/culling (same as DX version)
ICF float CalcSSA(float& distSQ, Fvector& C, dxRender_Visual* V)
{
    float R = V->vis.sphere.R + 0;
    distSQ = Device.vCameraPosition.distance_to_sqr(C) + EPS;
    return R / distSQ;
}

ICF float CalcSSA(float& distSQ, Fvector& C, float R)
{
    distSQ = Device.vCameraPosition.distance_to_sqr(C) + EPS;
    return R / distSQ;
}

// Backend globals
extern CBackend RCache;

// SSA globals (defined in rvk.cpp)
extern float r_ssaDISCARD;
extern float r_ssaDONTSORT;
extern float r_ssaLOD_A;
extern float r_ssaLOD_B;

void R_dsgraph_structure::r_dsgraph_render_graph(u32 _priority, bool _clear)
{
    // ========================================================================
    // Phase 1: Simplified Vulkan scene graph rendering
    //
    // Renders all visuals in lstNormal queue (built by Calculate())
    // Sorted by SSA (front-to-back) for early-Z optimization
    // ========================================================================

    // Access render queues from CRender
    CRender& RI = RImplementation;

    // Skip if queue is empty
    if (RI.lstNormal.empty())
        return;

    // Validate lstNormal size
    u32 count = (u32)RI.lstNormal.size();
    if (count > 100000) {
        Msg("! render_graph: lstNormal has suspicious size %u, skipping", count);
        RI.lstNormal.clear();
        return;
    }

    // Sort by SSA (descending = front-to-back) with crash protection
    __try {
        std::sort(RI.lstNormal.begin(), RI.lstNormal.end(),
            [](const R_dsgraph::_NormalItem& a, const R_dsgraph::_NormalItem& b) {
                return a.ssa > b.ssa;
            });
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Msg("! render_graph: CRASH in std::sort at frame %u, lstNormal size=%u",
            Device.dwFrame, count);
        FlushLog();
        RI.lstNormal.clear();
        return;
    }

    // One-shot diagnostic: log stride/type summary of lstNormal
    {
        static bool s_lstNormalSummary = false;
        if (!s_lstNormalSummary) {
            s_lstNormalSummary = true;
            u32 typeCount[16] = {};
            u32 strideCount32 = 0, strideCount36 = 0, strideOther = 0;
            u32 sklInNormal = 0;
            for (u32 i = 0; i < RI.lstNormal.size(); ++i) {
                if (!RI.lstNormal[i].pVisual) continue;
                vkRender_Visual* pDbg = reinterpret_cast<vkRender_Visual*>(RI.lstNormal[i].pVisual);
                if (pDbg->Type < 16) typeCount[pDbg->Type]++;
                // Check skeleton children in lstNormal (shouldn't happen for dynamic objects)
                if (pDbg->Type == 5) { // MT_SKELETON_GEOMDEF_ST
                    sklInNormal++;
                    if (sklInNormal <= 5) {
                        vkFVisual* fvDbg = reinterpret_cast<vkFVisual*>(pDbg);
                        Msg("[LST-NORMAL-SKL] Skeleton child in lstNormal! idx=%u stride=%u vCount=%u name='%s'",
                            i, fvDbg->m_mesh.vStride, fvDbg->m_mesh.vCount,
                            pDbg->dbg_name.size() > 0 ? pDbg->dbg_name.c_str() : "<empty>");
                    }
                }
                vkFVisual* fvS = reinterpret_cast<vkFVisual*>(pDbg);
                if (pDbg->Type == 0 || pDbg->Type == 5 || pDbg->Type == 7) {
                    if (fvS->m_mesh.vStride == 32) strideCount32++;
                    else if (fvS->m_mesh.vStride == 36) strideCount36++;
                    else strideOther++;
                }
            }
            Msg("[LST-NORMAL-SUMMARY] total=%u MT_NORMAL=%u MT_SKEL_ST=%u MT_TREE=%u s32=%u s36=%u sOther=%u",
                (u32)RI.lstNormal.size(), typeCount[0], typeCount[5], typeCount[7],
                strideCount32, strideCount36, strideOther);
        }
    }

    // Render all items
    u32 renderCount = 0;
    for (u32 idx = 0; idx < RI.lstNormal.size(); ++idx)
    {
        auto& item = RI.lstNormal[idx];
        if (!item.pVisual) continue;

        vkRender_Visual* pV = reinterpret_cast<vkRender_Visual*>(item.pVisual);

        __try {
            // Validate pointer is readable before calling Render
            volatile u32 typeCheck = pV->Type;
            (void)typeCheck;
            pV->Render(1.0f);
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            // Detailed crash diagnostics
            const char* typeName = "UNKNOWN";
            u32 vType = 0xDEAD;
            const char* dbgName = "<unreadable>";
            const char* meshInfo = "n/a";

            __try {
                vType = pV->Type;
                switch (vType) {
                case 0: typeName = "MT_NORMAL"; break;
                case 1: typeName = "MT_HIERRARHY"; break;
                case 2: typeName = "MT_PROGRESSIVE"; break;
                case 3: typeName = "MT_SKELETON_ANIM"; break;
                case 4: typeName = "MT_SKELETON_GEOMDEF_PM"; break;
                case 5: typeName = "MT_SKELETON_GEOMDEF_ST"; break;
                case 6: typeName = "MT_LOD"; break;
                case 7: typeName = "MT_TREE_ST"; break;
                case 8: typeName = "MT_PARTICLE_EFFECT"; break;
                case 9: typeName = "MT_PARTICLE_GROUP"; break;
                case 10: typeName = "MT_SKELETON_RIGID"; break;
                case 11: typeName = "MT_TREE_PM"; break;
                default: typeName = "INVALID_TYPE"; break;
                }
            } __except(EXCEPTION_EXECUTE_HANDLER) { vType = 0xDEAD; }

            __try {
                if (pV->dbg_name.c_str())
                    dbgName = pV->dbg_name.c_str();
            } __except(EXCEPTION_EXECUTE_HANDLER) { dbgName = "<crash reading name>"; }

            // Try to read mesh state for FVisual-derived types
            static char meshBuf[256];
            meshBuf[0] = 0;
            __try {
                if (vType == 0 || vType == 2 || vType == 4 || vType == 5 ||
                    vType == 7 || vType == 11) {
                    // These types derive from vkFVisual and have m_mesh
                    vkFVisual* fv = reinterpret_cast<vkFVisual*>(pV);
                    xr_sprintf(meshBuf, sizeof(meshBuf),
                        "VB=%p IB=%p vCount=%u iCount=%u stride=%u",
                        fv->m_mesh.p_rm_Vertices,
                        fv->m_mesh.p_rm_Indices,
                        fv->m_mesh.vCount,
                        fv->m_mesh.iCount,
                        fv->m_mesh.vStride);
                    meshInfo = meshBuf;
                }
            } __except(EXCEPTION_EXECUTE_HANDLER) { meshInfo = "<crash reading mesh>"; }

            Msg("! render_graph: CRASH visual[%u/%u] ptr=%p type=%u(%s) name='%s' mesh=[%s] frame=%u exc=0x%08X",
                idx, count, pV, vType, typeName, dbgName, meshInfo,
                Device.dwFrame, GetExceptionCode());
            FlushLog();
            continue;
        }
        renderCount++;
    }

    // Log occasionally
    static u32 lastLogFrame = 0;
    if (Device.dwFrame - lastLogFrame > 300) {
        Msg("[Vulkan] r_dsgraph_render_graph: rendered %u items", renderCount);
        lastLogFrame = Device.dwFrame;
    }

    // Clear queue if requested
    if (_clear)
        RI.lstNormal.clear();
}

// ============================================================================
// r_dsgraph_render_dynamic - Render dynamic objects with per-object transforms
// ============================================================================
// Dynamic objects (doors, boxes, NPCs, weapons on ground) are stored in lstMatrix
// with their world transform. Each object needs:
// 1. World matrix set via RCache (push constants)
// 2. Bone calculation for skeletal models
// 3. Recursive Render() call
// ============================================================================
void R_dsgraph_structure::r_dsgraph_render_dynamic(bool _clear)
{
    CRender& RI = RImplementation;
    u32 count = RI.lstMatrix.size();
    if (count == 0) return;

    u32 renderCount = 0;

    // One-shot diagnostic: log ALL lstMatrix items on first call
    {
        static bool s_lstMatrixDump = false;
        if (!s_lstMatrixDump && count > 0) {
            s_lstMatrixDump = true;
            Msg("[LST-MATRIX-DUMP] count=%u frame=%u", count, Device.dwFrame);
            u32 typeHist[16] = {};
            for (u32 d = 0; d < count && d < 50; ++d) {
                if (!RI.lstMatrix[d].pVisual) continue;
                vkRender_Visual* pDbg = reinterpret_cast<vkRender_Visual*>(RI.lstMatrix[d].pVisual);
                if (pDbg->Type < 16) typeHist[pDbg->Type]++;
                const char* nm = (pDbg->dbg_name.size() > 0) ? pDbg->dbg_name.c_str() : "<empty>";
                vkFVisual* fvDbg = (pDbg->Type == 0 || pDbg->Type == 5) ?
                    reinterpret_cast<vkFVisual*>(pDbg) : nullptr;
                if (fvDbg) {
                    Msg("[LST-MATRIX] [%u] Type=%u name='%s' stride=%u vCount=%u pos=(%.1f,%.1f,%.1f)",
                        d, pDbg->Type, nm, fvDbg->m_mesh.vStride, fvDbg->m_mesh.vCount,
                        RI.lstMatrix[d].Matrix._41, RI.lstMatrix[d].Matrix._42, RI.lstMatrix[d].Matrix._43);
                } else {
                    Msg("[LST-MATRIX] [%u] Type=%u name='%s' pos=(%.1f,%.1f,%.1f)",
                        d, pDbg->Type, nm,
                        RI.lstMatrix[d].Matrix._41, RI.lstMatrix[d].Matrix._42, RI.lstMatrix[d].Matrix._43);
                }
            }
            if (count > 50)
                Msg("[LST-MATRIX] ... and %u more items", count - 50);
            Msg("[LST-MATRIX-TYPES] MT_NORMAL=%u MT_SKEL_ST=%u MT_SKEL_ANIM=%u MT_SKEL_RIGID=%u MT_HIER=%u",
                typeHist[0], typeHist[5], typeHist[3], typeHist[10], typeHist[1]);
        }
    }

    // lstMatrix now contains LEAF visuals (decomposed by add_Visual -> add_leafs_to_lstMatrix).
    // Bones were already calculated at add-time in add_Visual().
    for (u32 idx = 0; idx < count; ++idx)
    {
        auto& item = RI.lstMatrix[idx];
        if (!item.pVisual) continue;

        vkRender_Visual* pV = reinterpret_cast<vkRender_Visual*>(item.pVisual);

        // One-shot diagnostic: find bedspread in dynamic render list
        {
            static bool s_bedDynDiag = false;
            if (!s_bedDynDiag && pV->dbg_name.size() > 0 &&
                (strstr(pV->dbg_name.c_str(), "bedspread") || strstr(pV->dbg_name.c_str(), "matras")))
            {
                s_bedDynDiag = true;
                Msg("[DYN-BED] Found '%s' in lstMatrix[%u/%u]: Type=%u ptr=%p",
                    pV->dbg_name.c_str(), idx, count, pV->Type, pV);
                Msg("[DYN-BED]   Matrix pos=(%.2f,%.2f,%.2f) row0=(%.4f,%.4f,%.4f,%.4f)",
                    item.Matrix._41, item.Matrix._42, item.Matrix._43,
                    item.Matrix._11, item.Matrix._12, item.Matrix._13, item.Matrix._14);
            }
        }

        __try {
            // Set per-object world matrix and previous frame matrix
            RCache.set_xform_world(item.Matrix);
            RCache.xforms.set_W_prev(item.PrevMatrix);

            // Render the leaf visual directly
            pV->Render(1.0f);
            renderCount++;
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            Msg("! render_dynamic: CRASH visual[%u/%u] ptr=%p type=%u frame=%u exc=0x%08X",
                idx, count, pV, pV->Type, Device.dwFrame, GetExceptionCode());
        }
    }

    // Restore identity world matrix for subsequent static rendering
    RCache.set_xform_world(Fidentity);
    RCache.xforms.set_W_prev(Fidentity);

    if (_clear)
        RI.lstMatrix.clear();
}

void R_dsgraph_structure::r_dsgraph_render_hud(bool NoPS)
{
    // ========================================================================
    // HUD 3D Rendering - Render weapons, hands, and HUD-mode particles
    // ========================================================================
    // mapHUD now contains LEAF visuals (after hierarchy decomposition in
    // add_leafs_HUD_VK). Bones were already calculated during the add phase.

    static u32 s_hudRenderLog = 0;
    if (s_hudRenderLog < 20) {
        Msg("[HUD-DIAG] r_dsgraph_render_hud: mapHUD.size()=%u frame=%u", mapHUD.size(), Device.dwFrame);
        s_hudRenderLog++;
    }

    if (mapHUD.size() == 0 && mapCamAttached.size() == 0) {
        return;  // Nothing to render
    }

    // Save current transformation matrices
    Fmatrix FTold = Device.mFullTransform;
    Fmatrix Pold_prev = Device.mProject_prev;

    // Switch to HUD projection (screen space, no far clipping)
    Device.mFullTransform = Device.mFullTransformHud;
    RCache.set_xform_project(Device.mProjectHud);
    RCache.set_xform_project_prev(Device.mProjectHud);

    // Rendering
    RImplementation.rmNear();
    if (!NoPS)
    {
        // Render all HUD leaf visuals (weapons, hands, attachments)
        for (u32 i = 0; i < mapHUD.size(); i++) {
            R_dsgraph::_MatrixItemS& item = mapHUD[i].val;
            if (!item.pVisual) continue;

            vkRender_Visual* pV = reinterpret_cast<vkRender_Visual*>(item.pVisual);
            RCache.set_xform_world(item.Matrix);
            RCache.set_xform_world_prev(item.PrevMatrix);
            pV->Render(1.0f);
        }
        mapHUD.clear();

        RImplementation.rmNormal();

        // Render camera-attached visuals (binoculars, scopes)
        if (mapCamAttached.size() > 0)
        {
            RImplementation.rmNear();

            Fmatrix camproj;
            camproj.build_projection(
                deg2rad(83.f),
                Device.fASPECT, VIEWPORT_NEAR,
                g_pGamePersistent->Environment().CurrentEnv->far_plane);
            Device.mFullTransform.mul(camproj, Device.mView);
            RCache.set_xform_project(camproj);

            for (u32 i = 0; i < mapCamAttached.size(); i++) {
                R_dsgraph::_MatrixItemS& item = mapCamAttached[i].val;
                if (!item.pVisual) continue;

                vkRender_Visual* pV = reinterpret_cast<vkRender_Visual*>(item.pVisual);
                RCache.set_xform_world(item.Matrix);
                RCache.set_xform_world_prev(item.PrevMatrix);
                pV->Render(1.0f);
            }
            mapCamAttached.clear();

            RImplementation.rmNormal();
        }
    }

    // Render HUD-mode particles (muzzle flashes, etc.) if enabled
    RImplementation.RenderHUDParticles();

    // Restore projection matrices
    Device.mFullTransform = FTold;
    RCache.set_xform_project(Device.mProject);
    RCache.set_xform_project_prev(Pold_prev);
}

void R_dsgraph_structure::r_dsgraph_render_hud_ui()
{
    // ========================================================================
    // HUD UI Rendering - Active item UI overlay
    // ========================================================================
    // Renders UI elements attached to active weapon/item (crosshairs, ammo counters, etc.)
    // ========================================================================

    // Switch to HUD projection
    Fmatrix FTold = Device.mFullTransform;
    Device.mFullTransform = Device.mFullTransformHud;
    RCache.set_xform_project(Device.mProjectHud);

    // Set near clip plane for HUD rendering
    RImplementation.rmNear();

    // Render active item UI (weapon crosshairs, ammo display, etc.)
    extern ENGINE_API CCustomHUD* g_hud;
    if (g_hud)
    {
        g_hud->RenderActiveItemUI();
    }

    // Restore normal clip plane
    RImplementation.rmNormal();

    // Restore normal projection
    Device.mFullTransform = FTold;
    RCache.set_xform_project(Device.mProject);
}

void R_dsgraph_structure::r_dsgraph_render_cam_ui()
{
    // ========================================================================
    // Camera-Attached UI Rendering
    // ========================================================================
    // Renders UI elements attached to camera with custom FOV
    // Used for special camera effects, binoculars, scopes, etc.
    // ========================================================================

    // Save current projection
    Fmatrix FTold = Device.mFullTransform;

    // Build custom projection matrix (FOV 83 degrees)
    Fmatrix camproj;
    camproj.build_projection(
        deg2rad(83.f),
        Device.fASPECT,
        VIEWPORT_NEAR,
        g_pGamePersistent->Environment().CurrentEnv->far_plane
    );

    // Update full transform with custom projection
    Device.mFullTransform.mul(camproj, Device.mView);
    RCache.set_xform_project(camproj);

    // Set near clip plane for camera UI
    RImplementation.rmNear();

    // Render camera-attached UI elements
    extern ENGINE_API CCustomHUD* g_hud;
    if (g_hud)
    {
        g_hud->RenderCamAttachedUI();
    }

    // Restore normal clip plane
    RImplementation.rmNormal();

    // Restore normal projection
    Device.mFullTransform = FTold;
    RCache.set_xform_project(Device.mProject);
}

// ============================================================================
// r_dsgraph_insert_static - Add static visual to render queue (Vulkan)
// ============================================================================
// Routes visuals to appropriate render queues based on shader flags:
// - bDistort → mapDistort (heat shimmer, glass refraction)
// - bStrictB2F → mapSorted (transparent objects, back-to-front sorted)
// - bEmissive → mapEmissive (self-illuminated, glowing objects)
// - default → lstNormal (opaque deferred geometry)
// ============================================================================
void R_dsgraph_structure::r_dsgraph_insert_static(dxRender_Visual* pVisual)
{
    if (!pVisual) return;

    CRender& RI = RImplementation;

    // Marker check prevents duplicate processing
    if (pVisual->vis.marker == RI.marker) return;
    pVisual->vis.marker = RI.marker;

    // Calculate SSA and distance
    float distSQ;
    float SSA = CalcSSA(distSQ, pVisual->vis.sphere.P, pVisual);
    if (SSA <= r_ssaDISCARD) return;

    // ========================================================================
    // Vulkan note: pVisual is actually a vkRender_Visual* (reinterpret_cast).
    // It does NOT have the DX11 ref_shader member, so we must NOT access
    // pVisual->shader. Distortion/emissive routing is skipped for now;
    // all geometry goes to lstNormal (opaque deferred queue).
    // ========================================================================

    // Select shader element (returns default opaque element for Vulkan)
    ShaderElement* sh = RImplementation.rimp_select_sh_static(pVisual, distSQ);
    if (!sh) return;
    if (!pmask[sh->flags.iPriority / 2]) return;

    // Add to normal render list (opaque deferred geometry)
    R_dsgraph::_NormalItem item;
    item.ssa     = SSA;
    item.pVisual = pVisual;
    RI.lstNormal.push_back(item);

    // Debug: log tree visuals being inserted
    static u32 s_treeInsertLog = 0;
    vkRender_Visual* vkV = reinterpret_cast<vkRender_Visual*>(pVisual);
    if ((vkV->Type == 7 || vkV->Type == 11) && s_treeInsertLog < 10)
    {
        Msg("[TREE-INSERT] type=%u name='%s' SSA=%.4f pos=(%.1f,%.1f,%.1f) lstNormal.size=%u",
            vkV->Type, vkV->dbg_name.c_str(), SSA,
            pVisual->vis.sphere.P.x, pVisual->vis.sphere.P.y, pVisual->vis.sphere.P.z,
            (u32)RI.lstNormal.size());
        s_treeInsertLog++;
    }
}

// ============================================================================
// r_dsgraph_insert_dynamic - Add dynamic visual to render queue (Vulkan)
// ============================================================================
// Routes dynamic visuals (characters, items) to appropriate queues:
// - HUD elements → mapHUD* queues
// - Distortion → mapDistort/mapHUDDistort
// - Transparent → mapSorted/mapHUDSorted
// - Emissive → mapEmissive/mapHUDEmissive
// - Opaque → lstNormal
// ============================================================================
void R_dsgraph_structure::r_dsgraph_insert_dynamic(dxRender_Visual* pVisual, Fvector& Center)
{
    if (!pVisual) return;

    CRender& RI = RImplementation;

    // Marker check prevents duplicate processing
    if (pVisual->vis.marker == RI.marker) return;
    pVisual->vis.marker = RI.marker;

    // Calculate SSA and distance
    float distSQ;
    float SSA = CalcSSA(distSQ, Center, pVisual);
    if (SSA <= r_ssaDISCARD) return;

    // ========================================================================
    // Vulkan note: pVisual is actually a vkRender_Visual* (reinterpret_cast).
    // It does NOT have the DX11 ref_shader member, so we must NOT access
    // pVisual->shader. Distortion/emissive/sorted routing is skipped;
    // all geometry goes to lstNormal (opaque deferred queue).
    // ========================================================================

    // Select shader element (returns default opaque element for Vulkan)
    ShaderElement* sh = RImplementation.rimp_select_sh_dynamic(pVisual, distSQ);
    if (!sh) return;
    if (!pmask[sh->flags.iPriority / 2]) return;

    // Skip invisible objects
    if (RI.val_bInvisible) return;

    // Add to normal render list (opaque deferred geometry)
    R_dsgraph::_NormalItem item;
    item.ssa     = SSA;
    item.pVisual = pVisual;
    RI.lstNormal.push_back(item);
}

// ============================================================================
// r_dsgraph_insert_LOD - Add LOD visual to LOD queue
// ============================================================================
void R_dsgraph_structure::r_dsgraph_insert_LOD(dxRender_Visual* pVisual)
{
    if (!pVisual) return;

    // Work with vkRender_Visual internally
    vkRender_Visual* pVKVisual = reinterpret_cast<vkRender_Visual*>(pVisual);

    // Calculate SSA (Screen Space Area)
    float distSQ = Device.vCameraPosition.distance_to_sqr(pVKVisual->vis.sphere.P);
    float ssa = pVKVisual->vis.sphere.R * pVKVisual->vis.sphere.R / (distSQ + EPS);

    // SSA culling - skip if too small
    extern float r_ssaDISCARD;
    extern float r_ssaLOD_A;
    extern float r_ssaLOD_B;

    if (ssa < r_ssaDISCARD) return;

    // Check if should use LOD imposter or full geometry
    if (ssa < r_ssaLOD_A)
    {
        // Use LOD imposter - add to LOD map
        R_dsgraph::mapLOD_Node* N = mapLOD.insertInAnyWay(distSQ);
        N->val.ssa = ssa;
        N->val.pVisual = pVisual;  // Store as dxRender_Visual*
    }

    if (ssa > r_ssaLOD_B || RImplementation.phase == CRender::PHASE_SMAP)
    {
        // Object is close enough - use full detail geometry
        // Add children of LOD visual (if it's an FLOD type)
        // For now, just add the visual itself
        r_dsgraph_insert_static(pVisual);
    }
}

void R_dsgraph_structure::r_dsgraph_render_lods(bool _setup_zb, bool _clear)
{
    // ========================================================================
    // LOD Rendering - Level of Detail geometry (flora imposters, etc.)
    // ========================================================================

    // Get LOD items in proper order
    if (_setup_zb)
        mapLOD.getLR(lstLODs);  // front-to-back (for Z-buffer setup)
    else
        mapLOD.getRL(lstLODs);  // back-to-front (for alpha blending)

    if (lstLODs.empty())
        return;

    // ========================================================================
    // Simplified Vulkan LOD rendering
    // The original D3D implementation uses complex billboarding with vertex locking.
    // For Vulkan, we render LOD visuals directly with basic transforms.
    // This works for most LOD objects (trees, grass) without billboard optimization.
    // ========================================================================

    RCache.set_xform_world(Fidentity);

    for (u32 i = 0; i < lstLODs.size(); i++)
    {
        R_dsgraph::_LodItem& item = lstLODs[i];
        if (!item.pVisual) continue;

        vkRender_Visual* pV = reinterpret_cast<vkRender_Visual*>(item.pVisual);

        // Calculate LOD level based on SSA (screen space area)
        // Higher SSA = closer to camera = higher detail
        extern float r_ssaLOD_A;
        extern float r_ssaLOD_B;
        float ssaRange = r_ssaLOD_A - r_ssaLOD_B;
        if (ssaRange < EPS_S) ssaRange = EPS_S;

        float ssaDiff = item.ssa - r_ssaLOD_B;
        float lodFactor = _max(0.0f, _min(1.0f, ssaDiff / ssaRange));

        // Render with calculated LOD factor
        pV->Render(lodFactor);
    }

    // Clear temporary list
    lstLODs.clear();

    // Clear LOD map if requested
    if (_clear)
        mapLOD.clear();
}

// Callback for sorted rendering (back-to-front)
static void __fastcall vk_sorted_render(R_dsgraph::mapSorted_Node* N)
{
    R_dsgraph::_MatrixItemS& item = N->val;
    if (!item.pVisual) return;

    vkRender_Visual* pV = reinterpret_cast<vkRender_Visual*>(item.pVisual);

    // Set object transformation
    RImplementation.set_Transform(&item.Matrix);

    // Render at full LOD
    pV->Render(1.0f);
}

void R_dsgraph_structure::r_dsgraph_render_sorted()
{
    // ========================================================================
    // Render transparent/sorted geometry (back-to-front)
    // ========================================================================

    // Main scene sorted items
    if (mapSorted.size()) {
        mapSorted.traverseRL(vk_sorted_render);
        mapSorted.clear();
    }

    // HUD sorted items (weapons with transparency, scopes, etc.)
    if (mapHUDSorted.size()) {
        // Switch to HUD projection
        Fmatrix FTold = Device.mFullTransform;
        Device.mFullTransform = Device.mFullTransformHud;
        RCache.set_xform_project(Device.mProjectHud);

        RImplementation.rmNear();
        mapHUDSorted.traverseRL(vk_sorted_render);
        mapHUDSorted.clear();

        // Camera-attached sorted items (scopes, binoculars with custom FOV)
        if (mapCamAttachedSorted.size()) {
            Fmatrix camproj;
            camproj.build_projection(
                deg2rad(83.f),
                Device.fASPECT, VIEWPORT_NEAR,
                g_pGamePersistent->Environment().CurrentEnv->far_plane);
            Device.mFullTransform.mul(camproj, Device.mView);
            RCache.set_xform_project(camproj);

            mapCamAttachedSorted.traverseRL(vk_sorted_render);
            mapCamAttachedSorted.clear();
        }

        RImplementation.rmNormal();

        // Restore normal projection
        Device.mFullTransform = FTold;
        RCache.set_xform_project(Device.mProject);
    } else if (mapCamAttachedSorted.size()) {
        mapCamAttachedSorted.traverseRL(vk_sorted_render);
        mapCamAttachedSorted.clear();
    }
}

// ============================================================================
// r_dsgraph_render_particles - Render particle effects (forward phase)
// ============================================================================
void CRender::r_dsgraph_render_particles()
{
    if (lstParticles.empty()) return;

    for (u32 i = 0; i < lstParticles.size(); ++i)
    {
        auto& item = lstParticles[i];
        if (!item.pVisual) continue;

        vkRender_Visual* pV = reinterpret_cast<vkRender_Visual*>(item.pVisual);
        pV->Render(1.0f);
    }

    lstParticles.clear();
}

#if defined(USE_DX11)
void R_dsgraph_structure::r_dsgraph_render_ScopeSorted()
{
    // TODO: Implement for Vulkan
}
#endif

void R_dsgraph_structure::r_dsgraph_render_emissive(bool clear, bool renderHUD)
{
    // ========================================================================
    // Emissive Geometry Rendering
    // Renders self-illuminated objects (glowing materials, lights, etc.)
    // These are additive blended on top of the scene
    // ========================================================================

    // Main scene emissive geometry
    // Examples: glowing signs, monitor screens, light fixtures
    if (mapEmissive.size())
    {
        mapEmissive.traverseANY(vk_sorted_render);
        if (clear)
            mapEmissive.clear();
    }

    // HUD emissive geometry (if requested)
    // Examples: weapon iron sights, red dot sights, laser pointers, flashlight cones
    if (renderHUD && mapHUDEmissive.size())
    {
        // Switch to HUD projection space
        Fmatrix FTold = Device.mFullTransform;
        Device.mFullTransform = Device.mFullTransformHud;
        RImplementation.rmNear();

        mapHUDEmissive.traverseANY(vk_sorted_render);
        if (clear)
            mapHUDEmissive.clear();

        // Restore normal projection
        Device.mFullTransform = FTold;
        RImplementation.rmNormal();
    }

    // Camera-attached emissive (if exists)
    // Examples: night vision glow, player light cone
    if (mapCamAttachedEmissive.size())
    {
        mapCamAttachedEmissive.traverseANY(vk_sorted_render);
        if (clear)
            mapCamAttachedEmissive.clear();
    }
}

// Callback for wallmark-level rendering (back-to-front, alpha blended)
static void __fastcall vk_wallmark_render(R_dsgraph::mapSorted_Node* N)
{
    R_dsgraph::_MatrixItemS& item = N->val;
    if (!item.pVisual) return;

    vkRender_Visual* pV = reinterpret_cast<vkRender_Visual*>(item.pVisual);

    VkCommandBuffer cmd = RCache.GetCommandBuffer();
    if (cmd == VK_NULL_HANDLE) return;

    VkPipelineLayout layout = VK::g_PipelineManager->GetLayout();
    if (layout == VK_NULL_HANDLE) return;

    // Push world matrix (offset 0)
    vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_VERTEX_BIT,
        0, sizeof(Fmatrix), &item.Matrix);

    // Push per-visual alpha ref (offset 200)
    vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_FRAGMENT_BIT,
        200, sizeof(float), &pV->m_fAlphaRef);

    // Bind material (diffuse texture, descriptor set 1)
    if (pV->m_pMaterial && pV->m_pMaterial->IsValid())
        pV->m_pMaterial->Bind(cmd);
    else if (g_MaterialManager && g_MaterialManager->GetDefaultMaterial())
        g_MaterialManager->GetDefaultMaterial()->Bind(cmd);

    // Cast to vkFVisual to access mesh data
    vkFVisual* pFV = dynamic_cast<vkFVisual*>(pV);
    if (pFV && pFV->m_mesh.p_rm_Vertices && pFV->m_mesh.p_rm_Indices)
    {
        RCache.set_Vertices(pFV->m_mesh.p_rm_Vertices->GetHandle(), pFV->m_mesh.vStride);
        RCache.set_Indices(pFV->m_mesh.p_rm_Indices->GetHandle(), pFV->m_mesh.iType);
        RCache.Render(4, pFV->m_mesh.vBase, 0, pFV->m_mesh.vCount,
                      pFV->m_mesh.iBase, pFV->m_mesh.dwPrimitives);

        RCache.stat.polys += pFV->m_mesh.dwPrimitives;
        RCache.stat.verts += pFV->m_mesh.vCount;
    }
}

void R_dsgraph_structure::r_dsgraph_render_wmarks()
{
    if (!mapWmark.size()) return;

    VkCommandBuffer cmd = RCache.GetCommandBuffer();
    if (cmd == VK_NULL_HANDLE) { mapWmark.clear(); return; }

    // Get wallmark-level pipeline (stride 32, tcOffset 24 for standard level geometry)
    VkPipeline wmpipe = RTarget->GetWallmarkLevelPipeline(32, 24);
    if (wmpipe == VK_NULL_HANDLE) { mapWmark.clear(); return; }

    // Bind wallmark pipeline
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, wmpipe);

    // Push View + Projection matrices
    VkPipelineLayout layout = VK::g_PipelineManager->GetLayout();
    vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_VERTEX_BIT,
                       64, sizeof(Fmatrix), &Device.mView);
    vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_VERTEX_BIT,
                       128, sizeof(Fmatrix), &Device.mProject);

    // Push UV scale for SHORT2 (stride-32 level geometry)
    float uvScale = 1.0f / 1024.0f;
    vkCmdPushConstants(cmd, layout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       192, sizeof(float), &uvScale);

    static u32 s_wmarkDiag = 0;
    if (s_wmarkDiag < 10) {
        Msg("[Vulkan] r_dsgraph_render_wmarks: mapWmark.size()=%u", mapWmark.size());
        s_wmarkDiag++;
    }

    // Traverse and render each wallmark visual (back-to-front for transparency)
    mapWmark.traverseRL(vk_wallmark_render);
    mapWmark.clear();
}

void R_dsgraph_structure::r_dsgraph_render_distort()
{
    // ========================================================================
    // Distortion Effects Rendering
    // Renders heat shimmer, glass refraction, explosions distortion, etc.
    // ========================================================================

    // Main scene distortion (sorted back-to-front for proper blending)
    if (mapDistort.size())
    {
        mapDistort.traverseRL(vk_sorted_render);  // Right-to-Left = Back-to-Front
        mapDistort.clear();
    }

    // HUD distortion effects (weapon scopes, visor effects, etc.)
    if (mapHUDDistort.size())
    {
        // Switch to HUD projection space
        Fmatrix FTold = Device.mFullTransform;
        Device.mFullTransform = Device.mFullTransformHud;

        // Set near clip plane for HUD rendering
        RImplementation.rmNear();

        // Render HUD distortion (sorted back-to-front)
        mapHUDDistort.traverseRL(vk_sorted_render);
        mapHUDDistort.clear();

        // Restore normal clip plane
        RImplementation.rmNormal();

        // Restore normal projection
        Device.mFullTransform = FTold;
    }
}

// ============================================================================
// Subspace rendering - with frustum parameter
// ============================================================================
void R_dsgraph_structure::r_dsgraph_render_subspace(
    IRender_Sector* _sector, CFrustum* _frustum, Fmatrix& mCombined,
    Fvector& _cop, BOOL _dynamic, BOOL _precise_portals)
{
    VERIFY(_sector);

    // Increment marker - critical for avoiding duplicate processing
    RImplementation.marker++;

    // Save current frustum
    CFrustum ViewSave = ViewBase;
    ViewBase = *_frustum;
    View = &ViewBase;

    // TODO: Precise portals optimization (requires Sectors_xrc collider)
    // For now, skip the portal dual-render check - it's an optimization
    // that helps with portals very close to the camera
    if (_precise_portals && RImplementation.rmPortals)
    {
        // Check if camera is too near to some portal - if so force DualRender
        // This would require CDB::COLLIDER Sectors_xrc which we can add later
    }

    // Traverse sector/portal structure from the subspace viewpoint
    vkPortalTraverser.traverse(
        _sector,                    // Starting sector
        ViewBase,                   // Frustum for this subspace
        _cop,                       // Center of projection (light/camera position)
        mCombined,                  // Combined view-projection matrix
        0                           // No special options for subspace (no HOM, no SSA culling)
    );

    // Add static geometry from all visible sectors
    for (u32 s_it = 0; s_it < vkPortalTraverser.r_sectors.size(); s_it++)
    {
        vkCSector* sector = (vkCSector*)vkPortalTraverser.r_sectors[s_it];
        vkRender_Visual* root = sector->root();

        // Safety: Skip sectors with no root visual
        if (!root)
            continue;

        // Process each frustum in the sector (portal-clipped frustums)
        for (u32 v_it = 0; v_it < sector->r_frustums.size(); v_it++)
        {
            // Use the portal-clipped frustum for precise culling
            View = &(sector->r_frustums[v_it]);

            // Add geometry with all frustum planes active
            RImplementation.add_Static(root, SF_RENDERING);
        }
    }

    // Add dynamic objects if requested
    if (_dynamic)
    {
        RImplementation.set_Object(0);

        // Query spatial database for dynamic objects in the subspace frustum
        g_SpatialSpace->q_frustum(
            lstRenderables,
            ISpatial_DB::O_ORDERED,
            STYPE_RENDERABLE,
            ViewBase
        );

        // Process each dynamic object
        for (u32 o_it = 0; o_it < lstRenderables.size(); o_it++)
        {
            ISpatial* spatial = lstRenderables[o_it];
            vkCSector* sector = (vkCSector*)spatial->spatial.sector;

            // Skip objects not associated with a sector
            if (0 == sector)
                continue;

            // Skip objects in sectors not touched by portal traversal
            if (vkPortalTraverser.i_marker != sector->r_marker)
                continue;

            // Test against each frustum in the sector
            for (u32 v_it = 0; v_it < sector->r_frustums.size(); v_it++)
            {
                View = &(sector->r_frustums[v_it]);

                // Quick sphere test
                if (!View->testSphere_dirty(spatial->spatial.sphere.P, spatial->spatial.sphere.R))
                    continue;

                // Get renderable interface
                IRenderable* renderable = spatial->dcast_Renderable();
                if (0 == renderable)
                    continue;

                // Render the object (adds to render queues)
                renderable->renderable_Render();
            }
        }
    }

    // Restore frustum state
    ViewBase = ViewSave;
    View = 0;
}

// ============================================================================
// Subspace rendering - convenience overload (builds frustum from matrix)
// ============================================================================
void R_dsgraph_structure::r_dsgraph_render_subspace(
    IRender_Sector* _sector, Fmatrix& mCombined, Fvector& _cop,
    BOOL _dynamic, BOOL _precise_portals)
{
    // Build frustum from combined matrix
    CFrustum temp;
    temp.CreateFromMatrix(mCombined, FRUSTUM_P_ALL & (~FRUSTUM_P_NEAR));

    // Call main implementation
    r_dsgraph_render_subspace(_sector, &temp, mCombined, _cop, _dynamic, _precise_portals);
}

void R_dsgraph_structure::r_dsgraph_render_R1_box(IRender_Sector* _sector, Fbox& _bb, int _element)
{
    // TODO: Implement for Vulkan
}

// ============================================================================
// Landscape rendering callbacks
// ============================================================================

// Landscape pass 0 - First rendering pass (base geometry)
static void __fastcall vk_landscape_pass0(R_dsgraph::mapLandscape_Node* N)
{
    VERIFY(N);
    vkRender_Visual* V = reinterpret_cast<vkRender_Visual*>(N->val.pVisual);
    VERIFY(V);
    // TODO: Implement shader validation - vkRender_Visual uses shader_id instead of shader object

    // Set shader element for pass 0
    // RCache.set_Element(N->val.se, 0);  // TODO Phase 2.x: Implement shader element setting

    // Calculate LOD based on SSA
    extern float r_ssaDONTSORT;
    float LOD = 1.0f;  // Default LOD
    if (N->val.ssa < r_ssaDONTSORT)
    {
        // Calculate LOD: higher SSA = more detail
        float R = V->vis.sphere.R;
        LOD = _max(0.0f, _min(1.0f, N->val.ssa / r_ssaDONTSORT));
    }

    // Set transform (landscape uses identity transform)
    RImplementation.set_Transform(&N->val.Matrix);

    // Render with calculated LOD
    V->Render(LOD);
}

// Landscape pass 1 - Second rendering pass (lighting/details)
static void __fastcall vk_landscape_pass1(R_dsgraph::mapLandscape_Node* N)
{
    VERIFY(N);
    vkRender_Visual* V = reinterpret_cast<vkRender_Visual*>(N->val.pVisual);
    VERIFY(V);
    // TODO: Implement shader validation - vkRender_Visual uses shader_id instead of shader object

    // Set shader element for pass 1
    // RCache.set_Element(N->val.se, 1);  // TODO Phase 2.x: Implement shader element setting

    // Apply landscape material (lighting parameters)
    // RImplementation.apply_lmaterial();  // TODO Phase 2.x: Implement material application

    // Calculate LOD (same as pass 0)
    extern float r_ssaDONTSORT;
    float LOD = 1.0f;
    if (N->val.ssa < r_ssaDONTSORT)
    {
        float R = V->vis.sphere.R;
        LOD = _max(0.0f, _min(1.0f, N->val.ssa / r_ssaDONTSORT));
    }

    // Set transform
    RImplementation.set_Transform(&N->val.Matrix);

    // Render with calculated LOD
    V->Render(LOD);
}

void R_dsgraph_structure::r_dsgraph_render_landscape(u32 pass, bool _clear)
{
    // ========================================================================
    // Landscape Rendering - Multi-pass terrain rendering
    // ========================================================================
    // Landscape objects are marked with bLandscape flag in shader
    // Rendered in 2 passes:
    //   Pass 0: Base geometry (diffuse, normals)
    //   Pass 1: Lighting and details (lightmap, ambient)
    // ========================================================================

    // Set identity world transform (landscape uses world-space coordinates)
    RCache.set_xform_world(Fidentity);

    // Render appropriate pass
    if (pass == 0)
    {
        // Pass 0: Base geometry (front-to-back for early-Z)
        mapLandscape.traverseLR(vk_landscape_pass0);
    }
    else
    {
        // Pass 1: Lighting (front-to-back)
        mapLandscape.traverseLR(vk_landscape_pass1);
    }

    // Clear landscape queue after rendering if requested
    if (_clear)
        mapLandscape.clear();
}

// Callback for water SSR rendering (front-to-back)
static void __fastcall vk_water_node_ssr(R_dsgraph::mapSorted_Node* N)
{
    VERIFY(N);
    R_dsgraph::_MatrixItemS& item = N->val;
    if (!item.pVisual) return;

    vkRender_Visual* pV = reinterpret_cast<vkRender_Visual*>(item.pVisual);

    // Set object transformation
    RImplementation.set_Transform(&item.Matrix);

    // Render water geometry for SSR pass
    pV->Render(1.0f);
}

// Callback for final water rendering (front-to-back)
static void __fastcall vk_water_node(R_dsgraph::mapSorted_Node* N)
{
    VERIFY(N);
    R_dsgraph::_MatrixItemS& item = N->val;
    if (!item.pVisual) return;

    vkRender_Visual* pV = reinterpret_cast<vkRender_Visual*>(item.pVisual);

    // Set object transformation
    RImplementation.set_Transform(&item.Matrix);

    // Render water geometry
    pV->Render(1.0f);
}

void R_dsgraph_structure::r_dsgraph_render_water_ssr()
{
    mapWater.traverseLR(vk_water_node_ssr);
}

void R_dsgraph_structure::r_dsgraph_render_water()
{
    mapWater.traverseLR(vk_water_node);
    mapWater.clear();
}

// ============================================================================
// Shader resource stubs
// ============================================================================

SGeometry::~SGeometry()
{
    // Vulkan stub - resources managed by Vulkan memory allocator
}

// Map from Shader* to texture name for Vulkan (ref_shader::create stores texture names here)
static xr_map<Shader*, shared_str> s_vkShaderTextureMap;

shared_str vkGetShaderTextureName(Shader* pSh)
{
    if (!pSh) return shared_str();
    auto it = s_vkShaderTextureMap.find(pSh);
    if (it != s_vkShaderTextureMap.end())
        return it->second;
    return shared_str();
}

Shader::~Shader()
{
    s_vkShaderTextureMap.erase(this);
}

STextureList::~STextureList()
{
}

void STextureList::clear()
{
    inherited_vec::clear();
}

void STextureList::clear_not_free()
{
    inherited_vec::clear();
}

u32 STextureList::find_texture_stage(const shared_str& TexName) const
{
    return 0;
}

SMatrixList::~SMatrixList()
{
}

SConstantList::~SConstantList()
{
}

void resptrcode_geom::create(D3DVERTEXELEMENT9* decl, ID3DVertexBuffer* vb, ID3DIndexBuffer* ib)
{
    // Vulkan stub - geometry created differently
}

void resptrcode_geom::create(u32 FVF, ID3DVertexBuffer* vb, ID3DIndexBuffer* ib)
{
    // Vulkan stub - geometry created differently
}

void resptrcode_shader::create(LPCSTR s_shader, LPCSTR s_textures, LPCSTR s_constants, LPCSTR s_matrices)
{
    // Vulkan: allocate a minimal Shader object so ref_shader is non-null
    // and store the texture name for later retrieval (used by wallmarks, etc.)
    if (!s_textures || !s_textures[0]) return;

    Shader* S = xr_new<Shader>();
    _set(S);
    s_vkShaderTextureMap[S] = s_textures;
}

// ============================================================================
// Additional shader/render resource stubs
// ============================================================================

#include "../xrRender/sh_atomic.h"
#include "../xrRender/r_constants.h"
#include "../xrRender/Light_DB.h"

SDeclaration::~SDeclaration()
{
    // Vulkan stub - declarations not needed in Vulkan
}

SVS::SVS() : vs(nullptr)
{
    // Vulkan stub
}

SVS::~SVS()
{
    // Vulkan stub - no D3D vertex shader to release
}

SPS::~SPS()
{
    // Vulkan stub - no D3D pixel shader to release
}

SState::~SState()
{
    // Vulkan stub - no D3D state to release
}

R_constant_table::~R_constant_table()
{
    // Vulkan stub - no D3D constant table to release
}

ShaderElement::ShaderElement()
{
    // Vulkan stub
    flags.iPriority = 0;
    flags.bStrictB2F = 0;
    flags.bEmissive = 0;
    flags.bDistort = 0;
    flags.bWmark = 0;
    flags.bLandscape = 0;
    flags.isLandscape = 0;
    flags.isWater = 0;
    flags.iScopeLense = 0;
}

ShaderElement::~ShaderElement()
{
    // Vulkan stub
}

SPass::~SPass()
{
    // Vulkan stub
}

// ============================================================================
// light class implementation for Vulkan
// (Replaces shared light.cpp which has R_R1/R_R2/R_R3/R_R4 guards)
// ============================================================================

#include "../xrRender/light.h"
#include "../../xrcdb/ISpatial.h"

static const float RSQRTDIV2 = 0.70710678118654752440084436210485f;

// Globals needed by light::get_LOD()
float r_ssaGLOD_start = 96.f, r_ssaGLOD_end = 64.f;

light::light(void) : ISpatial(g_SpatialSpace)
{
    spatial.type = STYPE_LIGHTSOURCE;
    flags.type = POINT;
    flags.bStatic = false;
    flags.bActive = false;
    flags.bShadow = false;
    flags.bVolumetric = false;
    flags.bHudMode = false;
    position.set(0, -1000, 0);
    direction.set(0, -1, 0);
    right.set(0, 0, 0);
    range = 8.f;
    cone = deg2rad(60.f);
    color.set(1, 1, 1, 1);

    m_volumetric_quality = 1;
    m_volumetric_intensity = 1;
    m_volumetric_distance = 1;

    frame_render = 0;
    virtual_size = 0.1f;
    omnipart_num = 0;
    sss_id = -1;
    sss_refresh = 0;
    sss_is_playerlight = false;
    sss_priority = 0;
    omipart_parent = nullptr;
    distance = 0;
    distance_lpos = 0;

    // R2+ fields (shadow mapping, attenuation, etc.)
    falloff = 0;
    attenuation0 = 1.f;
    attenuation1 = 0;
    attenuation2 = 0;
    ZeroMemory(omnipart, sizeof(omnipart));
    indirect_photons = 0;
    s_spot = NULL;
    s_point = NULL;
    m_xform_frame = 0;
    m_xform.identity();
    vis.frame2test = 0;
    vis.query_id = 0;
    vis.query_order = 0;
    vis.visible = true;
    vis.pending = false;
    vis.smap_ID = 0;
    vis.distance = 0;
    ZeroMemory(&X, sizeof(X));
}

light::~light()
{
    for (int f = 0; f < 6; f++) xr_delete(omnipart[f]);
    set_active(false);
}

void light::set_active(bool a)
{
    if (a)
    {
        if (flags.bActive) return;
        flags.bActive = true;
        spatial_register();
        spatial_move();
    }
    else
    {
        if (!flags.bActive) return;
        flags.bActive = false;
        spatial_move();
        spatial_unregister();
    }
}

void light::set_position(const Fvector& P)
{
    float eps = EPS_L;
    if (position.similar(P, eps)) return;
    position.set(P);
    spatial_move();
}

void light::set_range(float R)
{
    float eps = _max(range * 0.1f, EPS_L);
    if (fsimilar(range, R, eps)) return;
    range = R;
    spatial_move();
}

void light::set_cone(float angle)
{
    if (fsimilar(cone, angle)) return;
    VERIFY(cone < deg2rad(121.f));
    cone = angle;
    spatial_move();
}

void light::set_rotation(const Fvector& D, const Fvector& R)
{
    Fvector old_D = direction;
    direction.normalize(D);
    right.normalize(R);
    if (!fsimilar(1.f, old_D.dotproduct(D))) spatial_move();
}

void light::set_texture(LPCSTR name)
{
    // Vulkan: texture-based light projections not yet implemented
}

void light::spatial_move()
{
    switch (flags.type)
    {
    case IRender_Light::REFLECTED:
    case IRender_Light::POINT:
        spatial.sphere.set(position, range);
        break;
    case IRender_Light::SPOT:
        VERIFY2(cone < deg2rad(121.f), "Too large light-cone angle.");
        if (cone >= PI_DIV_2)
        {
            spatial.sphere.P.mad(position, direction, range);
            spatial.sphere.R = range * tanf(cone / 2.f);
        }
        else
        {
            spatial.sphere.R = range / (2.f * _sqr(_cos(cone / 2.f)));
            spatial.sphere.P.mad(position, direction, spatial.sphere.R);
        }
        break;
    case IRender_Light::OMNIPART:
        {
            const float fSphereR = range * RSQRTDIV2;
            spatial.sphere.P.mad(position, direction, fSphereR);
            spatial.sphere.R = fSphereR;
        }
        break;
    }

    ISpatial::spatial_move();

    if (flags.bActive) gi_generate();
    svis.invalidate();
}

vis_data& light::get_homdata()
{
    hom.sphere.set(spatial.sphere.P, spatial.sphere.R);
    hom.box.set(spatial.sphere.P, spatial.sphere.P);
    hom.box.grow(spatial.sphere.R);
    return hom;
}

Fvector light::spatial_sector_point()
{
    return position;
}

extern float ps_r2_slight_fade;

float light::get_LOD()
{
    if (!flags.bShadow) return 1;
    float distSQ = Device.vCameraPosition.distance_to_sqr(spatial.sphere.P) + EPS;
    distance = distSQ;
    float ssa = ps_r2_slight_fade * spatial.sphere.R / distSQ;
    float lod = _sqrt(clampr((ssa - r_ssaGLOD_end) / (r_ssaGLOD_start - r_ssaGLOD_end), 0.f, 1.f));
    return lod;
}

// R2+ light methods for Vulkan
void light::gi_generate()
{
    // Vulkan: GI not yet implemented
}

void light::xform_calc()
{
    if (Device.dwFrame == m_xform_frame) return;
    m_xform_frame = Device.dwFrame;

    Fvector L_dir, L_up, L_right;
    L_dir.set(direction);
    float l_dir_m = L_dir.magnitude();
    if (_valid(l_dir_m) && l_dir_m > EPS_S) L_dir.div(l_dir_m);
    else L_dir.set(0, 0, 1);

    if (right.square_magnitude() > EPS)
    {
        L_right.set(right);
        L_right.normalize();
        L_up.crossproduct(L_dir, L_right);
        L_up.normalize();
        L_right.crossproduct(L_up, L_dir);
        L_right.normalize();
    }
    else
    {
        L_up.set(0, 1, 0);
        if (_abs(L_up.dotproduct(L_dir)) > .99f) L_up.set(0, 0, 1);
        L_right.crossproduct(L_up, L_dir);
        L_right.normalize();
        L_up.crossproduct(L_dir, L_right);
        L_up.normalize();
    }

    Fmatrix mR;
    mR.i = L_right; mR._14 = 0;
    mR.j = L_up;    mR._24 = 0;
    mR.k = L_dir;   mR._34 = 0;
    mR.c = position; mR._44 = 1;

    switch (flags.type)
    {
    case IRender_Light::REFLECTED:
    case IRender_Light::POINT:
        {
            float L_R = range;
            Fmatrix mScale;
            mScale.scale(L_R, L_R, L_R);
            m_xform.mul_43(mR, mScale);
        }
        break;
    case IRender_Light::SPOT:
        {
            float s = 2.f * range * tanf(cone / 2.f);
            Fmatrix mScale;
            mScale.scale(s, s, range);
            m_xform.mul_43(mR, mScale);
        }
        break;
    case IRender_Light::OMNIPART:
        {
            float L_R = 2 * range;
            Fmatrix mScale;
            mScale.scale(L_R, L_R, L_R);
            m_xform.mul_43(mR, mScale);
        }
        break;
    default:
        m_xform.identity();
        break;
    }
}

void light::vis_prepare()
{
    // Vulkan: occlusion queries not yet implemented
}

void light::vis_update()
{
    // Vulkan: occlusion queries not yet implemented
}

//                              +X,             -X,             +Y,             -Y,             +Z,             -Z
static Fvector cmNorm[6] = {
    {0.f, 1.f, 0.f}, {0.f, 1.f, 0.f}, {0.f, 0.f, -1.f}, {0.f, 0.f, 1.f}, {0.f, 1.f, 0.f}, {0.f, 1.f, 0.f}
};
static Fvector cmDir[6] = {
    {1.f, 0.f, 0.f}, {-1.f, 0.f, 0.f}, {0.f, 1.f, 0.f}, {0.f, -1.f, 0.f}, {0.f, 0.f, 1.f}, {0.f, 0.f, -1.f}
};

void light::export_(light_Package& package)
{
    if (flags.bShadow)
    {
        switch (flags.type)
        {
        case IRender_Light::POINT:
            {
                if (0 == omnipart[0])
                    for (int f = 0; f < 6; f++) omnipart[f] = xr_new<light>();
                for (int f = 0; f < 6; f++)
                {
                    light* L = omnipart[f];
                    Fvector R;
                    R.crossproduct(cmNorm[f], cmDir[f]);
                    L->set_type(IRender_Light::OMNIPART);
                    L->set_shadow(true);
                    L->set_position(position);
                    L->set_rotation(cmDir[f], R);
                    L->set_cone(PI_DIV_2);
                    L->set_range(range);
                    L->set_color(color);
                    L->spatial.sector = spatial.sector;
                    L->s_spot = s_spot;
                    L->s_point = s_point;
                    L->set_virtual_size(virtual_size);
                    L->set_hud_mode(get_hud_mode());
                    L->omnipart_num = f;
                    L->omipart_parent = omnipart[0];
                    L->flags.bActive = true;
                    if (ps_ssfx_volumetric.x <= 0)
                        L->set_volumetric(flags.bVolumetric);
                    L->set_volumetric_quality(m_volumetric_quality);
                    L->set_volumetric_intensity(m_volumetric_intensity);
                    L->set_volumetric_distance(m_volumetric_distance);
                    package.v_shadowed.push_back(L);
                }
            }
            break;
        case IRender_Light::SPOT:
            this->set_volumetric_intensity(m_volumetric_intensity);
            package.v_shadowed.push_back(this);
            break;
        }
    }
    else
    {
        switch (flags.type)
        {
        case IRender_Light::POINT: package.v_point.push_back(this); break;
        case IRender_Light::SPOT:  package.v_spot.push_back(this);  break;
        }
    }
}

void light::set_attenuation_params(float a0, float a1, float a2, float fo)
{
    attenuation0 = a0;
    attenuation1 = a1;
    attenuation2 = a2;
    falloff = fo;
}

// ============================================================================
// light_Package implementation for Vulkan
// ============================================================================

#include "../xrRender/light_package.h"

void light_Package::clear()
{
    v_point.clear();
    v_spot.clear();
    v_shadowed.clear();
}

IC bool pred_light_cmp(light* _1, light* _2)
{
    if (_1->vis.pending)
    {
        if (_2->vis.pending) return _1->vis.query_order > _2->vis.query_order;
        else return false;
    }
    else
    {
        if (_2->vis.pending) return true;
        else return _1->range > _2->range;
    }
}

void light_Package::sort()
{
    std::stable_sort(v_point.begin(), v_point.end(), pred_light_cmp);
    std::stable_sort(v_spot.begin(), v_spot.end(), pred_light_cmp);
    std::stable_sort(v_shadowed.begin(), v_shadowed.end(), pred_light_cmp);
}

// ============================================================================
// CLight_DB implementation for Vulkan
// ============================================================================

CLight_DB::CLight_DB()
{
}

CLight_DB::~CLight_DB()
{
}

void CLight_DB::add_light(light* L)
{
    // Prevent adding the same light multiple times per frame
    if (Device.dwFrame == L->frame_render)
        return;

    L->frame_render = Device.dwFrame;

    // Disable shadows if noshadows option is set
    if (RImplementation.o.noshadows)
        L->flags.bShadow = FALSE;

    // Skip static lights unless R1 compatibility mode
    // (static lights are baked into lightmaps in R2+ renderers)
    if (L->flags.bStatic && !ps_r2_ls_flags.test(R2FLAG_R1LIGHTS))
        return;

    // Export light to package for rendering
    L->export_(package);
}

void CLight_DB::Load(IReader* fs)
{
    // Vulkan stub - level light loading not yet implemented
}

#if RENDER != R_R1
void CLight_DB::LoadHemi()
{
    // Vulkan stub
}
#endif

void CLight_DB::Unload()
{
    // Vulkan stub
}

light* CLight_DB::Create()
{
    light* L = xr_new<light>();
    L->flags.bStatic = false;
    L->flags.bActive = false;
    L->flags.bShadow = true;
    return L;
}

void CLight_DB::Update()
{
    // Update sun parameters from environment
    if (sun_original && sun_adapted)
    {
        light* _sun_original = (light*)sun_original._get();
        light* _sun_adapted = (light*)sun_adapted._get();
        CEnvDescriptor& E = *g_pGamePersistent->Environment().CurrentEnv;
        VERIFY(_valid(E.sun_dir));
        VERIFY2(E.sun_dir.y < 0, "Invalid sun direction settings in environment-config");

        Fvector OD, OP, AD, AP;
        OD.set(E.sun_dir).normalize();
        OP.mad(Device.vCameraPosition, OD, -500.f);
        AD.set(0, -.75f, 0).add(E.sun_dir);

        int counter = 0;
        while (AD.magnitude() < 0.001 && counter < 10)
        {
            AD.add(E.sun_dir);
            counter++;
        }
        AD.normalize();
        AP.mad(Device.vCameraPosition, AD, -500.f);

        sun_original->set_rotation(OD, _sun_original->right);
        sun_original->set_position(OP);
        sun_original->set_color(E.sun_color.x, E.sun_color.y, E.sun_color.z);
        sun_original->set_range(600.f);

        sun_adapted->set_rotation(AD, _sun_adapted->right);
        sun_adapted->set_position(AP);
        sun_adapted->set_color(E.sun_color.x * ps_r2_sun_lumscale, E.sun_color.y * ps_r2_sun_lumscale,
                               E.sun_color.z * ps_r2_sun_lumscale);
        sun_adapted->set_range(600.f);

        if (!::Render->is_sun_static())
        {
            sun_adapted->set_rotation(OD, _sun_original->right);
            sun_adapted->set_position(OP);
        }
    }

    // Clear light package — CRITICAL: must happen every frame before add_light() calls
    package.clear();
}
