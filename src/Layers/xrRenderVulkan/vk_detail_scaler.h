// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// Licensed under the same terms as X-Ray Engine (see root License.txt)

#pragma once

// ============================================================================
// cl_dt_scaler - Detail Texture Scaler
// ============================================================================
//
// Constant setup for detail texture scaling.
// Used by CTextureDescrMngr to store detail_scale from .thm files.
//
// In Vulkan, this will be passed via push constants or UBO.
// For now, it just stores the scale value extracted from .thm.
//
// ============================================================================

// Detail texture range (eye-params)
// This controls the fade distance for detail textures
extern float r__dtex_range;

class cl_dt_scaler : public R_constant_setup
{
public:
    float scale;

    cl_dt_scaler(float s) : scale(s) {}

    virtual void setup(R_constant* C)
    {
        // For Vulkan: This will be replaced with descriptor/push constant update
        // In DX9/DX11: RCache.set_c(C, scale, scale, scale, 1 / r__dtex_range);

        // TODO: In Vulkan, update push constants or UBO with:
        // vec4(scale, scale, scale, 1 / r__dtex_range)
    }
};
