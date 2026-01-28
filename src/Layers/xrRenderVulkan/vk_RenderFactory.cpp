// Vulkan Render Factory - Stub implementations
// Provides dxRenderFactory with stub implementations for Vulkan

#include "stdafx.h"
#pragma hdrstop

#include "../xrRender/dxRenderFactory.h"
#include "../../3rd party/imgui/imgui.h"

// Vulkan subsystem headers
#include "HW_Vulkan.h"
#include "vk_swapchain.h"
#include "vk_command_buffer.h"
#include "vk_sync.h"
#include "vk_R_Backend.h"
#include "vk_texture.h"
#include "vk_ui_shader.h"
#include "vk_rendertarget.h"
#include "rvk.h"
#include "vk_debug.h"

// Forward declare stub classes
class vkUISequenceVideoItem;
class vkUIShader;
class vkStatGraphRender;
class vkConsoleRender;
class vkRenderDeviceRender;
class vkApplicationRender;
class vkWallMarkArray;
class vkStatsRender;
class vkFlareRender;
class vkThunderboltRender;
class vkThunderboltDescRender;
class vkRainRender;
class vkLensFlareRender;
class vkImGuiRender;
class vkEnvironmentRender;
class vkEnvDescriptorMixerRender;
class vkEnvDescriptorRender;
class vkFontRender;
#ifdef DEBUG
class vkObjectSpaceRender;
#endif

// Include interface headers
#include "../../Include/xrRender/UISequenceVideoItem.h"
#include "../../Include/xrRender/UIShader.h"
#include "../../Include/xrRender/UIRender.h"
#include "../../Include/xrRender/StatGraphRender.h"
#include "../../Include/xrRender/ConsoleRender.h"
#include "../../Include/xrRender/RenderDeviceRender.h"
#include "../../Include/xrRender/ApplicationRender.h"
#include "../../Include/xrRender/WallMarkArray.h"
#include "../../Include/xrRender/StatsRender.h"
#include "../../Include/xrRender/ThunderboltRender.h"
#include "../../Include/xrRender/ThunderboltDescRender.h"
#include "../../Include/xrRender/RainRender.h"
#include "../../Include/xrRender/LensFlareRender.h"  // Contains both IFlareRender and ILensFlareRender
#include "../../Include/xrRender/ImGuiRender.h"
#include "../../Include/xrRender/EnvironmentRender.h"
#include "../../Include/xrRender/FontRender.h"
#ifdef DEBUG
#include "../../Include/xrRender/ObjectSpaceRender.h"
#endif

// Engine headers for font rendering
#include "../../xrEngine/GameFont.h"

// Forward declarations
class CApplication;
class CStatGraph;
class CEffect_Thunderbolt;
class CEffect_Rain;
class CLensFlare;
class CEnvironment;
class CEnvDescriptor;
struct ImDrawData;
struct ImGuiContext;

// External functions from xrRender_Vulkan.cpp
extern "C" void VulkanUI_EndPass();
extern "C" void VulkanUI_ReplayDeferred();
extern "C" VkDescriptorSetLayout VulkanUI_GetDescriptorSetLayout();
extern "C" VkDescriptorPool VulkanUI_GetDescriptorPool();
extern "C" void VulkanUI_ResetFrameStats(u32 frame);
extern "C" void VulkanUI_LogFrameStats();

// ============================================================================
// Stub class implementations
// ============================================================================

class vkUISequenceVideoItem : public IUISequenceVideoItem
{
public:
    void Copy(IUISequenceVideoItem&) override {}
    bool HasTexture() override { return false; }
    void CaptureTexture() override {}
    void ResetTexture() override {}
    BOOL video_IsPlaying() override { return FALSE; }
    void video_Sync(u32) override {}
    void video_Play(BOOL, u32) override {}
    void video_Stop() override {}
};

// ============================================================================
// Global UI Texture Cache - Struct must be defined before vkUIShader
// ============================================================================
struct UITextureFrame
{
	VK::CVulkanTexture texture;
	VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
};

struct UITextureCacheEntry
{
	// Single texture mode (non-animated)
	VK::CVulkanTexture texture;
	VkDescriptorSet descriptorSet = VK_NULL_HANDLE;

	// Sequence animation mode
	xr_vector<UITextureFrame*> seqFrames;
	u32 seqMSPF = 0;  // Milliseconds per frame
	bool seqCyclic = true;

	int refCount = 0;  // Number of UIShaders using this texture

	bool IsSequence() const { return !seqFrames.empty(); }

	VkDescriptorSet GetCurrentDescriptorSet() const
	{
		if (!IsSequence())
			return descriptorSet;

		if (seqFrames.empty() || seqMSPF == 0)
			return descriptorSet;

		// Calculate current frame based on time
		u32 time = Device.dwTimeContinual;
		u32 totalDuration = seqMSPF * (u32)seqFrames.size();
		u32 cycleTime = time % totalDuration;
		u32 frameIndex = cycleTime / seqMSPF;

		if (frameIndex >= seqFrames.size())
			frameIndex = (u32)seqFrames.size() - 1;

		return seqFrames[frameIndex]->descriptorSet;
	}

	~UITextureCacheEntry()
	{
		for (auto* frame : seqFrames)
			xr_delete(frame);
		seqFrames.clear();
	}
};

static xr_map<xr_string, UITextureCacheEntry*> g_UITextureCache;

// Forward declarations for texture cache functions
static UITextureCacheEntry* UITextureCache_LoadAndCache(const xr_string& textureName);
static UITextureCacheEntry* UITextureCache_Get(const xr_string& textureName);
static void UITextureCache_Release(const xr_string& textureName);

class vkUIShader : public IVkUIShader
{
	xr_string m_ShaderName;
	xr_string m_TextureName;
	bool m_bInited;
	UITextureCacheEntry* m_pCacheEntry;  // Points to shared cached texture (NOT owned)
	u32 m_TextureWidth;   // Cached texture dimensions (preserved during copy)
	u32 m_TextureHeight;
public:
	vkUIShader() : m_bInited(false), m_pCacheEntry(nullptr), m_TextureWidth(1), m_TextureHeight(1) {}

    void Copy(IUIShader& other) override
	{
		vkUIShader* pOther = static_cast<vkUIShader*>(&other);
		m_ShaderName = pOther->m_ShaderName;
		m_TextureName = pOther->m_TextureName;
		m_bInited = pOther->m_bInited;
		// Share cache entry - texture stays alive in cache
		m_pCacheEntry = pOther->m_pCacheEntry;
		if (m_pCacheEntry) {
			m_pCacheEntry->refCount++;  // Increment ref count
		}
		// Copy cached texture dimensions so GetTextureWidth/Height work after copy
		m_TextureWidth = pOther->m_TextureWidth;
		m_TextureHeight = pOther->m_TextureHeight;
	}

    void create(LPCSTR sh, LPCSTR tex) override
	{
		m_ShaderName = sh ? sh : "";
		m_TextureName = tex ? tex : "";
		m_bInited = true;  // Always report as inited so UI code tries to draw
		Msg("[Vulkan UI] UIShader created: shader=%s, texture=%s",
			m_ShaderName.c_str(), m_TextureName.c_str());

		// Load texture from cache if specified
		if (m_TextureName.length() > 0) {
			LoadTextureFromCache();
		}
	}

    bool inited() override
	{
		return m_bInited;
	}

    void destroy() override
	{
		// Release our reference to cached texture (but don't destroy it!)
		if (m_pCacheEntry) {
			UITextureCache_Release(m_TextureName);
			m_pCacheEntry = nullptr;
		}

		m_bInited = false;
		m_ShaderName.clear();
		m_TextureName.clear();
	}

	const char* GetTextureName() const { return m_TextureName.c_str(); }

	// IVkUIShader interface implementation
	VkDescriptorSet GetDescriptorSet() override
	{
		// Return descriptor set from cache if available
		// For sequences, this returns the current animated frame
		if (m_pCacheEntry) {
			return m_pCacheEntry->GetCurrentDescriptorSet();
		}
		return VK_NULL_HANDLE;
	}

	u32 GetTextureWidth() const override
	{
		// Use cached dimensions - they're preserved during Copy()
		return m_TextureWidth;
	}

	u32 GetTextureHeight() const override
	{
		// Use cached dimensions - they're preserved during Copy()
		return m_TextureHeight;
	}

private:
	void LoadTextureFromCache()
	{
		// Get or load texture from global cache
		m_pCacheEntry = UITextureCache_LoadAndCache(m_TextureName);

		if (m_pCacheEntry) {
			// Cache texture dimensions - for sequences, use first frame dimensions
			if (m_pCacheEntry->IsSequence() && !m_pCacheEntry->seqFrames.empty()) {
				m_TextureWidth = m_pCacheEntry->seqFrames[0]->texture.GetWidth();
				m_TextureHeight = m_pCacheEntry->seqFrames[0]->texture.GetHeight();
				Msg("[Vulkan UI] Using cached sequence: %s (%dx%d, %d frames)",
					m_TextureName.c_str(), m_TextureWidth, m_TextureHeight,
					(int)m_pCacheEntry->seqFrames.size());
			} else {
				m_TextureWidth = m_pCacheEntry->texture.GetWidth();
				m_TextureHeight = m_pCacheEntry->texture.GetHeight();
				Msg("[Vulkan UI] Using cached texture: %s (%dx%d)",
					m_TextureName.c_str(), m_TextureWidth, m_TextureHeight);
			}
		}
	}
};

class vkStatGraphRender : public IStatGraphRender
{
public:
    void Copy(IStatGraphRender&) override {}
    void OnDeviceCreate() override {}
    void OnDeviceDestroy() override {}
    void OnRender(CStatGraph&) override {}
};

class vkConsoleRender : public IConsoleRender
{
public:
    void Copy(IConsoleRender&) override {}
    void OnRender(bool) override {}
};

class vkRenderDeviceRender : public IRenderDeviceRender
{
    // Frame state tracking
    u32             m_CurrentImageIndex = UINT32_MAX;
    VkCommandBuffer m_CurrentCmd        = VK_NULL_HANDLE;
    bool            m_bFrameActive      = false;
    bool            m_bDeviceCreated    = false;
    bool            m_bNeedsWindowReposition = false;
    u32             m_repositionW       = 0;
    u32             m_repositionH       = 0;

public:
    void Copy(IRenderDeviceRender&) override {}
    void setGamma(float) override {}
    void setBrightness(float) override {}
    void setContrast(float) override {}
    void updateGamma() override {}
    void ValidateHW() override {}
    void SetupGPU(BOOL, BOOL, BOOL) override { vk_log("SetupGPU"); }
    void overdrawBegin() override {}
    void overdrawEnd() override {}
    void DeferredLoad(BOOL) override {}
    void ResourcesDeferredUpload() override {}
    void ResourcesDeferredUnload() override {}
    void ResourcesGetMemoryUsage(u32&, u32&, u32&, u32&) override {}
    void ResourcesDestroyNecessaryTextures() override {}
    void ResourcesStoreNecessaryTextures() override {}
    void ResourcesDumpMemoryUsage() override {}
    void ResourcesPrefetchCreateTexture(LPCSTR) override {}
    bool HWSupportsShaderYUV2RGB() override { return false; }
    DeviceState GetDeviceState() override { return DeviceState::dsOK; }
    BOOL GetForceGPU_REF() override { return FALSE; }
    u32 GetCacheStatPolys() override { return RCache.stat.polys; }
    void ClearTarget() override {}
    void SetCacheXform(Fmatrix& mView, Fmatrix& mProject) override {}
    void SetCacheXform_prev(Fmatrix& mView, Fmatrix& mProject) override {}
    void OnAssetsChanged() override {}

    // ========================================================================
    // Create - Initialize Vulkan device, swapchain, command buffers, sync
    // ========================================================================
    void Create(HWND hwnd, u32& w, u32& h, float& fw, float& fh, bool b) override
    {
        vk_log(std::format("Create {}x{}", w, h));

        // Make window fullscreen BEFORE creating the Vulkan surface/swapchain.
        // Device_create.cpp does this AFTER m_pRender->Create(), but for Vulkan
        // the swapchain size depends on the window/surface size, so we must set
        // the window to its final size first.  This also ensures ClipCursor
        // (called by Device_create.cpp after Create returns) uses the correct rect.
        {
            extern u32 g_screenmode;
            extern void GetMonitorResolution(u32& horizontal, u32& vertical);

            if (g_screenmode >= 1 && hwnd) {
                u32 monW = 0, monH = 0;
                GetMonitorResolution(monW, monH);
                if (monW > 0 && monH > 0) {
                    SetWindowLongPtr(hwnd, GWL_STYLE, WS_VISIBLE | WS_POPUP);
                    SetWindowPos(hwnd, HWND_TOP, 0, 0, monW, monH,
                                 SWP_FRAMECHANGED | SWP_SHOWWINDOW);
                    w = monW;
                    h = monH;
                    fw = float(w) / 2.f;
                    fh = float(h) / 2.f;
                    Msg("[Vulkan] Window set to borderless fullscreen: %ux%u before device init", w, h);
                }
            }
        }

        // If dimensions are 0, query actual window size from HWND
        if ((w == 0 || h == 0) && hwnd) {
            RECT rect;
            if (GetClientRect(hwnd, &rect)) {
                u32 actualW = rect.right - rect.left;
                u32 actualH = rect.bottom - rect.top;
                if (actualW > 0 && actualH > 0) {
                    w = actualW;
                    h = actualH;
                    fw = float(w) / 2.f;
                    fh = float(h) / 2.f;
                    vk_log(std::format("Window size from HWND: {}x{}", w, h));
                } else {
                    // Fallback to a reasonable default
                    w = 1280;
                    h = 720;
                    fw = float(w) / 2.f;
                    fh = float(h) / 2.f;
                    vk_warn(std::format("Using fallback window size: {}x{}", w, h));
                }
            }
        }

        // Step 1: Create Vulkan device (instance, physical device, logical device, VMA)
        VulkanHW.CreateDevice(hwnd);
        vk_log("VulkanHW device created");

        // Step 2: Create swapchain
        Swapchain.Create(w, h);
        vk_log(std::format("Swapchain created: {}x{}", w, h));

        // Step 3: Create command pools and buffers
        CommandManager.Create();
        vk_log("CommandManager created");

        // Step 4: Create synchronization primitives
        Sync.Create();
        vk_log("Sync primitives created");

        m_bDeviceCreated = true;
        vk_log("Create complete");
    }

    // ========================================================================
    // SetupStates - Called after Create, set default render states
    // ========================================================================
    void SetupStates() override
    {
        vk_log("SetupStates");
    }

    // ========================================================================
    // OnDeviceCreate - Called to initialize renderer subsystems
    // ========================================================================
    void OnDeviceCreate(LPCSTR shName) override
    {
        vk_log(std::format("OnDeviceCreate('{}')", shName ? shName : "null"));

        // Initialize the main renderer (CRender) which creates:
        // - HOM, ModelPool, ShaderManager, DescriptorManager
        // - PipelineManager, MaterialManager, BufferPool
        // - G-Buffer render target
        RImplementation.create();

        vk_log("OnDeviceCreate complete");
    }

    // ========================================================================
    // Begin - Start a new frame: wait for fence, acquire image, begin cmd buffer
    // ========================================================================
    void Begin() override
    {

        if (!m_bDeviceCreated) {
            return;
        }

        // Reset per-frame UI stats
        VulkanUI_ResetFrameStats(Device.dwFrame);

        // Lazy swapchain creation: if swapchain wasn't created (e.g., window was 0x0),
        // try to create it now that the window may have been resized
        if (Swapchain.m_Swapchain == VK_NULL_HANDLE && VulkanHW.m_hWnd) {
            RECT rect;
            if (GetClientRect(VulkanHW.m_hWnd, &rect)) {
                u32 w = rect.right - rect.left;
                u32 h = rect.bottom - rect.top;
                if (w > 0 && h > 0) {
                    vk_log(std::format("Lazy swapchain creation: {}x{}", w, h));
                    Swapchain.Create(w, h);
                }
            }
        }

        if (!Swapchain.ShouldRender()) {
            return;
        }

        u32 frameIndex = CommandManager.GetCurrentFrame();
        FrameSync& sync = Sync.GetCurrentFrame(frameIndex);

        // 1. Wait for previous frame to finish on this slot
        Sync.WaitForFence(frameIndex);

        // 2. Acquire next swapchain image
        m_CurrentImageIndex = Swapchain.AcquireNextImage(sync.imageAvailable);
        if (m_CurrentImageIndex == UINT32_MAX) {
            // Swapchain out of date - need recreation
            vk_warn("Swapchain out of date, skipping frame");
            m_bFrameActive = false;
            return;
        }

        // 2b. Resolution sync — after swapchain recreation the extent may have
        // changed (e.g. 640x480 → 2560x1600).  Update Device dimensions and
        // recreate G-Buffer render targets so everything matches.
        {
            u32 swW = Swapchain.m_Extent.width;
            u32 swH = Swapchain.m_Extent.height;
            if (swW != Device.dwWidth || swH != Device.dwHeight) {
                Msg("[Vulkan] Resolution sync: Device %ux%u -> %ux%u (Swapchain)",
                    Device.dwWidth, Device.dwHeight, swW, swH);
                Device.dwWidth  = swW;
                Device.dwHeight = swH;
                Device.fWidth_2  = float(swW) / 2.f;
                Device.fHeight_2 = float(swH) / 2.f;

                // Defer window repositioning to End() — calling SetWindowPos
                // here (inside Begin) would trigger synchronous WM_SIZE messages
                // that corrupt ImGui state before ImGui::Render().
                m_bNeedsWindowReposition = true;
                m_repositionW = swW;
                m_repositionH = swH;

                // Release cursor clipping entirely.  The engine's WM_ACTIVATE
                // handler clips the cursor to the client rect of the window,
                // but at startup the window is still at CW_USEDEFAULT position
                // (640x480), producing a wrong rect.  For borderless fullscreen
                // the cursor is physically bounded by the monitor, so releasing
                // clipping is safe and avoids fighting with the activation handler.
                ClipCursor(NULL);
                Msg("[Vulkan] Cursor clipping released (borderless fullscreen)");

                // Recreate G-Buffer render targets at new resolution
                if (RTarget) {
                    vkDeviceWaitIdle(VulkanHW.m_Device);
                    RTarget->OnResize(swW, swH);
                    Msg("[Vulkan] G-Buffer resized to %ux%u", swW, swH);
                }
            }
        }

        // 3. Reset fence only after successful acquire
        Sync.ResetFence(frameIndex);

        // 4. Begin command buffer
        m_CurrentCmd = CommandManager.Begin();

        // 5. Connect command buffer to RCache so rendering commands work
        RCache.BeginCommandBuffer(m_CurrentCmd);

        // Nothing has rendered to the swapchain yet this frame
        Swapchain.m_bRenderedThisFrame = false;

        m_bFrameActive = true;
    }

    // ========================================================================
    // Clear - Clear render targets (called between Begin and scene render)
    // ========================================================================
    void Clear() override
    {
        // Clearing is handled per-pass by the Vulkan renderer
    }

    // ========================================================================
    // End - Finish frame: end cmd buffer, submit, present
    // ========================================================================
    void End() override
    {
        if (!m_bDeviceCreated || !m_bFrameActive)
            return;

        u32 frameIndex = CommandManager.GetCurrentFrame();
        FrameSync& sync = Sync.GetCurrentFrame(frameIndex);

        // Replay deferred UI draw calls (from FrameMove, before cmd buffer was started)
        VulkanUI_ReplayDeferred();

        // End UI pass if active (before ending command buffer)
        VulkanUI_EndPass();

        // Fallback: if nothing rendered to the swapchain this frame,
        // clear it to black and transition to PRESENT_SRC so we don't present garbage.
        if (!Swapchain.m_bRenderedThisFrame) {
            VkImage swapImg = Swapchain.GetCurrentImage();
            if (swapImg != VK_NULL_HANDLE) {
                VkImageMemoryBarrier bar = {};
                bar.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                bar.srcAccessMask = 0;
                bar.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                bar.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                bar.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                bar.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                bar.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                bar.image = swapImg;
                bar.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                bar.subresourceRange.baseMipLevel = 0;
                bar.subresourceRange.levelCount = 1;
                bar.subresourceRange.baseArrayLayer = 0;
                bar.subresourceRange.layerCount = 1;

                vkCmdPipelineBarrier(m_CurrentCmd,
                    VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                    VK_PIPELINE_STAGE_TRANSFER_BIT,
                    0, 0, nullptr, 0, nullptr, 1, &bar);

                VkClearColorValue clearColor = {{0.0f, 0.0f, 0.0f, 1.0f}};
                VkImageSubresourceRange range = {};
                range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                range.baseMipLevel = 0;
                range.levelCount = 1;
                range.baseArrayLayer = 0;
                range.layerCount = 1;
                vkCmdClearColorImage(m_CurrentCmd, swapImg,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearColor, 1, &range);

                bar.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                bar.dstAccessMask = 0;
                bar.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                bar.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

                vkCmdPipelineBarrier(m_CurrentCmd,
                    VK_PIPELINE_STAGE_TRANSFER_BIT,
                    VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                    0, 0, nullptr, 0, nullptr, 1, &bar);
            }
        }

        // Log UI frame stats (throttled)
        VulkanUI_LogFrameStats();

        // Disconnect command buffer from RCache
        RCache.EndCommandBuffer();

        // End command buffer recording
        CommandManager.End(m_CurrentCmd);

        // Submit command buffer with synchronization:
        //   Wait on: imageAvailable (swapchain image ready)
        //   Signal:  renderFinished (rendering complete)
        //   Fence:   inFlightFence (CPU can proceed when GPU done)
        CommandManager.Submit(m_CurrentCmd, sync.imageAvailable, sync.renderFinished, sync.inFlightFence);

        // Present the rendered image
        Swapchain.Present(sync.renderFinished, m_CurrentImageIndex);

        // Advance to next frame slot
        CommandManager.NextFrame();

        m_bFrameActive = false;
        m_CurrentCmd = VK_NULL_HANDLE;

        // Deferred window repositioning — safe to call here because
        // ImGui::Render() has already finished (it runs between Begin/End).
        if (m_bNeedsWindowReposition) {
            m_bNeedsWindowReposition = false;
            if (VulkanHW.m_hWnd) {
                SetWindowLongPtr(VulkanHW.m_hWnd, GWL_STYLE, WS_VISIBLE | WS_POPUP);
                SetWindowPos(VulkanHW.m_hWnd, HWND_TOP, 0, 0, m_repositionW, m_repositionH,
                             SWP_FRAMECHANGED | SWP_SHOWWINDOW);

                // Release cursor clipping — the engine's WM_ACTIVATE handler
                // may have re-clipped to a stale rect during SetWindowPos.
                ClipCursor(NULL);

                Msg("[Vulkan] Window repositioned: 0,0 %ux%u (borderless fullscreen)",
                    m_repositionW, m_repositionH);
            }
        }

        // End() complete
    }

    // ========================================================================
    // Reset - Handle window resize (recreate swapchain)
    // ========================================================================
    void Reset(HWND hwnd, u32& w, u32& h, float& fw, float& fh) override
    {
        if (!m_bDeviceCreated) return;

        vk_log(std::format("Reset {}x{}", w, h));

        // Wait for GPU to finish all work
        if (VulkanHW.m_Device != VK_NULL_HANDLE) {
            vkDeviceWaitIdle(VulkanHW.m_Device);
        }

        // Notify renderer to release size-dependent resources
        RImplementation.reset_begin();

        // Recreate swapchain with new dimensions
        Swapchain.Recreate(w, h);

        // Notify renderer to recreate size-dependent resources
        RImplementation.reset_end();

        vk_log("Reset complete");
    }

    // ========================================================================
    // OnDeviceDestroy - Cleanup renderer subsystems
    // ========================================================================
    void OnDeviceDestroy(BOOL bKeepTextures) override
    {
        if (!m_bDeviceCreated) return;

        vk_log("OnDeviceDestroy");

        // Wait for GPU to finish
        if (VulkanHW.m_Device != VK_NULL_HANDLE) {
            vkDeviceWaitIdle(VulkanHW.m_Device);
        }

        // Destroy renderer subsystems
        RImplementation.destroy();

        vk_log("OnDeviceDestroy complete");
    }

    // ========================================================================
    // DestroyHW - Destroy Vulkan device and all GPU resources
    // ========================================================================
    void DestroyHW() override
    {
        if (!m_bDeviceCreated) return;

        vk_log("DestroyHW");

        // Wait for GPU
        if (VulkanHW.m_Device != VK_NULL_HANDLE) {
            vkDeviceWaitIdle(VulkanHW.m_Device);
        }

        // Destroy in reverse order of creation
        Sync.Destroy();
        CommandManager.Destroy();
        Swapchain.Destroy();
        VulkanHW.DestroyDevice();

        m_bDeviceCreated = false;
        vk_log("DestroyHW complete");
    }
};

class vkApplicationRender : public IApplicationRender
{
public:
    void Copy(IApplicationRender&) override {}
    void LoadBegin() override {}
    void destroy_loading_shaders() override {}
    void setLevelLogo(LPCSTR) override {}
    void load_draw_internal(CApplication&) override {}
    void KillHW() override {}
};

class vkWallMarkArray : public IWallMarkArray
{
public:
    void Copy(IWallMarkArray&) override {}
    void AppendMark(LPCSTR) override {}
    void clear() override {}
    bool empty() override { return true; }
    wm_shader GenerateWallmark() override { return wm_shader(); }
};

class vkStatsRender : public IStatsRender
{
public:
    void Copy(IStatsRender&) override {}
    void OutData1(CGameFont&) override {}
    void OutData2(CGameFont&) override {}
    void OutData3(CGameFont&) override {}
    void OutData4(CGameFont&) override {}
    void GuardVerts(CGameFont&) override {}
    void GuardDrawCalls(CGameFont&) override {}
    void SetDrawParams(IRenderDeviceRender*) override {}
};

class vkFlareRender : public IFlareRender
{
public:
    void Copy(IFlareRender&) override {}
    void CreateShader(LPCSTR, LPCSTR) override {}
    void DestroyShader() override {}
};

class vkThunderboltRender : public IThunderboltRender
{
public:
    void Copy(IThunderboltRender&) override {}
    void Render(CEffect_Thunderbolt&) override {}
};

class vkThunderboltDescRender : public IThunderboltDescRender
{
public:
    void Copy(IThunderboltDescRender&) override {}
    void CreateModel(LPCSTR) override {}
    void DestroyModel() override {}
};

class vkRainRender : public IRainRender
{
public:
    void Copy(IRainRender&) override {}
    void Render(CEffect_Rain&) override {}
    const Fsphere& GetDropBounds() const override { static Fsphere s; return s; }
};

class vkLensFlareRender : public ILensFlareRender
{
public:
    void Copy(ILensFlareRender&) override {}
    void Render(CLensFlare&, BOOL, BOOL, BOOL) override {}
    void OnDeviceCreate() override {}
    void OnDeviceDestroy() override {}
};

class vkImGuiRender : public IImGuiRender
{
    ImGuiContext* m_ctx = nullptr;
    bool m_fontsBuilt = false;
public:
    void Copy(IImGuiRender&) override {}
    void Frame() override
    {
        // Ensure fonts are built before ImGui::NewFrame() is called
        if (m_ctx && !m_fontsBuilt)
        {
            ImGui::SetCurrentContext(m_ctx);
            ImFontAtlas* fonts = ImGui::GetIO().Fonts;
            if (fonts->Fonts.Size == 0)
                fonts->AddFontDefault();
            unsigned char* pixels = nullptr;
            int width = 0, height = 0;
            fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
            m_fontsBuilt = true;
        }
    }
    void Render(ImDrawData*) override {}
    void OnDeviceCreate(ImGuiContext* ctx) override
    {
        m_ctx = ctx;
        m_fontsBuilt = false;
    }
    void OnDeviceDestroy() override { m_ctx = nullptr; m_fontsBuilt = false; }
    void OnDeviceResetBegin() override {}
    void OnDeviceResetEnd() override { m_fontsBuilt = false; }
};

class vkEnvironmentRender : public IEnvironmentRender
{
public:
    void Copy(IEnvironmentRender&) override {}
    void OnFrame(CEnvironment&) override {}
    void OnLoad() override {}
    void OnUnload() override {}
    void RenderSky(CEnvironment&, bool) override {}
    void RenderClouds(CEnvironment&) override {}
    void OnDeviceCreate() override {}
    void OnDeviceDestroy() override {}
    particles_systems::library_interface const& particles_systems_library() override
    {
        static particles_systems::library_interface* dummy = nullptr;
        return *dummy;  // Will crash if called, but these are stubs
    }
};

class vkEnvDescriptorMixerRender : public IEnvDescriptorMixerRender
{
public:
    void Copy(IEnvDescriptorMixerRender&) override {}
    void Destroy() override {}
    void Clear() override {}
    void lerp(IEnvDescriptorRender*, IEnvDescriptorRender*) override {}
};

class vkEnvDescriptorRender : public IEnvDescriptorRender
{
public:
    void Copy(IEnvDescriptorRender&) override {}
    void OnDeviceCreate(CEnvDescriptor&) override {}
    void OnDeviceDestroy() override {}
};

class vkFontRender : public IFontRender
{
	xr_string m_ShaderName;
	xr_string m_TextureName;
	vkUIShader m_Shader;
public:
    void Initialize(LPCSTR cShader, LPCSTR cTexture) override
	{
		m_ShaderName = cShader ? cShader : "";
		m_TextureName = cTexture ? cTexture : "";
		Msg("[Vulkan UI] FontRender initialized: shader=%s, texture=%s",
			m_ShaderName.c_str(), m_TextureName.c_str());

		// Create UI shader with font texture
		m_Shader.create(m_ShaderName.c_str(), m_TextureName.c_str());
	}

    void OnRender(CGameFont& owner) override;
};

#ifdef DEBUG
class vkObjectSpaceRender : public IObjectSpaceRender
{
public:
    void Copy(IObjectSpaceRender&) override {}
    void dbgRender() override {}
    void dbgAddSphere(const Fsphere&, u32) override {}
    void SetShader() override {}
};
#endif

// ============================================================================
// Global UI Texture Cache - Function implementations
// ============================================================================

static UITextureCacheEntry* UITextureCache_LoadAndCache(const xr_string& textureName)
{
	// Check cache first
	auto it = g_UITextureCache.find(textureName);
	if (it != g_UITextureCache.end()) {
		it->second->refCount++;
		return it->second;
	}

	// Not in cache - try to load
	string_path fn;
	bool found = false;
	xr_string actualName = textureName;

	// Create new cache entry
	UITextureCacheEntry* entry = xr_new<UITextureCacheEntry>();

	// First check for .seq sequence file
	if (FS.exist(fn, "$game_textures$", textureName.c_str(), ".seq")) {
		Msg("[Vulkan UI Cache] Found sequence file: %s.seq", textureName.c_str());

		IReader* seqFile = FS.r_open(fn);
		if (seqFile) {
			string256 buffer;

			// Read first line - could be "cycled" or FPS number
			seqFile->r_string(buffer, sizeof(buffer));
			entry->seqCyclic = false;

			if (0 == stricmp(buffer, "cycled")) {
				entry->seqCyclic = true;
				seqFile->r_string(buffer, sizeof(buffer));
			}

			u32 fps = atoi(buffer);
			if (fps > 0) {
				entry->seqMSPF = 1000 / fps;
				Msg("[Vulkan UI Cache] Sequence FPS: %d (MSPF: %d), cyclic: %s",
					fps, entry->seqMSPF, entry->seqCyclic ? "yes" : "no");
			}

			// Read frame texture names
			VkDescriptorSetLayout layout = VulkanUI_GetDescriptorSetLayout();
			VkDescriptorPool pool = VulkanUI_GetDescriptorPool();

			while (!seqFile->eof()) {
				seqFile->r_string(buffer, sizeof(buffer));
				_Trim(buffer);
				if (buffer[0]) {
					string_path frameFn;
					if (FS.exist(frameFn, "$game_textures$", buffer, ".dds")) {
						UITextureFrame* frame = xr_new<UITextureFrame>();
						if (frame->texture.LoadDDS(frameFn)) {
							// Create descriptor set for this frame
							if (layout != VK_NULL_HANDLE && pool != VK_NULL_HANDLE) {
								VkDescriptorSetAllocateInfo allocInfo = {};
								allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
								allocInfo.descriptorPool = pool;
								allocInfo.descriptorSetCount = 1;
								allocInfo.pSetLayouts = &layout;

								if (vkAllocateDescriptorSets(VulkanHW.m_Device, &allocInfo, &frame->descriptorSet) == VK_SUCCESS) {
									VkDescriptorImageInfo imageInfo = {};
									imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
									imageInfo.imageView = frame->texture.GetView();
									imageInfo.sampler = frame->texture.GetSampler();

									VkWriteDescriptorSet write = {};
									write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
									write.dstSet = frame->descriptorSet;
									write.dstBinding = 0;
									write.dstArrayElement = 0;
									write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
									write.descriptorCount = 1;
									write.pImageInfo = &imageInfo;

									vkUpdateDescriptorSets(VulkanHW.m_Device, 1, &write, 0, nullptr);
								}
							}
							entry->seqFrames.push_back(frame);
							Msg("[Vulkan UI Cache] Loaded sequence frame %d: %s", (int)entry->seqFrames.size(), buffer);
						} else {
							xr_delete(frame);
							Msg("! [Vulkan UI Cache] Failed to load sequence frame: %s", buffer);
						}
					}
				}
			}
			FS.r_close(seqFile);

			if (!entry->seqFrames.empty()) {
				found = true;
				Msg("[Vulkan UI Cache] Sequence loaded: %s (%d frames)", textureName.c_str(), (int)entry->seqFrames.size());
			}
		}
	}

	// If no sequence, try regular texture
	if (!found && FS.exist(fn, "$game_textures$", textureName.c_str(), ".dds")) {
		found = true;
	} else if (!found) {
		// Try animated texture fallback (first frame only)
		xr_string firstFrame = textureName + "_01";
		if (FS.exist(fn, "$game_textures$", firstFrame.c_str(), ".dds")) {
			found = true;
			actualName = firstFrame;
			Msg("[Vulkan UI Cache] Animated texture fallback: %s -> %s", textureName.c_str(), firstFrame.c_str());
		}
	}

	if (!found) {
		Msg("! [Vulkan UI Cache] Texture not found: %s", textureName.c_str());
		xr_delete(entry);
		return nullptr;
	}

	// If we found a sequence, we're done loading
	if (entry->IsSequence()) {
		entry->refCount = 1;
		g_UITextureCache[textureName] = entry;
		return entry;
	}

	// Load single texture
	if (!entry->texture.LoadDDS(fn)) {
		Msg("! [Vulkan UI Cache] Failed to load texture: %s", fn);
		xr_delete(entry);
		return nullptr;
	}

	Msg("[Vulkan UI Cache] Loaded texture: %s (%dx%d)",
		textureName.c_str(), entry->texture.GetWidth(), entry->texture.GetHeight());

	// Create descriptor set for the cached texture
	VkDescriptorSetLayout layout = VulkanUI_GetDescriptorSetLayout();
	VkDescriptorPool pool = VulkanUI_GetDescriptorPool();

	if (layout != VK_NULL_HANDLE && pool != VK_NULL_HANDLE) {
		VkDescriptorSetAllocateInfo allocInfo = {};
		allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocInfo.descriptorPool = pool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = &layout;

		VkResult res = vkAllocateDescriptorSets(VulkanHW.m_Device, &allocInfo, &entry->descriptorSet);
		if (res == VK_SUCCESS) {
			VkDescriptorImageInfo imageInfo = {};
			imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			imageInfo.imageView = entry->texture.GetView();
			imageInfo.sampler = entry->texture.GetSampler();

			VkWriteDescriptorSet write = {};
			write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			write.dstSet = entry->descriptorSet;
			write.dstBinding = 0;
			write.dstArrayElement = 0;
			write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			write.descriptorCount = 1;
			write.pImageInfo = &imageInfo;

			vkUpdateDescriptorSets(VulkanHW.m_Device, 1, &write, 0, nullptr);
			Msg("[Vulkan UI Cache] Descriptor set created for: %s", textureName.c_str());
		} else {
			Msg("! [Vulkan UI Cache] Failed to allocate descriptor set: %d", res);
		}
	}

	entry->refCount = 1;
	g_UITextureCache[textureName] = entry;
	return entry;
}

static void UITextureCache_Release(const xr_string& textureName)
{
	auto it = g_UITextureCache.find(textureName);
	if (it != g_UITextureCache.end()) {
		it->second->refCount--;
		// Don't destroy - keep in cache for future use
		// Textures are only destroyed on shutdown
	}
}

// Called on shutdown to cleanup all cached textures
void UITextureCache_DestroyAll()
{
	Msg("[Vulkan UI Cache] Destroying %zu cached textures", g_UITextureCache.size());
	for (auto& pair : g_UITextureCache) {
		if (pair.second) {
			pair.second->texture.Destroy();
			// Descriptor set is freed when pool is destroyed
			xr_delete(pair.second);
		}
	}
	g_UITextureCache.clear();
}

// ============================================================================
// vkUIShader method implementations
// ============================================================================

// CreateDescriptorSet is now handled by UITextureCache_LoadAndCache()

// ============================================================================
// vkFontRender method implementations
// ============================================================================

extern ENGINE_API BOOL g_bRendering;
extern ENGINE_API Fvector2 g_current_font_scale;
extern XRAPI_API IUIRender* UIRender;

#define MAX_MB_CHARS 4096

void vkFontRender::OnRender(CGameFont& owner)
{
	VERIFY(g_bRendering);

	// Check if we have strings to render
	if (owner.strings.empty())
		return;

	// Set font shader
	if (m_Shader.inited())
		UIRender->SetShader(m_Shader);

	// Update texture size in owner if needed
	if (!(owner.uFlags & CGameFont::fsValid))
	{
		Fvector2 texSize;
		UIRender->GetActiveTextureResolution(texSize);
		owner.vTS.set((int)texSize.x, (int)texSize.y);
		owner.fTCHeight = owner.fHeight / float(owner.vTS.y);
		owner.uFlags |= CGameFont::fsValid;
	}

	// Render strings in batches
	for (u32 i = 0; i < owner.strings.size();)
	{
		// Calculate batch size (how many strings fit in MAX_MB_CHARS)
		int count = 1;
		int length = owner.smart_strlen(owner.strings[i].string);

		while ((i + count) < owner.strings.size())
		{
			int L = owner.smart_strlen(owner.strings[i + count].string);
			if ((L + length) < MAX_MB_CHARS)
			{
				count++;
				length += L;
			}
			else
				break;
		}

		// Start primitive (4 vertices per character, triangle list)
		UIRender->StartPrimitive(length * 4, IUIRender::ptTriList, IUIRender::pttTL);

		// Render each string in the batch
		u32 last = i + count;
		for (; i < last; i++)
		{
			CGameFont::String& PS = owner.strings[i];
			wide_char wsStr[MAX_MB_CHARS];

			// Convert to wide string for unicode support
			int len = owner.IsMultibyte() ? mbhMulti2Wide(wsStr, NULL, MAX_MB_CHARS, PS.string) : xr_strlen(PS.string);

			if (len)
			{
				float X = float(iFloor(PS.x));
				float Y = float(iFloor(PS.y));
				float S = PS.height * g_current_font_scale.y;
				float Y2 = Y + S;
				float fSize = 0;

				// Handle text alignment
				if (PS.align)
					fSize = owner.IsMultibyte() ? owner.SizeOf_(wsStr) : owner.SizeOf_(PS.string);

				switch (PS.align)
				{
				case CGameFont::alCenter:
					X -= (iFloor(fSize * 0.5f)) * g_current_font_scale.x;
					break;
				case CGameFont::alRight:
					X -= iFloor(fSize);
					break;
				}

				// Setup colors (gradient makes bottom darker)
				u32 clr, clr2;
				clr2 = clr = PS.c;
				if (owner.uFlags & CGameFont::fsGradient)
				{
					u32 _R = color_get_R(clr) / 2;
					u32 _G = color_get_G(clr) / 2;
					u32 _B = color_get_B(clr) / 2;
					u32 _A = color_get_A(clr);
					clr2 = color_rgba(_R, _G, _B, _A);
				}

				// Render each character
				float tu, tv;
				for (int j = 0; j < len; j++)
				{
					// Get character texture coordinates
					Fvector l = owner.IsMultibyte() ? owner.GetCharTC(wsStr[1 + j]) : owner.GetCharTC((u16)(u8)PS.string[j]);

					float scw = l.z * g_current_font_scale.x;  // Scaled character width
					float fTCWidth = l.z / owner.vTS.x;

					if (!fis_zero(l.z))  // Skip zero-width characters (spaces)
					{
						tu = (l.x / owner.vTS.x);
						tv = (l.y / owner.vTS.y);

						// Generate quad (2 triangles = 6 vertices) for character
						// Triangle 1: bottom-left, top-left, bottom-right
						UIRender->PushPoint(X, Y2, 0, clr2, tu, tv + owner.fTCHeight);
						UIRender->PushPoint(X, Y, 0, clr, tu, tv);
						UIRender->PushPoint(X + scw, Y2, 0, clr2, tu + fTCWidth, tv + owner.fTCHeight);

						// Triangle 2: top-left, bottom-right, top-right
						UIRender->PushPoint(X, Y, 0, clr, tu, tv);
						UIRender->PushPoint(X + scw, Y2, 0, clr2, tu + fTCWidth, tv + owner.fTCHeight);
						UIRender->PushPoint(X + scw, Y, 0, clr, tu + fTCWidth, tv);
					}

					// Advance to next character position
					X += scw * owner.vInterval.x;
					if (owner.IsMultibyte())
					{
						X -= 2;
						if (IsNeedSpaceCharacter(wsStr[1 + j]))
							X += owner.fXStep;
					}
				}
			}
		}

		// Flush the batch
		UIRender->FlushPrimitive();
	}
}

// ============================================================================
// dxRenderFactory implementation
// ============================================================================

// Global factory instance
dxRenderFactory RenderFactoryImpl;

#define FACTORY_IMPLEMENT(Class) \
    I##Class* dxRenderFactory::Create##Class() { return xr_new<vk##Class>(); } \
    void dxRenderFactory::Destroy##Class(I##Class* p) { vk##Class* vp = static_cast<vk##Class*>(p); xr_delete(vp); }

FACTORY_IMPLEMENT(UISequenceVideoItem)
FACTORY_IMPLEMENT(UIShader)
FACTORY_IMPLEMENT(StatGraphRender)
FACTORY_IMPLEMENT(ConsoleRender)
FACTORY_IMPLEMENT(RenderDeviceRender)
FACTORY_IMPLEMENT(ApplicationRender)
FACTORY_IMPLEMENT(WallMarkArray)
FACTORY_IMPLEMENT(StatsRender)
FACTORY_IMPLEMENT(FlareRender)
FACTORY_IMPLEMENT(ThunderboltRender)
FACTORY_IMPLEMENT(ThunderboltDescRender)
FACTORY_IMPLEMENT(RainRender)
FACTORY_IMPLEMENT(LensFlareRender)
FACTORY_IMPLEMENT(ImGuiRender)
FACTORY_IMPLEMENT(EnvironmentRender)
FACTORY_IMPLEMENT(EnvDescriptorMixerRender)
FACTORY_IMPLEMENT(EnvDescriptorRender)
FACTORY_IMPLEMENT(FontRender)
#ifdef DEBUG
FACTORY_IMPLEMENT(ObjectSpaceRender)
#endif

#undef FACTORY_IMPLEMENT
