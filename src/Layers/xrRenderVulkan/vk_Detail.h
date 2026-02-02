// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// SPDX-License-Identifier: MIT

#pragma once

#include "stdafx.h"
#include "vk_shader.h"
#include "vk_buffer.h"
#include "../xrRender/detailformat.h"

namespace VK
{

// ============================================================================
// CDetail - Single detail object type (grass model, debris model, etc.)
// Vulkan port of CDetail from DetailModel.h
// ============================================================================
class CDetail
{
public:
    // Geometry data
    VK::CVulkanBuffer*  m_VertexBuffer;     // Base mesh vertices (single copy)
    VK::CVulkanBuffer*  m_IndexBuffer;      // Base mesh indices
    u32                 m_VertexCount;      // Number of vertices
    u32                 m_IndexCount;       // Number of indices
    u32                 m_VertexStride;     // Stride in bytes

    // Material
    VK::CVulkanShader*  m_Shader;           // Shader reference
    ref_texture         m_Texture;          // Diffuse texture

    // Rendering parameters
    u32                 m_Flags;            // Detail flags
    float               m_MinScale;         // Minimum scale
    float               m_MaxScale;         // Maximum scale

    // Reference
    shared_str          m_Name;             // Detail name

public:
    CDetail();
    ~CDetail();

    void Load(IReader* S);                  // Load from .details file
    void Unload();                          // Release resources

    // Vertex structure for detail geometry
    struct Vertex
    {
        Fvector     pos;        // Local position
        Fvector2    uv;         // Texture coordinates
        float       height;     // Normalized height [0..1] for wind animation
    };
};

} // namespace VK
