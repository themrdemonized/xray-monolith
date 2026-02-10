// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// Licensed under the same terms as X-Ray Engine (see root License.txt)

#include "stdafx.h"
#include "vk_Visual.h"
#include <unordered_map>
#include "vk_R_Backend.h"
#include "vk_buffer_pool.h"
#include "vk_shader.h"      // Phase 2.34: Shader binding
#include "vk_material.h"    // Phase 2.34: Material binding
#include "vk_pipeline.h"    // For g_PipelineManager (pipeline switching)
#include "vk_rendertarget.h" // For RTarget->GetGBufferPipeline()
#include "vk_descriptors.h"  // For g_DescriptorManager (bone SSBO descriptor)
#include "vk_d3d_compat.h"   // For VK_GetFVFVertexSize (FVF parsing)
#include "rvk.h"
// IMPORTANT: Must use same dxRender_Visual→vkRender_Visual mapping as skeleton files
// to ensure CKinematics memory layout matches the objects created by vkCreateKinematics()
#define FBasicVisualH
#define dxRender_Visual vkRender_Visual
#include "../xrRender/SkeletonCustom.h"  // For CKinematics bone transforms
#undef dxRender_Visual
#undef FBasicVisualH
#include "3DFluid/vk3DFluidVolume.h"     // Phase 0: 3D Fluid system

// UV Diagnostic: shadow VB data for dumping UV values (defined in rvk_loader.cpp)
extern void VBShadow_DumpUVs(VK::CVulkanBuffer* vb, u32 vBase, u32 vCount, const char* label);

// Factory functions for Skeleton classes (implemented in vk_Skeleton*.cpp wrapper files)
extern "C" void* vkCreateKinematics();
extern "C" void* vkCreateKinematicsAnimated();

// ============================================================================
// VK_Render_Mesh implementation
// ============================================================================
VK_Render_Mesh::~VK_Render_Mesh()
{
    Destroy();
}

void VK_Render_Mesh::Destroy()
{
    // Only delete buffers if we own them (not a shared Copy)
    if (bOwnsBuffers)
    {
        if (p_rm_Vertices)
        {
            xr_delete(p_rm_Vertices);
        }

        if (p_rm_Indices)
        {
            xr_delete(p_rm_Indices);
        }

        if (m_fast)
        {
            xr_delete(m_fast);
        }
    }

    p_rm_Vertices = nullptr;
    p_rm_Indices = nullptr;
    m_fast = nullptr;
    vBase = vCount = vStride = 0;
    tcOffset = 24;
    iBase = iCount = 0;
    dwPrimitives = 0;
}

// ============================================================================
// vkRender_Visual implementation
// ============================================================================
vkRender_Visual::vkRender_Visual()
{
    Type = 0;
    skinning = -1;
}

vkRender_Visual::~vkRender_Visual()
{
}

void vkRender_Visual::Load(const char* name, IReader* data, u32 flags)
{
    dbg_name = name;

    // Load header (required)
    LoadHeader(data);

    // Load texture/shader info (optional)
    LoadTexture(data);
}

void vkRender_Visual::Release()
{
    // Base class has nothing to release
}

void vkRender_Visual::Copy(vkRender_Visual* from)
{
    Type = from->Type;
    vis = from->vis;
    skinning = from->skinning;
    dbg_name = from->dbg_name;
    m_pMaterial = from->m_pMaterial;
    m_fAlphaRef = from->m_fAlphaRef;
}

void vkRender_Visual::Render(float LOD)
{
    // Base class does nothing
}

void vkRender_Visual::LoadHeader(IReader* data)
{
    // Read OGF header
    ogf_header H;
    if (data->find_chunk(OGF_HEADER))
    {
        data->r(&H, sizeof(H));

        Type = H.type;
        shader_id = H.shader_id;  // Phase 2.34: Store shader ID for rendering

        // Set visibility data (copy from OGF header structs to engine structs)
        vis.box.set(H.bb.min, H.bb.max);
        vis.sphere.set(H.bs.c, H.bs.r);
    }
    else
    {
        Msg("![Vulkan] Missing OGF_HEADER in %s", dbg_name.c_str());

        // Initialize to safe defaults
        Type = 0;
        shader_id = 0;  // Will use default shader (index 0) or fallback in Render()
        vis.box.set(Fvector().set(0,0,0), Fvector().set(0,0,0));
        vis.sphere.set(Fvector().set(0,0,0), 0.0f);
    }
}

void vkRender_Visual::LoadTexture(IReader* data)
{
    // Read texture chunk (optional - present in standalone OGF models)
    if (data->find_chunk(OGF_TEXTURE))
    {
        string256 texture_name;
        string256 shader_name;

        data->r_stringZ(texture_name, sizeof(texture_name));
        data->r_stringZ(shader_name, sizeof(shader_name));

        // Create material from texture name (loads DDS, creates descriptor set)
        if (g_MaterialManager && texture_name[0])
        {
            m_pMaterial = g_MaterialManager->CreateMaterial(texture_name);
        }

        // Detect alpha-ref shader from OGF_TEXTURE shader name
        if (shader_name[0])
        {
            xr_string sn_lower = shader_name;
            std::transform(sn_lower.begin(), sn_lower.end(), sn_lower.begin(), ::tolower);
            if (sn_lower.find("aref") != xr_string::npos ||
                sn_lower.find("alpha") != xr_string::npos ||
                sn_lower.find("trans") != xr_string::npos)
            {
                m_fAlphaRef = 200.0f / 255.0f;  // DX11 def_aref uses oAREF=200
            }
        }
    }

    // Fallback: if no OGF_TEXTURE chunk (level geometry), use shader_id from header
    if (!m_pMaterial && RImplementation.Shaders.size() > 0)
    {
        if (shader_id < (u16)RImplementation.Shaders.size())
        {
            VK::CVulkanShader* pShader = RImplementation.Shaders[shader_id];
            if (pShader)
            {
                m_pMaterial = pShader->GetMaterial();
                if (pShader->m_bAlphaRef)
                    m_fAlphaRef = 200.0f / 255.0f;  // DX11 def_aref uses oAREF=200
            }
        }
    }
}

// ============================================================================
// vkFVisual implementation - Standard triangle mesh
// ============================================================================
vkFVisual::vkFVisual()
{
    Type = MT_NORMAL;
}

vkFVisual::~vkFVisual()
{
    Release();
}

void vkFVisual::Load(const char* name, IReader* data, u32 flags)
{
    // Load base class data
    vkRender_Visual::Load(name, data, flags);

    // Load geometry (pass flags for VLOAD_NOVERTICES support)
    LoadGeometry(data, flags);

    // Load fast-path (optional)
    LoadFastPath(data);
}

void vkFVisual::Release()
{
    m_mesh.Destroy();
    vkRender_Visual::Release();
}

void vkFVisual::Copy(vkRender_Visual* from)
{
    vkRender_Visual::Copy(from);

    vkFVisual* src = dynamic_cast<vkFVisual*>(from);
    if (!src) return;

    // Share buffer references (not deep copy)
    // In X-Ray, meshes typically share buffers and only differ by shader
    // Mark as non-owning so Destroy() won't double-free the shared buffers
    m_mesh.p_rm_Vertices = src->m_mesh.p_rm_Vertices;
    m_mesh.vBase = src->m_mesh.vBase;
    m_mesh.vCount = src->m_mesh.vCount;
    m_mesh.vStride = src->m_mesh.vStride;
    m_mesh.tcOffset = src->m_mesh.tcOffset;

    m_mesh.p_rm_Indices = src->m_mesh.p_rm_Indices;
    m_mesh.iBase = src->m_mesh.iBase;
    m_mesh.iCount = src->m_mesh.iCount;

    m_mesh.bOwnsBuffers = false;  // Don't free shared buffers on destroy
    m_mesh.iType = src->m_mesh.iType;

    m_mesh.dwPrimitives = src->m_mesh.dwPrimitives;

    // Copy fast-path reference
    m_mesh.m_fast = src->m_mesh.m_fast;
}

void vkFVisual::Render(float LOD)
{
    if (!m_mesh.IsValid())
        return;

    // Per-visual diagnostic: log every unique stride-32 visual once
    // Key = VB pointer XOR vBase to identify unique geometry
    if (m_mesh.vStride == 32) {
        static xr_set<u64> s_loggedVisuals;
        static u32 s_logCount = 0;
        u64 key = (u64)(uintptr_t)m_mesh.p_rm_Vertices ^ ((u64)m_mesh.vBase << 32);
        if (s_logCount < 500 && s_loggedVisuals.find(key) == s_loggedVisuals.end()) {
            s_loggedVisuals.insert(key);
            s_logCount++;
            const Fmatrix& Wdbg = RCache.xforms.m_w;
            const char* matName = (m_pMaterial && m_pMaterial->m_Name.size() > 0)
                ? m_pMaterial->m_Name.c_str() : "<none>";
            const char* vName = (dbg_name.size() > 0) ? dbg_name.c_str() : "<anon>";
            Msg("[VIS-DIAG] #%u name='%s' tex='%s' stride=%u tcOff=%u vBase=%u vCount=%u iBase=%u iCount=%u pos=(%.1f,%.1f,%.1f)",
                s_logCount, vName, matName,
                m_mesh.vStride, m_mesh.tcOffset,
                m_mesh.vBase, m_mesh.vCount,
                m_mesh.iBase, m_mesh.iCount,
                Wdbg._41, Wdbg._42, Wdbg._43);

            // UV Diagnostic: dump UV values for first 20 visuals
            if (s_logCount <= 20 && m_mesh.p_rm_Vertices) {
                string256 diagLabel;
                xr_sprintf(diagLabel, "VIS#%u[%s]", s_logCount, matName);
                VBShadow_DumpUVs(m_mesh.p_rm_Vertices, m_mesh.vBase, m_mesh.vCount, diagLabel);
            }
        }
    }

    // Push current world matrix to GPU (push constant offset 0)
    // The world matrix is set by the caller via RCache.set_xform_world()
    // but that only caches it — we must push to GPU here.
    {
        VkCommandBuffer cmd = RCache.GetCommandBuffer();
        if (cmd != VK_NULL_HANDLE) {
            VkPipelineLayout layout = VK::g_PipelineManager->GetLayout();
            if (layout != VK_NULL_HANDLE) {
                const Fmatrix& W = RCache.xforms.m_w;
                vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Fmatrix), &W);
                const Fmatrix& Wprev = RCache.xforms.m_w_prev;
                vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_VERTEX_BIT, 64, sizeof(Fmatrix), &Wprev);

                // Per-visual alpha test: push m_fAlphaRef (offset 200)
                // -1.0 = disabled (solid), 0.5 = enabled (foliage/aref shaders)
                vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_FRAGMENT_BIT,
                    200, sizeof(float), &m_fAlphaRef);

                // Always push uvScale to guard against stale values
                float uvScale = (m_mesh.vStride == 32) ? (1.0f / 1024.0f) : 1.0f;
                vkCmdPushConstants(cmd, layout,
                    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                    192, sizeof(float), &uvScale);
            }
        }
    }

    u32 stride = m_mesh.vStride;

    // Handle stride=12 (trees: FLOAT3 position only, no normals/UVs)
    if (stride == 12)
    {
        if (!m_mesh.p_rm_Vertices || !m_mesh.p_rm_Indices)
            return;

        // Switch to stride-12 pipeline only if not already active
        if (RCache.m_CurrentGBufStride != 12)
        {
            VkPipeline pipeline = RTarget->GetGBufferPipeline(12);
            if (pipeline == VK_NULL_HANDLE)
                return;

            RCache.set_Pipeline(pipeline);

            // Tree positions are quantized by FTreeVisual_quant=2048, need prescale
            float uvScale = 1.0f / 2048.0f;
            VkCommandBuffer cmd = RCache.GetCommandBuffer();
            if (cmd != VK_NULL_HANDLE)
            {
                VkPipelineLayout layout = VK::g_PipelineManager->GetLayout();
                vkCmdPushConstants(cmd, layout,
                    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                    192, sizeof(float), &uvScale);
                // alphaRef is pushed per-visual above (m_fAlphaRef)
            }

            RCache.m_CurrentGBufStride = 12;
        }

        // Bind tree's own material (has actual leaf/bark textures with alpha channel)
        VkCommandBuffer cmd = RCache.GetCommandBuffer();
        if (cmd != VK_NULL_HANDLE)
        {
            if (m_pMaterial && m_pMaterial->IsValid())
                m_pMaterial->Bind(cmd);
            else if (g_MaterialManager && g_MaterialManager->GetDefaultMaterial())
                g_MaterialManager->GetDefaultMaterial()->Bind(cmd);
        }

        RCache.set_Vertices(m_mesh.p_rm_Vertices->GetHandle(), m_mesh.vStride);
        RCache.set_Indices(m_mesh.p_rm_Indices->GetHandle(), m_mesh.iType);
        RCache.Render(4, m_mesh.vBase, 0, m_mesh.vCount, m_mesh.iBase, m_mesh.dwPrimitives);

        RCache.stat.polys += m_mesh.dwPrimitives;
        RCache.stat.verts += m_mesh.vCount;
        return;
    }

    // Supported vertex strides:
    //   32 = level static (FLOAT3 pos + D3DCOLOR normal @12 + SHORT2 UV @24)
    //   36 = skinned 1W   (FLOAT4 pos + D3DCOLOR normal @16 + FLOAT2 UV @28)
    //   40 = skinned 4W   (FLOAT4 pos + D3DCOLOR normal @16 + FLOAT2 UV @28)
    //   44 = skinned 2W/3W(FLOAT4 pos + D3DCOLOR normal @16 + FLOAT2 UV @28)
    if (stride != 32 && stride != 36 && stride != 40 && stride != 44)
        return;

    // Both buffers must be valid to render
    if (!m_mesh.p_rm_Vertices || !m_mesh.p_rm_Indices)
        return;

    // Switch G-Buffer pipeline if stride changed, or if stride-32 and tcOffset changed
    // tcOffset only matters for stride-32 (two sub-layouts: lmap@24 vs vert-lit@28)
    u32 tcOff = m_mesh.tcOffset;
    bool needSwitch = (stride != RCache.m_CurrentGBufStride);
    if (!needSwitch && stride == 32)
        needSwitch = (tcOff != RCache.m_CurrentGBufTcOffset);

    if (needSwitch)
    {
        VkPipeline pipeline = RTarget->GetGBufferPipeline(stride, tcOff);
        if (pipeline != VK_NULL_HANDLE)
        {
            RCache.set_Pipeline(pipeline);

            // Update UV scale push constant at offset 192
            // NOTE: do NOT override alphaRef here — the caller controls alpha test
            float uvScale = (stride == 32) ? (1.0f / 1024.0f) : 1.0f;
            VkCommandBuffer cmd = RCache.GetCommandBuffer();
            if (cmd != VK_NULL_HANDLE)
            {
                VkPipelineLayout layout = VK::g_PipelineManager->GetLayout();
                vkCmdPushConstants(cmd, layout,
                    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                    192, sizeof(float), &uvScale);
            }

            RCache.m_CurrentGBufStride = stride;
            RCache.m_CurrentGBufTcOffset = tcOff;
        }
        else
        {
            return;  // Can't render without a valid pipeline
        }
    }

    // Bind material descriptor set (Set 1 - diffuse texture)
    {
        VkCommandBuffer cmd = RCache.GetCommandBuffer();
        if (cmd != VK_NULL_HANDLE)
        {
            if (m_pMaterial && m_pMaterial->IsValid())
                m_pMaterial->Bind(cmd);
            else if (g_MaterialManager && g_MaterialManager->GetDefaultMaterial())
                g_MaterialManager->GetDefaultMaterial()->Bind(cmd);
        }
    }

    // Bind vertex buffer
    RCache.set_Vertices(m_mesh.p_rm_Vertices->GetHandle(), m_mesh.vStride);

    // Bind index buffer
    RCache.set_Indices(m_mesh.p_rm_Indices->GetHandle(), m_mesh.iType);

    // Draw
    RCache.Render(4, m_mesh.vBase, 0, m_mesh.vCount, m_mesh.iBase, m_mesh.dwPrimitives);

    // Update statistics
    RCache.stat.polys += m_mesh.dwPrimitives;
    RCache.stat.verts += m_mesh.vCount;
}

// ============================================================================
// ConvertFVFToLevelFormat - Convert FVF vertex data to stride-32 level format
// ============================================================================
// Level-static stride-32 layout (matches pipeline expectation):
//   FLOAT3  position   @ offset 0   (12 bytes)
//   D3DCOLOR normal    @ offset 12  (4 bytes)
//   D3DCOLOR tangent   @ offset 16  (4 bytes)  -- neutral 0x80808080
//   D3DCOLOR binormal  @ offset 20  (4 bytes)  -- neutral 0x80808080
//   SHORT2  tc0        @ offset 24  (4 bytes)  -- UV * 1024
//   SHORT2  tc1        @ offset 28  (4 bytes)  -- lightmap, zero
// Total: 32 bytes
//
static void ConvertFVFToLevelFormat(const u8* src, u8* dst, u32 vertCount, u32 fvf, u32 fvfStride)
{
    // Compute byte offsets of FVF components
    u32 posOffset = 0;
    u32 posSize = 0;
    switch (fvf & D3DFVF_POSITION_MASK) {
    case D3DFVF_XYZ:    posSize = 12; break;
    case D3DFVF_XYZRHW: posSize = 16; break;
    case D3DFVF_XYZB1:  posSize = 16; break;
    case D3DFVF_XYZB2:  posSize = 20; break;
    case D3DFVF_XYZB3:  posSize = 24; break;
    case D3DFVF_XYZB4:  posSize = 28; break;
    case D3DFVF_XYZB5:  posSize = 32; break;
    case D3DFVF_XYZW:   posSize = 16; break;
    default:            posSize = 12; break;
    }

    u32 off = posSize;
    u32 normalOffset = 0;
    bool hasNormal = false;
    if (fvf & D3DFVF_NORMAL)  { normalOffset = off; hasNormal = true; off += 12; }
    if (fvf & D3DFVF_PSIZE)   { off += 4; }
    if (fvf & D3DFVF_DIFFUSE) { off += 4; }
    if (fvf & D3DFVF_SPECULAR){ off += 4; }
    u32 texCount = (fvf & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT;
    u32 tc0Offset = off;
    bool hasTC = (texCount > 0);

    const u32 LEVEL_STRIDE = 32;

    for (u32 i = 0; i < vertCount; i++)
    {
        const u8* sv = src + i * fvfStride;
        u8* dv = dst + i * LEVEL_STRIDE;

        // Position: copy FLOAT3 (always first 12 bytes)
        CopyMemory(dv, sv + posOffset, 12);

        // Normal: pack FLOAT3 -> D3DCOLOR
        if (hasNormal) {
            const float* N = (const float*)(sv + normalOffset);
            u32 packed = color_rgba(vk_q_N(N[0]), vk_q_N(N[1]), vk_q_N(N[2]), 0);
            CopyMemory(dv + 12, &packed, 4);
        } else {
            u32 upNormal = color_rgba(vk_q_N(0.f), vk_q_N(1.f), vk_q_N(0.f), 0);
            CopyMemory(dv + 12, &upNormal, 4);
        }

        // Tangent + Binormal: neutral values
        u32 neutral = 0x80808080u;
        CopyMemory(dv + 16, &neutral, 4);
        CopyMemory(dv + 20, &neutral, 4);

        // TC0: convert FLOAT2 -> SHORT2 (multiply by 1024)
        if (hasTC) {
            const float* uv = (const float*)(sv + tc0Offset);
            s16 su = (s16)clampr(iFloor(uv[0] * 1024.f + 0.5f), -32768, 32767);
            s16 sv16 = (s16)clampr(iFloor(uv[1] * 1024.f + 0.5f), -32768, 32767);
            CopyMemory(dv + 24, &su, 2);
            CopyMemory(dv + 26, &sv16, 2);
        } else {
            ZeroMemory(dv + 24, 4);
        }

        // TC1 (lightmap): zero
        ZeroMemory(dv + 28, 4);
    }
}

void vkFVisual::LoadGeometry(IReader* data, u32 flags)
{
    BOOL bNoVertices = (flags & VLOAD_NOVERTICES) != 0;

    // Try OGF_GCONTAINER first (shared buffers - most common for level geometry)
    if (data->find_chunk(OGF_GCONTAINER))
    {
        // Container format:
        // u32 vb_id, u32 vb_offset, u32 vb_count
        // u32 ib_id, u32 ib_offset, u32 ib_count

        u32 vb_id = data->r_u32();
        u32 vb_offset = data->r_u32();
        u32 vb_count = data->r_u32();

        u32 ib_id = data->r_u32();
        u32 ib_offset = data->r_u32();
        u32 ib_count = data->r_u32();

        // Always load index buffer info
        m_mesh.iBase = ib_offset;
        m_mesh.iCount = ib_count;
        m_mesh.dwPrimitives = ib_count / 3;

        // Get IB from pool
        if (VK::g_BufferPool)
        {
            m_mesh.p_rm_Indices = VK::g_BufferPool->GetIndexBuffer(ib_id);
            if (m_mesh.p_rm_Indices)
                m_mesh.iType = VK::g_BufferPool->GetIndexType(ib_id);
        }

        // Only load VB if not skipping vertices
        if (!bNoVertices)
        {
            m_mesh.vBase = vb_offset;
            m_mesh.vCount = vb_count;

            if (VK::g_BufferPool)
            {
                m_mesh.p_rm_Vertices = VK::g_BufferPool->GetVertexBuffer(vb_id);
                if (m_mesh.p_rm_Vertices)
                {
                    m_mesh.vStride = VK::g_BufferPool->GetVertexStride(vb_id);
                    m_mesh.tcOffset = VK::g_BufferPool->GetTexCoordOffset(vb_id);
                }
            }
        }

        if (!m_mesh.p_rm_Indices)
        {
            Msg("! [Vulkan] Visual '%s' geometry: IB[%u] not found in pool",
                dbg_name.c_str(), ib_id);
        }
        if (!bNoVertices && !m_mesh.p_rm_Vertices)
        {
            Msg("! [Vulkan] Visual '%s' geometry: VB[%u] not found in pool",
                dbg_name.c_str(), vb_id);
        }

        return;
    }

    // Try inline vertices (OGF_VERTICES + OGF_INDICES) - standalone models (weapons, etc.)
    if (!bNoVertices && data->find_chunk(OGF_VERTICES))
    {
        u32 vert_format = data->r_u32();
        u32 vert_count = data->r_u32();

        // Compute FVF stride from format flags
        u32 fvf_stride = VK_GetFVFVertexSize(vert_format);
        if (fvf_stride == 0) fvf_stride = 32;

        Msg("[FVF] LoadGeometry '%s': FVF=0x%X stride=%u verts=%u",
            dbg_name.c_str(), vert_format, fvf_stride, vert_count);

        // Read raw FVF vertex data
        xr_vector<u8> raw_data(vert_count * fvf_stride);
        data->r(raw_data.data(), raw_data.size());

        // Convert FVF data to level-static stride-32 format
        u32 level_stride = 32;
        xr_vector<u8> converted(vert_count * level_stride);
        ConvertFVFToLevelFormat(raw_data.data(), converted.data(), vert_count, vert_format, fvf_stride);

        m_mesh.vStride = level_stride;
        m_mesh.vCount = vert_count;
        m_mesh.vBase = 0;

        // Create and upload converted vertex buffer
        u32 data_size = vert_count * level_stride;
        m_mesh.p_rm_Vertices = xr_new<VK::CVulkanBuffer>();
        m_mesh.p_rm_Vertices->Create(
            data_size,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE
        );
        m_mesh.p_rm_Vertices->Upload(converted.data(), data_size);
    }

    if (data->find_chunk(OGF_INDICES))
    {
        u32 idx_count = data->r_u32();
        m_mesh.iCount = idx_count;
        m_mesh.iBase = 0;
        m_mesh.dwPrimitives = idx_count / 3;
        m_mesh.iType = VK_INDEX_TYPE_UINT16;

        // Read index data
        u32 data_size = idx_count * sizeof(u16);

        // Create index buffer
        m_mesh.p_rm_Indices = xr_new<VK::CVulkanBuffer>();
        m_mesh.p_rm_Indices->Create(
            data_size,
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE
        );

        // Read and upload index data
        xr_vector<u16> idata(idx_count);
        data->r(idata.data(), data_size);
        m_mesh.p_rm_Indices->Upload(idata.data(), data_size);
    }
}

void vkFVisual::LoadFastPath(IReader* data)
{
    // Fast-path is optional simplified geometry for shadow maps
    if (!data->find_chunk(OGF_FASTPATH))
        return;

    // TODO: Load fast-path geometry
    // Similar to LoadGeometry but into m_mesh.m_fast

}

// ============================================================================
// vkFHierrarhyVisual implementation - Composite visual
// ============================================================================
vkFHierrarhyVisual::vkFHierrarhyVisual()
{
    Type = MT_HIERRARHY;
    bDontDelete = FALSE;
}

vkFHierrarhyVisual::~vkFHierrarhyVisual()
{
    Release();
}

void vkFHierrarhyVisual::Load(const char* name, IReader* data, u32 flags)
{
    vkRender_Visual::Load(name, data, flags);

    // Try link-based children first (references to pre-loaded visuals)
    if (data->find_chunk(OGF_CHILDREN_L))
    {
        u32 count = data->r_u32();
        children.resize(count);

        for (u32 i = 0; i < count; ++i)
        {
            u32 id = data->r_u32();
            children[i] = RImplementation.getVisual(id);

            if (!children[i]) {
                Msg("![Vulkan] Hierarchy '%s': Failed to load child %u (visual ID %u)",
                    dbg_name.c_str(), i, id);
                Msg("![Vulkan] Child will be skipped during rendering");
            }
        }

        bDontDelete = TRUE;

        return;
    }

    // Try stream-based children (inline child definitions)
    if (data->find_chunk(OGF_CHILDREN))
    {
        IReader* chunk = data->open_chunk(OGF_CHILDREN);
        if (chunk)
        {
            u32 count = 0;
            IReader* child_chunk;

            while ((child_chunk = chunk->open_chunk(count)) != nullptr)
            {
                // Create child name: "parent_name:N"
                string_path child_name;
                xr_sprintf(child_name, "%s:%u", name, count + 1);

                // Create child visual
                IRenderVisual* child = RImplementation.model_CreateChild(child_name, child_chunk);
                if (child)
                {
                    children.push_back(child);
                }

                child_chunk->close();
                ++count;
            }

            chunk->close();
            bDontDelete = FALSE;

        }
    }
}

void vkFHierrarhyVisual::Release()
{
    if (!bDontDelete)
    {
        // Delete owned children
        for (auto& child : children)
        {
            if (child)
            {
                RImplementation.model_Delete(child, TRUE);
            }
        }

        for (auto& child : children_invisible)
        {
            if (child)
            {
                RImplementation.model_Delete(child, TRUE);
            }
        }
    }

    children.clear();
    children_invisible.clear();

    vkRender_Visual::Release();
}

void vkFHierrarhyVisual::Copy(vkRender_Visual* from)
{
    vkRender_Visual::Copy(from);

    vkFHierrarhyVisual* src = dynamic_cast<vkFHierrarhyVisual*>(from);
    if (!src) return;

    // Deep copy children
    children.clear();
    children.reserve(src->children.size());

    for (u32 i = 0; i < src->children.size(); i++)
    {
        auto& src_child = src->children[i];
        IRenderVisual* child_copy = RImplementation.model_Duplicate(src_child);
        children.push_back(child_copy);
    }

    children_invisible.clear();
    children_invisible.reserve(src->children_invisible.size());

    for (auto& src_child : src->children_invisible)
    {
        IRenderVisual* child_copy = RImplementation.model_Duplicate(src_child);
        children_invisible.push_back(child_copy);
    }

    bDontDelete = FALSE;
}

void vkFHierrarhyVisual::Render(float LOD)
{
    // Render all visible children
    for (auto& child : children)
    {
        if (child)
        {
            vkRender_Visual* vk_child = static_cast<vkRender_Visual*>(child);
            vk_child->Render(LOD);
        }
    }
}

void vkFHierrarhyVisual::Spawn()
{
    // Spawn all children
    for (auto& child : children)
    {
        if (child)
        {
            static_cast<vkRender_Visual*>(child)->Spawn();
        }
    }

    for (auto& child : children_invisible)
    {
        if (child)
        {
            static_cast<vkRender_Visual*>(child)->Spawn();
        }
    }
}

void vkFHierrarhyVisual::Depart()
{
    // Depart all children
    for (auto& child : children)
    {
        if (child)
        {
            static_cast<vkRender_Visual*>(child)->Depart();
        }
    }

    for (auto& child : children_invisible)
    {
        if (child)
        {
            static_cast<vkRender_Visual*>(child)->Depart();
        }
    }
}

// ============================================================================
// vkFProgressive implementation - LOD mesh
// ============================================================================
vkFProgressive::vkFProgressive()
{
    Type = MT_PROGRESSIVE;
    sw_count = 0;
    sw_offsets = nullptr;
    sw_counts = nullptr;
    last_lod = 0;
}

vkFProgressive::~vkFProgressive()
{
    Release();
}

void vkFProgressive::Load(const char* name, IReader* data, u32 flags)
{
    vkFVisual::Load(name, data, flags);
    LoadSlidingWindow(data);
}

void vkFProgressive::Release()
{
    xr_free(sw_offsets);
    xr_free(sw_counts);
    sw_offsets = nullptr;
    sw_counts = nullptr;
    sw_count = 0;
    vkFVisual::Release();
}

void vkFProgressive::Copy(vkRender_Visual* from)
{
    vkFVisual::Copy(from);

    vkFProgressive* src = dynamic_cast<vkFProgressive*>(from);
    if (!src) return;

    sw_count = src->sw_count;
    if (sw_count > 0)
    {
        sw_offsets = xr_alloc<u32>(sw_count);
        sw_counts = xr_alloc<u32>(sw_count);
        CopyMemory(sw_offsets, src->sw_offsets, sw_count * sizeof(u32));
        CopyMemory(sw_counts, src->sw_counts, sw_count * sizeof(u32));
    }
    last_lod = 0;
}

void vkFProgressive::Render(float LOD)
{
    if (!m_mesh.IsValid() || sw_count == 0)
    {
        vkFVisual::Render(LOD);
        return;
    }

    u32 stride = m_mesh.vStride;

    // Validate stride
    if (stride != 32 && stride != 36 && stride != 40 && stride != 44)
        return;

    if (!m_mesh.p_rm_Vertices || !m_mesh.p_rm_Indices)
        return;

    // Per-progressive-visual diagnostic: log once per unique mesh
    {
        static xr_set<u64> s_loggedProg;
        static u32 s_progLogCount = 0;
        u64 key = (u64)(uintptr_t)m_mesh.p_rm_Vertices ^ ((u64)m_mesh.vBase << 32);
        if (s_progLogCount < 200 && s_loggedProg.find(key) == s_loggedProg.end()) {
            s_loggedProg.insert(key);
            s_progLogCount++;
            const Fmatrix& Wdbg = RCache.xforms.m_w;
            const char* matName = (m_pMaterial && m_pMaterial->m_Name.size() > 0)
                ? m_pMaterial->m_Name.c_str() : "<none>";
            const char* vName = (dbg_name.size() > 0) ? dbg_name.c_str() : "<anon>";
            Msg("[PROG-DIAG] #%u name='%s' tex='%s' stride=%u tcOff=%u vBase=%u vCount=%u iBase=%u iCount=%u sw=%u LOD=%.2f alphaRef=%.3f pos=(%.1f,%.1f,%.1f)",
                s_progLogCount, vName, matName,
                stride, m_mesh.tcOffset,
                m_mesh.vBase, m_mesh.vCount,
                m_mesh.iBase, m_mesh.iCount,
                sw_count, LOD, m_fAlphaRef,
                Wdbg._41, Wdbg._42, Wdbg._43);

            // UV Diagnostic: dump UV values for interesting visuals
            if (m_mesh.p_rm_Vertices && stride == 32) {
                string256 diagLabel;
                xr_sprintf(diagLabel, "PROG#%u[%s]", s_progLogCount, matName);
                VBShadow_DumpUVs(m_mesh.p_rm_Vertices, m_mesh.vBase, m_mesh.vCount, diagLabel);
            }
        }
    }

    // Push world matrix and per-visual alphaRef (same as vkFVisual::Render)
    {
        VkCommandBuffer cmd = RCache.GetCommandBuffer();
        if (cmd != VK_NULL_HANDLE) {
            VkPipelineLayout layout = VK::g_PipelineManager->GetLayout();
            if (layout != VK_NULL_HANDLE) {
                const Fmatrix& W = RCache.xforms.m_w;
                vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Fmatrix), &W);
                vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_FRAGMENT_BIT,
                    200, sizeof(float), &m_fAlphaRef);

                // Always push uvScale to guard against stale values
                float uvScale = (stride == 32) ? (1.0f / 1024.0f) : 1.0f;
                vkCmdPushConstants(cmd, layout,
                    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                    192, sizeof(float), &uvScale);
            }
        }
    }

    // Switch pipeline if stride changed, or if stride-32 and tcOffset changed
    // tcOffset only matters for stride-32 (two sub-layouts: lmap@24 vs vert-lit@28)
    u32 tcOff = m_mesh.tcOffset;
    bool needSwitch = (stride != RCache.m_CurrentGBufStride);
    if (!needSwitch && stride == 32)
        needSwitch = (tcOff != RCache.m_CurrentGBufTcOffset);

    if (needSwitch)
    {
        VkPipeline pipeline = RTarget->GetGBufferPipeline(stride, tcOff);
        if (pipeline != VK_NULL_HANDLE)
        {
            RCache.set_Pipeline(pipeline);

            // Update UV scale only — alphaRef is controlled by the caller
            float uvScale = (stride == 32) ? (1.0f / 1024.0f) : 1.0f;
            VkCommandBuffer cmd = RCache.GetCommandBuffer();
            if (cmd != VK_NULL_HANDLE)
            {
                VkPipelineLayout layout = VK::g_PipelineManager->GetLayout();
                vkCmdPushConstants(cmd, layout,
                    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                    192, sizeof(float), &uvScale);
            }

            RCache.m_CurrentGBufStride = stride;
            RCache.m_CurrentGBufTcOffset = tcOff;
        }
        else
        {
            return;
        }
    }

    // Calculate LOD index (invert: LOD=1 means full detail = index 0)
    u32 lod_idx = 0;
    if (LOD >= 0.f && LOD <= 1.f)
    {
        lod_idx = iFloor((1.f - LOD) * (sw_count - 1) + 0.5f);
        clamp(lod_idx, 0u, sw_count - 1);
    }

    // Bind material descriptor set (Set 1)
    {
        VkCommandBuffer cmd = RCache.GetCommandBuffer();
        if (cmd != VK_NULL_HANDLE)
        {
            if (m_pMaterial && m_pMaterial->IsValid())
                m_pMaterial->Bind(cmd);
            else if (g_MaterialManager && g_MaterialManager->GetDefaultMaterial())
                g_MaterialManager->GetDefaultMaterial()->Bind(cmd);
        }
    }

    // Bind buffers
    RCache.set_Vertices(m_mesh.p_rm_Vertices->GetHandle(), m_mesh.vStride);
    RCache.set_Indices(m_mesh.p_rm_Indices->GetHandle(), m_mesh.iType);

    // Draw with LOD-specific index range
    u32 start_idx = sw_offsets[lod_idx];
    u32 idx_count = sw_counts[lod_idx];
    u32 prim_count = idx_count / 3;

    RCache.Render(4, m_mesh.vBase, 0, m_mesh.vCount, m_mesh.iBase + start_idx, prim_count);
    RCache.stat.polys += prim_count;
    RCache.stat.verts += m_mesh.vCount;

    last_lod = lod_idx;
}

void vkFProgressive::LoadSlidingWindow(IReader* data)
{
    // Try to load sliding window data from OGF_SWIDATA chunk
    if (data->find_chunk(OGF_SWIDATA))
    {
        // Reserved
        data->r_u32();
        data->r_u32();
        data->r_u32();
        data->r_u32();

        sw_count = data->r_u32();
        if (sw_count > 0)
        {
            sw_offsets = xr_alloc<u32>(sw_count);
            sw_counts = xr_alloc<u32>(sw_count);

            for (u32 i = 0; i < sw_count; ++i)
            {
                sw_offsets[i] = data->r_u32();           // offset in index buffer
                sw_counts[i] = (u32)data->r_u16() * 3;  // num_tris * 3 = index count
                data->r_u16();                            // num_verts (skip)
            }
        }
    }
    else
    {
        // No sliding window - use full mesh
        sw_count = 1;
        sw_offsets = xr_alloc<u32>(1);
        sw_counts = xr_alloc<u32>(1);
        sw_offsets[0] = 0;
        sw_counts[0] = m_mesh.iCount;
    }
}

// ============================================================================
// vkFTreeVisual implementation - Tree visual
// ============================================================================
vkFTreeVisual::vkFTreeVisual()
{
    Type = MT_TREE_ST;
    xform.identity();
    ZeroMemory(&c_scale, sizeof(c_scale));
    ZeroMemory(&c_bias, sizeof(c_bias));
    c_scale.rgb.set(1.f, 1.f, 1.f);
    c_scale.hemi = 1.f;
    c_scale.sun = 1.f;
}

vkFTreeVisual::~vkFTreeVisual()
{
}

void vkFTreeVisual::Load(const char* name, IReader* data, u32 flags)
{
    vkFVisual::Load(name, data, flags);
    LoadTreeDef(data);

    // Trees always need alpha test for foliage cutout
    m_fAlphaRef = 200.0f / 255.0f;  // DX11 uses oAREF=200
}

void vkFTreeVisual::Release()
{
    vkFVisual::Release();
}

void vkFTreeVisual::Copy(vkRender_Visual* from)
{
    vkFVisual::Copy(from);

    vkFTreeVisual* src = dynamic_cast<vkFTreeVisual*>(from);
    if (!src) return;

    c_scale = src->c_scale;
    c_bias = src->c_bias;
    xform = src->xform;
}

static u32 s_treeLogCounter = 0;

void vkFTreeVisual::Render(float LOD)
{
    if (s_treeLogCounter < 20)
    {
        Msg("[TREE] Render #%u: vCount=%u iCount=%u stride=%u vBase=%u iBase=%u",
            s_treeLogCounter, m_mesh.vCount, m_mesh.iCount, m_mesh.vStride,
            m_mesh.vBase, m_mesh.iBase);
        Msg("[TREE]   xform row0=(%.3f,%.3f,%.3f,%.3f)", xform._11, xform._12, xform._13, xform._14);
        Msg("[TREE]   xform row1=(%.3f,%.3f,%.3f,%.3f)", xform._21, xform._22, xform._23, xform._24);
        Msg("[TREE]   xform row2=(%.3f,%.3f,%.3f,%.3f)", xform._31, xform._32, xform._33, xform._34);
        Msg("[TREE]   xform row3=(%.3f,%.3f,%.3f,%.3f) [translation]", xform._41, xform._42, xform._43, xform._44);
        Msg("[TREE]   mat=%p matValid=%d shader_id=%u VB=%p IB=%p",
            (void*)m_pMaterial,
            (m_pMaterial && m_pMaterial->IsValid()) ? 1 : 0,
            (u32)shader_id,
            (void*)m_mesh.p_rm_Vertices,
            (void*)m_mesh.p_rm_Indices);
        Msg("[TREE]   c_scale=(%.2f,%.2f,%.2f) hemi=%.2f sun=%.2f",
            c_scale.rgb.x, c_scale.rgb.y, c_scale.rgb.z, c_scale.hemi, c_scale.sun);
        s_treeLogCounter++;
    }

    // Set tree's xform as both current and previous world matrix.
    // Trees are static objects — their model matrix doesn't change between frames,
    // so u_PrevModel must equal u_Model for correct motion vectors (camera-only motion).
    // Without this, u_PrevModel stays at identity → DLSS sees huge false motion.
    Fmatrix prevW = RCache.xforms.m_w;
    Fmatrix prevWprev = RCache.xforms.m_w_prev;
    RCache.xforms.m_w = xform;
    RCache.xforms.m_w_prev = xform;

    // Render geometry (binds material, vertex/index buffers, draws)
    vkFVisual::Render(LOD);

    // Restore previous matrices for subsequent visuals
    RCache.xforms.m_w = prevW;
    RCache.xforms.m_w_prev = prevWprev;
}

void vkFTreeVisual::LoadTreeDef(IReader* data)
{
    if (data->find_chunk(OGF_TREEDEF2))
    {
        // Read transform matrix
        data->r(&xform, sizeof(xform));

        // Read color scale
        data->r(&c_scale, sizeof(c_scale));

        // Read color bias
        data->r(&c_bias, sizeof(c_bias));

        // Match DX11: halve c_scale and c_bias on load
        c_scale.rgb.mul(.5f);
        c_scale.hemi *= .5f;
        c_scale.sun *= .5f;
        c_bias.rgb.mul(.5f);
        c_bias.hemi *= .5f;
        c_bias.sun *= .5f;

        static u32 s_treeLoadLog = 0;
        if (s_treeLoadLog < 20)
        {
            Msg("[TREE-LOAD] xform pos=(%.2f,%.2f,%.2f) scale_diag=(%.3f,%.3f,%.3f) _44=%.3f",
                xform._41, xform._42, xform._43,
                xform._11, xform._22, xform._33, xform._44);
            Msg("[TREE-LOAD] c_scale=(%.3f,%.3f,%.3f) hemi=%.3f sun=%.3f",
                c_scale.rgb.x, c_scale.rgb.y, c_scale.rgb.z, c_scale.hemi, c_scale.sun);
            s_treeLoadLog++;
        }
    }
    else
    {
        Msg("! [TREE-LOAD] OGF_TREEDEF2 chunk NOT FOUND - tree has no transform!");
    }
}

// ============================================================================
// vkFTreeVisual_ST implementation - Static tree
// ============================================================================
vkFTreeVisual_ST::vkFTreeVisual_ST()
{
    Type = MT_TREE_ST;
}

vkFTreeVisual_ST::~vkFTreeVisual_ST()
{
}

void vkFTreeVisual_ST::Load(const char* name, IReader* data, u32 flags)
{
    vkFTreeVisual::Load(name, data, flags);
}

void vkFTreeVisual_ST::Render(float LOD)
{
    vkFTreeVisual::Render(LOD);
}

// ============================================================================
// vkFTreeVisual_PM implementation - Progressive tree
// ============================================================================
vkFTreeVisual_PM::vkFTreeVisual_PM()
{
    Type = MT_TREE_PM;
    sw_count = 0;
    sw_offsets = nullptr;
    sw_counts = nullptr;
    last_lod = 0;
}

vkFTreeVisual_PM::~vkFTreeVisual_PM()
{
    Release();
}

void vkFTreeVisual_PM::Load(const char* name, IReader* data, u32 flags)
{
    vkFTreeVisual::Load(name, data, flags);

    // Load sliding window (same as FProgressive)
    if (data->find_chunk(OGF_SWIDATA))
    {
        data->r_u32(); data->r_u32(); data->r_u32(); data->r_u32(); // reserved
        sw_count = data->r_u32();
        if (sw_count > 0)
        {
            sw_offsets = xr_alloc<u32>(sw_count);
            sw_counts = xr_alloc<u32>(sw_count);
            for (u32 i = 0; i < sw_count; ++i)
            {
                sw_offsets[i] = data->r_u32();           // offset in index buffer
                sw_counts[i] = (u32)data->r_u16() * 3;  // num_tris * 3 = index count
                data->r_u16();                            // num_verts (skip)
            }
        }
    }
}

void vkFTreeVisual_PM::Release()
{
    xr_free(sw_offsets);
    xr_free(sw_counts);
    sw_offsets = nullptr;
    sw_counts = nullptr;
    sw_count = 0;
    vkFTreeVisual::Release();
}

void vkFTreeVisual_PM::Copy(vkRender_Visual* from)
{
    vkFTreeVisual::Copy(from);

    vkFTreeVisual_PM* src = dynamic_cast<vkFTreeVisual_PM*>(from);
    if (!src) return;

    sw_count = src->sw_count;
    if (sw_count > 0)
    {
        sw_offsets = xr_alloc<u32>(sw_count);
        sw_counts = xr_alloc<u32>(sw_count);
        CopyMemory(sw_offsets, src->sw_offsets, sw_count * sizeof(u32));
        CopyMemory(sw_counts, src->sw_counts, sw_count * sizeof(u32));
    }
}

void vkFTreeVisual_PM::Render(float LOD)
{
    if (!m_mesh.IsValid() || sw_count == 0)
    {
        vkFTreeVisual::Render(LOD);
        return;
    }

    if (!m_mesh.p_rm_Vertices || !m_mesh.p_rm_Indices)
        return;

    u32 stride = m_mesh.vStride;

    // Set tree's xform as world matrix so push constants get correct transform
    Fmatrix prevW = RCache.xforms.m_w;
    RCache.xforms.m_w = xform;

    // Push world matrix to GPU
    VkCommandBuffer cmd = RCache.GetCommandBuffer();
    if (cmd != VK_NULL_HANDLE)
    {
        VkPipelineLayout layout = VK::g_PipelineManager->GetLayout();
        if (layout != VK_NULL_HANDLE)
            vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Fmatrix), &xform);
    }

    // Switch pipeline if stride changed, or if stride-32 and tcOffset changed
    u32 tcOff = m_mesh.tcOffset;
    bool needSwitch = (stride != RCache.m_CurrentGBufStride);
    if (!needSwitch && stride == 32)
        needSwitch = (tcOff != RCache.m_CurrentGBufTcOffset);

    if (needSwitch)
    {
        VkPipeline pipeline = RTarget->GetGBufferPipeline(stride, tcOff);
        if (pipeline != VK_NULL_HANDLE)
        {
            RCache.set_Pipeline(pipeline);

            float uvScale = (stride == 32) ? (1.0f / 1024.0f) : 1.0f;
            if (cmd != VK_NULL_HANDLE)
            {
                VkPipelineLayout layout = VK::g_PipelineManager->GetLayout();
                vkCmdPushConstants(cmd, layout,
                    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                    192, sizeof(float), &uvScale);
            }

            RCache.m_CurrentGBufStride = stride;
            RCache.m_CurrentGBufTcOffset = tcOff;
        }
        else
        {
            RCache.xforms.m_w = prevW;
            return;
        }
    }

    // LOD selection (invert: LOD=1 means full detail = index 0)
    u32 lod_idx = 0;
    if (LOD >= 0.f && LOD <= 1.f)
    {
        lod_idx = iFloor((1.f - LOD) * (sw_count - 1) + 0.5f);
        clamp(lod_idx, 0u, sw_count - 1);
    }

    // Enable alpha test for tree foliage
    if (cmd != VK_NULL_HANDLE)
    {
        VkPipelineLayout layout = VK::g_PipelineManager->GetLayout();
        float alphaRefTree = 0.5f;
        vkCmdPushConstants(cmd, layout,
            VK_SHADER_STAGE_FRAGMENT_BIT,
            200, sizeof(float), &alphaRefTree);
    }

    // Bind material descriptor set (Set 1)
    if (cmd != VK_NULL_HANDLE)
    {
        if (m_pMaterial && m_pMaterial->IsValid())
            m_pMaterial->Bind(cmd);
        else if (g_MaterialManager && g_MaterialManager->GetDefaultMaterial())
            g_MaterialManager->GetDefaultMaterial()->Bind(cmd);
    }

    // Bind buffers and draw
    RCache.set_Vertices(m_mesh.p_rm_Vertices->GetHandle(), m_mesh.vStride);
    RCache.set_Indices(m_mesh.p_rm_Indices->GetHandle(), m_mesh.iType);

    u32 start_idx = sw_offsets[lod_idx];
    u32 idx_count = sw_counts[lod_idx];
    u32 prim_count = idx_count / 3;

    RCache.Render(4, m_mesh.vBase, 0, m_mesh.vCount, m_mesh.iBase + start_idx, prim_count);
    RCache.stat.polys += prim_count;
    RCache.stat.verts += m_mesh.vCount;

    // Restore alpha test to disabled for subsequent solid geometry
    if (cmd != VK_NULL_HANDLE)
    {
        VkPipelineLayout layout = VK::g_PipelineManager->GetLayout();
        float alphaRefOff = -1.0f;
        vkCmdPushConstants(cmd, layout,
            VK_SHADER_STAGE_FRAGMENT_BIT,
            200, sizeof(float), &alphaRefOff);
    }

    // Restore previous world matrix
    RCache.xforms.m_w = prevW;

    last_lod = lod_idx;
}

// ============================================================================
// vkFLOD implementation - LOD billboard
// ============================================================================
vkFLOD::vkFLOD()
{
    Type = MT_LOD;
    lod_factor = 1.f;
    ZeroMemory(facets, sizeof(facets));
}

vkFLOD::~vkFLOD()
{
    Release();
}

void vkFLOD::Load(const char* name, IReader* data, u32 flags)
{
    vkFHierrarhyVisual::Load(name, data, flags);
    LoadLODDef(data);
    BuildLODGeometry();
}

void vkFLOD::Release()
{
    lod_mesh.Destroy();
    vkFHierrarhyVisual::Release();
}

void vkFLOD::Copy(vkRender_Visual* from)
{
    vkFHierrarhyVisual::Copy(from);

    vkFLOD* src = dynamic_cast<vkFLOD*>(from);
    if (!src) return;

    CopyMemory(facets, src->facets, sizeof(facets));
    lod_factor = src->lod_factor;
    // Note: lod_mesh is shared, not deep-copied
}

void vkFLOD::Render(float LOD)
{
    // TODO: Select facet based on camera angle
    // For now, just render children (high-quality mesh)
    vkFHierrarhyVisual::Render(LOD);
}

void vkFLOD::LoadLODDef(IReader* data)
{
    if (data->find_chunk(OGF_LODDEF2))
    {
        for (int i = 0; i < 8; ++i)
        {
            data->r(&facets[i], sizeof(_face));
        }
    }
}

void vkFLOD::BuildLODGeometry()
{
    // TODO: Build VkBuffer from facets for LOD rendering
    // This creates billboard quads for each of the 8 viewing angles
}

// ============================================================================
// vkSkeletonX_ST implementation - Skinned static mesh
// ============================================================================
vkSkeletonX_ST::vkSkeletonX_ST()
{
    Type = MT_SKELETON_GEOMDEF_ST;
    RenderMode = 0;
    BonesUsed = 0;
}

vkSkeletonX_ST::~vkSkeletonX_ST()
{
}

void vkSkeletonX_ST::Load(const char* name, IReader* data, u32 flags)
{
    // ========================================================================
    // Step 1: Read OGF_VERTICES first for bone analysis (like DX11's CSkeletonX::_Load)
    // ========================================================================
    R_ASSERT(data->find_chunk(OGF_VERTICES));
    u32 dwVertType = data->r_u32();
    u32 dwVertCount = data->r_u32();
    void* _verts_ = data->pointer();  // Save pointer to raw skinned vertex data

    // Analyze bones to determine RenderMode
    xr_vector<u16> bids;
    u16 sw_bones_cnt = 0;

    switch (dwVertType)
    {
    case OGF_VERTEXFORMAT_FVF_1L:
    case 1:
        {
            struct vertBoned1W { Fvector P; Fvector3 N; Fvector3 T; Fvector3 B; Fvector2 tc; u32 matrix; };
            vertBoned1W* pVO = (vertBoned1W*)_verts_;
            for (u32 it = 0; it < dwVertCount; ++it)
            {
                u16 mid = (u16)pVO[it].matrix;
                if (bids.end() == std::find(bids.begin(), bids.end(), mid))
                    bids.push_back(mid);
                sw_bones_cnt = _max(sw_bones_cnt, mid);
            }
            if (1 == bids.size()) { RenderMode = RM_SINGLE; RMS_boneid = *bids.begin(); }
            else { RenderMode = RM_SKINNING_1B; RMS_bonecount = sw_bones_cnt + 1; BonesUsed = (u16)bids.size(); }
        }
        break;
    case OGF_VERTEXFORMAT_FVF_2L:
    case 2:
        {
            #pragma pack(push, 2)
            struct vertBoned2W { u16 matrix0; u16 matrix1; Fvector P; Fvector N; Fvector T; Fvector B; float w; float u, v; };
            #pragma pack(pop)
            vertBoned2W* pVO = (vertBoned2W*)_verts_;
            for (u32 it = 0; it < dwVertCount; ++it)
            {
                sw_bones_cnt = _max(sw_bones_cnt, pVO[it].matrix0);
                sw_bones_cnt = _max(sw_bones_cnt, pVO[it].matrix1);
                if (bids.end() == std::find(bids.begin(), bids.end(), pVO[it].matrix0)) bids.push_back(pVO[it].matrix0);
                if (bids.end() == std::find(bids.begin(), bids.end(), pVO[it].matrix1)) bids.push_back(pVO[it].matrix1);
            }
            RenderMode = RM_SKINNING_2B; RMS_bonecount = sw_bones_cnt + 1; BonesUsed = (u16)bids.size();
        }
        break;
    case OGF_VERTEXFORMAT_FVF_3L:
    case 3:
        {
            #pragma pack(push, 2)
            struct vertBoned3W { u16 m[3]; Fvector P; Fvector N; Fvector T; Fvector B; float w[2]; float u, v; };
            #pragma pack(pop)
            vertBoned3W* pVO = (vertBoned3W*)_verts_;
            for (u32 it = 0; it < dwVertCount; ++it)
                for (int k = 0; k < 3; k++) {
                    sw_bones_cnt = _max(sw_bones_cnt, pVO[it].m[k]);
                    if (bids.end() == std::find(bids.begin(), bids.end(), pVO[it].m[k])) bids.push_back(pVO[it].m[k]);
                }
            RenderMode = RM_SKINNING_3B; RMS_bonecount = sw_bones_cnt + 1; BonesUsed = (u16)bids.size();
        }
        break;
    case OGF_VERTEXFORMAT_FVF_4L:
    case 4:
        {
            #pragma pack(push, 2)
            struct vertBoned4W { u16 m[4]; Fvector P; Fvector N; Fvector T; Fvector B; float w[3]; float u, v; };
            #pragma pack(pop)
            vertBoned4W* pVO = (vertBoned4W*)_verts_;
            for (u32 it = 0; it < dwVertCount; ++it)
                for (int k = 0; k < 4; k++) {
                    sw_bones_cnt = _max(sw_bones_cnt, pVO[it].m[k]);
                    if (bids.end() == std::find(bids.begin(), bids.end(), pVO[it].m[k])) bids.push_back(pVO[it].m[k]);
                }
            RenderMode = RM_SKINNING_4B; RMS_bonecount = sw_bones_cnt + 1; BonesUsed = (u16)bids.size();
        }
        break;
    default:
        Msg("![Vulkan] vkSkeletonX_ST::Load() - unknown vertex type %d", dwVertType);
        RenderMode = RM_SINGLE; RMS_boneid = 0;
        break;
    }

    // ========================================================================
    // Step 2: Load base visual WITHOUT vertices (indices only)
    // This matches DX11: inherited1::Load(N, data, dwFlags | VLOAD_NOVERTICES)
    // ========================================================================
    data->seek(0);
    vkFVisual::Load(name, data, flags | VLOAD_NOVERTICES);

    // ========================================================================
    // Step 3: Convert vertBoned* -> vertHW_* and create Vulkan VB
    // This matches DX11's _Load_hw()
    // ========================================================================
    m_mesh.vBase = 0;
    m_mesh.vCount = dwVertCount;
    _Load_hw_VK(_verts_, dwVertType, dwVertCount);

    Msg("[SKL-ST] Load '%s': mode=%u vCount=%u stride=%u bones=%u",
        name, (u32)RenderMode, dwVertCount, m_mesh.vStride, (u32)BonesUsed);
}

void vkSkeletonX_ST::Release()
{
    vkFVisual::Release();
}

void vkSkeletonX_ST::Copy(vkRender_Visual* from)
{
    vkFVisual::Copy(from);

    vkSkeletonX_ST* src = dynamic_cast<vkSkeletonX_ST*>(from);
    if (!src) return;

    RenderMode = src->RenderMode;
    BonesUsed = src->BonesUsed;

    // Copy union data (bone ID or bone count depending on mode)
    if (RenderMode == RM_SINGLE)
        RMS_boneid = src->RMS_boneid;
    else
        RMS_bonecount = src->RMS_bonecount;
}

void vkSkeletonX_ST::Render(float LOD)
{
    // Frame-limited diagnostics (first 5 frames)
    static u32 s_stDiagFrame = 0;
    static u32 s_stDiagCount = 0;
    bool bDiag = (Device.dwFrame != s_stDiagFrame) && (s_stDiagCount < 5);
    if (bDiag) {
        s_stDiagFrame = Device.dwFrame;
        s_stDiagCount++;
        Msg("[SKL-ST] Render: this=%p Parent=%p RenderMode=%u RMS_boneid=%u Type=%u frame=%u",
            this, Parent, (u32)RenderMode, RMS_boneid, Type, Device.dwFrame);
        Msg("[SKL-ST]   m_mesh.valid=%d m_pMaterial=%p",
            m_mesh.IsValid() ? 1 : 0, m_pMaterial);
        if (m_mesh.IsValid())
            Msg("[SKL-ST]   VB=%p IB=%p vBase=%u vCount=%u iBase=%u iCount=%u vStride=%u",
                m_mesh.p_rm_Vertices, m_mesh.p_rm_Indices,
                m_mesh.vBase, m_mesh.vCount, m_mesh.iBase, m_mesh.iCount, m_mesh.vStride);
    }

    // Diagnostic: catch RM_SINGLE objects near camera (first 10)
    if (Parent && RenderMode == RM_SINGLE) {
        static u32 s_rm1NearCount = 0;
        if (s_rm1NearCount < 10) {
            // Check distance from camera to object
            Fvector objPos = { RCache.xforms.m_w._41, RCache.xforms.m_w._42, RCache.xforms.m_w._43 };
            Fvector camPos = { Device.vCameraPosition.x, Device.vCameraPosition.y, Device.vCameraPosition.z };
            float dist = objPos.distance_to(camPos);
            if (dist < 30.f) {
                s_rm1NearCount++;
                const char* pname = (Parent->dbg_name.size() > 0) ? Parent->dbg_name.c_str() : "?";
                Msg("[RM1-NEAR] #%u '%s' dist=%.1f stride=%u vCount=%u",
                    s_rm1NearCount, pname, dist, m_mesh.vStride, m_mesh.vCount);
                u16 bc = Parent->LL_BoneCount();
                if (bc > 0 && RMS_boneid < bc) {
                    Fmatrix boneMtx = Parent->LL_GetTransform_R(RMS_boneid);
                    Fmatrix W; W.mul_43(RCache.xforms.m_w, boneMtx);
                    Msg("[RM1-NEAR]   boneMtx diag=(%.4f,%.4f,%.4f) pos=(%.4f,%.4f,%.4f)",
                        boneMtx._11, boneMtx._22, boneMtx._33, boneMtx._41, boneMtx._42, boneMtx._43);
                    Msg("[RM1-NEAR]   W pos=(%.2f,%.2f,%.2f) scale=(%.4f,%.4f,%.4f)",
                        W._41, W._42, W._43,
                        sqrtf(W._11*W._11+W._12*W._12+W._13*W._13),
                        sqrtf(W._21*W._21+W._22*W._22+W._23*W._23),
                        sqrtf(W._31*W._31+W._32*W._32+W._33*W._33));
                    Msg("[RM1-NEAR]   pipeline=%p curGBufStride=%u",
                        RCache.m_CurrentPipeline, RCache.m_CurrentGBufStride);
                }
            }
        }
    }
    // One-shot diagnostic: catch bedspread in ANY render mode
    if (Parent) {
        static bool s_bedSklAny = false;
        if (!s_bedSklAny && Parent->dbg_name.size() > 0 &&
            strstr(Parent->dbg_name.c_str(), "bedspread"))
        {
            s_bedSklAny = true;
            Msg("[BED-ANY] vkSkeletonX_ST::Render bedspread child!");
            Msg("[BED-ANY]   this=%p Parent=%p RenderMode=%u boneid=%u bonecount=%u Type=%u",
                this, Parent, (u32)RenderMode, RMS_boneid, RMS_bonecount, Type);
            Msg("[BED-ANY]   vCount=%u iCount=%u vBase=%u iBase=%u stride=%u VB=%p IB=%p",
                m_mesh.vCount, m_mesh.iCount, m_mesh.vBase, m_mesh.iBase,
                m_mesh.vStride, m_mesh.p_rm_Vertices, m_mesh.p_rm_Indices);
            Msg("[BED-ANY]   Wold row0=(%.4f,%.4f,%.4f,%.4f) pos=(%.2f,%.2f,%.2f)",
                RCache.xforms.m_w._11, RCache.xforms.m_w._12, RCache.xforms.m_w._13, RCache.xforms.m_w._14,
                RCache.xforms.m_w._41, RCache.xforms.m_w._42, RCache.xforms.m_w._43);
            // Dump bone transform for RM_SINGLE
            if (RenderMode == RM_SINGLE) {
                u16 bc = Parent->LL_BoneCount();
                Msg("[BED-ANY]   boneCount=%u boneid=%u", bc, RMS_boneid);
                if (bc > 0 && RMS_boneid < bc) {
                    Fmatrix boneMtx = Parent->LL_GetTransform_R(RMS_boneid);
                    Msg("[BED-ANY]   mRenderTransform (bone %u):", RMS_boneid);
                    Msg("[BED-ANY]     r0=(%.6f,%.6f,%.6f,%.6f)", boneMtx._11, boneMtx._12, boneMtx._13, boneMtx._14);
                    Msg("[BED-ANY]     r1=(%.6f,%.6f,%.6f,%.6f)", boneMtx._21, boneMtx._22, boneMtx._23, boneMtx._24);
                    Msg("[BED-ANY]     r2=(%.6f,%.6f,%.6f,%.6f)", boneMtx._31, boneMtx._32, boneMtx._33, boneMtx._34);
                    Msg("[BED-ANY]     r3=(%.6f,%.6f,%.6f,%.6f)", boneMtx._41, boneMtx._42, boneMtx._43, boneMtx._44);
                    // Also log mTransform (before m2b_transform)
                    Fmatrix mT = Parent->LL_GetBoneInstance(RMS_boneid).mTransform;
                    Msg("[BED-ANY]   mTransform (bone %u):", RMS_boneid);
                    Msg("[BED-ANY]     r0=(%.6f,%.6f,%.6f,%.6f)", mT._11, mT._12, mT._13, mT._14);
                    Msg("[BED-ANY]     r1=(%.6f,%.6f,%.6f,%.6f)", mT._21, mT._22, mT._23, mT._24);
                    Msg("[BED-ANY]     r2=(%.6f,%.6f,%.6f,%.6f)", mT._31, mT._32, mT._33, mT._34);
                    Msg("[BED-ANY]     r3=(%.6f,%.6f,%.6f,%.6f)", mT._41, mT._42, mT._43, mT._44);
                    // Combined W
                    Fmatrix W; W.mul_43(RCache.xforms.m_w, boneMtx);
                    Msg("[BED-ANY]   W_combined (Wold * boneMtx):");
                    Msg("[BED-ANY]     r0=(%.6f,%.6f,%.6f,%.6f)", W._11, W._12, W._13, W._14);
                    Msg("[BED-ANY]     r1=(%.6f,%.6f,%.6f,%.6f)", W._21, W._22, W._23, W._24);
                    Msg("[BED-ANY]     r2=(%.6f,%.6f,%.6f,%.6f)", W._31, W._32, W._33, W._34);
                    Msg("[BED-ANY]     r3=(%.6f,%.6f,%.6f,%.6f)", W._41, W._42, W._43, W._44);
                    // View and Projection from Device
                    Msg("[BED-ANY]   View pos=(%.2f,%.2f,%.2f)", Device.mView._41, Device.mView._42, Device.mView._43);
                    Msg("[BED-ANY]   Proj _11=%.4f _22=%.4f", Device.mProject._11, Device.mProject._22);
                    // Pipeline state
                    Msg("[BED-ANY]   currentPipeline=%p currentGBufStride=%u",
                        RCache.m_CurrentPipeline, RCache.m_CurrentGBufStride);
                }
            }
        }
    }

    // Safety check
    if (!Parent) {
        Msg("! [SKL-ST] Parent is NULL! this=%p, skipping render.", this);
        return;
    }

    // Validate Parent object - check vtable pointer is not null/corrupt
    {
        void* vtbl = *(void**)Parent;
        if (!vtbl) {
            Msg("! [SKL-ST] Parent=%p has NULL vtable! Object freed/corrupt. Skipping render.", Parent);
            return;
        }

        // Check bone_instances by reading raw memory at known offset
        // bone_instances is a protected member of CKinematics
        // Try safe access through LL_GetBoneInstance
        u16 boneCount = 0;
        __try {
            boneCount = Parent->LL_BoneCount();
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            Msg("! [SKL-ST] CRASH reading Parent->LL_BoneCount()! Parent=%p vtable=%p", Parent, vtbl);
            return;
        }

        if (bDiag) {
            Msg("[SKL-ST]   Parent=%p vtable=%p boneCount=%u", Parent, vtbl, boneCount);
        }

        // Verify bone_instances is accessible by probing bone 0
        if (boneCount > 0) {
            __try {
                Fmatrix& testM = Parent->LL_GetTransform_R(0);
                // Just read first float to verify memory is accessible
                volatile float testVal = testM._11;
                (void)testVal;
            } __except(EXCEPTION_EXECUTE_HANDLER) {
                Msg("! [SKL-ST] CRASH probing Parent->LL_GetTransform_R(0)! bone_instances is likely NULL. Parent=%p boneCount=%u", Parent, boneCount);
                Msg("! [SKL-ST]   Calling CalculateBones(TRUE) to try to fix...");
                __try {
                    Parent->CalculateBones(TRUE);
                    // Try again
                    Fmatrix& testM2 = Parent->LL_GetTransform_R(0);
                    volatile float testVal2 = testM2._11;
                    (void)testVal2;
                    Msg("[SKL-ST]   CalculateBones fixed the issue! Continuing render.");
                } __except(EXCEPTION_EXECUTE_HANDLER) {
                    Msg("! [SKL-ST]   Still crashes after CalculateBones! Skipping render.");
                    return;
                }
            }
        }
    }

    // Get current world matrix
    Fmatrix Wold;
    __try { Wold = RCache.xforms.m_w; } __except(EXCEPTION_EXECUTE_HANDLER) {
        Msg("! [SKL-ST] CRASH reading xforms"); return;
    }

    Fmatrix W;

    if (RenderMode == RM_SINGLE)
    {
        // ====================================================================
        // Single bone: simple rigid attachment (no GPU skinning needed)
        // ====================================================================
        __try {
            Fmatrix boneMtx = Parent->LL_GetTransform_R(RMS_boneid);
            if (bDiag) {
                Msg("[SKL-ST]   BoneTransform(%u):", RMS_boneid);
                Msg("[SKL-ST]     row0=(%.4f, %.4f, %.4f, %.4f)", boneMtx._11, boneMtx._12, boneMtx._13, boneMtx._14);
                Msg("[SKL-ST]     row1=(%.4f, %.4f, %.4f, %.4f)", boneMtx._21, boneMtx._22, boneMtx._23, boneMtx._24);
                Msg("[SKL-ST]     row2=(%.4f, %.4f, %.4f, %.4f)", boneMtx._31, boneMtx._32, boneMtx._33, boneMtx._34);
                Msg("[SKL-ST]     row3=(%.4f, %.4f, %.4f, %.4f)", boneMtx._41, boneMtx._42, boneMtx._43, boneMtx._44);
                Msg("[SKL-ST]   Wold:");
                Msg("[SKL-ST]     row0=(%.4f, %.4f, %.4f, %.4f)", Wold._11, Wold._12, Wold._13, Wold._14);
                Msg("[SKL-ST]     row1=(%.4f, %.4f, %.4f, %.4f)", Wold._21, Wold._22, Wold._23, Wold._24);
                Msg("[SKL-ST]     row2=(%.4f, %.4f, %.4f, %.4f)", Wold._31, Wold._32, Wold._33, Wold._34);
                Msg("[SKL-ST]     row3=(%.4f, %.4f, %.4f, %.4f)", Wold._41, Wold._42, Wold._43, Wold._44);
            }
            W.mul_43(Wold, boneMtx);
            if (bDiag) {
                Msg("[SKL-ST]   W_combined:");
                Msg("[SKL-ST]     row0=(%.4f, %.4f, %.4f, %.4f)", W._11, W._12, W._13, W._14);
                Msg("[SKL-ST]     row1=(%.4f, %.4f, %.4f, %.4f)", W._21, W._22, W._23, W._24);
                Msg("[SKL-ST]     row2=(%.4f, %.4f, %.4f, %.4f)", W._31, W._32, W._33, W._34);
                Msg("[SKL-ST]     row3=(%.4f, %.4f, %.4f, %.4f)", W._41, W._42, W._43, W._44);
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            Msg("! [SKL-ST] CRASH in single bone transform boneid=%u", RMS_boneid);
            return;
        }

        __try {
            RCache.set_xform_world(W);
            VkCommandBuffer cmd = RCache.GetCommandBuffer();
            VkPipelineLayout layout = VK::g_PipelineManager->GetLayout();
            if (cmd != VK_NULL_HANDLE && layout != VK_NULL_HANDLE)
                vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Fmatrix), &W);
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            Msg("! [SKL-ST] CRASH in push constants (single)"); return;
        }

        __try {
            // One-shot per-name diagnostic: log first RM_SINGLE render of each unique skeleton type
            if (Parent && Parent->dbg_name.size() > 0) {
                static std::set<std::string> s_loggedRM1;
                std::string parentName(Parent->dbg_name.c_str());
                if (s_loggedRM1.find(parentName) == s_loggedRM1.end()) {
                    s_loggedRM1.insert(parentName);
                    Msg("[RM1-DIAG] '%s': stride=%u vCount=%u iCount=%u pipeline=%p material=%p(%s) curGBufStride=%u",
                        Parent->dbg_name.c_str(), m_mesh.vStride, m_mesh.vCount, m_mesh.iCount,
                        RCache.m_CurrentPipeline, m_pMaterial,
                        m_pMaterial ? (m_pMaterial->IsValid() ? "valid" : "INVALID") : "NULL",
                        RCache.m_CurrentGBufStride);
                }
            }
            vkFVisual::Render(LOD);
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            Msg("! [SKL-ST] CRASH in vkFVisual::Render (single)"); return;
        }
    }
    else if (RenderMode >= RM_SKINNING_1B && RenderMode <= RM_SKINNING_4B)
    {
        // ====================================================================
        // Multi-bone GPU skinning
        // ====================================================================
        __try {
            VkCommandBuffer cmd = RCache.GetCommandBuffer();
            VkPipelineLayout layout = VK::g_PipelineManager->GetLayout();
            if (cmd == VK_NULL_HANDLE || layout == VK_NULL_HANDLE) return;

            // 1. Upload bone matrices to SSBO (sub-allocated per skeleton)
            u32 boneCount = RMS_bonecount;
            if (boneCount > CBackend::MAX_BONES)
                boneCount = CBackend::MAX_BONES;

            // Align offset to 4 bones (256 bytes) for minStorageBufferOffsetAlignment
            u32 boneOffset = RCache.m_BoneWriteOffset;
            u32 alignedBoneCount = (boneCount + 3u) & ~3u; // round up to multiple of 4
            bool boneWriteOK = false;

            // Need 2x bones: current [0..N) + previous [N..2N) for DLSS motion vectors
            u32 totalAligned = alignedBoneCount * 2;

            // Save previous bone transforms for motion vectors (matching SkeletonX.cpp logic)
            // CRITICAL: Must happen BEFORE reading mRenderTransform_prev below!
            // Track which skeletons have been saved this frame
            static std::unordered_map<IKinematics*, u32> s_savedFrames;
            auto it = s_savedFrames.find(Parent);
            if (it == s_savedFrames.end() || it->second != Device.dwFrame)
            {
                s_savedFrames[Parent] = Device.dwFrame;

                // NOTE: Can't access Matrix_Prev/Matrix_Temp (protected), but we CAN access bone instances
                // Save bone matrices for next frame's motion vectors
                for (u16 b = 0; b < Parent->LL_BoneCount(); b++)
                {
                    CBoneInstance& Bone = Parent->LL_GetBoneInstance(b);
                    Bone.mRenderTransform_prev.set(Bone.mRenderTransform_temp);
                    Bone.mRenderTransform_temp.set(Bone.mRenderTransform);
                }
            }

            if (RCache.IsBoneBufferValid() && RCache.m_BoneMapped &&
                (boneOffset + totalAligned) <= CBackend::MAX_TOTAL_BONES)
            {
                for (u32 mid = 0; mid < boneCount; ++mid)
                {
                    __try {
                        RCache.m_BoneMapped[boneOffset + mid] = Parent->LL_GetTransform_R(mid);
                        RCache.m_BoneMapped[boneOffset + alignedBoneCount + mid] =
                            Parent->LL_GetBoneInstance(u16(mid)).mRenderTransform_prev;
                    } __except(EXCEPTION_EXECUTE_HANDLER) {
                        RCache.m_BoneMapped[boneOffset + mid].identity();
                        RCache.m_BoneMapped[boneOffset + alignedBoneCount + mid].identity();
                    }
                }

                // Advance write offset for next skeleton (current + previous)
                RCache.m_BoneWriteOffset = boneOffset + totalAligned;
                boneWriteOK = true;

                // ============================================================
                // SKINNING DIAGNOSTICS: log first N unique skeletons
                // ============================================================
                {
                    static std::set<std::string> s_loggedSkins;
                    static u32 s_skinDiagTotal = 0;
                    const char* pname = (Parent && Parent->dbg_name.size() > 0)
                        ? Parent->dbg_name.c_str() : "<?>";
                    std::string key(pname);
                    if (s_loggedSkins.find(key) == s_loggedSkins.end() && s_skinDiagTotal < 30)
                    {
                        s_loggedSkins.insert(key);
                        s_skinDiagTotal++;
                        Msg("[SKIN-DIAG] #%u '%s' mode=%u bones=%u stride=%u vCount=%u ssboOff=%u",
                            s_skinDiagTotal, pname, (u32)RenderMode,
                            boneCount, m_mesh.vStride, m_mesh.vCount, boneOffset);
                        // Dump first 8 bone matrices (diagonal + translation)
                        u32 dumpCount = (boneCount < 8) ? boneCount : 8;
                        for (u32 db = 0; db < dumpCount; ++db) {
                            const Fmatrix& bm = RCache.m_BoneMapped[boneOffset + db];
                            float scaleX = sqrtf(bm._11*bm._11 + bm._12*bm._12 + bm._13*bm._13);
                            float scaleY = sqrtf(bm._21*bm._21 + bm._22*bm._22 + bm._23*bm._23);
                            float scaleZ = sqrtf(bm._31*bm._31 + bm._32*bm._32 + bm._33*bm._33);
                            Msg("[SKIN-DIAG]   bone[%u]: scale=(%.3f,%.3f,%.3f) pos=(%.3f,%.3f,%.3f)",
                                db, scaleX, scaleY, scaleZ, bm._41, bm._42, bm._43);
                            // Flag suspicious bones (near-zero scale = collapsed)
                            if (scaleX < 0.01f || scaleY < 0.01f || scaleZ < 0.01f)
                                Msg("[SKIN-DIAG]   *** WARNING: bone[%u] has near-zero scale!", db);
                        }
                        // Check for identity mRenderTransform (means no animation applied)
                        const Fmatrix& bone0 = RCache.m_BoneMapped[boneOffset];
                        if (fabsf(bone0._11 - 1.f) < 0.001f && fabsf(bone0._22 - 1.f) < 0.001f &&
                            fabsf(bone0._33 - 1.f) < 0.001f && fabsf(bone0._41) < 0.001f &&
                            fabsf(bone0._42) < 0.001f && fabsf(bone0._43) < 0.001f)
                            Msg("[SKIN-DIAG]   *** bone[0] is IDENTITY - no animation?");
                        // Log world matrix
                        Msg("[SKIN-DIAG]   Wold pos=(%.2f,%.2f,%.2f)",
                            Wold._41, Wold._42, Wold._43);
                    }
                }
            }
            else
            {
                static u32 s_overflowLog = 0;
                if (s_overflowLog < 10) {
                    Msg("! [SKL-ST] Bone buffer overflow: offset=%u + bones=%u > max=%u, fallback to single-bone",
                        boneOffset, boneCount, CBackend::MAX_TOTAL_BONES);
                    s_overflowLog++;
                }
            }

            if (!boneWriteOK)
            {
                // Fallback: render with bone 0 transform only (no GPU skinning)
                W.mul_43(Wold, Parent->LL_GetTransform_R(0));
                RCache.set_xform_world(W);
                vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Fmatrix), &W);
                vkFVisual::Render(LOD);
            }
            else
            {
                // 2. Bind skinned descriptor set 2 with bone SSBO at correct offset
                if (g_DescriptorManager && RCache.IsBoneBufferValid())
                {
                    VkDescriptorSet objSet = g_DescriptorManager->AllocatePerObject();
                    if (objSet != VK_NULL_HANDLE)
                    {
                        VkDeviceSize ssboOffset = boneOffset * sizeof(Fmatrix);
                        g_DescriptorManager->UpdateStorageBuffer(
                            objSet, 1,
                            RCache.GetBoneBuffer(),
                            totalAligned * sizeof(Fmatrix),
                            ssboOffset);

                        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            layout, 2, 1, &objSet, 0, nullptr);
                    }
                    else
                    {
                        static u32 s_descFailST = 0;
                        if (s_descFailST < 20) {
                            s_descFailST++;
                            const char* pn = (Parent && Parent->dbg_name.size() > 0)
                                ? Parent->dbg_name.c_str() : "?";
                            Msg("! [SKIN-DIAG-ST] PerObject descriptor FAILED '%s' "
                                "mode=%u bones=%u - STALE bone data!", pn, (u32)RenderMode, boneCount);
                        }
                    }
                }

                // 3. Push world matrix, skinning mode, and bone count
                W = Wold;
                RCache.set_xform_world(W);
                vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Fmatrix), &W);

                u32 skinMode = (RenderMode == RM_SKINNING_1B) ? 1u :
                               (RenderMode == RM_SKINNING_2B) ? 2u :
                               (RenderMode == RM_SKINNING_3B) ? 3u : 4u;
                vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_VERTEX_BIT, 196, sizeof(u32), &skinMode);
                vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_VERTEX_BIT, 208, sizeof(u32), &alignedBoneCount);

                // 4. Switch to skinned pipeline
                u32 stride = m_mesh.vStride;
                VkPipeline skinnedPipeline = RTarget->GetGBufferPipelineSkinned(stride);
                if (skinnedPipeline == VK_NULL_HANDLE)
                {
                    // Fallback: render with bone 0 transform only
                    W.mul_43(Wold, Parent->LL_GetTransform_R(0));
                    RCache.set_xform_world(W);
                    vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Fmatrix), &W);
                    vkFVisual::Render(LOD);
                }
                else
                {
                    VkPipeline prevPipeline = RCache.m_CurrentPipeline;
                    u32 prevStride = RCache.m_CurrentGBufStride;
                    u32 prevTcOff = RCache.m_CurrentGBufTcOffset;

                    RCache.set_Pipeline(skinnedPipeline);
                    RCache.m_CurrentGBufStride = stride;
                    RCache.m_CurrentGBufTcOffset = 28;  // Skinned meshes: UV always at offset 28

                    float uvScale = 1.0f;
                    float alphaRef = -1.0f;  // No alpha test for skinned geometry
                    vkCmdPushConstants(cmd, layout,
                        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                        192, sizeof(float), &uvScale);
                    vkCmdPushConstants(cmd, layout,
                        VK_SHADER_STAGE_FRAGMENT_BIT,
                        200, sizeof(float), &alphaRef);

                    vkFVisual::Render(LOD);

                    if (prevPipeline != VK_NULL_HANDLE)
                    {
                        RCache.set_Pipeline(prevPipeline);
                        RCache.m_CurrentGBufStride = prevStride;
                        RCache.m_CurrentGBufTcOffset = prevTcOff;

                        // Restore correct uvScale for the previous pipeline
                        // Skinned render pushed 1.0 — level VBs (stride 32) need 1/1024
                        float restoreUvScale = (prevStride == 32) ? (1.0f / 1024.0f) : 1.0f;
                        vkCmdPushConstants(cmd, layout,
                            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                            192, sizeof(float), &restoreUvScale);
                    }
                }
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            Msg("! [SKL-ST] CRASH in skinned render RenderMode=%u", (u32)RenderMode);
        }
    }
    else
    {
        // Unknown mode: use world matrix as-is
        W = Wold;
        RCache.set_xform_world(W);
        VkCommandBuffer cmd = RCache.GetCommandBuffer();
        VkPipelineLayout layout = VK::g_PipelineManager->GetLayout();
        if (cmd != VK_NULL_HANDLE && layout != VK_NULL_HANDLE)
            vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Fmatrix), &W);
        vkFVisual::Render(LOD);
    }

    // Restore original world matrix
    RCache.set_xform_world(Wold);
    VkCommandBuffer cmd2 = RCache.GetCommandBuffer();
    VkPipelineLayout layout2 = VK::g_PipelineManager->GetLayout();
    if (cmd2 != VK_NULL_HANDLE && layout2 != VK_NULL_HANDLE)
        vkCmdPushConstants(cmd2, layout2, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Fmatrix), &Wold);
}

void vkSkeletonX_ST::AfterLoad(CKinematics* parent, u16 child_idx)
{
    SetParent(parent);
    ChildIDX = child_idx;
}

// ============================================================================
// _Load_hw_VK - Convert vertBoned* to vertHW_* and create Vulkan VB
// Mirrors DX11's CSkeletonX_ext::_Load_hw() from FSkinned.cpp
// ============================================================================
void vkSkeletonX_ST::_Load_hw_VK(void* _verts_, u32 dwVertType, u32 dwVertCount)
{
    // Local bone vertex structs (matching OGF file format)
    // Structs must match bone.h layout exactly (field order + #pragma pack(push, 2))
#pragma pack(push, 2)
    struct vertBoned1W { Fvector P; Fvector N; Fvector T; Fvector B; float u, v; u32 matrix; };
    struct vertBoned2W { u16 matrix0, matrix1; Fvector P; Fvector N; Fvector T; Fvector B; float w; float u, v; };
    struct vertBoned3W { u16 m[3]; Fvector P; Fvector N; Fvector T; Fvector B; float w[2]; float u, v; };
    struct vertBoned4W { u16 m[4]; Fvector P; Fvector N; Fvector T; Fvector B; float w[3]; float u, v; };
#pragma pack(pop)

    switch (RenderMode)
    {
    case RM_SINGLE:
    case RM_SKINNING_1B:
    {
        u32 vStride = sizeof(vertHW_1W);  // 36
        vertHW_1W* dst = xr_alloc<vertHW_1W>(dwVertCount);
        vertBoned1W* src = (vertBoned1W*)_verts_;
        float uvMinU = FLT_MAX, uvMaxU = -FLT_MAX;
        float uvMinV = FLT_MAX, uvMaxV = -FLT_MAX;
        for (u32 i = 0; i < dwVertCount; i++)
        {
            Fvector2 uv; uv.set(src->u, src->v);
            dst[i].set(src->P, src->N, src->T, src->B, uv, src->matrix * 3);
            if (src->u < uvMinU) uvMinU = src->u;
            if (src->u > uvMaxU) uvMaxU = src->u;
            if (src->v < uvMinV) uvMinV = src->v;
            if (src->v > uvMaxV) uvMaxV = src->v;
            src++;
        }
        // Log UV range for first 30 unique skinned meshes
        {
            static u32 s_uvDiagCount = 0;
            if (s_uvDiagCount < 30) {
                s_uvDiagCount++;
                Msg("[UV-RANGE] 1W vCount=%u mode=%u U=[%.3f..%.3f] V=[%.3f..%.3f] stride=%u",
                    dwVertCount, (u32)RenderMode, uvMinU, uvMaxU, uvMinV, uvMaxV, vStride);
            }
        }
        m_mesh.p_rm_Vertices = xr_new<VK::CVulkanBuffer>();
        m_mesh.p_rm_Vertices->Create(dwVertCount * vStride,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);
        m_mesh.p_rm_Vertices->Upload(dst, dwVertCount * vStride);
        m_mesh.vStride = vStride;
        xr_free(dst);
        break;
    }
    case RM_SKINNING_2B:
    {
        u32 vStride = sizeof(vertHW_2W);  // 44
        vertHW_2W* dst = xr_alloc<vertHW_2W>(dwVertCount);
        vertBoned2W* src = (vertBoned2W*)_verts_;
        for (u32 i = 0; i < dwVertCount; i++)
        {
            Fvector2 uv; uv.set(src->u, src->v);
            dst[i].set(src->P, src->N, src->T, src->B, uv,
                src->matrix0 * 3, src->matrix1 * 3, src->w);
            src++;
        }
        m_mesh.p_rm_Vertices = xr_new<VK::CVulkanBuffer>();
        m_mesh.p_rm_Vertices->Create(dwVertCount * vStride,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);
        m_mesh.p_rm_Vertices->Upload(dst, dwVertCount * vStride);
        m_mesh.vStride = vStride;
        xr_free(dst);
        break;
    }
    case RM_SKINNING_3B:
    {
        u32 vStride = sizeof(vertHW_3W);  // 44
        vertHW_3W* dst = xr_alloc<vertHW_3W>(dwVertCount);
        vertBoned3W* src = (vertBoned3W*)_verts_;
        for (u32 i = 0; i < dwVertCount; i++)
        {
            Fvector2 uv; uv.set(src->u, src->v);
            dst[i].set(src->P, src->N, src->T, src->B, uv,
                src->m[0] * 3, src->m[1] * 3, src->m[2] * 3,
                src->w[0], src->w[1]);
            src++;
        }
        m_mesh.p_rm_Vertices = xr_new<VK::CVulkanBuffer>();
        m_mesh.p_rm_Vertices->Create(dwVertCount * vStride,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);
        m_mesh.p_rm_Vertices->Upload(dst, dwVertCount * vStride);
        m_mesh.vStride = vStride;
        xr_free(dst);
        break;
    }
    case RM_SKINNING_4B:
    {
        u32 vStride = sizeof(vertHW_4W);  // 40
        vertHW_4W* dst = xr_alloc<vertHW_4W>(dwVertCount);
        vertBoned4W* src = (vertBoned4W*)_verts_;
        for (u32 i = 0; i < dwVertCount; i++)
        {
            Fvector2 uv; uv.set(src->u, src->v);
            dst[i].set(src->P, src->N, src->T, src->B, uv,
                src->m[0] * 3, src->m[1] * 3, src->m[2] * 3, src->m[3] * 3,
                src->w[0], src->w[1], src->w[2]);
            src++;
        }
        m_mesh.p_rm_Vertices = xr_new<VK::CVulkanBuffer>();
        m_mesh.p_rm_Vertices->Create(dwVertCount * vStride,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);
        m_mesh.p_rm_Vertices->Upload(dst, dwVertCount * vStride);
        m_mesh.vStride = vStride;
        xr_free(dst);
        break;
    }
    default:
        Msg("! [SKL-ST] _Load_hw_VK: unknown RenderMode %u", (u32)RenderMode);
        break;
    }
}

// ============================================================================
// vkSkeletonX_PM implementation - Skinned progressive mesh
// ============================================================================
vkSkeletonX_PM::vkSkeletonX_PM()
{
    Type = MT_SKELETON_GEOMDEF_PM;
    RenderMode = 0;
    BonesUsed = 0;
}

vkSkeletonX_PM::~vkSkeletonX_PM()
{
}

void vkSkeletonX_PM::Load(const char* name, IReader* data, u32 flags)
{
    // ========================================================================
    // Step 1: Read OGF_VERTICES first for bone analysis (like DX11's CSkeletonX::_Load)
    // ========================================================================
    R_ASSERT(data->find_chunk(OGF_VERTICES));
    u32 dwVertType = data->r_u32();
    u32 dwVertCount = data->r_u32();
    void* _verts_ = data->pointer();  // Save pointer to raw skinned vertex data

    // Analyze bones to determine RenderMode
    xr_vector<u16> bids;
    u16 sw_bones_cnt = 0;

    switch (dwVertType)
    {
    case OGF_VERTEXFORMAT_FVF_1L:
    case 1:
        {
            struct vertBoned1W { Fvector P; Fvector3 N; Fvector3 T; Fvector3 B; Fvector2 tc; u32 matrix; };
            vertBoned1W* pVO = (vertBoned1W*)_verts_;
            for (u32 it = 0; it < dwVertCount; ++it)
            {
                u16 mid = (u16)pVO[it].matrix;
                if (bids.end() == std::find(bids.begin(), bids.end(), mid))
                    bids.push_back(mid);
                sw_bones_cnt = _max(sw_bones_cnt, mid);
            }
            if (1 == bids.size()) { RenderMode = RM_SINGLE; RMS_boneid = *bids.begin(); }
            else { RenderMode = RM_SKINNING_1B; RMS_bonecount = sw_bones_cnt + 1; BonesUsed = (u16)bids.size(); }
        }
        break;
    case OGF_VERTEXFORMAT_FVF_2L:
    case 2:
        {
            #pragma pack(push, 2)
            struct vertBoned2W { u16 matrix0; u16 matrix1; Fvector P; Fvector N; Fvector T; Fvector B; float w; float u, v; };
            #pragma pack(pop)
            vertBoned2W* pVO = (vertBoned2W*)_verts_;
            for (u32 it = 0; it < dwVertCount; ++it)
            {
                sw_bones_cnt = _max(sw_bones_cnt, pVO[it].matrix0);
                sw_bones_cnt = _max(sw_bones_cnt, pVO[it].matrix1);
                if (bids.end() == std::find(bids.begin(), bids.end(), pVO[it].matrix0)) bids.push_back(pVO[it].matrix0);
                if (bids.end() == std::find(bids.begin(), bids.end(), pVO[it].matrix1)) bids.push_back(pVO[it].matrix1);
            }
            RenderMode = RM_SKINNING_2B; RMS_bonecount = sw_bones_cnt + 1; BonesUsed = (u16)bids.size();
        }
        break;
    case OGF_VERTEXFORMAT_FVF_3L:
    case 3:
        {
            #pragma pack(push, 2)
            struct vertBoned3W { u16 m[3]; Fvector P; Fvector N; Fvector T; Fvector B; float w[2]; float u, v; };
            #pragma pack(pop)
            vertBoned3W* pVO = (vertBoned3W*)_verts_;
            for (u32 it = 0; it < dwVertCount; ++it)
                for (int k = 0; k < 3; k++) {
                    sw_bones_cnt = _max(sw_bones_cnt, pVO[it].m[k]);
                    if (bids.end() == std::find(bids.begin(), bids.end(), pVO[it].m[k])) bids.push_back(pVO[it].m[k]);
                }
            RenderMode = RM_SKINNING_3B; RMS_bonecount = sw_bones_cnt + 1; BonesUsed = (u16)bids.size();
        }
        break;
    case OGF_VERTEXFORMAT_FVF_4L:
    case 4:
        {
            #pragma pack(push, 2)
            struct vertBoned4W { u16 m[4]; Fvector P; Fvector N; Fvector T; Fvector B; float w[3]; float u, v; };
            #pragma pack(pop)
            vertBoned4W* pVO = (vertBoned4W*)_verts_;
            for (u32 it = 0; it < dwVertCount; ++it)
                for (int k = 0; k < 4; k++) {
                    sw_bones_cnt = _max(sw_bones_cnt, pVO[it].m[k]);
                    if (bids.end() == std::find(bids.begin(), bids.end(), pVO[it].m[k])) bids.push_back(pVO[it].m[k]);
                }
            RenderMode = RM_SKINNING_4B; RMS_bonecount = sw_bones_cnt + 1; BonesUsed = (u16)bids.size();
        }
        break;
    default:
        Msg("![Vulkan] vkSkeletonX_PM::Load() - unknown vertex type %d", dwVertType);
        RenderMode = RM_SINGLE; RMS_boneid = 0;
        break;
    }

    // ========================================================================
    // Step 2: Load base visual WITHOUT vertices (indices + sliding window only)
    // ========================================================================
    data->seek(0);
    vkFProgressive::Load(name, data, flags | VLOAD_NOVERTICES);

    // ========================================================================
    // Step 3: Convert vertBoned* -> vertHW_* and create Vulkan VB
    // ========================================================================
    m_mesh.vBase = 0;
    m_mesh.vCount = dwVertCount;
    _Load_hw_VK(_verts_, dwVertType, dwVertCount);

    Msg("[SKL-PM] Load '%s': mode=%u vCount=%u stride=%u bones=%u",
        name, (u32)RenderMode, dwVertCount, m_mesh.vStride, (u32)BonesUsed);
}

void vkSkeletonX_PM::Release()
{
    vkFProgressive::Release();
}

void vkSkeletonX_PM::Copy(vkRender_Visual* from)
{
    vkFProgressive::Copy(from);

    vkSkeletonX_PM* src = dynamic_cast<vkSkeletonX_PM*>(from);
    if (!src) return;

    RenderMode = src->RenderMode;
    BonesUsed = src->BonesUsed;

    // Copy union data (bone ID or bone count depending on mode)
    if (RenderMode == RM_SINGLE)
        RMS_boneid = src->RMS_boneid;
    else
        RMS_bonecount = src->RMS_bonecount;
}

void vkSkeletonX_PM::Render(float LOD)
{
    // Frame-limited diagnostics (first 5 frames)
    static u32 s_pmDiagFrame = 0;
    static u32 s_pmDiagCount = 0;
    bool bDiag = (Device.dwFrame != s_pmDiagFrame) && (s_pmDiagCount < 5);
    if (bDiag) {
        s_pmDiagFrame = Device.dwFrame;
        s_pmDiagCount++;
        Msg("[SKL-PM] Render: this=%p Parent=%p RenderMode=%u RMS_boneid=%u Type=%u frame=%u",
            this, Parent, (u32)RenderMode, RMS_boneid, Type, Device.dwFrame);
        Msg("[SKL-PM]   m_mesh.valid=%d sw_count=%u m_pMaterial=%p", m_mesh.IsValid() ? 1 : 0, sw_count, m_pMaterial);
        if (m_mesh.IsValid())
            Msg("[SKL-PM]   VB=%p IB=%p vBase=%u vCount=%u iBase=%u vStride=%u",
                m_mesh.p_rm_Vertices, m_mesh.p_rm_Indices, m_mesh.vBase, m_mesh.vCount, m_mesh.iBase, m_mesh.vStride);
    }

    // Safety check
    if (!Parent) {
        Msg("! [SKL-PM] Parent is NULL! this=%p, skipping render.", this);
        return;
    }

    // Validate Parent object
    {
        void* vtbl = *(void**)Parent;
        if (!vtbl) {
            Msg("! [SKL-PM] Parent=%p has NULL vtable! Skipping render.", Parent);
            return;
        }

        u16 boneCount = 0;
        __try {
            boneCount = Parent->LL_BoneCount();
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            Msg("! [SKL-PM] CRASH reading Parent->LL_BoneCount()! Parent=%p", Parent);
            return;
        }

        if (bDiag) {
            Msg("[SKL-PM]   Parent=%p vtable=%p boneCount=%u", Parent, vtbl, boneCount);
        }

        // Probe bone_instances accessibility
        if (boneCount > 0) {
            __try {
                Fmatrix& testM = Parent->LL_GetTransform_R(0);
                volatile float testVal = testM._11;
                (void)testVal;
            } __except(EXCEPTION_EXECUTE_HANDLER) {
                Msg("! [SKL-PM] CRASH probing LL_GetTransform_R(0)! bone_instances NULL. Parent=%p boneCount=%u", Parent, boneCount);
                __try {
                    Parent->CalculateBones(TRUE);
                    Fmatrix& testM2 = Parent->LL_GetTransform_R(0);
                    volatile float testVal2 = testM2._11;
                    (void)testVal2;
                    Msg("[SKL-PM]   CalculateBones fixed it. Continuing.");
                } __except(EXCEPTION_EXECUTE_HANDLER) {
                    Msg("! [SKL-PM]   Still crashes after CalculateBones! Skipping.");
                    return;
                }
            }
        }
    }

    // Get current world matrix
    Fmatrix Wold;
    __try { Wold = RCache.xforms.m_w; } __except(EXCEPTION_EXECUTE_HANDLER) {
        Msg("! [SKL-PM] CRASH reading RCache.xforms"); return;
    }

    Fmatrix W;

    if (RenderMode == RM_SINGLE)
    {
        // ====================================================================
        // Single bone: simple rigid attachment
        // ====================================================================
        __try {
            W.mul_43(Wold, Parent->LL_GetTransform_R(RMS_boneid));
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            Msg("! [SKL-PM] CRASH in single bone transform boneid=%u", RMS_boneid);
            return;
        }

        __try {
            RCache.set_xform_world(W);
            VkCommandBuffer cmd = RCache.GetCommandBuffer();
            VkPipelineLayout layout = VK::g_PipelineManager->GetLayout();
            if (cmd != VK_NULL_HANDLE && layout != VK_NULL_HANDLE)
                vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Fmatrix), &W);
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            Msg("! [SKL-PM] CRASH in push constants (single)"); return;
        }

        __try {
            vkFProgressive::Render(LOD);
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            Msg("! [SKL-PM] CRASH in vkFProgressive::Render (single)"); return;
        }
    }
    else if (RenderMode >= RM_SKINNING_1B && RenderMode <= RM_SKINNING_4B)
    {
        // ====================================================================
        // Multi-bone GPU skinning
        // ====================================================================
        __try {
            VkCommandBuffer cmd = RCache.GetCommandBuffer();
            VkPipelineLayout layout = VK::g_PipelineManager->GetLayout();
            if (cmd == VK_NULL_HANDLE || layout == VK_NULL_HANDLE) return;

            // 1. Upload bone matrices to SSBO (sub-allocated per skeleton)
            u32 boneCount = RMS_bonecount;
            if (boneCount > CBackend::MAX_BONES)
                boneCount = CBackend::MAX_BONES;

            u32 boneOffset = RCache.m_BoneWriteOffset;
            u32 alignedBoneCount = (boneCount + 3u) & ~3u;
            bool boneWriteOK = false;

            // Need 2x bones: current [0..N) + previous [N..2N) for DLSS motion vectors
            u32 totalAligned = alignedBoneCount * 2;

            // Save previous bone transforms for motion vectors (matching SkeletonX.cpp logic)
            // CRITICAL: Must happen BEFORE reading mRenderTransform_prev below!
            // Track which skeletons have been saved this frame
            static std::unordered_map<IKinematics*, u32> s_savedFrames;
            auto it = s_savedFrames.find(Parent);
            if (it == s_savedFrames.end() || it->second != Device.dwFrame)
            {
                s_savedFrames[Parent] = Device.dwFrame;

                // NOTE: Can't access Matrix_Prev/Matrix_Temp (protected), but we CAN access bone instances
                // Save bone matrices for next frame's motion vectors
                for (u16 b = 0; b < Parent->LL_BoneCount(); b++)
                {
                    CBoneInstance& Bone = Parent->LL_GetBoneInstance(b);
                    Bone.mRenderTransform_prev.set(Bone.mRenderTransform_temp);
                    Bone.mRenderTransform_temp.set(Bone.mRenderTransform);
                }
            }

            if (RCache.IsBoneBufferValid() && RCache.m_BoneMapped &&
                (boneOffset + totalAligned) <= CBackend::MAX_TOTAL_BONES)
            {
                for (u32 mid = 0; mid < boneCount; ++mid)
                {
                    __try {
                        RCache.m_BoneMapped[boneOffset + mid] = Parent->LL_GetTransform_R(mid);
                        RCache.m_BoneMapped[boneOffset + alignedBoneCount + mid] =
                            Parent->LL_GetBoneInstance(u16(mid)).mRenderTransform_prev;
                    } __except(EXCEPTION_EXECUTE_HANDLER) {
                        RCache.m_BoneMapped[boneOffset + mid].identity();
                        RCache.m_BoneMapped[boneOffset + alignedBoneCount + mid].identity();
                    }
                }

                RCache.m_BoneWriteOffset = boneOffset + totalAligned;
                boneWriteOK = true;

                // SKINNING DIAGNOSTICS (PM)
                {
                    static std::set<std::string> s_loggedSkinsPM;
                    static u32 s_skinDiagPM = 0;
                    const char* pname = (Parent && Parent->dbg_name.size() > 0)
                        ? Parent->dbg_name.c_str() : "<?>";
                    std::string key(pname);
                    if (s_loggedSkinsPM.find(key) == s_loggedSkinsPM.end() && s_skinDiagPM < 30)
                    {
                        s_loggedSkinsPM.insert(key);
                        s_skinDiagPM++;
                        Msg("[SKIN-DIAG-PM] #%u '%s' mode=%u bones=%u stride=%u vCount=%u ssboOff=%u",
                            s_skinDiagPM, pname, (u32)RenderMode,
                            boneCount, m_mesh.vStride, m_mesh.vCount, boneOffset);
                        u32 dumpCount = (boneCount < 8) ? boneCount : 8;
                        for (u32 db = 0; db < dumpCount; ++db) {
                            const Fmatrix& bm = RCache.m_BoneMapped[boneOffset + db];
                            float scaleX = sqrtf(bm._11*bm._11 + bm._12*bm._12 + bm._13*bm._13);
                            float scaleY = sqrtf(bm._21*bm._21 + bm._22*bm._22 + bm._23*bm._23);
                            float scaleZ = sqrtf(bm._31*bm._31 + bm._32*bm._32 + bm._33*bm._33);
                            Msg("[SKIN-DIAG-PM]   bone[%u]: scale=(%.3f,%.3f,%.3f) pos=(%.3f,%.3f,%.3f)",
                                db, scaleX, scaleY, scaleZ, bm._41, bm._42, bm._43);
                            if (scaleX < 0.01f || scaleY < 0.01f || scaleZ < 0.01f)
                                Msg("[SKIN-DIAG-PM]   *** WARNING: bone[%u] near-zero scale!", db);
                        }
                    }
                }
            }
            else
            {
                static u32 s_overflowLogPM = 0;
                if (s_overflowLogPM < 10) {
                    Msg("! [SKL-PM] Bone buffer overflow: offset=%u + bones=%u > max=%u, fallback to single-bone",
                        boneOffset, boneCount, CBackend::MAX_TOTAL_BONES);
                    s_overflowLogPM++;
                }
            }

            if (!boneWriteOK)
            {
                // Fallback: render with bone 0 transform only (no GPU skinning)
                W.mul_43(Wold, Parent->LL_GetTransform_R(0));
                RCache.set_xform_world(W);
                vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Fmatrix), &W);
                vkFProgressive::Render(LOD);
            }
            else
            {
                // 2. Bind skinned descriptor set 2 with bone SSBO at correct offset
                if (g_DescriptorManager && RCache.IsBoneBufferValid())
                {
                    VkDescriptorSet objSet = g_DescriptorManager->AllocatePerObject();
                    if (objSet != VK_NULL_HANDLE)
                    {
                        VkDeviceSize ssboOffset = boneOffset * sizeof(Fmatrix);
                        g_DescriptorManager->UpdateStorageBuffer(
                            objSet, 1,
                            RCache.GetBoneBuffer(),
                            totalAligned * sizeof(Fmatrix),
                            ssboOffset);

                        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            layout, 2, 1, &objSet, 0, nullptr);
                    }
                    else
                    {
                        static u32 s_descFailPM = 0;
                        if (s_descFailPM < 20) {
                            s_descFailPM++;
                            const char* pn = (Parent && Parent->dbg_name.size() > 0)
                                ? Parent->dbg_name.c_str() : "?";
                            Msg("! [SKIN-DIAG-PM] PerObject descriptor FAILED '%s' "
                                "mode=%u bones=%u - STALE bone data!", pn, (u32)RenderMode, boneCount);
                        }
                    }
                }

                // 3. Push world matrix, skinning mode, and bone count
                W = Wold;
                RCache.set_xform_world(W);
                vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Fmatrix), &W);

                u32 skinMode = (RenderMode == RM_SKINNING_1B) ? 1u :
                               (RenderMode == RM_SKINNING_2B) ? 2u :
                               (RenderMode == RM_SKINNING_3B) ? 3u : 4u;
                vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_VERTEX_BIT, 196, sizeof(u32), &skinMode);
                vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_VERTEX_BIT, 208, sizeof(u32), &alignedBoneCount);

                // 4. Switch to skinned pipeline
                u32 stride = m_mesh.vStride;
                VkPipeline skinnedPipeline = RTarget->GetGBufferPipelineSkinned(stride);
                if (skinnedPipeline == VK_NULL_HANDLE)
                {
                    W.mul_43(Wold, Parent->LL_GetTransform_R(0));
                    RCache.set_xform_world(W);
                    vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Fmatrix), &W);
                    vkFProgressive::Render(LOD);
                }
                else
                {
                    VkPipeline prevPipeline = RCache.m_CurrentPipeline;
                    u32 prevStride = RCache.m_CurrentGBufStride;
                    u32 prevTcOff = RCache.m_CurrentGBufTcOffset;

                    RCache.set_Pipeline(skinnedPipeline);
                    RCache.m_CurrentGBufStride = stride;
                    RCache.m_CurrentGBufTcOffset = 28;  // Skinned meshes: UV always at offset 28

                    float uvScale = 1.0f;
                    float alphaRef = -1.0f;  // No alpha test for skinned geometry
                    vkCmdPushConstants(cmd, layout,
                        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                        192, sizeof(float), &uvScale);
                    vkCmdPushConstants(cmd, layout,
                        VK_SHADER_STAGE_FRAGMENT_BIT,
                        200, sizeof(float), &alphaRef);

                    vkFProgressive::Render(LOD);

                    if (prevPipeline != VK_NULL_HANDLE)
                    {
                        RCache.set_Pipeline(prevPipeline);
                        RCache.m_CurrentGBufStride = prevStride;
                        RCache.m_CurrentGBufTcOffset = prevTcOff;

                        // Restore correct uvScale for the previous pipeline
                        // Skinned render pushed 1.0 — level VBs (stride 32) need 1/1024
                        float restoreUvScale = (prevStride == 32) ? (1.0f / 1024.0f) : 1.0f;
                        vkCmdPushConstants(cmd, layout,
                            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                            192, sizeof(float), &restoreUvScale);
                    }
                }
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            Msg("! [SKL-PM] CRASH in skinned render RenderMode=%u", (u32)RenderMode);
        }
    }
    else
    {
        // Unknown mode
        W = Wold;
        RCache.set_xform_world(W);
        VkCommandBuffer cmd = RCache.GetCommandBuffer();
        VkPipelineLayout layout = VK::g_PipelineManager->GetLayout();
        if (cmd != VK_NULL_HANDLE && layout != VK_NULL_HANDLE)
            vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Fmatrix), &W);
        vkFProgressive::Render(LOD);
    }

    // Restore original world matrix
    RCache.set_xform_world(Wold);
    VkCommandBuffer cmd2 = RCache.GetCommandBuffer();
    VkPipelineLayout layout2 = VK::g_PipelineManager->GetLayout();
    if (cmd2 != VK_NULL_HANDLE && layout2 != VK_NULL_HANDLE)
        vkCmdPushConstants(cmd2, layout2, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Fmatrix), &Wold);
}

void vkSkeletonX_PM::AfterLoad(CKinematics* parent, u16 child_idx)
{
    SetParent(parent);
    ChildIDX = child_idx;
}

// ============================================================================
// _Load_hw_VK for vkSkeletonX_PM - identical logic to vkSkeletonX_ST version
// ============================================================================
void vkSkeletonX_PM::_Load_hw_VK(void* _verts_, u32 dwVertType, u32 dwVertCount)
{
    // Structs must match bone.h layout exactly (field order + #pragma pack(push, 2))
#pragma pack(push, 2)
    struct vertBoned1W { Fvector P; Fvector N; Fvector T; Fvector B; float u, v; u32 matrix; };
    struct vertBoned2W { u16 matrix0, matrix1; Fvector P; Fvector N; Fvector T; Fvector B; float w; float u, v; };
    struct vertBoned3W { u16 m[3]; Fvector P; Fvector N; Fvector T; Fvector B; float w[2]; float u, v; };
    struct vertBoned4W { u16 m[4]; Fvector P; Fvector N; Fvector T; Fvector B; float w[3]; float u, v; };
#pragma pack(pop)

    switch (RenderMode)
    {
    case RM_SINGLE:
    case RM_SKINNING_1B:
    {
        u32 vStride = sizeof(vertHW_1W);
        vertHW_1W* dst = xr_alloc<vertHW_1W>(dwVertCount);
        vertBoned1W* src = (vertBoned1W*)_verts_;
        for (u32 i = 0; i < dwVertCount; i++) {
            Fvector2 uv; uv.set(src->u, src->v);
            dst[i].set(src->P, src->N, src->T, src->B, uv, src->matrix * 3);
            src++;
        }
        m_mesh.p_rm_Vertices = xr_new<VK::CVulkanBuffer>();
        m_mesh.p_rm_Vertices->Create(dwVertCount * vStride,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);
        m_mesh.p_rm_Vertices->Upload(dst, dwVertCount * vStride);
        m_mesh.vStride = vStride;
        xr_free(dst);
        break;
    }
    case RM_SKINNING_2B:
    {
        u32 vStride = sizeof(vertHW_2W);
        vertHW_2W* dst = xr_alloc<vertHW_2W>(dwVertCount);
        vertBoned2W* src = (vertBoned2W*)_verts_;
        for (u32 i = 0; i < dwVertCount; i++) {
            Fvector2 uv; uv.set(src->u, src->v);
            dst[i].set(src->P, src->N, src->T, src->B, uv, src->matrix0 * 3, src->matrix1 * 3, src->w);
            src++;
        }
        m_mesh.p_rm_Vertices = xr_new<VK::CVulkanBuffer>();
        m_mesh.p_rm_Vertices->Create(dwVertCount * vStride,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);
        m_mesh.p_rm_Vertices->Upload(dst, dwVertCount * vStride);
        m_mesh.vStride = vStride;
        xr_free(dst);
        break;
    }
    case RM_SKINNING_3B:
    {
        u32 vStride = sizeof(vertHW_3W);
        vertHW_3W* dst = xr_alloc<vertHW_3W>(dwVertCount);
        vertBoned3W* src = (vertBoned3W*)_verts_;
        for (u32 i = 0; i < dwVertCount; i++) {
            Fvector2 uv; uv.set(src->u, src->v);
            dst[i].set(src->P, src->N, src->T, src->B, uv, src->m[0] * 3, src->m[1] * 3, src->m[2] * 3, src->w[0], src->w[1]);
            src++;
        }
        m_mesh.p_rm_Vertices = xr_new<VK::CVulkanBuffer>();
        m_mesh.p_rm_Vertices->Create(dwVertCount * vStride,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);
        m_mesh.p_rm_Vertices->Upload(dst, dwVertCount * vStride);
        m_mesh.vStride = vStride;
        xr_free(dst);
        break;
    }
    case RM_SKINNING_4B:
    {
        u32 vStride = sizeof(vertHW_4W);
        vertHW_4W* dst = xr_alloc<vertHW_4W>(dwVertCount);
        vertBoned4W* src = (vertBoned4W*)_verts_;
        for (u32 i = 0; i < dwVertCount; i++) {
            Fvector2 uv; uv.set(src->u, src->v);
            dst[i].set(src->P, src->N, src->T, src->B, uv,
                src->m[0] * 3, src->m[1] * 3, src->m[2] * 3, src->m[3] * 3,
                src->w[0], src->w[1], src->w[2]);
            src++;
        }
        m_mesh.p_rm_Vertices = xr_new<VK::CVulkanBuffer>();
        m_mesh.p_rm_Vertices->Create(dwVertCount * vStride,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);
        m_mesh.p_rm_Vertices->Upload(dst, dwVertCount * vStride);
        m_mesh.vStride = vStride;
        xr_free(dst);
        break;
    }
    default:
        Msg("! [SKL-PM] _Load_hw_VK: unknown RenderMode %u", (u32)RenderMode);
        break;
    }
}

// ============================================================================
// Particle system implementations (stubs)
// ============================================================================
vkParticleEffect::vkParticleEffect()
{
    Type = MT_PARTICLE_EFFECT;
}

vkParticleEffect::~vkParticleEffect()
{
}

void vkParticleEffect::Load(const char* name, IReader* data, u32 flags)
{
    vkRender_Visual::Load(name, data, flags);
    // TODO: Load particle effect definition
}

void vkParticleEffect::Render(float LOD)
{
    // TODO: Render particle effect
}

vkParticleGroup::vkParticleGroup()
{
    Type = MT_PARTICLE_GROUP;
}

vkParticleGroup::~vkParticleGroup()
{
    Release();
}

void vkParticleGroup::Load(const char* name, IReader* data, u32 flags)
{
    vkRender_Visual::Load(name, data, flags);
    // TODO: Load and create child particle effects
}

void vkParticleGroup::Release()
{
    for (auto& effect : effects)
    {
        xr_delete(effect);
    }
    effects.clear();
    vkRender_Visual::Release();
}

void vkParticleGroup::Render(float LOD)
{
    for (auto& effect : effects)
    {
        if (effect)
            effect->Render(LOD);
    }
}

// ============================================================================
// Factory function
// ============================================================================
vkRender_Visual* vkVisual_Create(u32 type)
{
    vkRender_Visual* V = nullptr;

    switch (type)
    {
    case MT_NORMAL:
        V = xr_new<vkFVisual>();
        break;

    case MT_HIERRARHY:
        V = xr_new<vkFHierrarhyVisual>();
        break;

    case MT_PROGRESSIVE:
        V = xr_new<vkFProgressive>();
        break;

    case MT_SKELETON_ANIM:
        // CKinematicsAnimated created via factory function (defined in vk_SkeletonAnimated.cpp)
        // Returns dxRender_Visual* which is binary compatible with vkRender_Visual*
        V = reinterpret_cast<vkRender_Visual*>(vkCreateKinematicsAnimated());
        break;

    case MT_SKELETON_RIGID:
        // CKinematics created via factory function (defined in vk_SkeletonRigid.cpp)
        // Returns dxRender_Visual* which is binary compatible with vkRender_Visual*
        V = reinterpret_cast<vkRender_Visual*>(vkCreateKinematics());
        break;

    case MT_SKELETON_GEOMDEF_ST:
        V = xr_new<vkSkeletonX_ST>();
        break;

    case MT_SKELETON_GEOMDEF_PM:
        V = xr_new<vkSkeletonX_PM>();
        break;

    case MT_LOD:
        V = xr_new<vkFLOD>();
        break;

    case MT_TREE_ST:
        V = xr_new<vkFTreeVisual_ST>();
        break;

    case MT_TREE_PM:
        V = xr_new<vkFTreeVisual_PM>();
        break;

    case MT_PARTICLE_EFFECT:
        V = xr_new<vkParticleEffect>();
        break;

    case MT_PARTICLE_GROUP:
        V = xr_new<vkParticleGroup>();
        break;

    case MT_3DFLUIDVOLUME:
        V = xr_new<VK::vk3DFluidVolume>();
        break;

    default:
        Msg("![Vulkan] Unknown visual type %u, creating base visual", type);
        V = xr_new<vkRender_Visual>();
        break;
    }

    V->Type = type;
    return V;
}

// ============================================================================
// Dummy visual factory (for failed visual loads)
// ============================================================================
vkRender_Visual* vkVisual_CreateDummy()
{
    // Create a basic visual with empty geometry
    vkRender_Visual* V = xr_new<vkRender_Visual>();

    // Set safe defaults
    V->Type = 0;
    V->shader_id = 0;

    // Set bounding box to zero (won't be visible)
    V->vis.box.set(Fvector().set(0,0,0), Fvector().set(0,0,0));
    V->vis.sphere.set(Fvector().set(0,0,0), 0.0f);

    V->dbg_name = "dummy_visual";

    Msg("[Vulkan] Created dummy visual (placeholder for failed load)");

    return V;
}
