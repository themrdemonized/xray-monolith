// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// Licensed under the same terms as X-Ray Engine (see root License.txt)

#pragma once
#include "../../Include/xrRender/UIShader.h"
#include <vulkan/vulkan.h>

/**
 * Extended UI Shader interface for Vulkan
 * Provides access to Vulkan-specific resources (descriptor sets, textures)
 */
class IVkUIShader : public IUIShader
{
public:
    virtual VkDescriptorSet GetDescriptorSet() = 0;
    virtual u32 GetTextureWidth() const = 0;
    virtual u32 GetTextureHeight() const = 0;
};
