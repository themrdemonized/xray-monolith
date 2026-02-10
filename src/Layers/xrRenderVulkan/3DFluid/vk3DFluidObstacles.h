// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// Licensed under the same terms as X-Ray Engine (see root License.txt)

#pragma once
#include "../vk_core.h"

namespace VK
{

/**
 * 3D Fluid Obstacles
 *
 * Статичная геометрия (OOBB - oriented bounding boxes) которая блокирует
 * поток жидкости в симуляции.
 */
class vk3DFluidObstacles
{
public:
    vk3DFluidObstacles();
    ~vk3DFluidObstacles();

    /**
     * Render obstacles в obstacles texture
     */
    void Render(VkCommandBuffer cmd);

    void Destroy();

private:
    // TODO: OOBB list
};

} // namespace VK
