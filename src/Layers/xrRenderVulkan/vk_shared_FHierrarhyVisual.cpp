// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// Licensed under the same terms as X-Ray Engine (see root License.txt)

// Vulkan wrapper for shared FHierrarhyVisual.cpp
// Compiles the shared implementation with Vulkan type mappings
#include "stdafx.h"
#include "vk_SkeletonCompat.h"

#define FBasicVisualH  // Prevent fbasicvisual.h (types already provided by compat)
#include "../xrRender/FHierrarhyVisual.cpp"
#undef FBasicVisualH
