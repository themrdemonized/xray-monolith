#pragma once

#include "../../xrEngine/stdafx.h"

#define XRRENDER_VULKAN_EXPORTS

// Vulkan headers
#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>

// Vulkan Memory Allocator (без implementation, он в vma_impl.cpp)
// VMA требует exceptions, поэтому изолирован в отдельном .cpp
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#define VMA_VULKAN_VERSION 1003000
#include "../../3rd party/vma/vk_mem_alloc.h"
