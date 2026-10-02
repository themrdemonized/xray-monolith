#ifndef	dxUIRender_included
#define	dxUIRender_included
#pragma once

#include "..\..\Include\xrRender\UIRender.h"

class dxUIRender : public IUIRender
{
public:
	dxUIRender() : PrimitiveType(ptNone), m_PointType(pttNone) { ; }

	virtual void CreateUIGeom();
	virtual void DestroyUIGeom();

	virtual void SetShader(IUIShader& shader);
	virtual void SetAlphaRef(int aref);
	//.	virtual void StartTriList(u32 iMaxVerts);
	//.	virtual void FlushTriList();
	//.	virtual void StartTriFan(u32 iMaxVerts);
	//.	virtual void FlushTriFan();
	//virtual void StartTriStrip(u32 iMaxVerts);
	//virtual void FlushTriStrip();
	//.	virtual void StartLineStrip(u32 iMaxVerts);
	//.	virtual void FlushLineStrip();
	//.	virtual void StartLineList(u32 iMaxVerts);
	//.	virtual void FlushLineList();
	virtual void SetScissor(Irect* rect = NULL);
	virtual void GetActiveTextureResolution(Fvector2& res);

	//.	virtual void PushPoint(float x, float y, u32 c, float u, float v);
	//	virtual void PushPoint(int x, int y, u32 c, float u, float v);
	virtual void PushPoint(float x, float y, float z, u32 C, float u, float v);

	virtual void StartPrimitive(u32 iMaxVerts, ePrimitiveType primType, ePointType pointType);
	virtual void FlushPrimitive();

	virtual LPCSTR UpdateShaderName(LPCSTR tex_name, LPCSTR sh_name);

	virtual void CacheSetXformWorld(const Fmatrix& M);
	virtual void CacheSetCullMode(CullMode);
	virtual bool SupportsFlatBackground() const;
	virtual void DrawFlatBackground(u32 color, float distance, LPCSTR texture);


    virtual bool SupportsModelPreview() const;
    virtual bool AcquirePreview(const void* owner, bool embedded);
    virtual void ReleasePreview(const void* owner);
    virtual bool IsPreviewOwner(const void* owner) const;
    virtual void ConfigurePreview(const void* owner, u32 color, LPCSTR texture, bool dry, float gain);
    virtual bool PreviewDry() const;
    virtual u32 PreviewBackgroundColor() const { return m_previewBackgroundColor; }
    virtual bool PreviewEmbedded() const;
    virtual bool BeginPreviewUI();
    virtual bool BeginPreviewModel(bool compose);
    virtual void EndPreviewPass();
    virtual bool SceneSuppressed() const;
    virtual void PresentPreviewModel();
    virtual void ReleaseUnusedPreview();

private:
    const void* m_previewOwner = nullptr;
    bool m_previewEmbedded = false;
    bool m_previewDry = false;
    u32 m_previewFrame = 0;
    u32 m_previewBackgroundColor = 0xff060706;
    float m_previewGain = 1.f;
    shared_str m_previewTexture;
    shared_str m_backgroundTexture;
#if defined(USE_DX11)
    ref_rt m_previewPosition, m_previewColor, m_previewDepth, m_previewModel, m_previewUI;
    ref_shader m_previewCompose;
    ref_shader m_previewPresent;
    ID3DRenderTargetView* m_previewSavedRT[4] = {};
    ID3DDepthStencilView* m_previewSavedDepth = nullptr;
    bool m_previewPass = false;
    u32 m_previewSavedCull = CULL_CCW;
    u32 m_previewModelFrame = u32(-1);
    void EnsurePreviewTargets(bool model);
    void SavePreviewTargets();
    void DrawPreviewQuad(bool present = false);
#endif

	ref_geom hGeom_TL;
	ref_geom hGeom_LIT;
	ref_shader m_flatBackgroundShader;

	ePrimitiveType PrimitiveType;
	ePointType m_PointType;

	//	Vertex buffer attributes
	u32 m_iMaxVerts;
	u32 vOffset;

	FVF::TL* TL_start_pv;
	FVF::TL* TL_pv;

	FVF::LIT* LIT_start_pv;
	FVF::LIT* LIT_pv;
};

extern dxUIRender UIRenderImpl;

#endif	//	dxUIRender_included
