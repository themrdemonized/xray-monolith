#ifndef RenderDeviceRender_included
#define RenderDeviceRender_included
#pragma once

class IRenderDeviceRender
{
public:
	enum DeviceState
	{
		dsOK = 0,
		dsLost,
		dsNeedReset
	};

public:
	virtual ~IRenderDeviceRender() { ; }
	virtual void Copy(IRenderDeviceRender& _in) = 0;

	//	Gamma correction functions
	virtual void setGamma(float fGamma) = 0;
	virtual void setBrightness(float fGamma) = 0;
	virtual void setContrast(float fGamma) = 0;
	virtual void updateGamma() = 0;

	//	Destroy
	virtual void OnDeviceDestroy(BOOL bKeepTextures) = 0;
	virtual void ValidateHW() = 0;
	virtual void DestroyHW() = 0;
	virtual void Reset(HWND hWnd, u32& dwWidth, u32& dwHeight, float& fWidth_2, float& fHeight_2) = 0;
	//	Init
	virtual void SetupStates() = 0;
	virtual void OnDeviceCreate(LPCSTR shName) = 0;
	virtual void Create(HWND hWnd, u32& dwWidth, u32& dwHeight, float& fWidth_2, float& fHeight_2, bool) = 0;
	virtual void SetupGPU(BOOL bForceGPU_SW, BOOL bForceGPU_NonPure, BOOL bForceGPU_REF) = 0;
	//	Overdraw
	virtual void overdrawBegin() = 0;
	virtual void overdrawEnd() = 0;

	//	Resources control
	virtual void DeferredLoad(BOOL E) = 0;
	virtual void ResourcesDeferredUpload() = 0;
	virtual void ResourcesDeferredUnload() = 0;
	virtual void ResourcesGetMemoryUsage(u32& m_base, u32& c_base, u32& m_lmaps, u32& c_lmaps) = 0;
	virtual void ResourcesDestroyNecessaryTextures() = 0;
	virtual void ResourcesStoreNecessaryTextures() = 0;
	virtual void ResourcesDumpMemoryUsage() = 0;
	virtual void ResourcesPrefetchCreateTexture(LPCSTR name) = 0;
	// Level preload: create the materials of a level that is not loaded yet, a few per frame during
	// play, and hold them until released, so that the load of that level finds them created.
	// Main thread only. A level is named by its folder, with the trailing separator.
	virtual void ResourcesPreloadLevel(LPCSTR level_folder) = 0; // a level being prepared is kept
	virtual void ResourcesPreloadUpdate(float budget_ms) = 0; // once per frame
	virtual void ResourcesPreloadRelease(LPCSTR level_folder) = 0; // nullptr: every level; freed once their loads have run
	virtual void ResourcesPreloadWait() = 0; // for the background work of every level (before a level change)

	//	HWSupport
	virtual bool HWSupportsShaderYUV2RGB() = 0;

	//	Device state
	virtual DeviceState GetDeviceState() = 0;
	virtual BOOL GetForceGPU_REF() = 0;
	virtual u32 GetCacheStatPolys() = 0;
	virtual void Begin() = 0;
	virtual void Clear() = 0;
	virtual void End() = 0;
	virtual void ClearTarget() = 0;
	virtual void SetCacheXform(Fmatrix& mView, Fmatrix& mProject) = 0;
	virtual void SetCacheXform_prev(Fmatrix& mView, Fmatrix& mProject) = 0;
	virtual void OnAssetsChanged() = 0;

	virtual bool SwitchOutputMonitor(HMONITOR hTargetMon, HWND hWnd, u32 g_screenmode, u32& vidModeW, u32& vidModeH) { return false; }
};

#endif	//	RenderDeviceRender_included
