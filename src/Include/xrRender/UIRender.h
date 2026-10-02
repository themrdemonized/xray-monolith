#ifndef	UIRender_included
#define	UIRender_included
#pragma once

class IUIShader;

class IUIRender
{
public:
	enum ePrimitiveType
	{
		ptNone = -1,
		ptTriList,
		//		ptTriFan,
		ptTriStrip,
		ptLineStrip,
		ptLineList
	};

	enum ePointType
	{
		pttNone = -1,
		pttTL,
		pttLIT
	};

	enum CullMode
	{
		cmNONE = 0,
		cmCW,
		cmCCW,
	};

public:
	//virtual ~IUIRender() {;}

	virtual void CreateUIGeom() = 0;
	virtual void DestroyUIGeom() = 0;

	virtual void SetShader(IUIShader& shader) = 0;
	virtual void SetAlphaRef(int aref) = 0;
	//.	virtual void StartTriList(u32 iMaxVerts) = 0;
	//.	virtual void FlushTriList() = 0;
	//.	virtual void StartTriFan(u32 iMaxVerts) = 0;
	//.	virtual void FlushTriFan() = 0;

	//virtual void StartTriStrip(u32 iMaxVerts) = 0;
	//virtual void FlushTriStrip() = 0;
	//.	virtual void StartLineStrip(u32 iMaxVerts) = 0;
	//.	virtual void FlushLineStrip() = 0;
	//.	virtual void StartLineList(u32 iMaxVerts) = 0;
	//.	virtual void FlushLineList() = 0;
	virtual void SetScissor(Irect* rect = NULL) = 0;
	virtual void GetActiveTextureResolution(Fvector2& res) = 0;

	//.	virtual void PushPoint(float x, float y, u32 c, float u, float v) = 0;
	//.	virtual void PushPoint(int x, int y, u32 c, float u, float v) = 0;
	virtual void PushPoint(float x, float y, float z, u32 C, float u, float v) = 0;

	virtual void StartPrimitive(u32 iMaxVerts, ePrimitiveType primType, ePointType pointType) = 0;
	virtual void FlushPrimitive() = 0;

	virtual LPCSTR UpdateShaderName(LPCSTR tex_name, LPCSTR sh_name) = 0;

	virtual void CacheSetXformWorld(const Fmatrix& M) = 0;
	virtual void CacheSetCullMode(CullMode) = 0;


    virtual bool SupportsModelPreview() const { return false; }
    virtual bool AcquirePreview(const void* owner, bool embedded) { return false; }
    virtual void ReleasePreview(const void* owner) {}
    virtual bool IsPreviewOwner(const void* owner) const { return false; }
    virtual void ConfigurePreview(const void* owner, u32 color, LPCSTR texture, bool dry, float gain) {}
    virtual bool PreviewDry() const { return false; }
    virtual u32 PreviewBackgroundColor() const { return 0xff060706; }
    virtual bool PreviewEmbedded() const { return false; }
    virtual bool BeginPreviewUI() { return false; }
    virtual bool BeginPreviewModel(bool compose) { return false; }
    virtual void EndPreviewPass() {}
    // Renewed by a visible fullscreen UI; expires if its script stops updating.
    virtual bool SceneSuppressed() const { return false; }
    virtual void PresentPreviewModel() {}
    virtual void ReleaseUnusedPreview() {}

	// Opt-in camera inspection background, drawn after scene postprocessing.
	virtual bool SupportsFlatBackground() const { return false; }
	virtual void DrawFlatBackground(u32 color, float distance, LPCSTR texture) {}
};

#endif	//	UIRender_included
