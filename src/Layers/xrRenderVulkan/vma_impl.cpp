// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// SPDX-License-Identifier: MIT

// ВАЖНО: VMA требует exceptions, поэтому компилируем этот файл с /EHsc
// Этот файл полностью изолирован и НЕ использует stdafx.h или xrCore.h

// Отключаем warnings
#pragma warning(push)
#pragma warning(disable: 4100) // unreferenced formal parameter
#pragma warning(disable: 4189) // local variable is initialized but not referenced

// Определяем Windows и Vulkan макросы напрямую
#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>

// Настройки VMA для минимальных зависимостей
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#define VMA_VULKAN_VERSION 1003000 // Vulkan 1.3

// Определяем VMA implementation только здесь
#define VMA_IMPLEMENTATION
#include "../../3rd party/vma/vk_mem_alloc.h"

#pragma warning(pop)

// Этот файл служит только для компиляции VMA implementation
// Все функции VMA будут доступны через vk_mem_alloc.h в других файлах
