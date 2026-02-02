// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// SPDX-License-Identifier: MIT

#include "stdafx.h"
#include "vk3DFluidEmitters.h"

namespace VK
{

vk3DFluidEmitters::vk3DFluidEmitters()
{
}

vk3DFluidEmitters::~vk3DFluidEmitters()
{
    Destroy();
}

void vk3DFluidEmitters::Render(VkCommandBuffer cmd)
{
    // TODO: Implement emitter rendering (Phase 3.2)
}

void vk3DFluidEmitters::Destroy()
{
    // TODO: Cleanup
}

} // namespace VK
