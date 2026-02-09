// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// SPDX-License-Identifier: MIT

#pragma once
#include "vk_core.h"

// NVIDIA NGX SDK headers
#include <nvsdk_ngx.h>
#include <nvsdk_ngx_vk.h>
#include <nvsdk_ngx_helpers.h>
#include <nvsdk_ngx_helpers_vk.h>

// Console variables (defined in vk_console.cpp)
extern u32 ps_r__dlss_quality;
extern u32 ps_r__dlss_preset;

// DLSS quality enum matching console variable
enum eDlssQuality : u32 {
    DLSS_OFF              = 0,
    DLSS_DLAA             = 1,  // Native resolution, max quality
    DLSS_QUALITY          = 2,
    DLSS_BALANCED         = 3,
    DLSS_PERFORMANCE      = 4,
    DLSS_ULTRA_PERF       = 5,
};

// DLSS render preset (AI model)
enum eDlssPreset : u32 {
    DLSS_PRESET_DEFAULT     = 0,  // Let NGX choose (CNN or Transformer depending on driver)
    DLSS_PRESET_TRANSFORMER = 1,  // Force Preset K — Transformer model (DLSS 4)
};

class CDlssManager
{
public:
    // Initialize NGX runtime — call after Vulkan device is created
    bool Init();

    // Shutdown NGX runtime — call before Vulkan device is destroyed
    void Shutdown();

    // Create DLSS feature for given render/display resolution pair
    bool CreateFeature(u32 renderW, u32 renderH, u32 displayW, u32 displayH, u32 qualityMode);

    // Destroy current DLSS feature (call before recreation or shutdown)
    void DestroyFeature();

    // Execute DLSS upscaling: reads color/depth/MV/exposure at render res,
    // writes upscaled result to output at display res
    void Evaluate(VkCommandBuffer cmd,
                  VkImage colorIn,   VkImageView colorView,
                  VkImage depthIn,   VkImageView depthView,
                  VkImage mvIn,      VkImageView mvView,
                  VkImage output,    VkImageView outputView,
                  VkImage exposure,  VkImageView exposureView,
                  float jitterX, float jitterY,
                  u32 renderW, u32 renderH,
                  u32 displayW, u32 displayH);

    // Is NGX initialized and GPU supports DLSS?
    bool IsAvailable() const { return m_bAvailable; }

    // Is DLSS feature handle allocated?
    bool IsFeatureCreated() const { return m_DlssHandle != nullptr; }

    // Query optimal render resolution for given display resolution + quality
    void GetOptimalResolution(u32 displayW, u32 displayH, u32 quality,
                              u32& outRenderW, u32& outRenderH);

    // Current quality setting the feature was created with
    u32 GetCurrentQuality() const { return m_CurrentQuality; }

private:
    // Map our quality enum to NGX perf quality value
    NVSDK_NGX_PerfQuality_Value MapQuality(u32 quality);

    NVSDK_NGX_Parameter* m_Params      = nullptr;
    NVSDK_NGX_Handle*    m_DlssHandle  = nullptr;
    bool m_bInitialized = false;
    bool m_bAvailable   = false;
    u32  m_CurrentQuality = 0;
};

extern CDlssManager g_DlssManager;
