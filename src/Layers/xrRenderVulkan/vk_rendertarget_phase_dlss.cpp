// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// SPDX-License-Identifier: MIT

#include "stdafx.h"
#include "vk_rendertarget.h"
#include "HW_Vulkan.h"
#include "vk_R_Backend.h"
#include "vk_swapchain.h"
#include "vk_dlss.h"
#include "rvk.h"

namespace VK
{

// ============================================================================
// phase_dlss() — DLSS Upscaling (rt_HDR → rt_DlssOutput)
// ============================================================================
//
// Slots between auto-exposure (PASS 6.52) and tonemap (PASS 6.55).
// Reads rt_HDR (render resolution), depth, motion vectors, and exposure,
// then writes upscaled result to rt_DlssOutput (display resolution).
//
// ============================================================================

void CRenderTarget::phase_dlss()
{
    if (!g_DlssManager.IsFeatureCreated())
        return;

    if (rt_DlssOutput.m_Image == VK_NULL_HANDLE)
        return;

    VkCommandBuffer cmd = RCache.GetCommandBuffer();
    if (cmd == VK_NULL_HANDLE)
        return;

    // ========================================================================
    // Step 1: Transition resources for NGX
    // ========================================================================

    VkImageMemoryBarrier barriers[3] = {};
    u32 barrierCount = 0;

    // rt_DlssOutput: UNDEFINED → GENERAL (NGX writes to it as storage image)
    barriers[barrierCount] = {};
    barriers[barrierCount].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barriers[barrierCount].srcAccessMask = 0;
    barriers[barrierCount].dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barriers[barrierCount].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barriers[barrierCount].newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barriers[barrierCount].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[barrierCount].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[barrierCount].image = rt_DlssOutput.m_Image;
    barriers[barrierCount].subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    barrierCount++;

    // Depth: DEPTH_ATTACHMENT → SHADER_READ_ONLY (NGX reads depth)
    barriers[barrierCount] = {};
    barriers[barrierCount].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barriers[barrierCount].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    barriers[barrierCount].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barriers[barrierCount].oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    barriers[barrierCount].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barriers[barrierCount].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[barrierCount].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[barrierCount].image = Swapchain.m_DepthImage;
    barriers[barrierCount].subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 };
    barrierCount++;

    // rt_MotionVector: already SHADER_READ_ONLY from gbuffer end, but ensure it
    barriers[barrierCount] = {};
    barriers[barrierCount].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barriers[barrierCount].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barriers[barrierCount].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barriers[barrierCount].oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barriers[barrierCount].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barriers[barrierCount].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[barrierCount].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[barrierCount].image = rt_MotionVector.m_Image;
    barriers[barrierCount].subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    barrierCount++;

    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, barrierCount, barriers);

    // ========================================================================
    // Step 2: Compute jitter in pixel space
    // ========================================================================
    // m_Jitter.current is in NDC [-1,1] range
    // Convert to pixel space: NDC * renderRes * 0.5
    float jitterPixelX = RImplementation.m_Jitter.current.x * (float)m_Width * 0.5f;
    float jitterPixelY = RImplementation.m_Jitter.current.y * (float)m_Height * 0.5f;

    // ========================================================================
    // Step 3: Call DLSS Evaluate
    // ========================================================================
    g_DlssManager.Evaluate(cmd,
        rt_HDR.m_Image,           rt_HDR.m_ImageView,              // color (render res)
        Swapchain.m_DepthImage,   Swapchain.m_DepthView,           // depth (render res)
        rt_MotionVector.m_Image,  rt_MotionVector.m_ImageView,     // MVs (render res)
        rt_DlssOutput.m_Image,    rt_DlssOutput.m_ImageView,       // output (display res)
        m_ExposureImage,          m_ExposureView,                   // exposure (1x1)
        jitterPixelX, jitterPixelY,
        m_Width, m_Height,
        m_DisplayWidth, m_DisplayHeight
    );

    // ========================================================================
    // Step 4: Transition rt_DlssOutput → SHADER_READ_ONLY for tonemap
    // ========================================================================
    {
        VkImageMemoryBarrier outBarrier = {};
        outBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        outBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        outBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        outBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        outBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        outBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        outBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        outBarrier.image = rt_DlssOutput.m_Image;
        outBarrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &outBarrier);
    }

    // ========================================================================
    // Step 5: Transition depth back to DEPTH_ATTACHMENT for subsequent passes
    // ========================================================================
    {
        VkImageMemoryBarrier depthBarrier = {};
        depthBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        depthBarrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        depthBarrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        depthBarrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        depthBarrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depthBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        depthBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        depthBarrier.image = Swapchain.m_DepthImage;
        depthBarrier.subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 };
        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
            0, 0, nullptr, 0, nullptr, 1, &depthBarrier);
    }
}

} // namespace VK
