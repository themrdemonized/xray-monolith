// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// Licensed under the same terms as X-Ray Engine (see root License.txt)

#include "stdafx.h"
#include "vk_rendertarget.h"
#include "HW_Vulkan.h"
#include "vk_R_Backend.h"

namespace VK
{

// ============================================================================
// phase_exposure() - Auto-Exposure Compute Pass
// ============================================================================
//
// Two-pass compute shader:
//   1. Histogram build: reads rt_HDR, builds 256-bin luminance histogram (SSBO)
//   2. Average + Exposure: scans histogram, excludes extremes, computes exposure,
//      temporally smooths, writes 1x1 R32F image.
//
// Called after wallmarks, before tonemap. Transitions rt_HDR from
// COLOR_ATTACHMENT to SHADER_READ_ONLY (which tonemap expects).
//
// ============================================================================

void CRenderTarget::phase_exposure()
{
    if (!m_bExposureReady)
        return;

    VkCommandBuffer cmd = RCache.GetCommandBuffer();
    if (cmd == VK_NULL_HANDLE)
        return;

    VkImage hdrImage = rt_HDR.m_Image;
    if (hdrImage == VK_NULL_HANDLE)
        return;

    // ========================================================================
    // Step 1: Transition rt_HDR: COLOR_ATTACHMENT → SHADER_READ_ONLY
    // ========================================================================
    // This is the transition that tonemap previously did — we do it here so
    // the histogram compute shader can sample rt_HDR.
    {
        VkImageMemoryBarrier barrier = {};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = hdrImage;
        barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &barrier);
    }

    // ========================================================================
    // Step 2: Dispatch histogram build
    // ========================================================================
    {
        struct HistPushConstants {
            float minLogLum;
            float logLumRange;
            u32   width;
            u32   height;
        } histPC;

        histPC.minLogLum  = -10.0f;
        histPC.logLumRange = 12.0f;
        histPC.width  = m_Width;
        histPC.height = m_Height;

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_ExposureHistPipeline.GetPipeline());
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_ExposureHistPipeLayout,
            0, 1, &m_ExposureHistDescSet, 0, nullptr);
        vkCmdPushConstants(cmd, m_ExposureHistPipeLayout, VK_SHADER_STAGE_COMPUTE_BIT,
            0, sizeof(histPC), &histPC);

        u32 groupsX = (m_Width  + 15) / 16;
        u32 groupsY = (m_Height + 15) / 16;
        vkCmdDispatch(cmd, groupsX, groupsY, 1);
    }

    // ========================================================================
    // Step 3: Barrier: SSBO compute-write → compute-read
    // ========================================================================
    {
        VkBufferMemoryBarrier bufBarrier = {};
        bufBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        bufBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        bufBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        bufBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bufBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bufBarrier.buffer = m_HistogramBuffer;
        bufBarrier.offset = 0;
        bufBarrier.size = 256 * sizeof(u32);

        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0, 0, nullptr, 1, &bufBarrier, 0, nullptr);
    }

    // ========================================================================
    // Step 4: Dispatch average + exposure
    // ========================================================================
    {
        struct AvgPushConstants {
            float minLogLum;
            float logLumRange;
            u32   totalPixels;
            float adaptSpeed;
            float keyValue;
        } avgPC;

        avgPC.minLogLum   = -10.0f;
        avgPC.logLumRange = 12.0f;
        avgPC.totalPixels = m_Width * m_Height;
        avgPC.adaptSpeed  = _max(0.0f, _min(1.5f * Device.fTimeDelta, 1.0f));
        avgPC.keyValue    = 0.18f;

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_ExposureAvgPipeline.GetPipeline());
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_ExposureAvgPipeLayout,
            0, 1, &m_ExposureAvgDescSet, 0, nullptr);
        vkCmdPushConstants(cmd, m_ExposureAvgPipeLayout, VK_SHADER_STAGE_COMPUTE_BIT,
            0, sizeof(avgPC), &avgPC);

        vkCmdDispatch(cmd, 1, 1, 1);
    }

    // ========================================================================
    // Step 5: Barrier: exposure image compute-write → fragment-read
    // ========================================================================
    // Exposure image stays in GENERAL layout (valid for both compute storage
    // and fragment shader sampled reads).
    {
        VkImageMemoryBarrier barrier = {};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = m_ExposureImage;
        barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &barrier);
    }

    // rt_HDR is now in SHADER_READ_ONLY layout — tonemap can sample it directly.
}

} // namespace VK
