#ifndef vk_SkeletonCompat_H
#define vk_SkeletonCompat_H
#pragma once

// Compatibility layer for Skeleton classes in Vulkan renderer
// This file provides all necessary type mappings and stubs

// STEP 1: Prevent DirectX headers from being included
#define xrD3DDefs_included

// STEP 2: Include Vulkan equivalents BEFORE xrRender headers
#include "vk_d3d_skeleton_compat.h"
#include "vk_Visual.h"
#include "vk_R_Backend.h"

// STEP 3: Include necessary engine headers
#include "../../xrCDB/ISpatial.h"     // For spatial optimization
#include "../xrRender/Shader.h"       // For ref_shader

// STEP 4: Type mappings
#define dxRender_Visual vkRender_Visual

// STEP 4b: Let shared FHierrarhyVisual be included naturally.
// With dxRender_Visual mapped to vkRender_Visual,
// FHierrarhyVisual will inherit from vkRender_Visual.

// STEP 5-6: IRender_Mesh and ref_constant are defined in FBasicVisual.h (xrRender)
// Don't redefine them here - let the wrapper files include the originals

// STEP 7: Forward declare CRender - defined in rvk.h
class CRender;
extern CRender RImplementation;

// STEP 8: RCache - global backend instance
extern CBackend RCache;

// STEP 9: Stub for HW (used by SkeletonX.cpp)
class CHW_Stub
{
public:
	struct
	{
		u32 bSoftware : 1;
		u32 geometry : 1;  // Geometry shader support
	} Caps;
	CHW_Stub() { Caps.bSoftware = 0; Caps.geometry = 1; }
};
extern CHW_Stub HW;

// STEP 10: Console variables (used by Skeleton classes)
extern int ps_r1_SoftwareSkinning;
extern float ps_r__WallmarkTTL;

// STEP 11: FVF vertex formats are defined in FVF.h (xrRender)
// Don't redefine them here - let the wrapper files include the originals

// STEP 12: Software skinning stubs (used by SkeletonX.cpp)
// These are NOT used in Vulkan (ps_r1_SoftwareSkinning = 0), but code references them
struct VertexSoftwareStub { void* data; };
extern VertexSoftwareStub* _VertexStream;
extern void* _VS;

#endif // vk_SkeletonCompat_H
