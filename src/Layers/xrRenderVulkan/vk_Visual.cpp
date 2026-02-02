// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// SPDX-License-Identifier: MIT

#include "stdafx.h"
#include "vk_Visual.h"
#include "vk_R_Backend.h"
#include "vk_buffer_pool.h"
#include "vk_shader.h"      // Phase 2.34: Shader binding
#include "vk_material.h"    // Phase 2.34: Material binding
#include "vk_pipeline.h"    // For g_PipelineManager (pipeline switching)
#include "vk_rendertarget.h" // For RTarget->GetGBufferPipeline()
#include "rvk.h"
#include "../xrRender/SkeletonCustom.h"  // For CKinematics bone transforms
#include "3DFluid/vk3DFluidVolume.h"     // Phase 0: 3D Fluid system

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
    if (p_rm_Vertices)
    {
        xr_delete(p_rm_Vertices);
        p_rm_Vertices = nullptr;
    }

    if (p_rm_Indices)
    {
        xr_delete(p_rm_Indices);
        p_rm_Indices = nullptr;
    }

    if (m_fast)
    {
        xr_delete(m_fast);
        m_fast = nullptr;
    }

    vBase = vCount = vStride = 0;
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
    }

    // Fallback: if no OGF_TEXTURE chunk (level geometry), use shader_id from header
    if (!m_pMaterial && RImplementation.Shaders.size() > 0)
    {
        if (shader_id < (u16)RImplementation.Shaders.size())
        {
            VK::CVulkanShader* pShader = RImplementation.Shaders[shader_id];
            if (pShader)
                m_pMaterial = pShader->GetMaterial();
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

    // Load geometry
    LoadGeometry(data);

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
    m_mesh.p_rm_Vertices = src->m_mesh.p_rm_Vertices;
    m_mesh.vBase = src->m_mesh.vBase;
    m_mesh.vCount = src->m_mesh.vCount;
    m_mesh.vStride = src->m_mesh.vStride;

    m_mesh.p_rm_Indices = src->m_mesh.p_rm_Indices;
    m_mesh.iBase = src->m_mesh.iBase;
    m_mesh.iCount = src->m_mesh.iCount;
    m_mesh.iType = src->m_mesh.iType;

    m_mesh.dwPrimitives = src->m_mesh.dwPrimitives;

    // Copy fast-path reference
    m_mesh.m_fast = src->m_mesh.m_fast;
}

void vkFVisual::Render(float LOD)
{
    if (!m_mesh.IsValid())
        return;

    // Supported vertex strides:
    //   32 = level static (FLOAT3 pos + D3DCOLOR normal @12 + SHORT2 UV @24)
    //   36 = skinned 1W   (FLOAT4 pos + D3DCOLOR normal @16 + FLOAT2 UV @28)
    //   40 = skinned 4W   (FLOAT4 pos + D3DCOLOR normal @16 + FLOAT2 UV @28)
    //   44 = skinned 2W/3W(FLOAT4 pos + D3DCOLOR normal @16 + FLOAT2 UV @28)
    u32 stride = m_mesh.vStride;
    if (stride != 32 && stride != 36 && stride != 40 && stride != 44)
        return;

    // Both buffers must be valid to render
    if (!m_mesh.p_rm_Vertices || !m_mesh.p_rm_Indices)
        return;

    // Switch G-Buffer pipeline if stride changed
    if (stride != RCache.m_CurrentGBufStride)
    {
        VkPipeline pipeline = RTarget->GetGBufferPipeline(stride);
        if (pipeline != VK_NULL_HANDLE)
        {
            RCache.set_Pipeline(pipeline);

            // Update UV scale push constant at offset 192
            float uvScale = (stride == 32) ? (1.0f / 1024.0f) : 1.0f;
            VkCommandBuffer cmd = RCache.GetCommandBuffer();
            if (cmd != VK_NULL_HANDLE)
            {
                VkPipelineLayout layout = VK::g_PipelineManager->GetLayout();
                vkCmdPushConstants(cmd, layout,
                    VK_SHADER_STAGE_VERTEX_BIT, 192, sizeof(float), &uvScale);
            }

            RCache.m_CurrentGBufStride = stride;
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

void vkFVisual::LoadGeometry(IReader* data)
{
    // Try OGF_GCONTAINER first (shared buffers - most common)
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

        m_mesh.vBase = vb_offset;
        m_mesh.vCount = vb_count;
        m_mesh.iBase = ib_offset;
        m_mesh.iCount = ib_count;
        m_mesh.dwPrimitives = ib_count / 3;

        // Get buffer references from level's shared buffer pool (Phase 2.23.3)
        if (VK::g_BufferPool)
        {
            m_mesh.p_rm_Vertices = VK::g_BufferPool->GetVertexBuffer(vb_id);
            m_mesh.p_rm_Indices = VK::g_BufferPool->GetIndexBuffer(ib_id);

            if (m_mesh.p_rm_Vertices)
                m_mesh.vStride = VK::g_BufferPool->GetVertexStride(vb_id);

            if (m_mesh.p_rm_Indices)
                m_mesh.iType = VK::g_BufferPool->GetIndexType(ib_id);

            if (!m_mesh.p_rm_Vertices || !m_mesh.p_rm_Indices)
            {
                Msg("! [Vulkan] Visual '%s' geometry: VB[%u] or IB[%u] not found in pool",
                    dbg_name.c_str(), vb_id, ib_id);
            }
        }
        else
        {
            Msg("! [Vulkan] BufferPool not initialized!");
        }

        return;
    }

    // Try inline vertices (OGF_VERTICES + OGF_INDICES)
    if (data->find_chunk(OGF_VERTICES))
    {
        u32 vert_format = data->r_u32();
        u32 vert_count = data->r_u32();

        // Calculate stride based on format
        // TODO: Parse vertex format properly
        m_mesh.vStride = 32; // Default assumption: position + normal + UV
        m_mesh.vCount = vert_count;
        m_mesh.vBase = 0;

        // Read vertex data
        u32 data_size = vert_count * m_mesh.vStride;

        // Create vertex buffer
        m_mesh.p_rm_Vertices = xr_new<VK::CVulkanBuffer>();
        m_mesh.p_rm_Vertices->Create(
            data_size,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE
        );

        // Read and upload vertex data
        xr_vector<u8> vdata(data_size);
        data->r(vdata.data(), data_size);
        m_mesh.p_rm_Vertices->Upload(vdata.data(), data_size);
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

    // Calculate LOD index
    u32 lod_idx = 0;
    if (LOD >= 0.f && LOD <= 1.f)
    {
        lod_idx = iFloor(LOD * (sw_count - 1) + 0.5f);
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
    if (m_mesh.p_rm_Vertices)
        RCache.set_Vertices(m_mesh.p_rm_Vertices->GetHandle(), m_mesh.vStride);
    if (m_mesh.p_rm_Indices)
        RCache.set_Indices(m_mesh.p_rm_Indices->GetHandle(), m_mesh.iType);

    // Draw with LOD-specific index range
    u32 start_idx = sw_offsets[lod_idx];
    u32 idx_count = sw_counts[lod_idx];
    u32 prim_count = idx_count / 3;

    RCache.Render(4, m_mesh.vBase, 0, m_mesh.vCount, start_idx, prim_count);
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
                sw_offsets[i] = data->r_u32();  // offset
                data->r_u16();                   // num_tris (unused, we calc from count)
                sw_counts[i] = data->r_u16() * 3; // num_verts -> index count
            }
        }
    }
    else
    {
        // No sliding window - use full mesh
        sw_count = 1;
        sw_offsets = xr_alloc<u32>(1);
        sw_counts = xr_alloc<u32>(1);
        sw_offsets[0] = m_mesh.iBase;
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

void vkFTreeVisual::Render(float LOD)
{
    // Push tree's xform as model matrix (offset 0, 64 bytes)
    VkCommandBuffer cmd = RCache.GetCommandBuffer();
    if (cmd != VK_NULL_HANDLE)
    {
        VkPipelineLayout layout = VK::g_PipelineManager->GetLayout();
        vkCmdPushConstants(cmd, layout,
            VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Fmatrix), &xform);
    }

    // Render geometry (binds material, vertex/index buffers, draws)
    vkFVisual::Render(LOD);

    // Restore identity model matrix for subsequent non-tree visuals
    Fmatrix identity;
    identity.identity();
    if (cmd != VK_NULL_HANDLE)
    {
        VkPipelineLayout layout = VK::g_PipelineManager->GetLayout();
        vkCmdPushConstants(cmd, layout,
            VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Fmatrix), &identity);
    }
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
                sw_offsets[i] = data->r_u32();
                data->r_u16();
                sw_counts[i] = data->r_u16() * 3;
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

    // LOD selection
    u32 lod_idx = 0;
    if (LOD >= 0.f && LOD <= 1.f)
    {
        lod_idx = iFloor(LOD * (sw_count - 1) + 0.5f);
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

    if (m_mesh.p_rm_Vertices)
        RCache.set_Vertices(m_mesh.p_rm_Vertices->GetHandle(), m_mesh.vStride);
    if (m_mesh.p_rm_Indices)
        RCache.set_Indices(m_mesh.p_rm_Indices->GetHandle(), m_mesh.iType);

    u32 start_idx = sw_offsets[lod_idx];
    u32 idx_count = sw_counts[lod_idx];
    u32 prim_count = idx_count / 3;

    RCache.Render(4, m_mesh.vBase, 0, m_mesh.vCount, start_idx, prim_count);
    RCache.stat.polys += prim_count;
    RCache.stat.verts += m_mesh.vCount;

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
    vkFVisual::Load(name, data, flags);

    // ========================================================================
    // Load bone data from OGF_VERTICES chunk
    // ========================================================================
    // This determines the rendering mode (single bone vs multi-bone skinning)
    // Based on DX implementation in SkeletonX.cpp:228-400

    data->seek(0);  // Reset to start
    IReader* V = data->open_chunk(OGF_VERTICES);
    if (!V) {
        Msg("![Vulkan] vkSkeletonX_ST::Load() - no OGF_VERTICES chunk");
        RenderMode = RM_SINGLE;
        RMS_boneid = 0;
        return;
    }

    u32 dwVertType = V->r_u32();
    u32 dwVertCount = V->r_u32();

    xr_vector<u16> bids;  // Bone IDs used by this mesh
    u16 sw_bones_cnt = 0;

    // Analyze vertex format to determine rendering mode
    switch (dwVertType)
    {
    case OGF_VERTEXFORMAT_FVF_1L:  // 1-Link (1 bone per vertex)
    case 1:
        {
            struct vertBoned1W { Fvector P; Fvector3 N; Fvector3 T; Fvector3 B; Fvector2 tc; u32 matrix; };
            vertBoned1W* pVO = (vertBoned1W*)V->pointer();

            // Collect all bone IDs used
            for (u32 it = 0; it < dwVertCount; ++it)
            {
                const vertBoned1W& VB = pVO[it];
                u16 mid = (u16)VB.matrix;

                if (bids.end() == std::find(bids.begin(), bids.end(), mid))
                    bids.push_back(mid);

                sw_bones_cnt = _max(sw_bones_cnt, mid);
            }

            // Determine rendering mode based on bone count
            if (1 == bids.size())
            {
                // Single bone - rigid attachment
                RenderMode = RM_SINGLE;
                RMS_boneid = *bids.begin();
            }
            else
            {
                // Multiple bones - GPU skinning (1 weight per vertex)
                RenderMode = RM_SKINNING_1B;
                RMS_bonecount = sw_bones_cnt + 1;
                BonesUsed = (u16)bids.size();
            }
        }
        break;

    case OGF_VERTEXFORMAT_FVF_2L:  // 2-Link (2 bones per vertex)
    case 2:
        {
            struct vertBoned2W { Fvector P; Fvector3 N; Fvector3 T; Fvector3 B; Fvector2 tc; u16 matrix0; u16 matrix1; float w; };
            vertBoned2W* pVO = (vertBoned2W*)V->pointer();

            for (u32 it = 0; it < dwVertCount; ++it)
            {
                const vertBoned2W& VB = pVO[it];
                sw_bones_cnt = _max(sw_bones_cnt, VB.matrix0);
                sw_bones_cnt = _max(sw_bones_cnt, VB.matrix1);

                if (bids.end() == std::find(bids.begin(), bids.end(), VB.matrix0))
                    bids.push_back(VB.matrix0);
                if (bids.end() == std::find(bids.begin(), bids.end(), VB.matrix1))
                    bids.push_back(VB.matrix1);
            }

            RenderMode = RM_SKINNING_2B;
            RMS_bonecount = sw_bones_cnt + 1;
            BonesUsed = (u16)bids.size();
        }
        break;

    case OGF_VERTEXFORMAT_FVF_3L:  // 3-Link (3 bones per vertex)
    case 3:
        {
            struct vertBoned3W { Fvector P; Fvector3 N; Fvector3 T; Fvector3 B; Fvector2 tc; u16 m[3]; float w[2]; };
            vertBoned3W* pVO = (vertBoned3W*)V->pointer();

            for (u32 it = 0; it < dwVertCount; ++it)
            {
                const vertBoned3W& VB = pVO[it];
                for (int k = 0; k < 3; k++)
                {
                    sw_bones_cnt = _max(sw_bones_cnt, VB.m[k]);
                    if (bids.end() == std::find(bids.begin(), bids.end(), VB.m[k]))
                        bids.push_back(VB.m[k]);
                }
            }

            RenderMode = RM_SKINNING_3B;
            RMS_bonecount = sw_bones_cnt + 1;
            BonesUsed = (u16)bids.size();
        }
        break;

    case OGF_VERTEXFORMAT_FVF_4L:  // 4-Link (4 bones per vertex)
    case 4:
        {
            struct vertBoned4W { Fvector P; Fvector3 N; Fvector3 T; Fvector3 B; Fvector2 tc; u16 m[4]; float w[3]; };
            vertBoned4W* pVO = (vertBoned4W*)V->pointer();

            for (u32 it = 0; it < dwVertCount; ++it)
            {
                const vertBoned4W& VB = pVO[it];
                for (int k = 0; k < 4; k++)
                {
                    sw_bones_cnt = _max(sw_bones_cnt, VB.m[k]);
                    if (bids.end() == std::find(bids.begin(), bids.end(), VB.m[k]))
                        bids.push_back(VB.m[k]);
                }
            }

            RenderMode = RM_SKINNING_4B;
            RMS_bonecount = sw_bones_cnt + 1;
            BonesUsed = (u16)bids.size();
        }
        break;

    default:
        Msg("![Vulkan] vkSkeletonX_ST::Load() - unknown vertex type %d", dwVertType);
        RenderMode = RM_SINGLE;
        RMS_boneid = 0;
        break;
    }

    V->close();
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
    // Safety check
    if (!Parent) {
        Msg("![Vulkan] vkSkeletonX_ST::Render() - no Parent skeleton");
        vkFVisual::Render(LOD);
        return;
    }

    // Apply bone transforms based on rendering mode
    switch (RenderMode)
    {
    case RM_SINGLE:
        {
            // Single-bone rendering: multiply world matrix by bone transform
            // This is used for rigid attachments (weapons, backpacks, etc.)
            Fmatrix W;
            W.mul_43(RCache.xforms.m_w, Parent->LL_GetTransform_R(RMS_boneid));
            RCache.set_xform_world(W);

            // Render with modified world matrix
            vkFVisual::Render(LOD);

            // Restore original world matrix
            RCache.set_xform_world(RCache.xforms.m_w);
        }
        break;

    case RM_SKINNING_1B:
    case RM_SKINNING_2B:
    case RM_SKINNING_3B:
    case RM_SKINNING_4B:
        {
            // GPU skinning: build bone matrix array and pass to shader
            // Each vertex is influenced by 1-4 bones (depending on mode)
            // Vertex shader will blend bone transforms based on bone indices and weights

            u32 bones_count = RMS_bonecount;
            if (bones_count == 0 || bones_count > 256) {
                Msg("![Vulkan] vkSkeletonX_ST::Render() - invalid bone count %d", bones_count);
                vkFVisual::Render(LOD);
                return;
            }

            // Build bone matrix array (world * bone_transform for each bone)
            Fmatrix* bones_array = (Fmatrix*)_alloca(bones_count * sizeof(Fmatrix));
            for (u32 i = 0; i < bones_count; i++)
            {
                bones_array[i].mul_43(RCache.xforms.m_w, Parent->LL_GetTransform_R(i));
            }

            // Upload bone matrices to uniform buffer
            // This will be accessible in vertex shader as:
            //   layout(binding = X) uniform BoneMatrices {
            //       mat4 sbones_array[256];
            //   };
            RCache.set_ca(nullptr, 0, bones_count, bones_array);

            // Render with GPU skinning
            // The vertex shader will apply bone transforms:
            //   vec4 pos = vec4(0.0);
            //   for (int i = 0; i < BONE_COUNT; i++) {
            //       pos += sbones_array[boneIndices[i]] * vec4(inPosition, 1.0) * boneWeights[i];
            //   }
            vkFVisual::Render(LOD);
        }
        break;

    case RM_SKINNING_SOFT:
        // CPU-side skinning (fallback for old hardware)
        // Not implemented - this is rarely used in modern rendering
        Msg("![Vulkan] vkSkeletonX_ST::Render() - soft skinning not implemented");
        vkFVisual::Render(LOD);
        break;

    default:
        // Unknown mode - render as static
        Msg("![Vulkan] vkSkeletonX_ST::Render() - unknown RenderMode %d", RenderMode);
        vkFVisual::Render(LOD);
        break;
    }
}

void vkSkeletonX_ST::AfterLoad(CKinematics* parent, u16 child_idx)
{
    SetParent(parent);
    ChildIDX = child_idx;
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
    vkFProgressive::Load(name, data, flags);

    // ========================================================================
    // Load bone data from OGF_VERTICES chunk
    // ========================================================================

    data->seek(0);
    IReader* V = data->open_chunk(OGF_VERTICES);
    if (!V) {
        Msg("![Vulkan] vkSkeletonX_PM::Load() - no OGF_VERTICES chunk");
        RenderMode = RM_SINGLE;
        RMS_boneid = 0;
        return;
    }

    u32 dwVertType = V->r_u32();
    u32 dwVertCount = V->r_u32();

    xr_vector<u16> bids;
    u16 sw_bones_cnt = 0;

    switch (dwVertType)
    {
    case OGF_VERTEXFORMAT_FVF_1L:
    case 1:
        {
            struct vertBoned1W { Fvector P; Fvector3 N; Fvector3 T; Fvector3 B; Fvector2 tc; u32 matrix; };
            vertBoned1W* pVO = (vertBoned1W*)V->pointer();

            for (u32 it = 0; it < dwVertCount; ++it)
            {
                const vertBoned1W& VB = pVO[it];
                u16 mid = (u16)VB.matrix;

                if (bids.end() == std::find(bids.begin(), bids.end(), mid))
                    bids.push_back(mid);

                sw_bones_cnt = _max(sw_bones_cnt, mid);
            }

            if (1 == bids.size())
            {
                RenderMode = RM_SINGLE;
                RMS_boneid = *bids.begin();
            }
            else
            {
                RenderMode = RM_SKINNING_1B;
                RMS_bonecount = sw_bones_cnt + 1;
                BonesUsed = (u16)bids.size();
            }
        }
        break;

    case OGF_VERTEXFORMAT_FVF_2L:
    case 2:
        {
            struct vertBoned2W { Fvector P; Fvector3 N; Fvector3 T; Fvector3 B; Fvector2 tc; u16 matrix0; u16 matrix1; float w; };
            vertBoned2W* pVO = (vertBoned2W*)V->pointer();

            for (u32 it = 0; it < dwVertCount; ++it)
            {
                const vertBoned2W& VB = pVO[it];
                sw_bones_cnt = _max(sw_bones_cnt, VB.matrix0);
                sw_bones_cnt = _max(sw_bones_cnt, VB.matrix1);

                if (bids.end() == std::find(bids.begin(), bids.end(), VB.matrix0))
                    bids.push_back(VB.matrix0);
                if (bids.end() == std::find(bids.begin(), bids.end(), VB.matrix1))
                    bids.push_back(VB.matrix1);
            }

            RenderMode = RM_SKINNING_2B;
            RMS_bonecount = sw_bones_cnt + 1;
            BonesUsed = (u16)bids.size();
        }
        break;

    case OGF_VERTEXFORMAT_FVF_3L:
    case 3:
        {
            struct vertBoned3W { Fvector P; Fvector3 N; Fvector3 T; Fvector3 B; Fvector2 tc; u16 m[3]; float w[2]; };
            vertBoned3W* pVO = (vertBoned3W*)V->pointer();

            for (u32 it = 0; it < dwVertCount; ++it)
            {
                const vertBoned3W& VB = pVO[it];
                for (int k = 0; k < 3; k++)
                {
                    sw_bones_cnt = _max(sw_bones_cnt, VB.m[k]);
                    if (bids.end() == std::find(bids.begin(), bids.end(), VB.m[k]))
                        bids.push_back(VB.m[k]);
                }
            }

            RenderMode = RM_SKINNING_3B;
            RMS_bonecount = sw_bones_cnt + 1;
            BonesUsed = (u16)bids.size();
        }
        break;

    case OGF_VERTEXFORMAT_FVF_4L:
    case 4:
        {
            struct vertBoned4W { Fvector P; Fvector3 N; Fvector3 T; Fvector3 B; Fvector2 tc; u16 m[4]; float w[3]; };
            vertBoned4W* pVO = (vertBoned4W*)V->pointer();

            for (u32 it = 0; it < dwVertCount; ++it)
            {
                const vertBoned4W& VB = pVO[it];
                for (int k = 0; k < 4; k++)
                {
                    sw_bones_cnt = _max(sw_bones_cnt, VB.m[k]);
                    if (bids.end() == std::find(bids.begin(), bids.end(), VB.m[k]))
                        bids.push_back(VB.m[k]);
                }
            }

            RenderMode = RM_SKINNING_4B;
            RMS_bonecount = sw_bones_cnt + 1;
            BonesUsed = (u16)bids.size();
        }
        break;

    default:
        Msg("![Vulkan] vkSkeletonX_PM::Load() - unknown vertex type %d", dwVertType);
        RenderMode = RM_SINGLE;
        RMS_boneid = 0;
        break;
    }

    V->close();
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
    // Safety check
    if (!Parent) {
        Msg("![Vulkan] vkSkeletonX_PM::Render() - no Parent skeleton");
        vkFProgressive::Render(LOD);
        return;
    }

    // Apply bone transforms based on rendering mode
    switch (RenderMode)
    {
    case RM_SINGLE:
        {
            // Single-bone rendering: multiply world matrix by bone transform
            Fmatrix W;
            W.mul_43(RCache.xforms.m_w, Parent->LL_GetTransform_R(RMS_boneid));
            RCache.set_xform_world(W);

            // Render with modified world matrix
            vkFProgressive::Render(LOD);

            // Restore original world matrix
            RCache.set_xform_world(RCache.xforms.m_w);
        }
        break;

    case RM_SKINNING_1B:
    case RM_SKINNING_2B:
    case RM_SKINNING_3B:
    case RM_SKINNING_4B:
        {
            // GPU skinning: build bone matrix array and pass to shader
            u32 bones_count = RMS_bonecount;
            if (bones_count == 0 || bones_count > 256) {
                Msg("![Vulkan] vkSkeletonX_PM::Render() - invalid bone count %d", bones_count);
                vkFProgressive::Render(LOD);
                return;
            }

            // Build bone matrix array (world * bone_transform for each bone)
            Fmatrix* bones_array = (Fmatrix*)_alloca(bones_count * sizeof(Fmatrix));
            for (u32 i = 0; i < bones_count; i++)
            {
                bones_array[i].mul_43(RCache.xforms.m_w, Parent->LL_GetTransform_R(i));
            }

            // Upload bone matrices to uniform buffer
            RCache.set_ca(nullptr, 0, bones_count, bones_array);

            // Render with GPU skinning (progressive LOD)
            vkFProgressive::Render(LOD);
        }
        break;

    case RM_SKINNING_SOFT:
        // CPU-side skinning not implemented
        Msg("![Vulkan] vkSkeletonX_PM::Render() - soft skinning not implemented");
        vkFProgressive::Render(LOD);
        break;

    default:
        // Unknown mode - render as static
        Msg("![Vulkan] vkSkeletonX_PM::Render() - unknown RenderMode %d", RenderMode);
        vkFProgressive::Render(LOD);
        break;
    }
}

void vkSkeletonX_PM::AfterLoad(CKinematics* parent, u16 child_idx)
{
    SetParent(parent);
    ChildIDX = child_idx;
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
