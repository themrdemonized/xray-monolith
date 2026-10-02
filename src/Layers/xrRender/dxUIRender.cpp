#include "stdafx.h"
#include "dxUIRender.h"

#include "dxUIShader.h"

dxUIRender UIRenderImpl;

void dxUIRender::CreateUIGeom()
{
	hGeom_TL.create(FVF::F_TL, RCache.Vertex.Buffer(), 0);
	hGeom_LIT.create(FVF::F_LIT, RCache.Vertex.Buffer(), 0);
}

void dxUIRender::DestroyUIGeom()
{

    for (auto& it : g_UIShadersCache)
        it.second.destroy();
    g_UIShadersCache.clear();

	hGeom_TL = NULL;
	hGeom_LIT = NULL;
	m_flatBackgroundShader.destroy();
#if defined(USE_DX11)
    m_previewPresent.destroy();m_previewCompose.destroy();m_previewPosition.destroy();m_previewColor.destroy();
    m_previewDepth.destroy();m_previewModel.destroy();m_previewUI.destroy();
    m_previewPass=false;
#endif
    m_previewOwner=nullptr;
}

void dxUIRender::SetShader(IUIShader& shader)
{
	dxUIShader* pShader = (dxUIShader*)&shader;
	VERIFY(&pShader);
	VERIFY(pShader->hShader);
	RCache.set_Shader(pShader->hShader);
}

void dxUIRender::SetAlphaRef(int aref)
{
	//CHK_DX(HW.pDevice->SetRenderState(D3DRS_ALPHAREF,aref));
	RCache.set_AlphaRef(aref);
}

/*
void dxUIRender::StartTriList(u32 iMaxVerts)
{
	VERIFY(PrimitiveType==ptNone);
	m_PointType = pttLIT;
	m_iMaxVerts = iMaxVerts;
	start_pv	= (FVF::LIT*)RCache.Vertex.Lock	(m_iMaxVerts,hGeom_fan.stride(),vOffset);
	pv			= start_pv;
	PrimitiveType = ptTriList;
}

void dxUIRender::FlushTriList()
{
	VERIFY(PrimitiveType==ptTriList);
	VERIFY(u32(pv-start_pv)<=m_iMaxVerts);

	std::ptrdiff_t p_cnt		= (pv-start_pv)/3;							
	RCache.Vertex.Unlock		(u32(pv-start_pv),hGeom_fan.stride());
	RCache.set_Geometry			(hGeom_fan);
	if (p_cnt!=0)RCache.Render	(D3DPT_TRIANGLELIST,vOffset,u32(p_cnt));

	PrimitiveType = ptNone;
}

void dxUIRender::StartTriFan(u32 iMaxVerts)
{
	VERIFY(PrimitiveType==ptNone);
	m_iMaxVerts = iMaxVerts;
	start_pv	= (FVF::LIT*)RCache.Vertex.Lock	(m_iMaxVerts,hGeom_fan.stride(),vOffset);
	pv			= start_pv;
	PrimitiveType = ptTriFan;
	m_PointType	= pttLIT;

}

void dxUIRender::FlushTriFan()
{
	VERIFY(PrimitiveType==ptTriFan);
	VERIFY(u32(pv-start_pv)<=m_iMaxVerts);

	std::ptrdiff_t p_cnt		= pv-start_pv;
	RCache.Vertex.Unlock		(u32(p_cnt),hGeom_fan.stride());
	RCache.set_Geometry	 		(hGeom_fan);
	if (p_cnt>2) RCache.Render	(D3DPT_TRIANGLEFAN,vOffset,u32(p_cnt-2));

	PrimitiveType = ptNone;
}

void dxUIRender::StartTriStrip(u32 iMaxVerts)
{
	VERIFY(PrimitiveType==ptNone);
	m_iMaxVerts = iMaxVerts;
	start_pv	= (FVF::TL*)RCache.Vertex.Lock	(m_iMaxVerts,hGeom_fan.stride(),vOffset);
	pv			= start_pv;
	PrimitiveType = ptTriStrip;
}

void dxUIRender::FlushTriStrip()
{
}


void dxUIRender::StartLineStrip(u32 iMaxVerts)
{
	VERIFY(PrimitiveType==ptNone);
	m_iMaxVerts = iMaxVerts;
	start_pv	= (FVF::LIT*)RCache.Vertex.Lock	(m_iMaxVerts,hGeom_fan.stride(),vOffset);
	pv			= start_pv;
	PrimitiveType = ptLineStrip;
	m_PointType = pttLIT;
}

void dxUIRender::FlushLineStrip()
{
	VERIFY(PrimitiveType==ptLineStrip);
	VERIFY(u32(pv-start_pv)<=m_iMaxVerts);

	std::ptrdiff_t p_cnt		= pv-start_pv;
	RCache.Vertex.Unlock		(u32(p_cnt),hGeom_fan.stride());
	RCache.set_Geometry	 		(hGeom_fan);
	if (p_cnt>1) RCache.Render	(D3DPT_LINESTRIP,vOffset,u32(p_cnt-1));

	PrimitiveType = ptNone;
}

void dxUIRender::StartLineList(u32 iMaxVerts)
{
	VERIFY(PrimitiveType==ptNone);
	m_iMaxVerts = iMaxVerts;
	start_pv	= (FVF::LIT*)RCache.Vertex.Lock	(m_iMaxVerts,hGeom_fan.stride(),vOffset);
	pv			= start_pv;
	PrimitiveType = ptLineList;
}

void dxUIRender::FlushLineList()
{
	VERIFY(PrimitiveType==ptLineList);
	VERIFY(u32(pv-start_pv)<=m_iMaxVerts);

	std::ptrdiff_t p_cnt		= pv-start_pv;
	RCache.Vertex.Unlock		(u32(p_cnt),hGeom_fan.stride());
	RCache.set_Geometry	 		(hGeom_fan);
	if (p_cnt>1) RCache.Render	(D3DPT_LINELIST,vOffset,u32(p_cnt)/2);

	PrimitiveType = ptNone;
}
*/
void dxUIRender::SetScissor(Irect* rect)
{
#if (RENDER == R_R3) || (RENDER == R_R4)
	RCache.set_Scissor(rect);
	StateManager.OverrideScissoring(rect ? true : false, TRUE);
#else	//	(RENDER == R_R3) || (RENDER == R_R4)
	RCache.set_Scissor(rect);
#endif	//	(RENDER == R_R3) || (RENDER == R_R4)
}

void dxUIRender::GetActiveTextureResolution(Fvector2& res)
{
	CTexture* T = RCache.get_ActiveTexture(0);
	res.set(float(T->get_Width()), float(T->get_Height()));
}

LPCSTR dxUIRender::UpdateShaderName(LPCSTR tex_name, LPCSTR sh_name)
{
	string_path buff;
	u32 v_dev = CAP_VERSION(HW.Caps.raster_major, HW.Caps.raster_minor);
	u32 v_need = CAP_VERSION(2, 0);
	if ((v_dev >= v_need) && FS.exist(buff, "$game_textures$", tex_name, ".ogm"))
		return "hud\\movie";
	else
		return sh_name;
}

/*
void dxUIRender::PushPoint(float x, float y, u32 c, float u, float v)
{
	VERIFY(m_PointType==pttNone);
	pv->set(x, y, 0.0f, c, u, v);
	++pv;
}
*/
/*
void dxUIRender::PushPoint(int x, int y, u32 c, float u, float v)
{
	VERIFY(m_PointType==pttNone);
	pv->set(x, y, 0, c, u, v);
	++pv;
}
*/

void dxUIRender::PushPoint(float x, float y, float z, u32 C, float u, float v)
{
	//.	VERIFY(m_PointType==pttLIT);
	switch (m_PointType)
	{
	case pttLIT:
		LIT_pv->set(x, y, z, C, u, v);
		++LIT_pv;
		break;
	case pttTL:
		TL_pv->set(x, y, C, u, v);
		++TL_pv;
		break;
	}
}

void dxUIRender::StartPrimitive(u32 iMaxVerts, ePrimitiveType primType, ePointType pointType)
{
	VERIFY(PrimitiveType==ptNone);
	VERIFY(m_PointType==pttNone);
	//.	R_ASSERT(pointType==pttLIT);

	m_iMaxVerts = iMaxVerts;
	PrimitiveType = primType;
	m_PointType = pointType;

	switch (m_PointType)
	{
	case pttLIT:
		LIT_start_pv = (FVF::LIT*)RCache.Vertex.Lock(m_iMaxVerts, hGeom_LIT.stride(), vOffset);
		LIT_pv = LIT_start_pv;
		break;
	case pttTL:
		TL_start_pv = (FVF::TL*)RCache.Vertex.Lock(m_iMaxVerts, hGeom_TL.stride(), vOffset);
		TL_pv = TL_start_pv;
		break;
	}
}

void dxUIRender::FlushPrimitive()
{
	u32 primCount = 0;
	_D3DPRIMITIVETYPE d3dPrimType = D3DPT_FORCE_DWORD;
	std::ptrdiff_t p_cnt = 0;

	switch (m_PointType)
	{
	case pttLIT:
		p_cnt = LIT_pv - LIT_start_pv;
		VERIFY(u32(p_cnt)<=m_iMaxVerts);

		RCache.Vertex.Unlock(u32(p_cnt), hGeom_LIT.stride());
		RCache.set_Geometry(hGeom_LIT);
		break;
	case pttTL:
		p_cnt = TL_pv - TL_start_pv;
		VERIFY(u32(p_cnt)<=m_iMaxVerts);

		RCache.Vertex.Unlock(u32(p_cnt), hGeom_TL.stride());
		RCache.set_Geometry(hGeom_TL);
		break;
	default:
		NODEFAULT;
	}

	//	Update data for primitive type
	switch (PrimitiveType)
	{
	case ptTriStrip:
		primCount = (u32)(p_cnt - 2);
		d3dPrimType = D3DPT_TRIANGLESTRIP;
		break;
	case ptTriList:
		primCount = (u32)(p_cnt / 3);
		d3dPrimType = D3DPT_TRIANGLELIST;
		break;
	case ptLineStrip:
		primCount = (u32)(p_cnt - 1);
		d3dPrimType = D3DPT_LINESTRIP;
		break;
	case ptLineList:
		primCount = (u32)(p_cnt / 2);
		d3dPrimType = D3DPT_LINELIST;
		break;
	default:
		NODEFAULT;
	}

	if (primCount > 0)
		RCache.Render(d3dPrimType, vOffset, primCount);

	PrimitiveType = ptNone;
	m_PointType = pttNone;
}

void dxUIRender::CacheSetXformWorld(const Fmatrix& M)
{
	RCache.set_xform_world(M);
}

void dxUIRender::CacheSetCullMode(CullMode m)
{
	RCache.set_CullMode(CULL_NONE + m);
}

// Keep this pass out of the scene's lighting, bloom, tone mapping and SSR.
// It runs in the camera-attachment UI pass, after phase_combine. The
// attachment geometry has already populated the near (0..0.02) depth range.
#if defined(USE_DX11)
class CBlender_FlatUIBackground : public IBlender
{
public:
    LPCSTR texture;
    explicit CBlender_FlatUIBackground(LPCSTR value) : texture(value) {}
	virtual LPCSTR getComment() { return "Flat inspection background"; }
	virtual BOOL canBeLMAPped() { return FALSE; }
	virtual void Compile(CBlender_Compile& C)
	{
		IBlender::Compile(C);
		if (C.iElement != 0) return;
		const bool msaa = RImplementation.o.dx10_msaa;
		// Use the three-argument overload, then set states explicitly. Passing
		// bool/BOOL values in the longer call is ambiguous with the GS overload
		// on MSVC (false can also match the geometry-shader name argument).
		C.r_Pass("ui_preview_background", "ui_preview_background", false);
		C.PassSET_ZB(!msaa, FALSE);
		C.PassSET_Blend(msaa, D3DBLEND_SRCALPHA, D3DBLEND_INVSRCALPHA, FALSE, 0);
		if (msaa) C.r_dx10Texture("s_inspection_depth", "$user$msaadepth");
        C.r_dx10Texture("s_preview_background",texture);
        C.r_dx10Sampler("smp_rtlinear");
		C.r_End();
	}
};
#endif

bool dxUIRender::SupportsFlatBackground() const
{
#if defined(USE_DX11)
	return HW.FeatureLevel >= D3D_FEATURE_LEVEL_11_0;
#else
	return false;
#endif
}

void dxUIRender::DrawFlatBackground(u32 color, float distance, LPCSTR texture)
{
#if defined(USE_DX11)
	if (!SupportsFlatBackground() || !_valid(distance) || distance <= 0.f) return;
	VERIFY(PrimitiveType == ptNone);
	if (m_backgroundTexture!=texture) { m_flatBackgroundShader.destroy();m_backgroundTexture=texture; }
	if (!m_flatBackgroundShader)
	{
		CBlender_FlatUIBackground blender(texture);
		m_flatBackgroundShader.create(&blender, "ui_preview_background");
	}
	// Use the same camera projection and viewport depth range as the gun.
	// Full-screen clip coordinates avoid the ordinary 2D UI transform path.
	const Fmatrix& P = Device.mProject;
	const float w = P._34 * distance + P._44;
	if (w <= EPS) return;
	const float z = (P._33 * distance + P._43) / w;
	if (z < 0.f || z > 1.f) return;
	u32 offset;
	FVF::LIT* v = (FVF::LIT*)RCache.Vertex.Lock(4, hGeom_LIT.stride(), offset);
	color |= 0xff000000;
	v[0].set(-1.f, -1.f, z, color, 0.f, 1.f);
	v[1].set(-1.f,  1.f, z, color, 0.f, 0.f);
	v[2].set( 1.f, -1.f, z, color, 1.f, 1.f);
	v[3].set( 1.f,  1.f, z, color, 1.f, 0.f);
	RCache.Vertex.Unlock(4, hGeom_LIT.stride());
	RCache.set_Element(m_flatBackgroundShader->E[0]);
    RCache.set_c("preview_options",texture ? 1.f : 0.f,1.f,0.f,0.f);
	RCache.set_Geometry(hGeom_LIT);
	RCache.set_CullMode(CULL_NONE);
	RCache.set_Stencil(FALSE);
	RCache.Render(D3DPT_TRIANGLESTRIP, offset, 2);
	RCache.set_CullMode(CULL_CCW);
#endif
}

// Scriptable UI previews: separate single-sample model G-buffer and UI
// texture. The world/PDA render targets are restored after every pass.
bool dxUIRender::SupportsModelPreview() const
{
#if defined(USE_DX11)
    return SupportsFlatBackground() && !RImplementation.o.dx10_msaa;
#else
    return false;
#endif
}
#if defined(USE_DX11)
class CBlender_Preview : public IBlender
{
public:
    LPCSTR texture;
    explicit CBlender_Preview(LPCSTR value) : texture(value) {}
    virtual LPCSTR getComment() { return "Isolated preview studio"; }
    virtual BOOL canBeLMAPped() { return FALSE; }
    virtual void Compile(CBlender_Compile& C)
    {
        IBlender::Compile(C);
        C.r_Pass("ui_preview_background", "ui_preview_model", false);
        C.PassSET_ZB(FALSE,FALSE);
        C.PassSET_Blend(FALSE,D3DBLEND_ONE,D3DBLEND_ZERO,FALSE,0);
        C.r_dx10Texture("s_preview_position","$user$ui_preview_position");
        C.r_dx10Texture("s_preview_color","$user$ui_preview_color");
        C.r_dx10Texture("s_preview_background",texture);
        C.r_dx10Sampler("smp_rtlinear");
        C.r_End();
    }
};
class CBlender_PreviewPresent : public IBlender
{
public:
    virtual LPCSTR getComment() { return "Fullscreen inspection model"; }
    virtual BOOL canBeLMAPped() { return FALSE; }
    virtual void Compile(CBlender_Compile& C)
    {
        IBlender::Compile(C);
        C.r_Pass("ui_preview_background", "ui_preview_present", false);
        C.PassSET_ZB(FALSE,FALSE);
        C.PassSET_Blend(FALSE,D3DBLEND_ONE,D3DBLEND_ZERO,FALSE,0);
        C.r_dx10Texture("s_preview_model","$user$ui_preview_model");
        C.r_End();
    }
};
void dxUIRender::EnsurePreviewTargets(bool model)
{
    const u32 w=Device.dwWidth,h=Device.dwHeight;
    // CRT reset recreates resources at their original dimensions. A preview
    // follows the display, so discard the complete target set after resizing.
    if ((m_previewUI && (m_previewUI->dwWidth!=w || m_previewUI->dwHeight!=h)) ||
        (m_previewModel && (m_previewModel->dwWidth!=w || m_previewModel->dwHeight!=h)))
    {
        VERIFY(!m_previewPass);
        m_previewPresent.destroy();m_previewCompose.destroy();
        m_previewPosition.destroy();m_previewColor.destroy();m_previewDepth.destroy();
        m_previewModel.destroy();m_previewUI.destroy();
        m_previewModelFrame=u32(-1);
    }
    if (!model && !m_previewUI) m_previewUI.create("$user$ui_preview_surface",w,h,D3DFMT_A8R8G8B8);
    // Status, crafting and showcase need only the UI target. Defer the model
    // buffers until the first actual preview, then reuse them across tabs.
    if (!model) return;
    if (!m_previewCompose) { CBlender_Preview blender(m_previewTexture.c_str());m_previewCompose.create(&blender,"ui_preview_model"); }
    if (m_previewModel) return;
    m_previewPosition.create("$user$ui_preview_position",w,h,D3DFMT_A16B16G16R16F);
    m_previewColor.create("$user$ui_preview_color",w,h,D3DFMT_A16B16G16R16F);
    m_previewDepth.create("$user$ui_preview_depth",w,h,D3DFMT_D24S8);
    m_previewModel.create("$user$ui_preview_model",w,h,D3DFMT_A8R8G8B8);
    Fcolor background;background.set(m_previewBackgroundColor);
    const FLOAT clear[4]={background.r,background.g,background.b,1.f};
    HW.pContext->ClearRenderTargetView(m_previewModel->pRT,clear);

}
void dxUIRender::SavePreviewTargets()
{
    VERIFY(!m_previewPass);m_previewPass=true;
    for(u32 i=0;i<4;++i) m_previewSavedRT[i]=RCache.get_RT(i);
    m_previewSavedDepth=RCache.get_ZB();
    m_previewSavedCull=RCache.get_CullMode();
}
void dxUIRender::DrawPreviewQuad(bool present)
{
    const u32 savedCull=RCache.get_CullMode();
    u32 offset;FVF::LIT* v=(FVF::LIT*)RCache.Vertex.Lock(4,hGeom_LIT.stride(),offset);
    v[0].set(-1,-1,0,0xffffffff,0,1);v[1].set(-1,1,0,0xffffffff,0,0);
    v[2].set(1,-1,0,0xffffffff,1,1);v[3].set(1,1,0,0xffffffff,1,0);
    RCache.Vertex.Unlock(4,hGeom_LIT.stride());
    RCache.set_Element(present ? m_previewPresent->E[0] : m_previewCompose->E[0]);RCache.set_Geometry(hGeom_LIT);
    if (!present) {
        Fcolor color;color.set(m_previewBackgroundColor);
        RCache.set_c("preview_background",color.r,color.g,color.b,color.a);
        RCache.set_c("preview_options",m_previewTexture.size() ? 1.f : 0.f,m_previewGain,0.f,0.f);
    }
    RCache.set_CullMode(CULL_NONE);RCache.set_Stencil(FALSE);
    RCache.Render(D3DPT_TRIANGLESTRIP,offset,2);
    RCache.set_CullMode(savedCull);
}
#endif
bool dxUIRender::BeginPreviewModel(bool compose)
{
#if defined(USE_DX11)
    if(!PreviewEmbedded() && !SceneSuppressed()) return false;
    PROF_EVENT("Isolated preview model pass");
    EnsurePreviewTargets(true);SavePreviewTargets();
    const FLOAT clear[4]={0,0,0,0};
    if(!compose)
    {
        m_previewModelFrame=Device.dwFrame;
        HW.pContext->ClearRenderTargetView(m_previewPosition->pRT,clear);
        HW.pContext->ClearRenderTargetView(m_previewColor->pRT,clear);
        HW.pContext->ClearDepthStencilView(m_previewDepth->pZRT,D3D_CLEAR_DEPTH|D3D_CLEAR_STENCIL,1.f,0);
        RCache.set_RT(m_previewPosition->pRT,0);RCache.set_RT(m_previewColor->pRT,1);
        // The isolated studio shader reads only position and color. Discard
        // heat/motion outputs instead of writing two unused FP16 surfaces.
        RCache.set_RT(nullptr,2);RCache.set_RT(nullptr,3);
        RCache.set_ZB(m_previewDepth->pZRT);
    }
    else
    {
        RCache.set_RT(m_previewModel->pRT,0);
        for(u32 i=1;i<4;++i) RCache.set_RT(nullptr,i);
        RCache.set_ZB(m_previewDepth->pZRT);
        DrawPreviewQuad();
    }
    return true;
#else
    return false;
#endif
}
bool dxUIRender::IsPreviewOwner(const void* owner) const
{
    return owner && owner==m_previewOwner && u32(Device.dwFrame-m_previewFrame)<=1 && SupportsModelPreview();
}
bool dxUIRender::AcquirePreview(const void* owner, bool embedded)
{
    if (!owner || !SupportsModelPreview()) return false;
    if (IsPreviewOwner(m_previewOwner) && owner!=m_previewOwner) return false;
#if defined(USE_DX11)
    // A recycled target must not show another owner's completed image.
    if (owner!=m_previewOwner || !IsPreviewOwner(owner)) m_previewModelFrame=u32(-1);
#endif
    m_previewOwner=owner;m_previewEmbedded=embedded;m_previewFrame=Device.dwFrame;
    return true;
}
void dxUIRender::ReleasePreview(const void* owner)
{
    if (owner==m_previewOwner) {
        m_previewOwner=nullptr;m_previewDry=false;
#if defined(USE_DX11)
        m_previewModelFrame=u32(-1);
#endif
    }
}
bool dxUIRender::PreviewEmbedded() const { return IsPreviewOwner(m_previewOwner) && m_previewEmbedded; }
bool dxUIRender::SceneSuppressed() const { return IsPreviewOwner(m_previewOwner) && !m_previewEmbedded; }
bool dxUIRender::PreviewDry() const { return IsPreviewOwner(m_previewOwner) && m_previewDry; }
void dxUIRender::ConfigurePreview(const void* owner, u32 color, LPCSTR texture, bool dry, float gain)
{
    if (!IsPreviewOwner(owner)) return;
    if (m_previewTexture!=texture)
    {
        m_previewTexture=texture;
#if defined(USE_DX11)
        m_previewCompose.destroy();
#endif
    }
    m_previewBackgroundColor=color;m_previewDry=dry;m_previewGain=gain;
}
void dxUIRender::ReleaseUnusedPreview()
{
#if defined(USE_DX11)
    if (PreviewEmbedded() || SceneSuppressed() || m_previewPass) return;
    m_previewPresent.destroy();m_previewCompose.destroy();
    m_previewPosition.destroy();m_previewColor.destroy();m_previewDepth.destroy();
    m_previewModel.destroy();m_previewUI.destroy();m_previewModelFrame=u32(-1);
#endif
}
void dxUIRender::PresentPreviewModel()
{
#if defined(USE_DX11)
    if (!SceneSuppressed() || !m_previewModel) return;
    if (!m_previewPresent)
    {
        CBlender_PreviewPresent blender;
        m_previewPresent.create(&blender,"ui_preview_present");
    }
    DrawPreviewQuad(true);
#endif
}
bool dxUIRender::BeginPreviewUI()
{
#if defined(USE_DX11)
    if(!SupportsModelPreview()) return false;
    EnsurePreviewTargets(false);SavePreviewTargets();
    RCache.set_RT(m_previewUI->pRT,0);
    for(u32 i=1;i<4;++i) RCache.set_RT(nullptr,i);
    RCache.set_ZB(nullptr);RCache.set_Stencil(FALSE);
    // Physical PDA UI is captured by CLevel before this frame's scene/model
    // pass. Its surface therefore consumes the last completed frame, whereas
    // an ordinary UI portal can consume the current one. Reject anything older.
    if (m_previewModel && m_previewModelFrame!=u32(-1) &&
        u32(Device.dwFrame-m_previewModelFrame)<=1)
        HW.pContext->CopyResource(m_previewUI->pSurface,m_previewModel->pSurface);
    else
    {
        Fcolor background;background.set(m_previewBackgroundColor);
    const FLOAT clear[4]={background.r,background.g,background.b,1.f};
        HW.pContext->ClearRenderTargetView(m_previewUI->pRT,clear);
    }
    return true;
#else
    return false;
#endif
}
void dxUIRender::EndPreviewPass()
{
#if defined(USE_DX11)
    if(!m_previewPass) return;
    for(u32 i=0;i<4;++i) RCache.set_RT(m_previewSavedRT[i],i);
    RCache.set_ZB(m_previewSavedDepth);RCache.set_CullMode(m_previewSavedCull);m_previewPass=false;
#endif
}
