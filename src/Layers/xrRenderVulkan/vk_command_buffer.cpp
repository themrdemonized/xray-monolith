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
        VK_CHECK_CRITICAL(vkCreateCommandPool(VulkanHW.m_Device, &poolInfo, nullptr, &m_CommandPools[i]));
    }

    // Создаём command buffers
    for (u32 i = 0; i < FRAMES_IN_FLIGHT; i++) {
        VkCommandBufferAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = m_CommandPools[i];
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;

        VK_CHECK_CRITICAL(vkAllocateCommandBuffers(VulkanHW.m_Device, &allocInfo, &m_CommandBuffers[i]));
    }

    // Create dedicated immediate command pool+buffer for one-shot operations
    // (texture uploads, layout transitions) — separate from per-frame render buffers
    VK_CHECK_CRITICAL(vkCreateCommandPool(VulkanHW.m_Device, &poolInfo, nullptr, &m_ImmediatePool));

    {
        VkCommandBufferAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = m_ImmediatePool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;
        VK_CHECK_CRITICAL(vkAllocateCommandBuffers(VulkanHW.m_Device, &allocInfo, &m_ImmediateCmd));
    }

    {
        VkFenceCreateInfo fenceInfo = {};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        VK_CHECK_CRITICAL(vkCreateFence(VulkanHW.m_Device, &fenceInfo, nullptr, &m_ImmediateFence));
    }

    Msg("[Vulkan] Command pools and buffers created (3 frames in flight + 1 immediate)");
}

// Уничтожение
void CVulkanCommandManager::Destroy()
{
    if (VulkanHW.m_Device == VK_NULL_HANDLE) return;

    if (m_ImmediateFence != VK_NULL_HANDLE) {
        vkDestroyFence(VulkanHW.m_Device, m_ImmediateFence, nullptr);
        m_ImmediateFence = VK_NULL_HANDLE;
    }

    if (m_ImmediatePool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(VulkanHW.m_Device, m_ImmediatePool, nullptr);
        m_ImmediatePool = VK_NULL_HANDLE;
    }

    for (u32 i = 0; i < FRAMES_IN_FLIGHT; i++) {
        if (m_CommandPools[i] != VK_NULL_HANDLE) {
            vkDestroyCommandPool(VulkanHW.m_Device, m_CommandPools[i], nullptr);
            m_CommandPools[i] = VK_NULL_HANDLE;
        }
    }

    Msg("[Vulkan] Command pools destroyed");
}

// Начало записи команд — returns VK_NULL_HANDLE on error
VkCommandBuffer CVulkanCommandManager::Begin()
{
    if (g_bDeviceLost) return VK_NULL_HANDLE;

    VkCommandBuffer cmd = m_CommandBuffers[m_CurrentFrame];

    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    VkResult res = vkBeginCommandBuffer(cmd, &beginInfo);
    if (res != VK_SUCCESS) {
        Msg("!Vulkan vkBeginCommandBuffer error: %d", res);
        if (res == VK_ERROR_DEVICE_LOST && !g_bDeviceLost) {
            Msg("!Vulkan DEVICE LOST in Begin()");
            g_bDeviceLost = true;
        }
        return VK_NULL_HANDLE;
    }

    return cmd;
}

// Завершение записи команд — returns false on error
bool CVulkanCommandManager::End(VkCommandBuffer cmd)
{
    if (g_bDeviceLost || cmd == VK_NULL_HANDLE) return false;

    VkResult res = vkEndCommandBuffer(cmd);
    if (res != VK_SUCCESS) {
        Msg("!Vulkan vkEndCommandBuffer error: %d", res);
        if (res == VK_ERROR_DEVICE_LOST && !g_bDeviceLost) {
            Msg("!Vulkan DEVICE LOST in End()");
            g_bDeviceLost = true;
        }
        return false;
    }
    return true;
}

// Submit команд в очередь — returns false on error
bool CVulkanCommandManager::Submit(VkCommandBuffer cmd, VkSemaphore waitSemaphore,
                                   VkSemaphore signalSemaphore, VkFence fence)
{
    if (g_bDeviceLost || cmd == VK_NULL_HANDLE) return false;

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

    VkResult res = vkQueueSubmit(VulkanHW.m_GraphicsQueue, 1, &submitInfo, fence);
    if (res != VK_SUCCESS) {
        Msg("!Vulkan vkQueueSubmit error: %d", res);
        if (res == VK_ERROR_DEVICE_LOST && !g_bDeviceLost) {
            Msg("!Vulkan DEVICE LOST in Submit()");
            g_bDeviceLost = true;
        }
        return false;
    }
    return true;
}

// ============================================================================
// Immediate (one-shot) command buffer — safe to call during rendering.
// Uses a dedicated pool+buffer that never conflicts with per-frame render buffers.
// ============================================================================
VkCommandBuffer CVulkanCommandManager::BeginImmediate()
{
    if (g_bDeviceLost || m_ImmediateCmd == VK_NULL_HANDLE) return VK_NULL_HANDLE;

    // Reset the immediate command buffer before reuse
    vkResetCommandBuffer(m_ImmediateCmd, 0);

    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    VkResult res = vkBeginCommandBuffer(m_ImmediateCmd, &beginInfo);
    if (res != VK_SUCCESS) {
        Msg("!Vulkan BeginImmediate error: %d", res);
        return VK_NULL_HANDLE;
    }

    return m_ImmediateCmd;
}

void CVulkanCommandManager::EndAndSubmitImmediate(VkCommandBuffer cmd)
{
    if (g_bDeviceLost || cmd == VK_NULL_HANDLE) return;

    vkEndCommandBuffer(cmd);

    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;

    vkResetFences(VulkanHW.m_Device, 1, &m_ImmediateFence);
    VkResult res = vkQueueSubmit(VulkanHW.m_GraphicsQueue, 1, &submitInfo, m_ImmediateFence);
    if (res != VK_SUCCESS) {
        Msg("!Vulkan EndAndSubmitImmediate: submit error %d", res);
        return;
    }

    vkWaitForFences(VulkanHW.m_Device, 1, &m_ImmediateFence, VK_TRUE, 2000000000ULL);
}
