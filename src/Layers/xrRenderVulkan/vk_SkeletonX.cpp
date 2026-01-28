// Vulkan wrapper for SkeletonX.cpp
// This file includes SkeletonX.cpp with Vulkan type mappings

#include "stdafx.h"

// Include compatibility layer BEFORE FBasicVisual.h gets included
#define FBasicVisualH  // Prevent FBasicVisual.h from being included
#include "vk_FBasicVisual.h"

// Now include the actual SkeletonX implementation
#include "../xrRender/SkeletonX.cpp"

// Cleanup defines
#undef dxRender_Visual
#undef FBasicVisualH
