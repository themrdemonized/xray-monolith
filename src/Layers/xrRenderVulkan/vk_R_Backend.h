// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// SPDX-License-Identifier: MIT

#pragma once
#include "stdafx.h"
#include "../xrRender/FVF.h"  // For FVF::L in debug rendering
#include "vk_buffer.h"         // For CVulkanBuffer (bone matrices)

// Forward declarations (must be struct to match Shader.h definitions)
struct SGeometry;
struct Shader;
struct ShaderElement;

// ============================================================================
// Rendering constants (D3D compatibility)
// ============================================================================
const u32 CULL_NONE = 1;  // No culling
const u32 CULL_CW = 2;    // Clockwise culling
const u32 CULL_CCW = 3;   // Counter-clockwise culling

// ============================================================================
// R_xforms - Matrix transformations for Vulkan backend
// ============================================================================
class R_xforms_vk
{
public:
    Fmatrix m_w;        // World matrix
    Fmatrix m_invw;     // Inverse world matrix (cached)
    Fmatrix m_v;        // View matrix
    Fmatrix m_p;        // Projection matrix
    Fmatrix m_wv;       // World * View
    Fmatrix m_vp;       // View * Projection
    Fmatrix m_wvp;      // World * View * Projection

    // Previous frame matrices (for motion vectors, TAA)
    Fmatrix m_w_prev;
    Fmatrix m_v_prev;
    Fmatrix m_p_prev;
    Fmatrix m_wv_prev;
    Fmatrix m_vp_prev;
    Fmatrix m_wvp_prev;

private:
    bool m_bInvWValid;

public:
    R_xforms_vk();
    void unmap();
    void set_W(const Fmatrix& m);
    void set_V(const Fmatrix& m);
    void set_P(const Fmatrix& m);
    void set_W_prev(const Fmatrix& m);
    void set_V_prev(const Fmatrix& m);
    void set_P_prev(const Fmatrix& m);

    IC const Fmatrix& get_W() { return m_w; }
    IC const Fmatrix& get_V() { return m_v; }
    IC const Fmatrix& get_P() { return m_p; }

    // Combined matrices
    IC const Fmatrix& get_WV() { return m_wv; }
    IC const Fmatrix& get_VP() { return m_vp; }
    IC const Fmatrix& get_WVP() { return m_wvp; }

    // Previous frame matrices
    IC const Fmatrix& get_W_prev() { return m_w_prev; }
    IC const Fmatrix& get_V_prev() { return m_v_prev; }
    IC const Fmatrix& get_P_prev() { return m_p_prev; }
    IC const Fmatrix& get_WVP_prev() { return m_wvp_prev; }

    // Inverse world matrix (computed on demand)
    IC const Fmatrix& get_invW() { apply_invw(); return m_invw; }

private:
    void apply_invw();
};

// ============================================================================
// R_hemi_vk - Hemisphere lighting data
// ============================================================================
class R_hemi_vk
{
public:
    float hemi_value;
    float sun_value;
    float material_value;
    float pos_faces[3];
    float neg_faces[3];
    float hotness[4];   // For heat vision
    float glowing[4];   // For silencer overheat

public:
    R_hemi_vk();
    void set_material(float h, float s, float r, float m);
    void set_pos_faces(float px, float py, float pz);
    void set_neg_faces(float nx, float ny, float nz);
    void set_hotness(float x, float y, float z, float w);
    void set_glowing(float x, float y, float z, float w);
};

// ============================================================================
// CBackend - Low-level Vulkan render backend
// ============================================================================
// ============================================================================
// _VertexStream_vk - Dynamic vertex buffer manager (ring buffer)
// ============================================================================
// Implements a ring buffer for dynamic vertex data (particles, UI, debug geometry).
// Uses persistent mapping with NOOVERWRITE/DISCARD pattern like D3D11.
//
// Usage:
//   u32 offset;
//   void* data = Vertex.Lock(100, 32, offset);  // Lock 100 vertices
//   memcpy(data, vertices, 100 * 32);
//   Vertex.Unlock(100, 32);
//   vkCmdDraw(..., offset, ...);
// ============================================================================
class _VertexStream_vk
{
private:
    VkBuffer        m_Buffer = VK_NULL_HANDLE;
    VmaAllocation   m_Allocation = VK_NULL_HANDLE;
    void*           m_MappedData = nullptr;

    u32             m_Size = 0;         // Total buffer size in bytes
    u32             m_Position = 0;     // Current write position in bytes
    u32             m_DiscardID = 0;    // Increments on each discard (for tracking)

#ifdef DEBUG
    u32             dbg_lock = 0;       // Debug: ensure Lock/Unlock pairs
#endif

public:
    _VertexStream_vk();
    ~_VertexStream_vk();

    // Create dynamic vertex buffer (called once at startup)
    void Create();

    // Destroy buffer
    void Destroy();

    // Reset at frame begin/end
    void reset_begin();
    void reset_end();

    // Accessors
    IC VkBuffer Buffer() const { return m_Buffer; }
    IC u32 DiscardID() const { return m_DiscardID; }
    IC u32 GetSize() const { return m_Size; }

    // Force flush (reset position to start)
    void Flush() { m_Position = m_Size; }

    // Lock for writing vertex data
    // Returns pointer to mapped memory, sets vOffset to vertex offset (not byte offset!)
    // Uses DISCARD (rewind) or NOOVERWRITE (append) strategy
    void* Lock(u32 vl_Count, u32 Stride, u32& vOffset);

    // Unlock after writing
    void Unlock(u32 Count, u32 Stride);
};

class CBackend
{
public:
    // === Current state ===
    VkCommandBuffer         m_Cmd = VK_NULL_HANDLE;
    VkPipeline              m_CurrentPipeline = VK_NULL_HANDLE;
    VkDescriptorSet         m_CurrentDescriptorSets[4] = {VK_NULL_HANDLE};
    u32                     m_BoundDescriptorSetCount = 0;

    // === Geometry ===
    VkBuffer                m_CurrentVB = VK_NULL_HANDLE;
    VkBuffer                m_CurrentIB = VK_NULL_HANDLE;
    u32                     m_VBStride = 0;
    VkIndexType             m_IndexType = VK_INDEX_TYPE_UINT16;

    // === G-Buffer stride tracking (for per-visual pipeline switching) ===
    u32                     m_CurrentGBufStride = 0;
    u32                     m_CurrentGBufTcOffset = 24;  // TEXCOORD0 offset (24=lmap, 28=vert-lit)

    // === Dynamic vertex/index streams (for runtime geometry) ===
    _VertexStream_vk        Vertex;

    // === Bone matrices for skeletal animation (GPU skinning) ===
    VK::CVulkanBuffer       m_BoneBuffer;           // Storage buffer for bone matrices (SSBO)
    static const u32        MAX_BONES = 256;        // Maximum bones per mesh
    static const u32        MAX_TOTAL_BONES = 16384; // Total bones in buffer (multiple skeletons, ~1MB)
    Fmatrix*                m_BoneMapped = nullptr; // Mapped pointer to bone data
    u32                     m_BoneWriteOffset = 0;  // Current write offset in bones (sub-allocation)

    // === Transforms ===
    R_xforms_vk             xforms;
    R_hemi_vk               hemi;

    // === Render targets ===
    VkImageView             m_CurrentRT[4] = {VK_NULL_HANDLE};
    VkImageView             m_CurrentZB = VK_NULL_HANDLE;
    u32                     m_RTCount = 0;

    // === Viewport/Scissor (dynamic state) ===
    VkViewport              m_Viewport;
    VkRect2D                m_Scissor;

    // === Render state ===
    u32                     m_StencilEnable;
    u32                     m_StencilFunc;
    u32                     m_StencilRef;
    u32                     m_StencilMask;
    u32                     m_StencilWriteMask;
    u32                     m_ColorWriteMask;
    u32                     m_CullMode;
    u32                     m_ZEnable;
    u32                     m_ZFunc;
    u32                     m_AlphaRef;

    // === Statistics ===
    struct _stats
    {
        u32 polys;
        u32 verts;
        u32 calls;
        u32 vs;
        u32 ps;
        u32 xforms;
        u32 target_rt;
        u32 target_zb;
        u32 r;  // General rendering stats counter (skeleton compatibility)
    } stat;

public:
    CBackend();
    ~CBackend();

    // ========================================================================
    // Transform methods (P1 - Critical)
    // ========================================================================
    IC void set_xform(u32 ID, const Fmatrix& M);
    IC void set_xform_world(const Fmatrix& M);
    IC void set_xform_view(const Fmatrix& M);
    IC void set_xform_project(const Fmatrix& M);

    IC void set_xform_world_prev(const Fmatrix& M);
    IC void set_xform_view_prev(const Fmatrix& M);
    IC void set_xform_project_prev(const Fmatrix& M);

    IC const Fmatrix& get_xform_world();
    IC const Fmatrix& get_xform_view();
    IC const Fmatrix& get_xform_project();
    IC const Fmatrix& get_xform_wv();
    IC const Fmatrix& get_xform_vp();
    IC const Fmatrix& get_xform_wvp();

    // Camera helpers
    IC Fvector get_camera_position();
    IC Fvector get_camera_direction();

    // ========================================================================
    // Geometry binding (P1 - Critical)
    // ========================================================================
    void set_Vertices(VkBuffer vb, u32 stride);
    void set_Indices(VkBuffer ib, VkIndexType indexType = VK_INDEX_TYPE_UINT16);
    void set_Geometry(SGeometry* geom);
    void set_Geometry(void* ref_geom);  // ref_geom compatibility wrapper

    // ========================================================================
    // Pipeline/Shader binding (P2 - Important)
    // ========================================================================
    void set_Pipeline(VkPipeline pipeline);
    void set_DescriptorSet(u32 index, VkDescriptorSet set);
    void set_Shader(Shader* S, u32 pass = 0);
    void set_Element(ShaderElement* S, u32 pass = 0);

    // ========================================================================
    // Constants (P2 - Important)
    // ========================================================================
    // Push constants for small frequent data
    void set_c(LPCSTR name, float x, float y, float z, float w);
    void set_c(LPCSTR name, const Fmatrix& M);
    void set_c(LPCSTR name, const Fvector4& V);

    // Shader constant retrieval (skeleton compatibility)
    void* get_c(LPCSTR name);

    // Constant arrays (skeleton compatibility)
    void set_ca(void* c, u32 startReg, u32 count, const void* data);
    void set_ca(void* c, u32 index, float v0, float v1, float v2, float v3);  // Vector overload

    // Get bone buffer for descriptor set binding
    IC VkBuffer GetBoneBuffer() const { return m_BoneBuffer.m_Buffer; }
    IC bool IsBoneBufferValid() const { return m_BoneBuffer.IsValid(); }

    // ========================================================================
    // Render target (P3 - Later)
    // ========================================================================
    void set_RT(VkImageView RT, u32 ID = 0);
    void set_ZB(VkImageView ZB);
    VkImageView get_RT(u32 ID = 0);
    VkImageView get_ZB();

    // ========================================================================
    // Render state (P3 - Later)
    // ========================================================================
    void set_Stencil(u32 enable, u32 func = 0, u32 ref = 0, u32 mask = 0,
                     u32 writemask = 0, u32 fail = 0, u32 pass = 0, u32 zfail = 0);
    void set_Z(u32 enable);
    void set_ZFunc(u32 func);
    void set_AlphaRef(u32 value);
    void set_ColorWriteEnable(u32 mask);
    void set_CullMode(u32 mode);
    u32  get_CullMode() { return m_CullMode; }
    void set_Scissor(Irect* rect = nullptr);

    // ========================================================================
    // Draw calls (P1 - Critical)
    // ========================================================================
    void Render(u32 primitive_type, u32 baseV, u32 startV, u32 countV, u32 startI, u32 PC);
    void Render(u32 primitive_type, u32 startV, u32 PC);

    // ========================================================================
    // Frame lifecycle (P1 - Critical)
    // ========================================================================
    void OnFrameBegin();
    void OnFrameEnd();
    void OnDeviceCreate();
    void OnDeviceDestroy();

    // ========================================================================
    // Command buffer management
    // ========================================================================
    void BeginCommandBuffer(VkCommandBuffer cmd);
    void EndCommandBuffer();
    VkCommandBuffer GetCommandBuffer() { return m_Cmd; }

    // ========================================================================
    // Debug rendering
    // ========================================================================
    void dbg_Draw(D3DPRIMITIVETYPE T, FVF::L* pVerts, int vcnt, u16* pIdx, int pcnt);
    void dbg_Draw(D3DPRIMITIVETYPE T, FVF::L* pVerts, int pcnt);
    IC void dbg_DrawAABB(Fvector& T, float sx, float sy, float sz, u32 C)
    {
        Fvector half_dim;
        half_dim.set(sx, sy, sz);
        Fmatrix TM;
        TM.translate(T);
        dbg_DrawOBB(TM, half_dim, C);
    }
    void dbg_DrawOBB(Fmatrix& T, Fvector& half_dim, u32 C);
    IC void dbg_DrawTRI(Fmatrix& T, Fvector* p, u32 C) { dbg_DrawTRI(T, p[0], p[1], p[2], C); }
    void dbg_DrawTRI(Fmatrix& T, Fvector& p1, Fvector& p2, Fvector& p3, u32 C);
    void dbg_DrawLINE(Fmatrix& T, Fvector& p1, Fvector& p2, u32 C);

    void ApplyViewportScissor();  // Made public for RImplementation access

private:
    void Invalidate();
};

// Global backend instance
extern CBackend RCache;

// ============================================================================
// Inline implementations
// ============================================================================
IC void CBackend::set_xform(u32 ID, const Fmatrix& M)
{
    stat.xforms++;
    switch (ID)
    {
    case 0: set_xform_world(M); break;
    case 1: set_xform_view(M); break;
    case 2: set_xform_project(M); break;
    }
}

IC void CBackend::set_xform_world(const Fmatrix& M)
{
    xforms.set_W(M);
}

IC void CBackend::set_xform_view(const Fmatrix& M)
{
    xforms.set_V(M);
}

IC void CBackend::set_xform_project(const Fmatrix& M)
{
    xforms.set_P(M);
}

IC void CBackend::set_xform_world_prev(const Fmatrix& M)
{
    xforms.set_W_prev(M);
}

IC void CBackend::set_xform_view_prev(const Fmatrix& M)
{
    xforms.set_V_prev(M);
}

IC void CBackend::set_xform_project_prev(const Fmatrix& M)
{
    xforms.set_P_prev(M);
}

IC const Fmatrix& CBackend::get_xform_world()
{
    return xforms.get_W();
}

IC const Fmatrix& CBackend::get_xform_view()
{
    return xforms.get_V();
}

IC const Fmatrix& CBackend::get_xform_project()
{
    return xforms.get_P();
}

// Combined matrix accessors
IC const Fmatrix& CBackend::get_xform_wv()
{
    return xforms.get_WV();
}

IC const Fmatrix& CBackend::get_xform_vp()
{
    return xforms.get_VP();
}

IC const Fmatrix& CBackend::get_xform_wvp()
{
    return xforms.get_WVP();
}

// Camera position from inverse view matrix
IC Fvector CBackend::get_camera_position()
{
    const Fmatrix& V = xforms.get_V();
    Fvector pos;
    // Camera position is the translation of inverse view matrix
    // For orthonormal view matrix: pos = -V^T * V.translation
    pos.x = -(V._11 * V._41 + V._12 * V._42 + V._13 * V._43);
    pos.y = -(V._21 * V._41 + V._22 * V._42 + V._23 * V._43);
    pos.z = -(V._31 * V._41 + V._32 * V._42 + V._33 * V._43);
    return pos;
}

// Camera direction (forward vector) from view matrix
IC Fvector CBackend::get_camera_direction()
{
    const Fmatrix& V = xforms.get_V();
    Fvector dir;
    // Camera forward is the negative Z axis of the view matrix transposed
    dir.x = -V._13;
    dir.y = -V._23;
    dir.z = -V._33;
    return dir;
}
