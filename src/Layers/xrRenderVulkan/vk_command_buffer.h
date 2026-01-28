// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// SPDX-License-Identifier: MIT

#pragma once
#include "vk_core.h"

// Command buffer management (triple buffering)
class CVulkanCommandManager
{
public:
    static constexpr u32 FRAMES_IN_FLIGHT = 3;

private:
    VkCommandPool   m_CommandPools[FRAMES_IN_FLIGHT];
    VkCommandBuffer m_CommandBuffers[FRAMES_IN_FLIGHT];
    u32             m_CurrentFrame = 0;

public:
    void Create();
    void Destroy();

    VkCommandBuffer Begin();
    void End(VkCommandBuffer cmd);
    void Submit(VkCommandBuffer cmd, VkSemaphore waitSemaphore, VkSemaphore signalSemaphore, VkFence fence);

    void NextFrame() { m_CurrentFrame = (m_CurrentFrame + 1) % FRAMES_IN_FLIGHT; }
    u32 GetCurrentFrame() const { return m_CurrentFrame; }

    VkCommandBuffer GetCurrentCommandBuffer() const { return m_CommandBuffers[m_CurrentFrame]; }
};

// Глобальный экземпляр
extern CVulkanCommandManager CommandManager;
