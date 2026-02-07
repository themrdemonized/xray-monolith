// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// SPDX-License-Identifier: MIT

#pragma once
// ============================================================================
// D3D9 Compatibility Helper Functions for Vulkan Renderer
//
// These functions work with D3DVERTEXELEMENT9 structures to parse level files.
// The D3D9 types themselves come from d3d9types.h (via engine headers).
// When D3D9 is removed from the project, uncomment the type definitions below.
//
// Based on stalker-cordisproject/src/Common/PlatformLinux.inl
// ============================================================================

#ifndef VK_D3D_COMPAT_H
#define VK_D3D_COMPAT_H

// ============================================================================
// D3D9 Type Definitions (uncomment when removing d3d9 dependency)
// ============================================================================
/*
// These are only needed when building without d3d9types.h

typedef enum _D3DDECLTYPE {
    D3DDECLTYPE_FLOAT1    =  0,
    D3DDECLTYPE_FLOAT2    =  1,
    D3DDECLTYPE_FLOAT3    =  2,
    D3DDECLTYPE_FLOAT4    =  3,
    D3DDECLTYPE_D3DCOLOR  =  4,
    D3DDECLTYPE_UBYTE4    =  5,
    D3DDECLTYPE_SHORT2    =  6,
    D3DDECLTYPE_SHORT4    =  7,
    D3DDECLTYPE_UBYTE4N   =  8,
    D3DDECLTYPE_SHORT2N   =  9,
    D3DDECLTYPE_SHORT4N   = 10,
    D3DDECLTYPE_USHORT2N  = 11,
    D3DDECLTYPE_USHORT4N  = 12,
    D3DDECLTYPE_UDEC3     = 13,
    D3DDECLTYPE_DEC3N     = 14,
    D3DDECLTYPE_FLOAT16_2 = 15,
    D3DDECLTYPE_FLOAT16_4 = 16,
    D3DDECLTYPE_UNUSED    = 17,
} D3DDECLTYPE;

typedef enum _D3DDECLMETHOD {
    D3DDECLMETHOD_DEFAULT          = 0,
    D3DDECLMETHOD_PARTIALU         = 1,
    D3DDECLMETHOD_PARTIALV         = 2,
    D3DDECLMETHOD_CROSSUV          = 3,
    D3DDECLMETHOD_UV               = 4,
    D3DDECLMETHOD_LOOKUP           = 5,
    D3DDECLMETHOD_LOOKUPPRESAMPLED = 6
} D3DDECLMETHOD;

typedef enum _D3DDECLUSAGE {
    D3DDECLUSAGE_POSITION     = 0,
    D3DDECLUSAGE_BLENDWEIGHT  = 1,
    D3DDECLUSAGE_BLENDINDICES = 2,
    D3DDECLUSAGE_NORMAL       = 3,
    D3DDECLUSAGE_PSIZE        = 4,
    D3DDECLUSAGE_TEXCOORD     = 5,
    D3DDECLUSAGE_TANGENT      = 6,
    D3DDECLUSAGE_BINORMAL     = 7,
    D3DDECLUSAGE_TESSFACTOR   = 8,
    D3DDECLUSAGE_POSITIONT    = 9,
    D3DDECLUSAGE_COLOR        = 10,
    D3DDECLUSAGE_FOG          = 11,
    D3DDECLUSAGE_DEPTH        = 12,
    D3DDECLUSAGE_SAMPLE       = 13
} D3DDECLUSAGE;

typedef struct _D3DVERTEXELEMENT9 {
    WORD    Stream;
    WORD    Offset;
    BYTE    Type;
    BYTE    Method;
    BYTE    Usage;
    BYTE    UsageIndex;
} D3DVERTEXELEMENT9, *LPD3DVERTEXELEMENT9;

#define MAXD3DDECLLENGTH 64
#define D3DDECL_END() {0xFF, 0, D3DDECLTYPE_UNUSED, 0, 0, 0}
*/

// ============================================================================
// Helper Functions for Vertex Declarations
// ============================================================================

// Get size in bytes for a D3D declaration type
inline u32 VK_GetDeclTypeSize(BYTE type)
{
    switch (type)
    {
    case D3DDECLTYPE_FLOAT1:    return 4;
    case D3DDECLTYPE_FLOAT2:    return 8;
    case D3DDECLTYPE_FLOAT3:    return 12;
    case D3DDECLTYPE_FLOAT4:    return 16;
    case D3DDECLTYPE_D3DCOLOR:  return 4;
    case D3DDECLTYPE_UBYTE4:    return 4;
    case D3DDECLTYPE_SHORT2:    return 4;
    case D3DDECLTYPE_SHORT4:    return 8;
    case D3DDECLTYPE_UBYTE4N:   return 4;
    case D3DDECLTYPE_SHORT2N:   return 4;
    case D3DDECLTYPE_SHORT4N:   return 8;
    case D3DDECLTYPE_USHORT2N:  return 4;
    case D3DDECLTYPE_USHORT4N:  return 8;
    case D3DDECLTYPE_UDEC3:     return 4;
    case D3DDECLTYPE_DEC3N:     return 4;
    case D3DDECLTYPE_FLOAT16_2: return 4;
    case D3DDECLTYPE_FLOAT16_4: return 8;
    default:                    return 0;
    }
}

// Count elements in vertex declaration (excluding END marker)
inline u32 VK_GetDeclLength(const D3DVERTEXELEMENT9* dcl)
{
    u32 count = 0;
    while (dcl[count].Stream != 0xFF)  // D3DDECL_END() sets Stream to 0xFF
        count++;
    return count;
}

// Calculate vertex size from declaration for a specific stream
inline u32 VK_GetDeclVertexSize(const D3DVERTEXELEMENT9* dcl, UINT stream = 0)
{
    u32 size = 0;
    for (u32 i = 0; dcl[i].Stream != 0xFF; i++)
    {
        if (dcl[i].Stream == stream)
        {
            u32 offset = dcl[i].Offset + VK_GetDeclTypeSize(dcl[i].Type);
            if (offset > size)
                size = offset;
        }
    }
    return size;
}

// ============================================================================
// FVF (Flexible Vertex Format) Helpers
// ============================================================================

// Compute vertex stride from D3D FVF flags (mirrors D3DXGetFVFVertexSize)
inline u32 VK_GetFVFVertexSize(u32 fvf)
{
    u32 size = 0;
    switch (fvf & D3DFVF_POSITION_MASK) {
    case D3DFVF_XYZ:    size += 12; break;
    case D3DFVF_XYZRHW: size += 16; break;
    case D3DFVF_XYZB1:  size += 16; break;
    case D3DFVF_XYZB2:  size += 20; break;
    case D3DFVF_XYZB3:  size += 24; break;
    case D3DFVF_XYZB4:  size += 28; break;
    case D3DFVF_XYZB5:  size += 32; break;
    case D3DFVF_XYZW:   size += 16; break;
    }
    if (fvf & D3DFVF_NORMAL)   size += 12;
    if (fvf & D3DFVF_PSIZE)    size += 4;
    if (fvf & D3DFVF_DIFFUSE)  size += 4;
    if (fvf & D3DFVF_SPECULAR) size += 4;
    u32 texCount = (fvf & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT;
    size += texCount * 8;  // each tex coord set = FLOAT2
    return size;
}

// ============================================================================
// Vulkan Format Conversion
// ============================================================================

// Convert D3D declaration type to Vulkan format
inline VkFormat VK_DeclTypeToVkFormat(BYTE type)
{
    switch (type)
    {
    case D3DDECLTYPE_FLOAT1:    return VK_FORMAT_R32_SFLOAT;
    case D3DDECLTYPE_FLOAT2:    return VK_FORMAT_R32G32_SFLOAT;
    case D3DDECLTYPE_FLOAT3:    return VK_FORMAT_R32G32B32_SFLOAT;
    case D3DDECLTYPE_FLOAT4:    return VK_FORMAT_R32G32B32A32_SFLOAT;
    case D3DDECLTYPE_D3DCOLOR:  return VK_FORMAT_B8G8R8A8_UNORM;
    case D3DDECLTYPE_UBYTE4:    return VK_FORMAT_R8G8B8A8_UINT;
    case D3DDECLTYPE_SHORT2:    return VK_FORMAT_R16G16_SINT;
    case D3DDECLTYPE_SHORT4:    return VK_FORMAT_R16G16B16A16_SINT;
    case D3DDECLTYPE_UBYTE4N:   return VK_FORMAT_R8G8B8A8_UNORM;
    case D3DDECLTYPE_SHORT2N:   return VK_FORMAT_R16G16_SNORM;
    case D3DDECLTYPE_SHORT4N:   return VK_FORMAT_R16G16B16A16_SNORM;
    case D3DDECLTYPE_USHORT2N:  return VK_FORMAT_R16G16_UNORM;
    case D3DDECLTYPE_USHORT4N:  return VK_FORMAT_R16G16B16A16_UNORM;
    case D3DDECLTYPE_UDEC3:     return VK_FORMAT_A2B10G10R10_UINT_PACK32;
    case D3DDECLTYPE_DEC3N:     return VK_FORMAT_A2B10G10R10_SNORM_PACK32;
    case D3DDECLTYPE_FLOAT16_2: return VK_FORMAT_R16G16_SFLOAT;
    case D3DDECLTYPE_FLOAT16_4: return VK_FORMAT_R16G16B16A16_SFLOAT;
    default:                    return VK_FORMAT_UNDEFINED;
    }
}

// ============================================================================
// Vertex Declarator (svector-based container)
// ============================================================================
typedef svector<D3DVERTEXELEMENT9, MAXD3DDECLLENGTH + 1> VertexDeclarator;

#endif // VK_D3D_COMPAT_H
