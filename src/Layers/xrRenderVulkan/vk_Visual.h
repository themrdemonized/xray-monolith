// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// Licensed under the same terms as X-Ray Engine (see root License.txt)

#pragma once

#include "stdafx.h"
#include "../../xrEngine/vis_common.h"
#include "../../Include/xrRender/RenderVisual.h"
#include "vk_buffer.h"

// Flag: skip vertex loading in base class (skinned meshes create their own VB)
#ifndef VLOAD_NOVERTICES
#define VLOAD_NOVERTICES (1<<0)
#endif

// ============================================================================
// Helper: quantize float normal component [-1,+1] to u8 [0,255]
// Matches DX11's q_N() in FSkinned.cpp
// ============================================================================
inline u8 vk_q_N(float v)
{
    int _v = clampr(iFloor((v + 1.f) * 127.5f), 0, 255);
    return u8(_v);
}

// ============================================================================
// Hardware vertex formats for skinned meshes
// These match DX11's vertHW_1W/2W/3W/4W from FSkinned.cpp
// vertBoned* (raw OGF data) is converted to these formats for GPU consumption
// ============================================================================

// 1-weight skinned vertex (36 bytes)
struct vertHW_1W
{
    float _P[4];    // Position (xyz) + 1.0 (w)
    u32   _N_I;     // Normal packed (RGB) + bone_index (A)
    u32   _T;       // Tangent packed (RGB)
    u32   _B;       // Binormal packed (RGB)
    float _tc[2];   // TexCoord UV

    void set(Fvector3& P, Fvector3 N, Fvector3 T, Fvector3 B, Fvector2& tc, int index)
    {
        N.normalize_safe();
        T.normalize_safe();
        B.normalize_safe();
        _P[0] = P.x; _P[1] = P.y; _P[2] = P.z; _P[3] = 1.f;
        _N_I = color_rgba(vk_q_N(N.x), vk_q_N(N.y), vk_q_N(N.z), u8(index));
        _T   = color_rgba(vk_q_N(T.x), vk_q_N(T.y), vk_q_N(T.z), 0);
        _B   = color_rgba(vk_q_N(B.x), vk_q_N(B.y), vk_q_N(B.z), 0);
        _tc[0] = tc.x; _tc[1] = tc.y;
    }
};  // sizeof = 36
static_assert(sizeof(vertHW_1W) == 36, "vertHW_1W must be 36 bytes");

// 2-weight skinned vertex (44 bytes)
struct vertHW_2W
{
    float _P[4];      // Position (xyz) + 1.0 (w)
    u32   _N_w;       // Normal packed (RGB) + weight0 (A)
    u32   _T;         // Tangent packed (RGB)
    u32   _B;         // Binormal packed (RGB)
    float _tc_i[4];   // tc.xy + bone_index0 (as float bits) + bone_index1 (as float bits)

    void set(Fvector3& P, Fvector3 N, Fvector3 T, Fvector3 B, Fvector2& tc,
             int index0, int index1, float w)
    {
        N.normalize_safe();
        T.normalize_safe();
        B.normalize_safe();
        _P[0] = P.x; _P[1] = P.y; _P[2] = P.z; _P[3] = 1.f;
        _N_w  = color_rgba(vk_q_N(N.x), vk_q_N(N.y), vk_q_N(N.z), u8(clampr(iFloor(w * 255.f + .5f), 0, 255)));
        _T    = color_rgba(vk_q_N(T.x), vk_q_N(T.y), vk_q_N(T.z), 0);
        _B    = color_rgba(vk_q_N(B.x), vk_q_N(B.y), vk_q_N(B.z), 0);
        _tc_i[0] = tc.x; _tc_i[1] = tc.y;
        // Store bone indices as actual float values for Vulkan
        // (DX11 used D3DDECLTYPE_SHORT2 which auto-converts s16->float,
        //  but Vulkan R32G32B32A32_SFLOAT reads raw float bits)
        _tc_i[2] = float(index0);
        _tc_i[3] = float(index1);
    }
};  // sizeof = 44
static_assert(sizeof(vertHW_2W) == 44, "vertHW_2W must be 44 bytes");

// 3-weight skinned vertex (44 bytes)
struct vertHW_3W
{
    float _P[4];      // Position (xyz) + 1.0 (w)
    u32   _N_w;       // Normal packed (RGB) + weight0 (A)
    u32   _T_w;       // Tangent packed (RGB) + weight1 (A)
    u32   _B_i;       // Binormal packed (RGB) + bone_index2 (A)
    float _tc_i[4];   // tc.xy + bone_index0 + bone_index1

    void set(Fvector3& P, Fvector3 N, Fvector3 T, Fvector3 B, Fvector2& tc,
             int index0, int index1, int index2, float w0, float w1)
    {
        N.normalize_safe();
        T.normalize_safe();
        B.normalize_safe();
        _P[0] = P.x; _P[1] = P.y; _P[2] = P.z; _P[3] = 1.f;
        _N_w  = color_rgba(vk_q_N(N.x), vk_q_N(N.y), vk_q_N(N.z), u8(clampr(iFloor(w0 * 255.f + .5f), 0, 255)));
        _T_w  = color_rgba(vk_q_N(T.x), vk_q_N(T.y), vk_q_N(T.z), u8(clampr(iFloor(w1 * 255.f + .5f), 0, 255)));
        _B_i  = color_rgba(vk_q_N(B.x), vk_q_N(B.y), vk_q_N(B.z), u8(index2));
        _tc_i[0] = tc.x; _tc_i[1] = tc.y;
        // Store bone indices as actual float values for Vulkan
        _tc_i[2] = float(index0);
        _tc_i[3] = float(index1);
    }
};  // sizeof = 44
static_assert(sizeof(vertHW_3W) == 44, "vertHW_3W must be 44 bytes");

// 4-weight skinned vertex (40 bytes)
struct vertHW_4W
{
    float _P[4];    // Position (xyz) + 1.0 (w)
    u32   _N_w;     // Normal packed (RGB) + weight0 (A)
    u32   _T_w;     // Tangent packed (RGB) + weight1 (A)
    u32   _B_w;     // Binormal packed (RGB) + weight2 (A)
    float _tc[2];   // TexCoord UV
    u32   _i;       // 4 bone indices packed as RGBA

    void set(Fvector3& P, Fvector3 N, Fvector3 T, Fvector3 B, Fvector2& tc,
             int index0, int index1, int index2, int index3,
             float w0, float w1, float w2)
    {
        N.normalize_safe();
        T.normalize_safe();
        B.normalize_safe();
        _P[0] = P.x; _P[1] = P.y; _P[2] = P.z; _P[3] = 1.f;
        _N_w = color_rgba(vk_q_N(N.x), vk_q_N(N.y), vk_q_N(N.z), u8(clampr(iFloor(w0 * 255.f + .5f), 0, 255)));
        _T_w = color_rgba(vk_q_N(T.x), vk_q_N(T.y), vk_q_N(T.z), u8(clampr(iFloor(w1 * 255.f + .5f), 0, 255)));
        _B_w = color_rgba(vk_q_N(B.x), vk_q_N(B.y), vk_q_N(B.z), u8(clampr(iFloor(w2 * 255.f + .5f), 0, 255)));
        _tc[0] = tc.x; _tc[1] = tc.y;
        _i = color_rgba(u8(index0), u8(index1), u8(index2), u8(index3));
    }
};  // sizeof = 40
static_assert(sizeof(vertHW_4W) == 40, "vertHW_4W must be 40 bytes");

// Forward declarations
namespace VK {
    class CVulkanBuffer;
    class CMaterial;
}

// ============================================================================
// Vulkan Mesh Data - replaces IRender_Mesh from DX11
// Contains VkBuffer handles instead of D3D buffers
// ============================================================================
struct VK_Render_Mesh
{
    // Vertex buffer
    VK::CVulkanBuffer*  p_rm_Vertices = nullptr;
    u32                 vBase = 0;      // First vertex offset
    u32                 vCount = 0;     // Vertex count
    u32                 vStride = 0;    // Bytes per vertex
    u32                 tcOffset = 24;  // TEXCOORD0 byte offset (24=lmap, 28=vert-lit)

    // Index buffer
    VK::CVulkanBuffer*  p_rm_Indices = nullptr;
    u32                 iBase = 0;      // First index offset
    u32                 iCount = 0;     // Index count
    VkIndexType         iType = VK_INDEX_TYPE_UINT16;

    // Primitive info
    u32                 dwPrimitives = 0;

    // Fast-path geometry (for shadow maps)
    VK_Render_Mesh*     m_fast = nullptr;

    // Ownership flag: false when buffers are shared via Copy() (don't delete them)
    bool                bOwnsBuffers = true;

    VK_Render_Mesh() = default;
    ~VK_Render_Mesh();

    void Destroy();
    bool IsValid() const { return p_rm_Vertices != nullptr && vCount > 0; }
};

// ============================================================================
// vkRender_Visual - Base visual class for Vulkan renderer
// Equivalent to dxRender_Visual from DX11 renderer
// ============================================================================
class vkRender_Visual : public IRenderVisual
{
public:
    // Type from OGF header
    u32                 Type = 0;

    // Shader ID (index into RImplementation.Shaders array)
    // Phase 2.34: Shader-Material Binding
    u16                 shader_id = 0;

    // Visibility data (bounding box/sphere for culling)
    vis_data            vis;

    // Skinning quality (-1 = no skinning)
    s32                 skinning = -1;

    // Material (diffuse texture + descriptor set)
    VK::CMaterial*      m_pMaterial = nullptr;

    // Alpha-test threshold: -1.0 = disabled (solid), 0.5 = enabled (foliage/aref)
    float               m_fAlphaRef = -1.0f;

    // Debug name
    shared_str          dbg_name;

    // Debug ID (used by FHierrarhyVisual shared code)
    u32                 dbg_id = 0;

public:
    vkRender_Visual();
    virtual ~vkRender_Visual();

    // ========================================================================
    // IRenderVisual interface
    // ========================================================================
    virtual vis_data&   _BCL getVisData() override { return vis; }
    virtual u32         getType() override { return Type; }

    // ========================================================================
    // Core methods - must be implemented by derived classes
    // ========================================================================

    // Load visual data from OGF file
    virtual void Load(const char* name, IReader* data, u32 flags);

    // Release GPU resources
    virtual void Release();

    // Copy from another visual (for duplication)
    virtual void Copy(vkRender_Visual* from);

    // Render the visual with specified LOD
    virtual void Render(float LOD);

    // Called when instance is activated from pool
    virtual void Spawn() {}

    // Called when instance is returned to pool
    virtual void Depart() {}

    // Set debug ID (used by shared FHierrarhyVisual)
    virtual void setID(u32 id) { dbg_id = id; }

    // HeatVision support (stub)
    virtual void MarkAsHot(bool is_hot) {}

protected:
    // Load common data from OGF header
    void LoadHeader(IReader* data);

    // Load shader/texture info
    void LoadTexture(IReader* data);
};

// ============================================================================
// vkFVisual - Standard triangle mesh for Vulkan
// Equivalent to Fvisual from DX11 renderer
// ============================================================================
class vkFVisual : public vkRender_Visual
{
public:
    // Mesh data (vertex/index buffers)
    VK_Render_Mesh      m_mesh;

public:
    vkFVisual();
    virtual ~vkFVisual();

    // ========================================================================
    // vkRender_Visual overrides
    // ========================================================================
    virtual void Load(const char* name, IReader* data, u32 flags) override;
    virtual void Release() override;
    virtual void Copy(vkRender_Visual* from) override;
    virtual void Render(float LOD) override;

protected:
    // Load geometry from OGF_GCONTAINER chunk
    // flags: 0 = normal, VLOAD_NOVERTICES = skip vertex loading (indices only)
    void LoadGeometry(IReader* data, u32 flags = 0);

    // Load fast-path geometry from OGF_FASTPATH chunk
    void LoadFastPath(IReader* data);
};

// ============================================================================
// vkFHierrarhyVisual - Composite visual with children
// Equivalent to FHierrarhyVisual from DX11 renderer
// ============================================================================
class vkFHierrarhyVisual : public vkRender_Visual
{
public:
    xr_vector<IRenderVisual*>   children;
    xr_vector<IRenderVisual*>   children_invisible;
    BOOL                        bDontDelete = FALSE;

public:
    vkFHierrarhyVisual();
    virtual ~vkFHierrarhyVisual();

    // ========================================================================
    // IRenderVisual overrides
    // ========================================================================
    virtual xr_vector<IRenderVisual*>* get_children() override { return &children; }
    virtual xr_vector<IRenderVisual*>* get_children_invisible() override { return &children_invisible; }

    // ========================================================================
    // vkRender_Visual overrides
    // ========================================================================
    virtual void Load(const char* name, IReader* data, u32 flags) override;
    virtual void Release() override;
    virtual void Copy(vkRender_Visual* from) override;
    virtual void Render(float LOD) override;
    virtual void Spawn() override;
    virtual void Depart() override;
};

// ============================================================================
// Model type and OGF chunk IDs are defined in fmesh.h
// Include it for MT_*, OGF_* enums and ogf_header struct
// ============================================================================
#include "../../xrEngine/fmesh.h"

// ============================================================================
// vkFProgressive - LOD mesh with sliding window
// Equivalent to FProgressive from DX11 renderer
// ============================================================================
class vkFProgressive : public vkFVisual
{
public:
    // Sliding window data for LOD
    u32     sw_count = 0;       // Number of SW items
    u32*    sw_offsets = nullptr;  // Offset into index buffer per LOD
    u32*    sw_counts = nullptr;   // Index count per LOD
    u32     last_lod = 0;

public:
    vkFProgressive();
    virtual ~vkFProgressive();

    virtual void Load(const char* name, IReader* data, u32 flags) override;
    virtual void Release() override;
    virtual void Copy(vkRender_Visual* from) override;
    virtual void Render(float LOD) override;

protected:
    void LoadSlidingWindow(IReader* data);
};

// ============================================================================
// vkFTreeVisual - Tree visual with color scale/bias and transform
// Equivalent to FTreeVisual from DX11 renderer
// ============================================================================
class vkFTreeVisual : public vkFVisual
{
public:
    // Color components for tree lighting
    struct _5color
    {
        Fvector rgb;    // RGB static lighting
        float   hemi;   // Hemisphere
        float   sun;    // Sun
    };

    _5color     c_scale;
    _5color     c_bias;
    Fmatrix     xform;      // Tree transform matrix

public:
    vkFTreeVisual();
    virtual ~vkFTreeVisual();

    virtual void Load(const char* name, IReader* data, u32 flags) override;
    virtual void Release() override;
    virtual void Copy(vkRender_Visual* from) override;
    virtual void Render(float LOD) override;

protected:
    void LoadTreeDef(IReader* data);
};

// ============================================================================
// vkFTreeVisual_ST - Static tree visual
// ============================================================================
class vkFTreeVisual_ST : public vkFTreeVisual
{
public:
    vkFTreeVisual_ST();
    virtual ~vkFTreeVisual_ST();

    virtual void Load(const char* name, IReader* data, u32 flags) override;
    virtual void Render(float LOD) override;
};

// ============================================================================
// vkFTreeVisual_PM - Progressive tree visual with LOD
// ============================================================================
class vkFTreeVisual_PM : public vkFTreeVisual
{
public:
    // Sliding window for LOD
    u32     sw_count = 0;
    u32*    sw_offsets = nullptr;
    u32*    sw_counts = nullptr;
    u32     last_lod = 0;

public:
    vkFTreeVisual_PM();
    virtual ~vkFTreeVisual_PM();

    virtual void Load(const char* name, IReader* data, u32 flags) override;
    virtual void Release() override;
    virtual void Copy(vkRender_Visual* from) override;
    virtual void Render(float LOD) override;
};

// ============================================================================
// vkFLOD - LOD billboard visual (8 facets for distant objects)
// Equivalent to FLOD from DX11 renderer
// ============================================================================
class vkFLOD : public vkFHierrarhyVisual
{
public:
    // LOD vertex structure
    struct _vertex
    {
        Fvector     v;
        Fvector2    t;
        u32         c_rgb_hemi;
        u8          c_sun;
    };

    // LOD face structure (quad)
    struct _face
    {
        _vertex     v[4];
        Fvector     N;
    };

    _face           facets[8];      // 8 viewing angles
    float           lod_factor;
    VK_Render_Mesh  lod_mesh;       // LOD geometry buffer

public:
    vkFLOD();
    virtual ~vkFLOD();

    virtual void Load(const char* name, IReader* data, u32 flags) override;
    virtual void Release() override;
    virtual void Copy(vkRender_Visual* from) override;
    virtual void Render(float LOD) override;

protected:
    void LoadLODDef(IReader* data);
    void BuildLODGeometry();
};

// ============================================================================
// vkSkeletonX_ST - Skinned static mesh (no progressive LOD)
// Equivalent to CSkeletonX_ST from DX11 renderer
// ============================================================================
class CKinematics;  // Forward declaration

class vkSkeletonX_ST : public vkFVisual
{
public:
    // Rendering modes (matching DX)
    enum { RM_SKINNING_SOFT, RM_SINGLE, RM_SKINNING_1B, RM_SKINNING_2B, RM_SKINNING_3B, RM_SKINNING_4B };

    // Bone data
    u16         RenderMode = 0;     // Rendering mode (RM_SINGLE, RM_SKINNING_1B, etc.)
    u16         BonesUsed = 0;      // Number of bones affecting this mesh

    // Parent skeleton (set by AfterLoad)
    CKinematics* Parent = nullptr;
    u16          ChildIDX = 0;

    // Render-mode specific data
    union
    {
        struct
        {
            // soft-skinning only (CPU-side)
            u32 cache_DiscardID;
            u32 cache_vCount;
            u32 cache_vOffset;
        };

        u32 RMS_boneid;     // single-bone-rendering (RM_SINGLE)
        u32 RMS_bonecount;  // skinning, maximal bone ID (RM_SKINNING_*)
    };

public:
    vkSkeletonX_ST();
    virtual ~vkSkeletonX_ST();

    virtual void Load(const char* name, IReader* data, u32 flags) override;
    virtual void Release() override;
    virtual void Copy(vkRender_Visual* from) override;
    virtual void Render(float LOD) override;

    // Called by CKinematics::Load to link child to parent
    virtual void AfterLoad(CKinematics* parent, u16 child_idx);
    void SetParent(CKinematics* p) { Parent = p; }

protected:
    // Convert vertBoned* to vertHW_* and create Vulkan VB (matches DX11's _Load_hw)
    void _Load_hw_VK(void* _verts_, u32 dwVertType, u32 dwVertCount);

private:
    // Render() decomposition helpers
    bool DiagnoseRender();                                      // one-shot diagnostics, returns bDiag flag
    bool ValidateParent(bool bDiag);                            // safety checks, returns false if render should abort
    void RenderSingleBone(const Fmatrix& Wold, float LOD, bool bDiag);  // RM_SINGLE path
    void RenderSkinned(const Fmatrix& Wold, float LOD);        // GPU skinning path (1B/2B/3B/4B)
    void RenderFallback(const Fmatrix& Wold, float LOD);       // unknown mode fallback
    void RestoreWorldMatrix(const Fmatrix& Wold);               // restore original world matrix
};

// ============================================================================
// vkSkeletonX_PM - Skinned progressive mesh (with LOD)
// Equivalent to CSkeletonX_PM from DX11 renderer
// ============================================================================
class vkSkeletonX_PM : public vkFProgressive
{
public:
    // Rendering modes (matching DX)
    enum { RM_SKINNING_SOFT, RM_SINGLE, RM_SKINNING_1B, RM_SKINNING_2B, RM_SKINNING_3B, RM_SKINNING_4B };

    // Bone data
    u16         RenderMode = 0;     // Rendering mode (RM_SINGLE, RM_SKINNING_1B, etc.)
    u16         BonesUsed = 0;      // Number of bones affecting this mesh

    // Parent skeleton (set by AfterLoad)
    CKinematics* Parent = nullptr;
    u16          ChildIDX = 0;

    // Render-mode specific data
    union
    {
        struct
        {
            // soft-skinning only (CPU-side)
            u32 cache_DiscardID;
            u32 cache_vCount;
            u32 cache_vOffset;
        };

        u32 RMS_boneid;     // single-bone-rendering (RM_SINGLE)
        u32 RMS_bonecount;  // skinning, maximal bone ID (RM_SKINNING_*)
    };

public:
    vkSkeletonX_PM();
    virtual ~vkSkeletonX_PM();

    virtual void Load(const char* name, IReader* data, u32 flags) override;
    virtual void Release() override;
    virtual void Copy(vkRender_Visual* from) override;
    virtual void Render(float LOD) override;

    // Called by CKinematics::Load to link child to parent
    virtual void AfterLoad(CKinematics* parent, u16 child_idx);
    void SetParent(CKinematics* p) { Parent = p; }

protected:
    // Convert vertBoned* to vertHW_* and create Vulkan VB (matches DX11's _Load_hw)
    void _Load_hw_VK(void* _verts_, u32 dwVertType, u32 dwVertCount);
};

// ============================================================================
// Particle system visuals (stubs - actual implementation requires PS system)
// ============================================================================
class vkParticleEffect : public vkRender_Visual
{
public:
    vkParticleEffect();
    virtual ~vkParticleEffect();

    virtual void Load(const char* name, IReader* data, u32 flags) override;
    virtual void Render(float LOD) override;
};

class vkParticleGroup : public vkRender_Visual
{
public:
    xr_vector<vkParticleEffect*> effects;

public:
    vkParticleGroup();
    virtual ~vkParticleGroup();

    virtual void Load(const char* name, IReader* data, u32 flags) override;
    virtual void Release() override;
    virtual void Render(float LOD) override;
};

// ============================================================================
// Shared skinned vertex loading
// Converts vertBoned* (OGF format) to vertHW_* and uploads to GPU buffer.
// renderMode: RM_SINGLE/RM_SKINNING_1B/2B/3B/4B from skeleton classes
// diagTag: optional tag for diagnostic messages (e.g. "SKL-ST", "SKL-PM")
// ============================================================================
void vkLoadSkinnedVertices(VK_Render_Mesh& mesh, u16 renderMode, void* verts, u32 vertCount, const char* diagTag = nullptr);

// ============================================================================
// Factory function - creates visual based on type
// ============================================================================
vkRender_Visual* vkVisual_Create(u32 type);

// Factory function - creates dummy visual for unknown types
// Returns a valid but empty visual that won't crash when rendered
vkRender_Visual* vkVisual_CreateDummy();
