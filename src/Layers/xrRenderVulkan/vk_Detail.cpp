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
}

CDetail::~CDetail()
{
    Unload();
}

// ============================================================================
// Load - Load detail geometry from .details file
// Ported from DetailModel.cpp
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

    // Create shader
    if (g_VulkanShaderManager)
    {
        m_Shader = g_VulkanShaderManager->CreateShader(fnS, fnT);
        if (!m_Shader)
        {
            Msg("![Vulkan] Failed to create detail shader: %s / %s", fnS, fnT);
            m_Shader = g_VulkanShaderManager->GetDefaultShader();
        }
    }

    // Read vertices and indices counts
    u32 vCount = S->r_u32();
    u32 iCount = S->r_u32();
    m_VertexCount = vCount;

    if (vCount == 0)
    {
        Msg("![Vulkan] Detail %s has 0 vertices", m_Name.c_str());
        m_IndexCount = iCount;
        return;
    }

    // Read raw vertex data (fvfVertexIn format: Fvector P + s16 u, s16 v)
    struct fvfVertexIn { Fvector P; short u, v; };
    u32 size_vertices = vCount * sizeof(fvfVertexIn);
    xr_vector<fvfVertexIn> rawVerts(vCount);
    S->r(rawVerts.data(), size_vertices);

    // Read index data
    m_IndexCount = iCount;
    xr_vector<u16> indices(iCount);
    S->r(indices.data(), iCount * sizeof(u16));

    // Calculate bounding box for height normalization
    Fbox bv_bb;
    bv_bb.invalidate();
    for (u32 i = 0; i < vCount; i++)
        bv_bb.modify(rawVerts[i].P);

    // Convert to Vulkan vertex format
    xr_vector<Vertex> vertices(vCount);
    for (u32 i = 0; i < vCount; i++)
    {
        vertices[i].pos = rawVerts[i].P;
        vertices[i].uv.x = float(rawVerts[i].u) / 32767.0f;
        vertices[i].uv.y = float(rawVerts[i].v) / 32767.0f;

        // Height for wind animation (normalized Y position)
        float minY = bv_bb.min.y;
        float maxY = bv_bb.max.y;
        float range = maxY - minY;
        vertices[i].height = (range > 0.01f) ? ((rawVerts[i].P.y - minY) / range) : 0.0f;
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

    Msg("[Vulkan] Detail loaded: %s (%d verts, %d indices)",
        m_Name.c_str(), vCount, iCount);
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
