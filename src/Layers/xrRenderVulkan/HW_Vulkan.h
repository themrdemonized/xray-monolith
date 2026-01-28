// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// SPDX-License-Identifier: MIT

#pragma once
#include "vk_core.h"
#include "HWCaps_Vulkan.h"

// Аналог CHW из DirectX, но для Vulkan (переименован чтобы избежать конфликта с R4)
class CVulkanHW : public pureAppActivate, public pureAppDeactivate
{
public:
    // Vulkan Core handles
    VkInstance              m_Instance       = VK_NULL_HANDLE;
    VkPhysicalDevice        m_PhysicalDevice = VK_NULL_HANDLE;
    VkDevice                m_Device         = VK_NULL_HANDLE;
    VkSurfaceKHR            m_Surface        = VK_NULL_HANDLE;

    // Queues
    VkQueue     m_GraphicsQueue  = VK_NULL_HANDLE;
    VkQueue     m_PresentQueue   = VK_NULL_HANDLE;
    u32         m_GraphicsFamily = UINT32_MAX;
    u32         m_PresentFamily  = UINT32_MAX;

    // Memory allocator
    VmaAllocator m_Allocator = VK_NULL_HANDLE;

    // Command pool for transfer operations
    VkCommandPool m_TransferCommandPool = VK_NULL_HANDLE;

    // Capabilities (аналог Caps из DX11)
    VulkanCaps Caps;

    // Window handle (аналог m_hWnd из DX11)
    HWND m_hWnd = nullptr;

#ifdef DEBUG
    VkDebugUtilsMessengerEXT m_DebugMessenger = VK_NULL_HANDLE;
#endif

public:
    CVulkanHW();
    ~CVulkanHW();

    // Основные методы (аналог DX11)
    void CreateDevice(HWND hWnd);
    void DestroyDevice();
    void Reset(HWND hWnd);

    // App activation/deactivation
    virtual void OnAppActivate() override;
    virtual void OnAppDeactivate() override;

    // Single-time command helpers (for buffer uploads, etc.)
    VkCommandBuffer BeginSingleTimeCommands();
    void EndSingleTimeCommands(VkCommandBuffer cmd);

    // Accessor methods
    VmaAllocator GetAllocator() const { return m_Allocator; }
    VkDevice GetDevice() const { return m_Device; }

private:
    // Вспомогательные методы
    bool FindQueueFamilies();
    bool CreateLogicalDevice();
    bool CreateVMA();
};

// Глобальный экземпляр Vulkan HW (переименован чтобы избежать конфликта с R4)
extern ECORE_API CVulkanHW VulkanHW;
