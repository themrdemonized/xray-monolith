// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// Licensed under the same terms as X-Ray Engine (see root License.txt)

#pragma once
#ifndef VK_SECTOR_H
#define VK_SECTOR_H
// ============================================================================
// Vulkan Sector/Portal System
// Based on xrRender/r__sector.h but without DX dependencies
// ============================================================================

#include "../../xrCore/_stl_extensions.h"
#include "../../xrCDB/frustum.h"
#include "../../xrEngine/render.h"  // IRender_Portal, IRender_Sector
#include "../xrRender/Shader.h"     // ref_shader, ref_geom

// Forward declarations
class vkCPortal;
class vkCSector;
class vkRender_Visual;

// ============================================================================
// Scissor rectangle for portal culling
// ============================================================================
struct vk_scissor : public Fbox2
{
    float depth;
};

// ============================================================================
// vkCPortal - Connection between two sectors
// ============================================================================
class vkCPortal : public IRender_Portal
{
private:
    svector<Fvector, 8> poly;       // Portal polygon vertices
    vkCSector*          pFace;      // Front sector
    vkCSector*          pBack;      // Back sector

public:
    Fplane              P;          // Portal plane
    Fsphere             S;          // Bounding sphere
    u32                 marker;     // Traversal marker
    BOOL                bDualRender;// Render from both sides

public:
    vkCPortal();
    virtual ~vkCPortal();

    // Setup portal from vertices and connected sectors
    void Setup(Fvector* V, int vcnt, vkCSector* face, vkCSector* back);

    // Accessors
    svector<Fvector, 8>& getPoly() { return poly; }
    vkCSector* Back() { return pBack; }
    vkCSector* Front() { return pFace; }
    vkCSector* getSector(vkCSector* pFrom) { return pFrom == pFace ? pBack : pFace; }

    vkCSector* getSectorFacing(const Fvector& V)
    {
        if (P.classify(V) > 0) return pFace;
        else return pBack;
    }

    vkCSector* getSectorBack(const Fvector& V)
    {
        if (P.classify(V) > 0) return pBack;
        else return pFace;
    }

    float distance(const Fvector& V) { return _abs(P.classify(V)); }
};

// ============================================================================
// vkCSector - Level sector containing visuals
// ============================================================================
class vkCSector : public IRender_Sector
{
protected:
    vkRender_Visual*            m_root;     // Root visual of this sector
    xr_vector<vkCPortal*>       m_portals;  // Connected portals

public:
    xr_vector<CFrustum>         r_frustums;
    xr_vector<vk_scissor>       r_scissors;
    vk_scissor                  r_scissor_merged;
    u32                         r_marker;

public:
    vkCSector();
    virtual ~vkCSector();

    // Accessors
    vkRender_Visual* root() { return m_root; }
    xr_vector<vkCPortal*>& getPortals() { return m_portals; }

    // Portal traversal
    void traverse(CFrustum& F, vk_scissor& R);

    // Load sector from level data
    void load(IReader& fs);
};

// ============================================================================
// vkCPortalTraverser - Traverses visible sectors through portals
// ============================================================================
class vkCPortalTraverser
{
public:
    enum
    {
        VQ_HOM     = (1 << 0),  // Use HOM occlusion
        VQ_SSA     = (1 << 1),  // Use screen-space area culling
        VQ_SCISSOR = (1 << 2),  // Use scissor optimization
        VQ_FADE    = (1 << 3),  // Fade distant portals (requires SSA)
    };

public:
    u32                     i_marker;       // Current traversal marker
    u32                     i_options;      // Culling options
    Fvector                 i_vBase;        // Camera position
    Fmatrix                 i_mXFORM;       // View-projection matrix
    Fmatrix                 i_mXFORM_01;    // Normalized VP matrix (0-1 range)
    vkCSector*              i_start;        // Starting sector

    xr_vector<IRender_Sector*>              r_sectors;      // Visible sectors
    xr_vector<std::pair<vkCPortal*, float>> f_portals;      // Fading portals

    // Portal fade rendering resources
    ref_shader                  f_shader;       // Portal shader
    ref_geom                    f_geom;         // Portal geometry

public:
    vkCPortalTraverser();
    ~vkCPortalTraverser();

    void initialize();
    void destroy();

    // Main traversal function
    void traverse(IRender_Sector* start, CFrustum& F, Fvector& vBase, Fmatrix& mXFORM, u32 options);

    // Portal fading (for LOD transitions)
    void fade_portal(vkCPortal* _p, float ssa);
    void fade_render();

#ifdef DEBUG
    void dbg_draw();
#endif
};

// Global portal traverser instance
extern vkCPortalTraverser vkPortalTraverser;

#endif // VK_SECTOR_H
