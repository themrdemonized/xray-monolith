// Vulkan ResourceManager - Scripting stub
// Vulkan uses SPIR-V shaders, not Lua scripts

#include "stdafx.h"
#pragma hdrstop

#include "../xrRender/ResourceManager.h"

// Vulkan doesn't use Lua shader scripts - shaders are precompiled to SPIR-V
void CResourceManager::LS_Load()
{
    Msg("* [Vulkan] LS_Load: Skipping Lua shader scripts (using SPIR-V)");
    // No-op for Vulkan - shaders loaded via CVulkanPipelineManager
}

void CResourceManager::LS_Unload()
{
    Msg("* [Vulkan] LS_Unload: No Lua scripts to unload");
    // No-op for Vulkan
}
