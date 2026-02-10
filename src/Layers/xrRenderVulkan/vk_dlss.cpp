// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// Licensed under the same terms as X-Ray Engine (see root License.txt)

#include "stdafx.h"
#include "vk_dlss.h"
#include "HW_Vulkan.h"

CDlssManager g_DlssManager;

// ============================================================================
// Map quality enum → NGX PerfQuality value
// ============================================================================
NVSDK_NGX_PerfQuality_Value CDlssManager::MapQuality(u32 quality)
{
    switch (quality)
    {
    case DLSS_DLAA:        return NVSDK_NGX_PerfQuality_Value_DLAA;
    case DLSS_QUALITY:     return NVSDK_NGX_PerfQuality_Value_MaxQuality;
    case DLSS_BALANCED:    return NVSDK_NGX_PerfQuality_Value_Balanced;
    case DLSS_PERFORMANCE: return NVSDK_NGX_PerfQuality_Value_MaxPerf;
    case DLSS_ULTRA_PERF:  return NVSDK_NGX_PerfQuality_Value_UltraPerformance;
    default:               return NVSDK_NGX_PerfQuality_Value_Balanced;
    }
}

// ============================================================================
// Init — initialize NGX runtime
// ============================================================================
bool CDlssManager::Init()
{
    if (m_bInitialized)
        return m_bAvailable;

    Msg("[DLSS] Initializing NGX...");

    // Convert application path to wide string for NGX
    wchar_t appPath[512] = {};
    {
        const char* narrow = Core.ApplicationPath;
        MultiByteToWideChar(CP_ACP, 0, narrow, -1, appPath, 512);
        Msg("[DLSS] Application path: %s", narrow);
    }

    // Set up feature info with logging and DLL search path
    const wchar_t* searchPaths[] = { appPath };
    NVSDK_NGX_FeatureCommonInfo featureInfo = {};
    featureInfo.PathListInfo.Path   = searchPaths;
    featureInfo.PathListInfo.Length  = 1;
    featureInfo.LoggingInfo.LoggingCallback = nullptr;
    featureInfo.LoggingInfo.MinimumLoggingLevel = NVSDK_NGX_LOGGING_LEVEL_ON;
    featureInfo.LoggingInfo.DisableOtherLoggingSinks = false;

    NVSDK_NGX_Result res = NVSDK_NGX_VULKAN_Init(
        231313131,                      // Application ID
        appPath,                        // Data/temp path for NGX
        VulkanHW.m_Instance,
        VulkanHW.m_PhysicalDevice,
        VulkanHW.m_Device,
        vkGetInstanceProcAddr,
        vkGetDeviceProcAddr,
        &featureInfo,
        NVSDK_NGX_Version_API
    );

    if (NVSDK_NGX_FAILED(res))
    {
        Msg("![DLSS] NGX Init failed: 0x%08X — DLSS will be unavailable", (u32)res);

        // Try GetFeatureRequirements even without Init for diagnostics
        NVSDK_NGX_FeatureDiscoveryInfo discoveryInfo = {};
        discoveryInfo.SDKVersion = NVSDK_NGX_Version_API;
        discoveryInfo.FeatureID = NVSDK_NGX_Feature_SuperSampling;
        discoveryInfo.Identifier.IdentifierType = NVSDK_NGX_Application_Identifier_Type_Project_Id;
        discoveryInfo.Identifier.v.ProjectDesc.ProjectId = "xray-vulkan-renderer";
        discoveryInfo.Identifier.v.ProjectDesc.EngineType = NVSDK_NGX_ENGINE_TYPE_CUSTOM;
        discoveryInfo.Identifier.v.ProjectDesc.EngineVersion = "1.0.0";
        discoveryInfo.ApplicationDataPath = appPath;
        discoveryInfo.FeatureInfo = &featureInfo;

        NVSDK_NGX_FeatureRequirement featureReq = {};
        NVSDK_NGX_Result reqRes = NVSDK_NGX_VULKAN_GetFeatureRequirements(
            VulkanHW.m_Instance, VulkanHW.m_PhysicalDevice,
            &discoveryInfo, &featureReq);

        if (NVSDK_NGX_SUCCEED(reqRes))
        {
            Msg("[DLSS] FeatureRequirements: supported=0x%X, minHWArch=%u, minOS=%s",
                (u32)featureReq.FeatureSupported, featureReq.MinHWArchitecture, featureReq.MinOSVersion);
        }

        m_bInitialized = true;
        m_bAvailable = false;
        return false;
    }

    Msg("[DLSS] NGX Init succeeded");

    // Get capability parameters
    res = NVSDK_NGX_VULKAN_GetCapabilityParameters(&m_Params);
    if (NVSDK_NGX_FAILED(res))
    {
        Msg("![DLSS] GetCapabilityParameters failed: 0x%08X", (u32)res);
        NVSDK_NGX_VULKAN_Shutdown1(VulkanHW.m_Device);
        m_bInitialized = true;
        m_bAvailable = false;
        return false;
    }

    // Check if DLSS is supported on this GPU
    int dlssAvailable = 0;
    res = m_Params->Get(NVSDK_NGX_Parameter_SuperSampling_Available, &dlssAvailable);
    Msg("[DLSS] SuperSampling_Available query: result=0x%08X, available=%d", (u32)res, dlssAvailable);

    // Query additional diagnostics
    {
        int needsUpdatedDriver = 0;
        m_Params->Get(NVSDK_NGX_Parameter_SuperSampling_NeedsUpdatedDriver, &needsUpdatedDriver);
        Msg("[DLSS] NeedsUpdatedDriver: %d", needsUpdatedDriver);

        unsigned int minDriverMajor = 0, minDriverMinor = 0;
        m_Params->Get(NVSDK_NGX_Parameter_SuperSampling_MinDriverVersionMajor, &minDriverMajor);
        m_Params->Get(NVSDK_NGX_Parameter_SuperSampling_MinDriverVersionMinor, &minDriverMinor);
        Msg("[DLSS] MinDriverVersion: %u.%u", minDriverMajor, minDriverMinor);

        // Also try GetFeatureRequirements for detailed reasons
        NVSDK_NGX_FeatureDiscoveryInfo discoveryInfo = {};
        discoveryInfo.SDKVersion = NVSDK_NGX_Version_API;
        discoveryInfo.FeatureID = NVSDK_NGX_Feature_SuperSampling;
        discoveryInfo.Identifier.IdentifierType = NVSDK_NGX_Application_Identifier_Type_Project_Id;
        discoveryInfo.Identifier.v.ProjectDesc.ProjectId = "xray-vulkan-renderer";
        discoveryInfo.Identifier.v.ProjectDesc.EngineType = NVSDK_NGX_ENGINE_TYPE_CUSTOM;
        discoveryInfo.Identifier.v.ProjectDesc.EngineVersion = "1.0.0";
        discoveryInfo.ApplicationDataPath = appPath;
        discoveryInfo.FeatureInfo = &featureInfo;

        NVSDK_NGX_FeatureRequirement featureReq = {};
        NVSDK_NGX_Result reqRes = NVSDK_NGX_VULKAN_GetFeatureRequirements(
            VulkanHW.m_Instance, VulkanHW.m_PhysicalDevice,
            &discoveryInfo, &featureReq);

        if (NVSDK_NGX_SUCCEED(reqRes))
        {
            Msg("[DLSS] FeatureRequirements: supported=0x%X, minHWArch=%u, minOS=%s",
                (u32)featureReq.FeatureSupported, featureReq.MinHWArchitecture, featureReq.MinOSVersion);
            if (featureReq.FeatureSupported != NVSDK_NGX_FeatureSupportResult_Supported)
            {
                if (featureReq.FeatureSupported & NVSDK_NGX_FeatureSupportResult_CheckNotPresent)
                    Msg("![DLSS]   Reason: Feature check/DLL not present");
                if (featureReq.FeatureSupported & NVSDK_NGX_FeatureSupportResult_DriverVersionUnsupported)
                    Msg("![DLSS]   Reason: Driver version unsupported");
                if (featureReq.FeatureSupported & NVSDK_NGX_FeatureSupportResult_AdapterUnsupported)
                    Msg("![DLSS]   Reason: Adapter (GPU) unsupported");
                if (featureReq.FeatureSupported & NVSDK_NGX_FeatureSupportResult_OSVersionBelowMinimumSupported)
                    Msg("![DLSS]   Reason: OS version too old");
                if (featureReq.FeatureSupported & NVSDK_NGX_FeatureSupportResult_NotImplemented)
                    Msg("![DLSS]   Reason: Feature not implemented");
            }
        }
        else
        {
            Msg("[DLSS] GetFeatureRequirements failed: 0x%08X", (u32)reqRes);
        }
    }

    if (NVSDK_NGX_FAILED(res) || !dlssAvailable)
    {
        Msg("![DLSS] DLSS not supported on this GPU (available=%d)", dlssAvailable);
        m_bInitialized = true;
        m_bAvailable = false;
        return false;
    }

    m_bInitialized = true;
    m_bAvailable = true;
    Msg("[DLSS] NGX initialized — DLSS is available");
    return true;
}

// ============================================================================
// Shutdown — release NGX runtime
// ============================================================================
void CDlssManager::Shutdown()
{
    if (!m_bInitialized)
        return;

    DestroyFeature();

    if (m_Params)
    {
        NVSDK_NGX_VULKAN_DestroyParameters(m_Params);
        m_Params = nullptr;
    }

    NVSDK_NGX_VULKAN_Shutdown1(VulkanHW.m_Device);
    m_bInitialized = false;
    m_bAvailable = false;

    Msg("[DLSS] NGX shutdown");
}

// ============================================================================
// CreateFeature — allocate DLSS feature for given dimensions
// ============================================================================
bool CDlssManager::CreateFeature(u32 renderW, u32 renderH, u32 displayW, u32 displayH, u32 qualityMode)
{
    if (!m_bAvailable || !m_Params)
    {
        Msg("![DLSS] Cannot create feature — NGX not available");
        return false;
    }

    // Destroy previous feature if any
    DestroyFeature();

    Msg("[DLSS] Creating feature: render %dx%d → display %dx%d, quality=%d",
        renderW, renderH, displayW, displayH, qualityMode);

    // Need a one-shot command buffer for feature creation
    VkCommandBuffer cmd = VulkanHW.BeginSingleTimeCommands();
    if (cmd == VK_NULL_HANDLE)
    {
        Msg("![DLSS] Failed to allocate command buffer for feature creation");
        return false;
    }

    // Set render preset hints based on ps_r__dlss_preset
    // Preset K = Transformer model (DLSS 4), Default = let NGX decide
    {
        NVSDK_NGX_DLSS_Hint_Render_Preset preset = NVSDK_NGX_DLSS_Hint_Render_Preset_Default;
        if (ps_r__dlss_preset == DLSS_PRESET_TRANSFORMER)
            preset = NVSDK_NGX_DLSS_Hint_Render_Preset_K;

        m_Params->Set(NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_DLAA, (int)preset);
        m_Params->Set(NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Quality, (int)preset);
        m_Params->Set(NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Balanced, (int)preset);
        m_Params->Set(NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Performance, (int)preset);
        m_Params->Set(NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_UltraPerformance, (int)preset);

        Msg("[DLSS] Render preset: %s", ps_r__dlss_preset == DLSS_PRESET_TRANSFORMER ? "Transformer (K)" : "Default");
    }

    NVSDK_NGX_DLSS_Create_Params dlssCreateParams = {};
    dlssCreateParams.Feature.InWidth       = renderW;
    dlssCreateParams.Feature.InHeight      = renderH;
    dlssCreateParams.Feature.InTargetWidth  = displayW;
    dlssCreateParams.Feature.InTargetHeight = displayH;
    dlssCreateParams.Feature.InPerfQualityValue = MapQuality(qualityMode);
    // HDR input, depth is NOT inverted (0=near, 1=far), MVs are already de-jittered
    dlssCreateParams.InFeatureCreateFlags = NVSDK_NGX_DLSS_Feature_Flags_IsHDR
                                          | NVSDK_NGX_DLSS_Feature_Flags_MVLowRes;

    NVSDK_NGX_Result res = NGX_VULKAN_CREATE_DLSS_EXT1(
        VulkanHW.m_Device,
        cmd,
        1,                  // CreationNodeMask
        1,                  // VisibilityNodeMask
        &m_DlssHandle,
        m_Params,
        &dlssCreateParams
    );

    VulkanHW.EndSingleTimeCommands(cmd);

    if (NVSDK_NGX_FAILED(res))
    {
        Msg("![DLSS] Feature creation failed: 0x%08X", (u32)res);
        m_DlssHandle = nullptr;
        return false;
    }

    m_CurrentQuality = qualityMode;
    Msg("[DLSS] Feature created successfully");
    return true;
}

// ============================================================================
// DestroyFeature — release DLSS feature handle
// ============================================================================
void CDlssManager::DestroyFeature()
{
    if (m_DlssHandle)
    {
        NVSDK_NGX_VULKAN_ReleaseFeature(m_DlssHandle);
        m_DlssHandle = nullptr;
        Msg("[DLSS] Feature destroyed");
    }
    m_CurrentQuality = 0;
}

// ============================================================================
// Evaluate — run DLSS upscaling
// ============================================================================
void CDlssManager::Evaluate(VkCommandBuffer cmd,
                            VkImage colorIn,   VkImageView colorView,
                            VkImage depthIn,   VkImageView depthView,
                            VkImage mvIn,      VkImageView mvView,
                            VkImage output,    VkImageView outputView,
                            VkImage exposure,  VkImageView exposureView,
                            float jitterX, float jitterY,
                            u32 renderW, u32 renderH,
                            u32 displayW, u32 displayH)
{
    if (!m_DlssHandle || !m_Params)
        return;

    // Color input (render resolution, R16G16B16A16_SFLOAT)
    NVSDK_NGX_Resource_VK colorRes = NVSDK_NGX_Create_ImageView_Resource_VK(
        colorView, colorIn,
        {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
        VK_FORMAT_R16G16B16A16_SFLOAT,
        renderW, renderH,
        false   // readWrite = false (input)
    );

    // Depth input (render resolution, D32_SFLOAT)
    NVSDK_NGX_Resource_VK depthRes = NVSDK_NGX_Create_ImageView_Resource_VK(
        depthView, depthIn,
        {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1},
        VK_FORMAT_D32_SFLOAT,
        renderW, renderH,
        false
    );

    // Motion vectors (render resolution, R16G16_SFLOAT)
    NVSDK_NGX_Resource_VK mvRes = NVSDK_NGX_Create_ImageView_Resource_VK(
        mvView, mvIn,
        {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
        VK_FORMAT_R16G16_SFLOAT,
        renderW, renderH,
        false
    );

    // Output (display resolution, R16G16B16A16_SFLOAT)
    NVSDK_NGX_Resource_VK outputRes = NVSDK_NGX_Create_ImageView_Resource_VK(
        outputView, output,
        {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
        VK_FORMAT_R16G16B16A16_SFLOAT,
        displayW, displayH,
        true    // readWrite = true (output)
    );

    // Exposure (1x1 R32F)
    NVSDK_NGX_Resource_VK exposureRes = NVSDK_NGX_Create_ImageView_Resource_VK(
        exposureView, exposure,
        {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
        VK_FORMAT_R32_SFLOAT,
        1, 1,
        false
    );

    // Set motion vector scale
    // Our MVs are (currNDC - prevNDC) * 0.5, range [-0.5, 0.5]
    // To get pixel-space: multiply by renderWidth (for X), renderHeight (for Y)
    // Negative X because DLSS expects positive = right motion
    m_Params->Set(NVSDK_NGX_Parameter_MV_Scale_X, -(float)renderW);
    m_Params->Set(NVSDK_NGX_Parameter_MV_Scale_Y,  (float)renderH);

    NVSDK_NGX_VK_DLSS_Eval_Params evalParams = {};
    evalParams.Feature.pInColor   = &colorRes;
    evalParams.Feature.pInOutput  = &outputRes;
    evalParams.pInDepth           = &depthRes;
    evalParams.pInMotionVectors   = &mvRes;
    evalParams.pInExposureTexture = &exposureRes;
    evalParams.Feature.InSharpness = 0.0f;
    evalParams.InJitterOffsetX    = jitterX;   // Pixel-space jitter
    evalParams.InJitterOffsetY    = jitterY;
    evalParams.InRenderSubrectDimensions = {renderW, renderH};

    NVSDK_NGX_Result res = NGX_VULKAN_EVALUATE_DLSS_EXT(
        cmd,
        m_DlssHandle,
        m_Params,
        &evalParams
    );

    if (NVSDK_NGX_FAILED(res))
    {
        Msg("![DLSS] Evaluate failed: 0x%08X", (u32)res);
    }
}

// ============================================================================
// GetOptimalResolution — query recommended render resolution
// ============================================================================
void CDlssManager::GetOptimalResolution(u32 displayW, u32 displayH, u32 quality,
                                        u32& outRenderW, u32& outRenderH)
{
    if (!m_bAvailable || !m_Params)
    {
        outRenderW = displayW;
        outRenderH = displayH;
        return;
    }

    // DLAA renders at native resolution
    if (quality == DLSS_DLAA)
    {
        outRenderW = displayW;
        outRenderH = displayH;
        return;
    }

    u32 optW = 0, optH = 0;
    u32 maxW = 0, maxH = 0;
    u32 minW = 0, minH = 0;
    float sharpness = 0.0f;

    NVSDK_NGX_Result res = NGX_DLSS_GET_OPTIMAL_SETTINGS(
        m_Params,
        displayW, displayH,
        MapQuality(quality),
        &optW, &optH,
        &maxW, &maxH,
        &minW, &minH,
        &sharpness
    );

    if (NVSDK_NGX_FAILED(res) || optW == 0 || optH == 0)
    {
        Msg("![DLSS] GetOptimalSettings failed, falling back to display resolution");
        outRenderW = displayW;
        outRenderH = displayH;
        return;
    }

    outRenderW = optW;
    outRenderH = optH;
    Msg("[DLSS] Optimal render resolution for %dx%d quality=%d: %dx%d (range %dx%d - %dx%d)",
        displayW, displayH, quality, optW, optH, minW, minH, maxW, maxH);
}
