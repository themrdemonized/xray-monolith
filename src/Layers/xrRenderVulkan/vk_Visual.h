#pragma once

#include "stdafx.h"
#include "../../xrEngine/vis_common.h"
#include "../../Include/xrRender/RenderVisual.h"
#include "vk_buffer.h"

// Forward declarations
namespace VK {
    class CVulkanBuffer;
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

    // Index buffer
    VK::CVulkanBuffer*  p_rm_Indices = nullptr;
    u32                 iBase = 0;      // First index offset
    u32                 iCount = 0;     // Index count
    VkIndexType         iType = VK_INDEX_TYPE_UINT16;

    // Primitive info
    u32                 dwPrimitives = 0;

    // Fast-path geometry (for shadow maps)
    VK_Render_Mesh*     m_fast = nullptr;

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

    // Shader/Material (ref-counted)
    // TODO: Replace with Vulkan pipeline reference
    // ref_shader       shader;

    // Visibility data (bounding box/sphere for culling)
    vis_data            vis;

    // Skinning quality (-1 = no skinning)
    s32                 skinning = -1;

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
    void LoadGeometry(IReader* data);

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
class vkSkeletonX_ST : public vkFVisual
{
public:
    // Bone data
    u32         RenderMode = 0;     // 1W, 2W, 3W, 4W weights
    u16         BonesUsed = 0;      // Number of bones affecting this mesh

public:
    vkSkeletonX_ST();
    virtual ~vkSkeletonX_ST();

    virtual void Load(const char* name, IReader* data, u32 flags) override;
    virtual void Release() override;
    virtual void Copy(vkRender_Visual* from) override;
    virtual void Render(float LOD) override;
};

// ============================================================================
// vkSkeletonX_PM - Skinned progressive mesh (with LOD)
// Equivalent to CSkeletonX_PM from DX11 renderer
// ============================================================================
class vkSkeletonX_PM : public vkFProgressive
{
public:
    // Bone data
    u32         RenderMode = 0;
    u16         BonesUsed = 0;

public:
    vkSkeletonX_PM();
    virtual ~vkSkeletonX_PM();

    virtual void Load(const char* name, IReader* data, u32 flags) override;
    virtual void Release() override;
    virtual void Copy(vkRender_Visual* from) override;
    virtual void Render(float LOD) override;
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
// Factory function - creates visual based on type
// ============================================================================
vkRender_Visual* vkVisual_Create(u32 type);
