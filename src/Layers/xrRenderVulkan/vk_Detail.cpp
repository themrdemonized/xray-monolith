// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// SPDX-License-Identifier: MIT

#include "stdafx.h"
#include "vk_Detail.h"
#include "rvk.h"

namespace VK
{

// ============================================================================
// CDetail - Constructor/Destructor
// ============================================================================
CDetail::CDetail()
{
    m_VertexBuffer = nullptr;
    m_IndexBuffer = nullptr;
    m_Shader = nullptr;
    m_VertexCount = 0;
    m_IndexCount = 0;
    m_VertexStride = sizeof(Vertex);
    m_Flags = 0;
    m_MinScale = 0.5f;
    m_MaxScale = 1.5f;
    bv_sphere.P.set(0, 0, 0);
    bv_sphere.R = 0;
    bv_bb.invalidate();
}

CDetail::~CDetail()
{
    Unload();
}

// ============================================================================
// Load - Load detail geometry from .details file
// Ported from DetailModel.cpp - uses correct fvfVertexIn format
// ============================================================================
void CDetail::Load(IReader* S)
{
    // Read shader and texture names (original format: 2 stringZ fields)
    string256 fnS, fnT;
    S->r_stringZ(fnS, sizeof(fnS));
    S->r_stringZ(fnT, sizeof(fnT));

    // Store texture name for reference
    m_Name = fnT;

    // Read params
    m_Flags = S->r_u32();
    m_MinScale = S->r_float();
    m_MaxScale = S->r_float();

    // Read vertices and indices counts
    u32 vCount = S->r_u32();
    u32 iCount = S->r_u32();
    m_VertexCount = vCount;
    m_IndexCount = iCount;

    R_ASSERT(0 == (iCount % 3));

    if (vCount == 0)
    {
        Msg("![Vulkan] Detail %s has 0 vertices", m_Name.c_str());
        return;
    }

    // ========================================================================
    // Read raw vertex data
    // DX11 fvfVertexIn format: { Fvector P; float u, v; } = 20 bytes
    // NOT short u,v! The UV coords are stored as floats.
    // ========================================================================
    struct fvfVertexIn
    {
        Fvector P;
        float u, v;
    };
    u32 size_vertices = vCount * sizeof(fvfVertexIn);
    xr_vector<fvfVertexIn> rawVerts(vCount);
    S->r(rawVerts.data(), size_vertices);

    // Read index data
    xr_vector<u16> indices(iCount);
    S->r(indices.data(), iCount * sizeof(u16));

    // Calculate bounding box for height normalization and collision
    bv_bb.invalidate();
    for (u32 i = 0; i < vCount; i++)
        bv_bb.modify(rawVerts[i].P);
    bv_bb.getsphere(bv_sphere.P, bv_sphere.R);

    // ========================================================================
    // Convert to Vulkan vertex format
    // ========================================================================
    xr_vector<Vertex> vertices(vCount);
    float minY = bv_bb.min.y;
    float maxY = bv_bb.max.y;
    float rangeY = maxY - minY;

    for (u32 i = 0; i < vCount; i++)
    {
        vertices[i].pos = rawVerts[i].P;

        // UV coords are already float in the source format - use directly
        vertices[i].uv.x = rawVerts[i].u;
        vertices[i].uv.y = rawVerts[i].v;

        // Height for wind animation (normalized Y position)
        vertices[i].height = (rangeY > 0.01f) ? ((rawVerts[i].P.y - minY) / rangeY) : 0.0f;
    }

    if (iCount == 0)
    {
        Msg("![Vulkan] Detail %s has 0 indices", m_Name.c_str());
        return;
    }

    // Create Vulkan vertex buffer
    u32 vbSize = vCount * sizeof(Vertex);
    m_VertexBuffer = xr_new<VK::CVulkanBuffer>();
    m_VertexBuffer->Create(
        vbSize,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE
    );
    m_VertexBuffer->Upload(vertices.data(), vbSize);

    // Create Vulkan index buffer
    u32 ibSize = iCount * sizeof(u16);
    m_IndexBuffer = xr_new<VK::CVulkanBuffer>();
    m_IndexBuffer->Create(
        ibSize,
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE
    );
    m_IndexBuffer->Upload(indices.data(), ibSize);

    Msg("[Vulkan] Detail loaded: %s (%d verts, %d indices, scale %.1f-%.1f)",
        m_Name.c_str(), vCount, iCount, m_MinScale, m_MaxScale);
}

// ============================================================================
// Unload - Release Vulkan resources
// ============================================================================
void CDetail::Unload()
{
    if (m_VertexBuffer)
    {
        m_VertexBuffer->Destroy();
        xr_delete(m_VertexBuffer);
    }

    if (m_IndexBuffer)
    {
        m_IndexBuffer->Destroy();
        xr_delete(m_IndexBuffer);
    }

    m_Shader = nullptr;
    m_Texture = nullptr;
}

} // namespace VK
