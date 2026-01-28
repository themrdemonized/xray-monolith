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
