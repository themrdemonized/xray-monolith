// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// SPDX-License-Identifier: MIT

// Vulkan Renderer - Shared code stubs
// These are temporary stubs for shared xrRender code that the Vulkan renderer
// references but doesn't have full implementations for yet.
// This allows the project to link while the full Vulkan implementations are developed.

#include "stdafx.h"
#pragma hdrstop

#include "../xrRender/r__dsgraph_structure.h"
#include "../xrRender/Shader.h"
#include "rvk.h"
#include "vk_Visual.h"
#include "vk_sector.h"
#include "vk_R_Backend.h"
#include "../../xrEngine/device.h"
#include "../../xrEngine/IGame_Persistent.h"
#include "../../xrEngine/Environment.h"

// Include for sorting
#include <algorithm>

// Type mapping removed - use explicit casts instead
// #define dxRender_Visual vkRender_Visual

// Forward declarations for HUD
class CHUDManager;
extern CHUDManager* g_hud;

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

    // ========================================================================
    // Sort by SSA (descending = front-to-back)
    // This optimizes early-Z rejection in the depth buffer
    // ========================================================================
    std::sort(RI.lstNormal.begin(), RI.lstNormal.end(),
        [](const R_dsgraph::_NormalItem& a, const R_dsgraph::_NormalItem& b) {
            return a.ssa > b.ssa;  // Larger SSA (closer) first
        });

    // ========================================================================
    // Render all items
    // ========================================================================
    u32 renderCount = 0;
    for (auto& item : RI.lstNormal)
    {
        if (!item.pVisual) continue;

        // Cast to Vulkan visual type
        vkRender_Visual* pV = reinterpret_cast<vkRender_Visual*>(item.pVisual);

        // Render with full LOD (1.0)
        // TODO Phase 2: Calculate actual LOD from SSA
        pV->Render(1.0f);
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

void R_dsgraph_structure::r_dsgraph_render_hud(bool NoPS)
{
    // ========================================================================
    // HUD 3D Rendering - Render weapons, hands, and HUD-mode particles
    // ========================================================================

    if (mapHUD.size() == 0) {
        return;  // Nothing to render
    }

    // Save current transformation matrix
    Fmatrix FTold = Device.mFullTransform;

    // Switch to HUD projection (screen space, no far clipping)
    Device.mFullTransform = Device.mFullTransformHud;

    // Set near clip plane for HUD (1 meter in front of camera)
    RImplementation.rmNear();

    // Render all HUD visuals (weapons, hands, attachments)
    // mapHUD is a FixedMAP — iterate TNodes, access val for _MatrixItemS
    for (u32 i = 0; i < mapHUD.size(); i++) {
        R_dsgraph::_MatrixItemS& item = mapHUD[i].val;
        if (!item.pVisual) continue;

        vkRender_Visual* pV = reinterpret_cast<vkRender_Visual*>(item.pVisual);

        // Set object transformation
        RImplementation.set_Transform(&item.Matrix);

        // Render at full LOD
        pV->Render(1.0f);
    }

    // Clear HUD render queue
    mapHUD.clear();

    // Render HUD-mode particles (muzzle flashes, etc.) if enabled
    if (!NoPS) {
        RImplementation.RenderHUDParticles();
    }

    // Restore normal projection
    Device.mFullTransform = FTold;
    RImplementation.rmNormal();
}

void R_dsgraph_structure::r_dsgraph_render_hud_ui()
{
    // ========================================================================
    // HUD UI Rendering - Active item UI overlay
    // ========================================================================
    // Renders UI elements attached to active weapon/item (crosshairs, ammo counters, etc.)
    // Rendered in HUD projection space (2D screen overlay)
    // ========================================================================

    // Switch to HUD projection
    Fmatrix FTold = Device.mFullTransform;
    Device.mFullTransform = Device.mFullTransformHud;

    // Set near clip plane for HUD rendering
    RImplementation.rmNear();

    // Render active item UI (weapon crosshairs, ammo display, etc.)
    // TODO: Implement HUD UI rendering
    // Temporarily disabled due to undefined CHUDManager type
    /*
    extern CHUDManager* g_hud;
    if (g_hud)
    {
        g_hud->RenderActiveItemUI();
    }
    */

    // Restore normal clip plane
    RImplementation.rmNormal();

    // Restore normal projection
    Device.mFullTransform = FTold;
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
        deg2rad(83.f),                                          // FOV: 83 degrees
        Device.fASPECT,                                         // Aspect ratio
        VIEWPORT_NEAR,                                          // Near plane (R_VIEWPORT_NEAR)
        g_pGamePersistent->Environment().CurrentEnv->far_plane  // Far plane
    );

    // Update full transform with custom projection
    Device.mFullTransform.mul(camproj, Device.mView);

    // Set near clip plane for camera UI
    RImplementation.rmNear();

    // Render camera-attached UI elements
    // TODO: Implement camera UI rendering
    // Temporarily disabled due to undefined CHUDManager type
    /*
    extern CHUDManager* g_hud;
    if (g_hud)
    {
        g_hud->RenderCamAttachedUI();
    }
    */

    // Restore normal clip plane
    RImplementation.rmNormal();

    // Restore normal projection
    Device.mFullTransform = FTold;
}

// ============================================================================
// r_dsgraph_insert_static - Add static visual to render queue (Vulkan)
// ============================================================================
// Simplified version: skips DX shader selection, adds directly to lstNormal.
// Original DX version routes to mapNormalPasses/mapSorted/mapDistort based on
// shader elements. Vulkan uses flat list + per-visual shader binding.
// ============================================================================
void R_dsgraph_structure::r_dsgraph_insert_static(dxRender_Visual* pVisual)
{
    if (!pVisual) return;

    // Work with vkRender_Visual internally, cast to/from dxRender_Visual at boundaries
    vkRender_Visual* pVKVisual = reinterpret_cast<vkRender_Visual*>(pVisual);

    CRender& RI = RImplementation;

    // Marker check prevents duplicate processing
    if (pVKVisual->vis.marker == RI.marker) return;
    pVKVisual->vis.marker = RI.marker;

    // Calculate SSA for culling
    float distSQ = Device.vCameraPosition.distance_to_sqr(pVKVisual->vis.sphere.P);
    float R = pVKVisual->vis.sphere.R;
    float ssa = R * R / (distSQ + EPS);

    // SSA discard - skip tiny objects
    if (ssa <= r_ssaDISCARD) return;

    // Add to normal render list
    R_dsgraph::_NormalItem item;
    item.ssa     = ssa;
    item.pVisual = pVisual;  // Store as dxRender_Visual*
    RI.lstNormal.push_back(item);
}

// ============================================================================
// r_dsgraph_insert_dynamic - Add dynamic visual to render queue (Vulkan)
// ============================================================================
// Simplified version: skips DX shader selection and distort/HUD routing.
// Adds dynamic objects directly to lstNormal with SSA-based culling.
// ============================================================================
void R_dsgraph_structure::r_dsgraph_insert_dynamic(dxRender_Visual* pVisual, Fvector& Center)
{
    if (!pVisual) return;

    // Work with vkRender_Visual internally
    vkRender_Visual* pVKVisual = reinterpret_cast<vkRender_Visual*>(pVisual);

    CRender& RI = RImplementation;

    // Marker check prevents duplicate processing
    if (pVKVisual->vis.marker == RI.marker) return;
    pVKVisual->vis.marker = RI.marker;

    // Calculate SSA for culling
    float distSQ = Device.vCameraPosition.distance_to_sqr(Center);
    float R = pVKVisual->vis.sphere.R;
    float ssa = R * R / (distSQ + EPS);

    // SSA discard
    if (ssa <= r_ssaDISCARD) return;

    // Add to normal render list (dynamic objects go to same queue for now)
    R_dsgraph::_NormalItem item;
    item.ssa     = ssa;
    item.pVisual = pVisual;  // Store as dxRender_Visual*
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

        mapHUDSorted.traverseRL(vk_sorted_render);
        mapHUDSorted.clear();

        // Restore normal projection
        Device.mFullTransform = FTold;
    }

    // Camera-attached sorted items
    if (mapCamAttachedSorted.size()) {
        mapCamAttachedSorted.traverseRL(vk_sorted_render);
        mapCamAttachedSorted.clear();
    }
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

void R_dsgraph_structure::r_dsgraph_render_wmarks()
{
    // TODO: Implement for Vulkan
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

Shader::~Shader()
{
    // Vulkan stub
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
    // Vulkan stub - shaders created via SPIR-V
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
    // Vulkan stub - sun parameter updates not yet implemented
}
