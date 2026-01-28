#include "stdafx.h"
#include "vk_sector.h"
#include "vk_Visual.h"
#include "rvk.h"
#include "../../xrEngine/xrLevel.h"
#include "../../xrEngine/IGame_Persistent.h"
#include "../../xrEngine/Environment.h"
#include "../xrRender/fvf.h"
#include "vk_R_Backend.h"

// VULKAN_DIAG: Static init diagnostics
static void VulkanDiagWriteSector(const char* msg) {
	HANDLE h = CreateFileA("D:\\anomaly\\appdata\\logs\\vulkan_diag.txt",
		FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS,
		FILE_ATTRIBUTE_NORMAL, NULL);
	if (h != INVALID_HANDLE_VALUE) {
		DWORD written;
		WriteFile(h, msg, (DWORD)strlen(msg), &written, NULL);
		WriteFile(h, "\r\n", 2, &written, NULL);
		FlushFileBuffers(h);
		CloseHandle(h);
	}
}

// ============================================================================
// Global Portal Traverser
// ============================================================================
static struct DiagSector1 { DiagSector1() { VulkanDiagWriteSector("[DIAG] vk_sector.cpp: before vkPortalTraverser"); } } g_diagSector1;
vkCPortalTraverser vkPortalTraverser;
static struct DiagSector2 { DiagSector2() { VulkanDiagWriteSector("[DIAG] vk_sector.cpp: after vkPortalTraverser"); } } g_diagSector2;

// External SSA thresholds (defined in render settings)
extern float r_ssaDISCARD;
extern float r_ssaLOD_A, r_ssaLOD_B;

#ifdef DEBUG
// Debug flag (defined in engine)
extern BOOL bDebug;
#endif

// ============================================================================
// vkCPortal Implementation
// ============================================================================
vkCPortal::vkCPortal()
    : pFace(nullptr)
    , pBack(nullptr)
    , marker(0xFFFFFFFF)
    , bDualRender(FALSE)
{
    P.build(Fvector().set(0, 0, 0), Fvector().set(0, 1, 0));
    S.set(Fvector().set(0, 0, 0), 0.f);
}

vkCPortal::~vkCPortal()
{
}

void vkCPortal::Setup(Fvector* V, int vcnt, vkCSector* face, vkCSector* back)
{
    // Calculate bounding sphere
    Fbox BB;
    BB.invalidate();
    for (int v = 0; v < vcnt; v++)
        BB.modify(V[v]);
    BB.getsphere(S.P, S.R);

    // Store polygon vertices
    poly.assign(V, vcnt);
    pFace = face;
    pBack = back;
    marker = 0xFFFFFFFF;

    // Calculate plane normal
    Fvector N, T;
    N.set(0, 0, 0);

    FPU::m64r();
    u32 _cnt = 0;
    for (int i = 2; i < vcnt; i++)
    {
        T.mknormal_non_normalized(poly[0], poly[i - 1], poly[i]);
        float m = T.magnitude();
        if (m > EPS_S)
        {
            N.add(T.div(m));
            _cnt++;
        }
    }
    R_ASSERT2(_cnt, "Invalid portal detected");
    N.div(float(_cnt));
    P.build(poly[0], N);
    FPU::m24r();
}

// ============================================================================
// vkCSector Implementation
// ============================================================================
vkCSector::vkCSector()
    : m_root(nullptr)
    , r_marker(0)
{
    r_scissor_merged.set(0, 0, 1, 1);
    r_scissor_merged.depth = 0;
}

vkCSector::~vkCSector()
{
    // Note: m_root is owned by Visuals array, don't delete here
    // Portals are owned by Portals array, don't delete here
}

void vkCSector::load(IReader& fs)
{
    // Load portal references
    u32 size = fs.find_chunk(fsP_Portals);
    R_ASSERT(0 == (size & 1));
    u32 count = size / 2;
    m_portals.reserve(count);

    while (count)
    {
        u16 ID = fs.r_u16();
        vkCPortal* P = (vkCPortal*)RImplementation.getPortal(ID);
        m_portals.push_back(P);
        count--;
    }

    // Load root visual
    if (g_dedicated_server)
    {
        m_root = nullptr;
    }
    else
    {
        size = fs.find_chunk(fsP_Root);
        R_ASSERT(size == 4);
        m_root = (vkRender_Visual*)RImplementation.getVisual(fs.r_u32());
    }
}

void vkCSector::traverse(CFrustum& F, vk_scissor& R_scissor)
{
    // Register this sector as visible
    if (r_marker != vkPortalTraverser.i_marker)
    {
        r_marker = vkPortalTraverser.i_marker;
        vkPortalTraverser.r_sectors.push_back(this);
        r_frustums.clear();
        r_scissors.clear();
    }
    r_frustums.push_back(F);
    r_scissors.push_back(R_scissor);

    // Search visible portals and traverse through them
    sPoly S, D;
    for (u32 I = 0; I < m_portals.size(); I++)
    {
        if (m_portals[I]->marker == vkPortalTraverser.i_marker)
            continue;

        vkCPortal* PORTAL = m_portals[I];
        vkCSector* pSector;

        // Select target sector
        if (PORTAL->bDualRender)
        {
            pSector = PORTAL->getSector(this);
        }
        else
        {
            pSector = PORTAL->getSectorBack(vkPortalTraverser.i_vBase);
            if (pSector == this) continue;
            if (pSector == vkPortalTraverser.i_start) continue;
        }

        // Early-out: sphere test
        if (!F.testSphere_dirty(PORTAL->S.P, PORTAL->S.R))
            continue;

        // SSA culling (if enabled)
        if (vkPortalTraverser.i_options & vkCPortalTraverser::VQ_SSA)
        {
            Fvector dir2portal;
            dir2portal.sub(PORTAL->S.P, vkPortalTraverser.i_vBase);
            float R = PORTAL->S.R;
            float distSQ = dir2portal.square_magnitude();
            float ssa = R * R / distSQ;
            dir2portal.div(_sqrt(distSQ));
            ssa *= _abs(PORTAL->P.n.dotproduct(dir2portal));
            if (ssa < r_ssaDISCARD) continue;

            if (vkPortalTraverser.i_options & vkCPortalTraverser::VQ_FADE)
            {
                if (ssa < r_ssaLOD_A)
                    vkPortalTraverser.fade_portal(PORTAL, ssa);
                if (ssa < r_ssaLOD_B)
                    continue;
            }
        }

        // Clip portal polygon by frustum
        svector<Fvector, 8>& POLY = PORTAL->getPoly();
        S.assign(&*POLY.begin(), POLY.size());
        D.clear();
        sPoly* P = F.ClipPoly(S, D);
        if (0 == P) continue;

        // Scissor optimization
        vk_scissor scissor;
        if ((vkPortalTraverser.i_options & vkCPortalTraverser::VQ_SCISSOR) && (!PORTAL->bDualRender))
        {
            // Build scissor rectangle in projection space
            Fbox2 bb;
            bb.invalidate();
            float depth = flt_max;
            sPoly& p = *P;

            for (u32 vit = 0; vit < p.size(); vit++)
            {
                Fvector4 t;
                Fmatrix& M = vkPortalTraverser.i_mXFORM_01;
                Fvector& v = p[vit];

                t.x = v.x * M._11 + v.y * M._21 + v.z * M._31 + M._41;
                t.y = v.x * M._12 + v.y * M._22 + v.z * M._32 + M._42;
                t.z = v.x * M._13 + v.y * M._23 + v.z * M._33 + M._43;
                t.w = v.x * M._14 + v.y * M._24 + v.z * M._34 + M._44;
                t.mul(1.f / t.w);

                if (t.x < bb.min.x) bb.min.x = t.x;
                if (t.x > bb.max.x) bb.max.x = t.x;
                if (t.y < bb.min.y) bb.min.y = t.y;
                if (t.y > bb.max.y) bb.max.y = t.y;
                if (t.z < depth) depth = t.z;
            }

            if (depth < EPS)
            {
                scissor = R_scissor;

                // HOM culling (slower algorithm for close portals)
                if ((vkPortalTraverser.i_options & vkCPortalTraverser::VQ_HOM) &&
                    (!RImplementation.HOM->visible(*P)))
                    continue;
            }
            else
            {
                // Intersect with parent scissor
                scissor.min.x = _max(bb.min.x, R_scissor.min.x);
                scissor.min.y = _max(bb.min.y, R_scissor.min.y);
                scissor.max.x = _min(bb.max.x, R_scissor.max.x);
                scissor.max.y = _min(bb.max.y, R_scissor.max.y);
                scissor.depth = depth;

                // Non-empty box check
                if (scissor.min.x >= scissor.max.x) continue;
                if (scissor.min.y >= scissor.max.y) continue;

                // HOM culling (faster algorithm)
                if ((vkPortalTraverser.i_options & vkCPortalTraverser::VQ_HOM) &&
                    (!RImplementation.HOM->visible(scissor, depth)))
                    continue;
            }
        }
        else
        {
            scissor = R_scissor;

            // HOM culling
            if ((vkPortalTraverser.i_options & vkCPortalTraverser::VQ_HOM) &&
                (!RImplementation.HOM->visible(*P)))
                continue;
        }

        // Create new frustum and recurse
        CFrustum Clip;
        Clip.CreateFromPortal(P, PORTAL->P.n, vkPortalTraverser.i_vBase, vkPortalTraverser.i_mXFORM);
        PORTAL->marker = vkPortalTraverser.i_marker;
        PORTAL->bDualRender = FALSE;
        pSector->traverse(Clip, scissor);
    }
}

// ============================================================================
// vkCPortalTraverser Implementation
// ============================================================================
vkCPortalTraverser::vkCPortalTraverser()
    : i_marker(0xFFFFFFFF)
    , i_options(0)
    , i_start(nullptr)
{
    i_vBase.set(0, 0, 0);
    i_mXFORM.identity();
    i_mXFORM_01.identity();
}

vkCPortalTraverser::~vkCPortalTraverser()
{
}

void vkCPortalTraverser::initialize()
{
    // Create portal fade shader
    // Uses simple position+color vertex format for alpha-blended portal rendering
    f_shader.create("portal");

    // Create geometry with FVF::L format (position + color)
    // Uses dynamic vertex buffer for runtime triangulation
    // TODO Phase 2.x: Implement geometry creation for Vulkan
    // f_geom.create(FVF::F_L, RCache.Vertex.Buffer(), 0);
}

void vkCPortalTraverser::destroy()
{
    f_geom.destroy();
    f_shader.destroy();
}

void vkCPortalTraverser::traverse(IRender_Sector* start, CFrustum& F, Fvector& vBase, Fmatrix& mXFORM, u32 options)
{
    // Viewport normalization matrix (0-1 range)
    Fmatrix m_viewport_01 = {
        1.f / 2.f, 0.0f, 0.0f, 0.0f,
        0.0f, -1.f / 2.f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        1.f / 2.f + 0 + 0, 1.f / 2.f + 0 + 0, 0.0f, 1.0f
    };

    if (options & VQ_FADE)
    {
        f_portals.clear();
        f_portals.reserve(16);
    }

    VERIFY(start);
    i_marker++;
    i_options = options;
    i_vBase = vBase;
    i_mXFORM = mXFORM;
    i_mXFORM_01.mul(m_viewport_01, mXFORM);
    i_start = (vkCSector*)start;
    r_sectors.clear();

    vk_scissor scissor;
    scissor.set(0, 0, 1, 1);
    scissor.depth = 0;
    i_start->traverse(F, scissor);

    if (options & VQ_SCISSOR)
    {
        // Merge scissor info for each sector
        for (u32 s = 0; s < r_sectors.size(); s++)
        {
            vkCSector* S = (vkCSector*)r_sectors[s];
            S->r_scissor_merged.invalidate();
            S->r_scissor_merged.depth = flt_max;

            for (u32 it = 0; it < S->r_scissors.size(); it++)
            {
                S->r_scissor_merged.merge(S->r_scissors[it]);
                if (S->r_scissors[it].depth < S->r_scissor_merged.depth)
                    S->r_scissor_merged.depth = S->r_scissors[it].depth;
            }
        }
    }
}

void vkCPortalTraverser::fade_portal(vkCPortal* _p, float ssa)
{
    f_portals.push_back(std::make_pair(_p, ssa));
}

// Sorting predicate: back-to-front by distance to camera
ICF bool psort_pred(const std::pair<vkCPortal*, float>& _1, const std::pair<vkCPortal*, float>& _2)
{
    float d1 = vkPortalTraverser.i_vBase.distance_to_sqr(_1.first->S.P);
    float d2 = vkPortalTraverser.i_vBase.distance_to_sqr(_2.first->S.P);
    return d2 > d1; // descending, back to front
}

void vkCPortalTraverser::fade_render()
{
    if (f_portals.empty())
        return;

    // Sort portals back-to-front for proper alpha blending
    std::sort(f_portals.begin(), f_portals.end(), psort_pred);

    // Calculate total triangle count (fan triangulation)
    u32 _pcount = 0;
    for (u32 _it = 0; _it < f_portals.size(); _it++)
        _pcount += f_portals[_it].first->getPoly().size() - 2;

    // Fill vertex buffer
    u32 _offset = 0;
    FVF::L* _v = (FVF::L*)RCache.Vertex.Lock(_pcount * 3, f_geom.stride(), _offset);

    float ssaRange = r_ssaLOD_A - r_ssaLOD_B;
    Fvector _ambient_f = g_pGamePersistent->Environment().CurrentEnv->ambient;
    u32 _ambient = color_rgba_f(_ambient_f.x, _ambient_f.y, _ambient_f.z, 0);

    for (u32 _it = 0; _it < f_portals.size(); _it++)
    {
        std::pair<vkCPortal*, float>& fp = f_portals[_it];
        vkCPortal* _P = fp.first;
        float _ssa = fp.second;

        // Calculate alpha based on SSA (smaller SSA = more transparent)
        // ssaScale: 0.0 (at r_ssaLOD_B) to 1.0 (at r_ssaLOD_A)
        float ssaDiff = _ssa - r_ssaLOD_B;
        float ssaScale = ssaDiff / ssaRange;
        int iA = iFloor((1 - ssaScale) * 255.5f);
        clamp(iA, 0, 255);
        u32 _clr = subst_alpha(_ambient, u32(iA));

        // Triangulate portal polygon (fan from first vertex)
        u32 _polys = _P->getPoly().size() - 2;
        for (u32 _pit = 0; _pit < _polys; _pit++)
        {
            _v->set(_P->getPoly()[0], _clr);
            _v++;
            _v->set(_P->getPoly()[_pit + 1], _clr);
            _v++;
            _v->set(_P->getPoly()[_pit + 2], _clr);
            _v++;
        }
    }

    RCache.Vertex.Unlock(_pcount * 3, f_geom.stride());

    // Render portal overlays
    RCache.set_xform_world(Fidentity);
    RCache.set_Shader(f_shader._get());
    RCache.set_Geometry(f_geom._get());
    RCache.set_CullMode(CULL_NONE);
    RCache.Render(D3DPT_TRIANGLELIST, _offset, _pcount);
    RCache.set_CullMode(CULL_CCW);

    // Cleanup
    f_portals.clear();
}

#ifdef DEBUG
xr_vector<IRender_Sector*> dbg_sectors;

void vkCPortalTraverser::dbg_draw()
{
    // Debug visualization: Draw scissor rectangles for visible sectors
    // This shows the screen-space regions where each sector is visible

    RCache.OnFrameEnd();
    RCache.set_xform_world(Fidentity);
    RCache.set_xform_view(Fidentity);
    RCache.set_xform_project(Fidentity);

    for (u32 s = 0; s < dbg_sectors.size(); s++)
    {
        vkCSector* S = (vkCSector*)dbg_sectors[s];
        FVF::L verts[5];
        Fbox2 bb = S->r_scissor_merged;

        // Transform from 0-1 range to -1 to 1 NDC space
        bb.min.x = bb.min.x * 2 - 1;
        bb.max.x = bb.max.x * 2 - 1;
        bb.min.y = (1 - bb.min.y) * 2 - 1;
        bb.max.y = (1 - bb.max.y) * 2 - 1;

        // Build line strip for scissor rectangle
        verts[0].set(bb.min.x, bb.min.y, EPS, 0xffffffff);
        verts[1].set(bb.max.x, bb.min.y, EPS, 0xffffffff);
        verts[2].set(bb.max.x, bb.max.y, EPS, 0xffffffff);
        verts[3].set(bb.min.x, bb.max.y, EPS, 0xffffffff);
        verts[4].set(bb.min.x, bb.min.y, EPS, 0xffffffff);

        RCache.dbg_Draw(D3DPT_LINESTRIP, verts, 4);
    }
}
#endif
