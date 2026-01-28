// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// SPDX-License-Identifier: MIT

#include "stdafx.h"
#include "vk_command_buffer.h"
#include "HW_Vulkan.h"

// VULKAN_DIAG
static void VulkanDiagWriteCmdBuf(const char* msg) {
	HANDLE h = CreateFileA("D:\\anomaly\\appdata\\logs\\vulkan_diag.txt",
		FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS,
		FILE_ATTRIBUTE_NORMAL, NULL);
	if (h != INVALID_HANDLE_VALUE) { DWORD w; WriteFile(h, msg, (DWORD)strlen(msg), &w, NULL); WriteFile(h, "\r\n", 2, &w, NULL); FlushFileBuffers(h); CloseHandle(h); }
}
static struct DiagCmdBuf1 { DiagCmdBuf1() { VulkanDiagWriteCmdBuf("[DIAG] vk_command_buffer.cpp: before CommandManager"); } } g_diagCmdBuf1;

// Глобальный экземпляр
CVulkanCommandManager CommandManager;
static struct DiagCmdBuf2 { DiagCmdBuf2() { VulkanDiagWriteCmdBuf("[DIAG] vk_command_buffer.cpp: after CommandManager"); } } g_diagCmdBuf2;

// Создание command pools и buffers
void CVulkanCommandManager::Create()
{
    Msg("[Vulkan] Creating command pools and buffers...");

    // Создаём command pool для каждого frame in flight
    VkCommandPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = VulkanHW.m_GraphicsFamily;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    for (u32 i = 0; i < FRAMES_IN_FLIGHT; i++) {
        VK_CHECK(vkCreateCommandPool(VulkanHW.m_Device, &poolInfo, nullptr, &m_CommandPools[i]));
    }

    // Создаём command buffers
    for (u32 i = 0; i < FRAMES_IN_FLIGHT; i++) {
        VkCommandBufferAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = m_CommandPools[i];
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;

        VK_CHECK(vkAllocateCommandBuffers(VulkanHW.m_Device, &allocInfo, &m_CommandBuffers[i]));
    }

    Msg("[Vulkan] Command pools and buffers created (3 frames in flight)");
}

// Уничтожение
void CVulkanCommandManager::Destroy()
{
    if (VulkanHW.m_Device == VK_NULL_HANDLE) return;

    for (u32 i = 0; i < FRAMES_IN_FLIGHT; i++) {
        if (m_CommandPools[i] != VK_NULL_HANDLE) {
            vkDestroyCommandPool(VulkanHW.m_Device, m_CommandPools[i], nullptr);
            m_CommandPools[i] = VK_NULL_HANDLE;
        }
    }

    Msg("[Vulkan] Command pools destroyed");
}

// Начало записи команд
VkCommandBuffer CVulkanCommandManager::Begin()
{
    VkCommandBuffer cmd = m_CommandBuffers[m_CurrentFrame];

    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    VK_CHECK(vkBeginCommandBuffer(cmd, &beginInfo));

    return cmd;
}

// Завершение записи команд
void CVulkanCommandManager::End(VkCommandBuffer cmd)
{
    VK_CHECK(vkEndCommandBuffer(cmd));
}

// Submit команд в очередь
void CVulkanCommandManager::Submit(VkCommandBuffer cmd, VkSemaphore waitSemaphore,
                                   VkSemaphore signalSemaphore, VkFence fence)
{
    VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };

    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = waitSemaphore != VK_NULL_HANDLE ? 1 : 0;
    submitInfo.pWaitSemaphores = &waitSemaphore;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    submitInfo.signalSemaphoreCount = signalSemaphore != VK_NULL_HANDLE ? 1 : 0;
    submitInfo.pSignalSemaphores = &signalSemaphore;

    VK_CHECK(vkQueueSubmit(VulkanHW.m_GraphicsQueue, 1, &submitInfo, fence));
}
