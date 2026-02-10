// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// Licensed under the same terms as X-Ray Engine (see root License.txt)

#include "stdafx.h"
#include "vk3DFluidObstacles.h"

namespace VK
{

vk3DFluidObstacles::vk3DFluidObstacles()
{
}

vk3DFluidObstacles::~vk3DFluidObstacles()
{
    Destroy();
}

void vk3DFluidObstacles::Render(VkCommandBuffer cmd)
{
    // TODO: Implement obstacle rendering (Phase 3.3)
}

void vk3DFluidObstacles::Destroy()
{
    // TODO: Cleanup
}

} // namespace VK
