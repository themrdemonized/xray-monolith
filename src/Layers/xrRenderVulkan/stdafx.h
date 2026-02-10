// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// Licensed under the same terms as X-Ray Engine (see root License.txt)

#pragma once

#include "../../xrEngine/stdafx.h"

#define XRRENDER_VULKAN_EXPORTS

// Render backend identifiers
#define R_R1    1
#define R_R2    2
#define R_R3    3
#define R_R4    4
#define R_VK    5
#define RENDER  R_VK

// Vulkan headers
#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>

// Vulkan Memory Allocator
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#define VMA_VULKAN_VERSION 1003000
#include "../../3rd party/vma/vk_mem_alloc.h"

// Forward declarations for DirectX types (for compatibility with shared headers)
// These are not used in Vulkan renderer, only for compilation
typedef void* ID3DTexture2D;
typedef void* ID3DVertexBuffer;
typedef void* ID3DIndexBuffer;
typedef void* ID3DRenderTargetView;
typedef void* ID3DDepthStencilView;
typedef void* ID3DPixelShader;
typedef void* ID3DVertexShader;

// X-Ray Engine render system headers
#include "../../xrParticles/psystem.h"

#include "../xrRender/HW.h"
#include "../xrRender/Shader.h"
#include "../xrRender/resourcemanager.h"
#include "../xrRender/PSLibrary.h"

#include "../../xrEngine/vis_common.h"
#include "../../xrEngine/render.h"
#include "../xrRender/blenders\blender.h"
#include "../xrRender/blenders\blender_clsid.h"
#include "../xrRender/xrRender_console.h"

// Forward declarations for game-specific types
class CHUDManager;
class dxRender_Visual;
