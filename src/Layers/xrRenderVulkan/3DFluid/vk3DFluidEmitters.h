// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// SPDX-License-Identifier: MIT

#pragma once
#include "../vk_core.h"

namespace VK
{

/**
 * 3D Fluid Emitters
 *
 * Источники дыма/огня для fluid simulation.
 * Типы:
 * - SimpleGaussian: Статичный gaussian spot
 * - SimpleDraught: Oscillating vertical flow
 */
class vk3DFluidEmitters
{
public:
    vk3DFluidEmitters();
    ~vk3DFluidEmitters();

    /**
     * Render emitters в simulation (инжект density и velocity)
     */
    void Render(VkCommandBuffer cmd);

    void Destroy();

private:
    // TODO: Emitter list
};

} // namespace VK
