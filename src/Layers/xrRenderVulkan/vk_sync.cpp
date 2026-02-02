// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// SPDX-License-Identifier: MIT

#include "stdafx.h"
#include "vk_sync.h"
#include "HW_Vulkan.h"

// VULKAN_DIAG
static void VulkanDiagWriteSync(const char* msg) {
	HANDLE h = CreateFileA("D:\\anomaly\\appdata\\logs\\vulkan_diag.txt",
		FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS,
		FILE_ATTRIBUTE_NORMAL, NULL);
	if (h != INVALID_HANDLE_VALUE) { DWORD w; WriteFile(h, msg, (DWORD)strlen(msg), &w, NULL); WriteFile(h, "\r\n", 2, &w, NULL); FlushFileBuffers(h); CloseHandle(h); }
}
static struct DiagSync1 { DiagSync1() { VulkanDiagWriteSync("[DIAG] vk_sync.cpp: before Sync"); } } g_diagSync1;

// Глобальный экземпляр
CVulkanSync Sync;
static struct DiagSync2 { DiagSync2() { VulkanDiagWriteSync("[DIAG] vk_sync.cpp: after Sync"); } } g_diagSync2;

// Создание synchronization primitives
void CVulkanSync::Create()
{
    Msg("[Vulkan] Creating synchronization primitives...");

    VkSemaphoreCreateInfo semaphoreInfo = {};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo = {};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;  // Создаём в signaled состоянии

    for (u32 i = 0; i < FRAMES_IN_FLIGHT; i++) {
        VK_CHECK_CRITICAL(vkCreateSemaphore(VulkanHW.m_Device, &semaphoreInfo, nullptr, &m_FrameSync[i].imageAvailable));
        VK_CHECK_CRITICAL(vkCreateSemaphore(VulkanHW.m_Device, &semaphoreInfo, nullptr, &m_FrameSync[i].renderFinished));
        VK_CHECK_CRITICAL(vkCreateFence(VulkanHW.m_Device, &fenceInfo, nullptr, &m_FrameSync[i].inFlightFence));
    }

    Msg("[Vulkan] Synchronization primitives created (3 frames in flight)");
}

// Уничтожение
void CVulkanSync::Destroy()
{
    if (VulkanHW.m_Device == VK_NULL_HANDLE) return;

    for (u32 i = 0; i < FRAMES_IN_FLIGHT; i++) {
        if (m_FrameSync[i].imageAvailable != VK_NULL_HANDLE) {
            vkDestroySemaphore(VulkanHW.m_Device, m_FrameSync[i].imageAvailable, nullptr);
            m_FrameSync[i].imageAvailable = VK_NULL_HANDLE;
        }

        if (m_FrameSync[i].renderFinished != VK_NULL_HANDLE) {
            vkDestroySemaphore(VulkanHW.m_Device, m_FrameSync[i].renderFinished, nullptr);
            m_FrameSync[i].renderFinished = VK_NULL_HANDLE;
        }

        if (m_FrameSync[i].inFlightFence != VK_NULL_HANDLE) {
            vkDestroyFence(VulkanHW.m_Device, m_FrameSync[i].inFlightFence, nullptr);
            m_FrameSync[i].inFlightFence = VK_NULL_HANDLE;
        }
    }

    Msg("[Vulkan] Synchronization primitives destroyed");
}

// Ожидание fence — returns false on error (device lost, timeout)
bool CVulkanSync::WaitForFence(u32 frameIndex)
{
    if (g_bDeviceLost) return false;

    VkResult res = vkWaitForFences(VulkanHW.m_Device, 1, &m_FrameSync[frameIndex].inFlightFence,
                                   VK_TRUE, 2000000000ULL);  // 2 second timeout
    if (res == VK_SUCCESS) return true;

    if (res == VK_ERROR_DEVICE_LOST) {
        if (!g_bDeviceLost) {
            Msg("!Vulkan DEVICE LOST in WaitForFence");
            g_bDeviceLost = true;
        }
    } else if (res == VK_TIMEOUT) {
        Msg("!Vulkan WaitForFence timeout (frame %u)", frameIndex);
    } else {
        Msg("!Vulkan WaitForFence error: %d", res);
    }
    return false;
}

// Сброс fence — returns false on error
bool CVulkanSync::ResetFence(u32 frameIndex)
{
    if (g_bDeviceLost) return false;

    VkResult res = vkResetFences(VulkanHW.m_Device, 1, &m_FrameSync[frameIndex].inFlightFence);
    if (res != VK_SUCCESS) {
        Msg("!Vulkan ResetFence error: %d", res);
        return false;
    }
    return true;
}
