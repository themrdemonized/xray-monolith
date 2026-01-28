#ifndef vk_d3d_skeleton_compat_H
#define vk_d3d_skeleton_compat_H
#pragma once

// Minimal DirectX type emulation for Skeleton classes
// These types are ONLY used by CKinematics/CKinematicsAnimated for data storage
// They don't interact with actual rendering in Vulkan

// Fake D3D buffer types (just opaque pointers for Skeleton classes)
typedef void* ID3DVertexBuffer;
typedef void* ID3DIndexBuffer;
// ref_geom is defined in Shader.h - don't redefine it here

// Fake D3D shader types (needed by sh_atomic.h when including Skeleton headers)
typedef void* ID3DVertexShader;
typedef void* ID3DPixelShader;
typedef void* ID3DGeometryShader;
typedef void* ID3DBaseTexture;
typedef void* ID3DState;  // For SState in sh_atomic.h
typedef void* ID3DShaderResourceView;  // For sh_texture.h
typedef void* ID3DTexture2D;  // For sh_rt.h
typedef void* ID3DRenderTargetView;  // For sh_rt.h
typedef void* ID3DDepthStencilView;  // For sh_rt.h

// Fake D3D descriptor structures (just empty structs, never used)
struct D3D_TEXTURE2D_DESC {
    unsigned int Width;
    unsigned int Height;
    unsigned int MipLevels;
    unsigned int ArraySize;
    unsigned int Format;
    unsigned int SampleCount;
    unsigned int SampleQuality;
    unsigned int Usage;
    unsigned int BindFlags;
    unsigned int CPUAccessFlags;
    unsigned int MiscFlags;
};

#endif // vk_d3d_skeleton_compat_H
