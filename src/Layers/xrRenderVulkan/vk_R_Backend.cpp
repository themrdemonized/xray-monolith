#include "stdafx.h"
#include "vk_R_Backend.h"
#include "HW_Vulkan.h"
#include "../xrRender/fvf.h"

// VULKAN_DIAG
static void VulkanDiagWriteBackend(const char* msg) {
	FILE* f = fopen("D:\\anomaly\\appdata\\logs\\vulkan_diag.txt", "a");
	if (f) { fprintf(f, "%s\n", msg); fflush(f); fclose(f); }
}
struct VulkanDiagBackend {
	VulkanDiagBackend() { VulkanDiagWriteBackend("[DIAG] Before CBackend RCache construction"); }
} g_VulkanDiagBackend;

// Global backend instance
CBackend RCache;

struct VulkanDiagBackend2 {
	VulkanDiagBackend2() { VulkanDiagWriteBackend("[DIAG] After CBackend RCache construction"); }
} g_VulkanDiagBackend2;

// ============================================================================
// R_xforms_vk implementation
// ============================================================================
R_xforms_vk::R_xforms_vk()
{
    m_w.identity();
    m_invw.identity();
    m_v.identity();
    m_p.identity();
    m_wv.identity();
    m_vp.identity();
    m_wvp.identity();

    m_w_prev.identity();
    m_v_prev.identity();
    m_p_prev.identity();
    m_wv_prev.identity();
    m_vp_prev.identity();
    m_wvp_prev.identity();

    m_bInvWValid = true;
}

void R_xforms_vk::unmap()
{
    // Reset all matrices to identity
    m_w.identity();
    m_invw.identity();
    m_v.identity();
    m_p.identity();
    m_wv.identity();
    m_vp.identity();
    m_wvp.identity();
    m_bInvWValid = true;
}

void R_xforms_vk::set_W(const Fmatrix& m)
{
    m_w = m;
    m_bInvWValid = false;

    // Recalculate derived matrices
    m_wv.mul(m_v, m_w);
    m_wvp.mul(m_vp, m_w);
}

void R_xforms_vk::set_V(const Fmatrix& m)
{
    m_v = m;

    // Recalculate derived matrices
    m_wv.mul(m_v, m_w);
    m_vp.mul(m_p, m_v);
    m_wvp.mul(m_vp, m_w);
}

void R_xforms_vk::set_P(const Fmatrix& m)
{
    m_p = m;

    // Recalculate derived matrices
    m_vp.mul(m_p, m_v);
    m_wvp.mul(m_vp, m_w);
}

void R_xforms_vk::set_W_prev(const Fmatrix& m)
{
    m_w_prev = m;
    m_wv_prev.mul(m_v_prev, m_w_prev);
    m_wvp_prev.mul(m_vp_prev, m_w_prev);
}

void R_xforms_vk::set_V_prev(const Fmatrix& m)
{
    m_v_prev = m;
    m_wv_prev.mul(m_v_prev, m_w_prev);
    m_vp_prev.mul(m_p_prev, m_v_prev);
    m_wvp_prev.mul(m_vp_prev, m_w_prev);
}

void R_xforms_vk::set_P_prev(const Fmatrix& m)
{
    m_p_prev = m;
    m_vp_prev.mul(m_p_prev, m_v_prev);
    m_wvp_prev.mul(m_vp_prev, m_w_prev);
}

void R_xforms_vk::apply_invw()
{
    if (!m_bInvWValid)
    {
        m_invw.invert(m_w);
        m_bInvWValid = true;
    }
}

// ============================================================================
// R_hemi_vk implementation
// ============================================================================
R_hemi_vk::R_hemi_vk()
{
    hemi_value = 0.f;
    sun_value = 0.f;
    material_value = 0.f;

    pos_faces[0] = pos_faces[1] = pos_faces[2] = 0.f;
    neg_faces[0] = neg_faces[1] = neg_faces[2] = 0.f;

    hotness[0] = hotness[1] = hotness[2] = hotness[3] = 0.f;
    glowing[0] = glowing[1] = glowing[2] = glowing[3] = 0.f;
}

void R_hemi_vk::set_material(float h, float s, float r, float m)
{
    hemi_value = h;
    sun_value = s;
    material_value = m;
}

void R_hemi_vk::set_pos_faces(float px, float py, float pz)
{
    pos_faces[0] = px;
    pos_faces[1] = py;
    pos_faces[2] = pz;
}

void R_hemi_vk::set_neg_faces(float nx, float ny, float nz)
{
    neg_faces[0] = nx;
    neg_faces[1] = ny;
    neg_faces[2] = nz;
}

void R_hemi_vk::set_hotness(float x, float y, float z, float w)
{
    hotness[0] = x;
    hotness[1] = y;
    hotness[2] = z;
    hotness[3] = w;
}

void R_hemi_vk::set_glowing(float x, float y, float z, float w)
{
    glowing[0] = x;
    glowing[1] = y;
    glowing[2] = z;
    glowing[3] = w;
}

// ============================================================================
// CBackend implementation
// ============================================================================
CBackend::CBackend()
{
    VulkanDiagWriteBackend("[DIAG] CBackend::CBackend() - before Invalidate()");
    Invalidate();
    VulkanDiagWriteBackend("[DIAG] CBackend::CBackend() - after Invalidate()");
}

CBackend::~CBackend()
{
}

void CBackend::Invalidate()
{
    VulkanDiagWriteBackend("[DIAG] Invalidate: step 1 - m_Cmd");
    m_Cmd = VK_NULL_HANDLE;
    VulkanDiagWriteBackend("[DIAG] Invalidate: step 2 - m_CurrentPipeline");
    m_CurrentPipeline = VK_NULL_HANDLE;

    VulkanDiagWriteBackend("[DIAG] Invalidate: step 3 - descriptor sets");
    for (u32 i = 0; i < 4; ++i)
    {
        m_CurrentDescriptorSets[i] = VK_NULL_HANDLE;
        m_CurrentRT[i] = VK_NULL_HANDLE;
    }
    m_BoundDescriptorSetCount = 0;

    VulkanDiagWriteBackend("[DIAG] Invalidate: step 4 - buffers");
    m_CurrentVB = VK_NULL_HANDLE;
    m_CurrentIB = VK_NULL_HANDLE;
    m_VBStride = 0;
    m_IndexType = VK_INDEX_TYPE_UINT16;

    m_CurrentZB = VK_NULL_HANDLE;
    m_RTCount = 0;

    VulkanDiagWriteBackend("[DIAG] Invalidate: step 5 - viewport");
    // Default viewport
    m_Viewport.x = 0.f;
    m_Viewport.y = 0.f;
    m_Viewport.width = 1.f;
    m_Viewport.height = 1.f;
    m_Viewport.minDepth = 0.f;
    m_Viewport.maxDepth = 1.f;

    // Default scissor
    m_Scissor.offset = {0, 0};
    m_Scissor.extent = {1, 1};

    VulkanDiagWriteBackend("[DIAG] Invalidate: step 6 - render state");
    // Default render state
    m_StencilEnable = 0;
    m_StencilFunc = 0;
    m_StencilRef = 0;
    m_StencilMask = 0;
    m_StencilWriteMask = 0;
    m_ColorWriteMask = 0xF;  // All channels
    m_CullMode = 1;  // Back face culling
    m_ZEnable = 1;
    m_ZFunc = 2;  // Less
    m_AlphaRef = 0;

    VulkanDiagWriteBackend("[DIAG] Invalidate: step 7 - stats");
    // Reset statistics
    memset(&stat, 0, sizeof(stat));
    VulkanDiagWriteBackend("[DIAG] Invalidate: step 7b - after ZeroMemory");

    VulkanDiagWriteBackend("[DIAG] Invalidate: step 8 - xforms.unmap()");
    // Reset transforms
    xforms.unmap();
    VulkanDiagWriteBackend("[DIAG] Invalidate: DONE");
}

// ============================================================================
// Frame lifecycle
// ============================================================================
static u32 s_FrameCount = 0;

void CBackend::OnFrameBegin()
{
    // Reset per-frame statistics
    ZeroMemory(&stat, sizeof(stat));

    // Reset transforms
    xforms.unmap();

    // Only log occasionally to avoid spam
    s_FrameCount++;
    if (s_FrameCount % 300 == 1) {  // Every ~5 seconds at 60fps
        Msg("[Vulkan] CBackend::OnFrameBegin (frame %u)", s_FrameCount);
    }
}

void CBackend::OnFrameEnd()
{
    // Log statistics occasionally
    if (s_FrameCount % 300 == 0) {
        Msg("[Vulkan] Frame stats - polys:%u verts:%u calls:%u xforms:%u",
            stat.polys, stat.verts, stat.calls, stat.xforms);
    }
}

void CBackend::OnDeviceCreate()
{
    Msg("[Vulkan] CBackend::OnDeviceCreate");
    Invalidate();
}

void CBackend::OnDeviceDestroy()
{
    Msg("[Vulkan] CBackend::OnDeviceDestroy");
    Invalidate();
}

// ============================================================================
// Command buffer management
// ============================================================================
void CBackend::BeginCommandBuffer(VkCommandBuffer cmd)
{
    m_Cmd = cmd;
}

void CBackend::EndCommandBuffer()
{
    m_Cmd = VK_NULL_HANDLE;
}

// ============================================================================
// Geometry binding
// ============================================================================
void CBackend::set_Vertices(VkBuffer vb, u32 stride)
{
    if (m_CurrentVB != vb || m_VBStride != stride)
    {
        m_CurrentVB = vb;
        m_VBStride = stride;

        if (m_Cmd != VK_NULL_HANDLE && vb != VK_NULL_HANDLE)
        {
            VkDeviceSize offset = 0;
            vkCmdBindVertexBuffers(m_Cmd, 0, 1, &vb, &offset);
        }
    }
}

void CBackend::set_Indices(VkBuffer ib, VkIndexType indexType)
{
    if (m_CurrentIB != ib || m_IndexType != indexType)
    {
        m_CurrentIB = ib;
        m_IndexType = indexType;

        if (m_Cmd != VK_NULL_HANDLE && ib != VK_NULL_HANDLE)
        {
            vkCmdBindIndexBuffer(m_Cmd, ib, 0, indexType);
        }
    }
}

void CBackend::set_Geometry(SGeometry* geom)
{
    // TODO: Extract VB/IB from SGeometry when implemented
    // For now, this is a stub
}

// ============================================================================
// Pipeline/Shader binding
// ============================================================================
void CBackend::set_Pipeline(VkPipeline pipeline)
{
    if (m_CurrentPipeline != pipeline)
    {
        m_CurrentPipeline = pipeline;

        if (m_Cmd != VK_NULL_HANDLE && pipeline != VK_NULL_HANDLE)
        {
            vkCmdBindPipeline(m_Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        }
    }
}

void CBackend::set_DescriptorSet(u32 index, VkDescriptorSet set)
{
    if (index < 4 && m_CurrentDescriptorSets[index] != set)
    {
        m_CurrentDescriptorSets[index] = set;
        if (index >= m_BoundDescriptorSetCount)
            m_BoundDescriptorSetCount = index + 1;

        // Note: Actual binding happens at draw time to batch descriptor set binds
    }
}

void CBackend::set_Shader(Shader* S, u32 pass)
{
    // TODO: Implement when Vulkan shader system is integrated
    // This will need to:
    // 1. Get the pipeline from the shader pass
    // 2. Bind the pipeline
    // 3. Set up descriptor sets for textures/constants
}

void CBackend::set_Element(ShaderElement* S, u32 pass)
{
    // TODO: Implement when Vulkan shader system is integrated
}

// ============================================================================
// Constants (Push constants / Uniform buffers)
// ============================================================================
void CBackend::set_c(LPCSTR name, float x, float y, float z, float w)
{
    // TODO: Implement push constants or uniform buffer updates
    // For now, store in a pending update structure
}

void CBackend::set_c(LPCSTR name, const Fmatrix& M)
{
    // TODO: Implement push constants or uniform buffer updates
}

void CBackend::set_c(LPCSTR name, const Fvector4& V)
{
    set_c(name, V.x, V.y, V.z, V.w);
}

void* CBackend::get_c(LPCSTR name)
{
    // TODO: Implement shader constant retrieval
    // For skeleton compatibility, return nullptr for now
    // Skeleton code uses this to get constant handles for bone matrices
    return nullptr;
}

void CBackend::set_ca(void* c, u32 startReg, u32 count, const void* data)
{
    // TODO: Implement shader constant array setting
    // For skeleton compatibility, stub for now
    // Skeleton code uses this to upload bone matrix arrays
}

void CBackend::set_ca(void* c, u32 index, float v0, float v1, float v2, float v3)
{
    // TODO: Implement shader constant vector setting
    // For skeleton compatibility, stub for now
    // Skeleton code uses this to set individual bone matrix rows
}

void CBackend::set_Geometry(void* ref_geom)
{
    // ref_geom compatibility wrapper
    // Cast void* to SGeometry* (ref_geom is a typedef for void*)
    set_Geometry(static_cast<SGeometry*>(ref_geom));
}

// ============================================================================
// Render targets
// ============================================================================
void CBackend::set_RT(VkImageView RT, u32 ID)
{
    if (ID < 4)
    {
        m_CurrentRT[ID] = RT;
        if (RT != VK_NULL_HANDLE && ID >= m_RTCount)
            m_RTCount = ID + 1;
        stat.target_rt++;
    }
}

void CBackend::set_ZB(VkImageView ZB)
{
    m_CurrentZB = ZB;
    stat.target_zb++;
}

VkImageView CBackend::get_RT(u32 ID)
{
    return (ID < 4) ? m_CurrentRT[ID] : VK_NULL_HANDLE;
}

VkImageView CBackend::get_ZB()
{
    return m_CurrentZB;
}

// ============================================================================
// Render state
// ============================================================================
void CBackend::set_Stencil(u32 enable, u32 func, u32 ref, u32 mask,
                           u32 writemask, u32 fail, u32 pass, u32 zfail)
{
    m_StencilEnable = enable;
    m_StencilFunc = func;
    m_StencilRef = ref;
    m_StencilMask = mask;
    m_StencilWriteMask = writemask;

    // Note: In Vulkan 1.3 with dynamic state, we can set stencil dynamically
    if (m_Cmd != VK_NULL_HANDLE && enable)
    {
        vkCmdSetStencilReference(m_Cmd, VK_STENCIL_FACE_FRONT_AND_BACK, ref);
        vkCmdSetStencilCompareMask(m_Cmd, VK_STENCIL_FACE_FRONT_AND_BACK, mask);
        vkCmdSetStencilWriteMask(m_Cmd, VK_STENCIL_FACE_FRONT_AND_BACK, writemask);
    }
}

void CBackend::set_Z(u32 enable)
{
    m_ZEnable = enable;
    // Note: Depth test enable is part of pipeline state in Vulkan
    // With VK_EXT_extended_dynamic_state3, we could set this dynamically
}

void CBackend::set_ZFunc(u32 func)
{
    m_ZFunc = func;
    // Note: Depth compare op is part of pipeline state in Vulkan
}

void CBackend::set_AlphaRef(u32 value)
{
    m_AlphaRef = value;
    // Alpha test is typically done in fragment shader in modern APIs
}

void CBackend::set_ColorWriteEnable(u32 mask)
{
    m_ColorWriteMask = mask;
    // Note: Color write mask is part of pipeline state
    // With VK_EXT_extended_dynamic_state3, we could set this dynamically
}

void CBackend::set_CullMode(u32 mode)
{
    m_CullMode = mode;

    // With VK_EXT_extended_dynamic_state, we can set cull mode dynamically
    if (m_Cmd != VK_NULL_HANDLE)
    {
        VkCullModeFlags vkMode = VK_CULL_MODE_NONE;
        switch (mode)
        {
        case 1: vkMode = VK_CULL_MODE_NONE; break;      // D3DCULL_NONE
        case 2: vkMode = VK_CULL_MODE_FRONT_BIT; break; // D3DCULL_CW
        case 3: vkMode = VK_CULL_MODE_BACK_BIT; break;  // D3DCULL_CCW
        }
        vkCmdSetCullMode(m_Cmd, vkMode);
    }
}

void CBackend::set_Scissor(Irect* rect)
{
    if (rect)
    {
        m_Scissor.offset.x = rect->x1;
        m_Scissor.offset.y = rect->y1;
        m_Scissor.extent.width = rect->x2 - rect->x1;
        m_Scissor.extent.height = rect->y2 - rect->y1;
    }
    else
    {
        // Full viewport scissor
        m_Scissor.offset = {0, 0};
        m_Scissor.extent.width = (u32)m_Viewport.width;
        m_Scissor.extent.height = (u32)m_Viewport.height;
    }

    if (m_Cmd != VK_NULL_HANDLE)
    {
        vkCmdSetScissor(m_Cmd, 0, 1, &m_Scissor);
    }
}

void CBackend::ApplyViewportScissor()
{
    if (m_Cmd != VK_NULL_HANDLE)
    {
        vkCmdSetViewport(m_Cmd, 0, 1, &m_Viewport);
        vkCmdSetScissor(m_Cmd, 0, 1, &m_Scissor);
    }
}

// ============================================================================
// Draw calls
// ============================================================================
void CBackend::Render(u32 primitive_type, u32 baseV, u32 startV, u32 countV, u32 startI, u32 PC)
{
    if (m_Cmd == VK_NULL_HANDLE || m_CurrentPipeline == VK_NULL_HANDLE)
        return;

    // Calculate index count from primitive count
    u32 indexCount = 0;
    switch (primitive_type)
    {
    case 4:  // D3DPT_TRIANGLELIST
        indexCount = PC * 3;
        break;
    case 5:  // D3DPT_TRIANGLESTRIP
        indexCount = PC + 2;
        break;
    case 1:  // D3DPT_POINTLIST
        indexCount = PC;
        break;
    case 2:  // D3DPT_LINELIST
        indexCount = PC * 2;
        break;
    case 3:  // D3DPT_LINESTRIP
        indexCount = PC + 1;
        break;
    default:
        indexCount = PC * 3;
        break;
    }

    // Draw indexed
    vkCmdDrawIndexed(m_Cmd, indexCount, 1, startI, baseV, 0);

    // Update statistics
    stat.calls++;
    stat.verts += countV;
    stat.polys += PC;
}

void CBackend::Render(u32 primitive_type, u32 startV, u32 PC)
{
    if (m_Cmd == VK_NULL_HANDLE || m_CurrentPipeline == VK_NULL_HANDLE)
        return;

    // Calculate vertex count from primitive count
    u32 vertexCount = 0;
    switch (primitive_type)
    {
    case 4:  // D3DPT_TRIANGLELIST
        vertexCount = PC * 3;
        break;
    case 5:  // D3DPT_TRIANGLESTRIP
        vertexCount = PC + 2;
        break;
    case 1:  // D3DPT_POINTLIST
        vertexCount = PC;
        break;
    case 2:  // D3DPT_LINELIST
        vertexCount = PC * 2;
        break;
    case 3:  // D3DPT_LINESTRIP
        vertexCount = PC + 1;
        break;
    default:
        vertexCount = PC * 3;
        break;
    }

    // Draw non-indexed
    vkCmdDraw(m_Cmd, vertexCount, 1, startV, 0);

    // Update statistics
    stat.calls++;
    stat.verts += vertexCount;
    stat.polys += PC;
}

// ============================================================================
// Debug rendering (stubs for now)
// ============================================================================

void CBackend::dbg_Draw(D3DPRIMITIVETYPE T, FVF::L* pVerts, int vcnt, u16* pIdx, int pcnt)
{
    // TODO: Implement indexed debug draw for Vulkan
    // This requires:
    // 1. Create/update dynamic vertex buffer with pVerts
    // 2. Create/update dynamic index buffer with pIdx
    // 3. Bind debug shader (simple position+color shader)
    // 4. Set proper blend mode (alpha blend)
    // 5. Draw indexed primitives

    // For now, this is a stub
    // Used by: HOM wireframe rendering, portal scissor visualization
}

void CBackend::dbg_Draw(D3DPRIMITIVETYPE T, FVF::L* pVerts, int pcnt)
{
    // TODO: Implement non-indexed debug draw for Vulkan
    // This requires:
    // 1. Create/update dynamic vertex buffer with pVerts
    // 2. Bind debug shader (simple position+color shader)
    // 3. Set proper blend mode (alpha blend)
    // 4. Draw primitives

    // For now, this is a stub
    // Used by: HOM solid rendering, portal fade, debug lines/triangles
}

void CBackend::dbg_DrawOBB(Fmatrix& T, Fvector& half_dim, u32 C)
{
    // TODO: Implement debug OBB rendering
}

void CBackend::dbg_DrawTRI(Fmatrix& T, Fvector& p1, Fvector& p2, Fvector& p3, u32 C)
{
    // TODO: Implement debug triangle rendering
}

void CBackend::dbg_DrawLINE(Fmatrix& T, Fvector& p1, Fvector& p2, u32 C)
{
    // TODO: Implement debug line rendering
}
