// CRender.cpp: implementation of the CRender class.
//
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "../../xrEngine/igame_persistent.h"
#include "../../xrEngine/environment.h"
#include "../xrRender/fbasicvisual.h"
#include "../../xrEngine/CustomHUD.h"
#include "../../xrEngine/xr_object.h"
#include "../../xrEngine/fmesh.h"
#include "../xrRender/SkeletonCustom.h"
#include "../xrRender/lighttrack.h"
#include "../xrRender/dxRenderDeviceRender.h"
#include "../xrRender/dxWallMarkArray.h"
#include "../xrRender/dxUIShader.h"
#include "../../xrCore/ShaderSourceCRC.h"
//#include "../../xrServerEntities/smart_cast.h"

#ifndef _EDITOR
#include "../../xrCPU_Pipe/ttapi.h"
#endif

#include "../../xrParticles/ParticlesAsyncManager.h"
using namespace R_dsgraph;

CRender RImplementation;

//////////////////////////////////////////////////////////////////////////
ShaderElement* CRender::rimp_select_sh_dynamic(dxRender_Visual* pVisual, float cdist_sq)
{
	switch (phase)
	{
case PHASE_NORMAL: return (L_Projector->shadowing()
		                           ? pVisual->shader->E[SE_R1_NORMAL_HQ]
		                           : pVisual->shader->E[SE_R1_NORMAL_LQ])._get();
	case PHASE_POINT: return pVisual->shader->E[SE_R1_LPOINT]._get();
	case PHASE_SPOT: return pVisual->shader->E[SE_R1_LSPOT]._get();
	default: NODEFAULT;
	}
#ifdef DEBUG
	return	0;
#endif
}

//////////////////////////////////////////////////////////////////////////
ShaderElement* CRender::rimp_select_sh_static(dxRender_Visual* pVisual, float cdist_sq)
{
	switch (phase)
	{
	case PHASE_NORMAL: return (((_sqrt(cdist_sq) - pVisual->vis.sphere.R) < 44)
		                           ? pVisual->shader->E[SE_R1_NORMAL_HQ]
		                           : pVisual->shader->E[SE_R1_NORMAL_LQ])._get();
	case PHASE_POINT: return pVisual->shader->E[SE_R1_LPOINT]._get();
	case PHASE_SPOT: return pVisual->shader->E[SE_R1_LSPOT]._get();
	default: NODEFAULT;
	}
#ifdef DEBUG
	return	0;
#endif
}

//////////////////////////////////////////////////////////////////////////
void CRender::create()
{
	L_DB = 0;
	L_Shadows = 0;
	L_Projector = 0;

	Device.seqFrame.Add(this,REG_PRIORITY_HIGH + 0x12345678);

	// c-setup
	dxRenderDeviceRender::Instance().Resources->RegisterConstantSetup("L_dynamic_pos", &r1_dlight_binder_PR);
	dxRenderDeviceRender::Instance().Resources->RegisterConstantSetup("L_dynamic_color", &r1_dlight_binder_color);
	dxRenderDeviceRender::Instance().Resources->RegisterConstantSetup("L_dynamic_xform", &r1_dlight_binder_xform);

	// distortion
	u32 v_dev = CAP_VERSION(HW.Caps.raster_major, HW.Caps.raster_minor);
	u32 v_need = CAP_VERSION(1, 4);
	o.distortion = v_dev >= v_need && !Core.ParamsData.test(ECoreParams::nodistort);
	Msg("* distortion: %s, dev(%d),need(%d)", o.distortion ? "used" : "unavailable", v_dev, v_need);

	//	Color mapping
	o.color_mapping = v_dev >= v_need && !Core.ParamsData.test(ECoreParams::nocolormap);
	Msg("* color_mapping: %s, dev(%d),need(%d)", o.color_mapping ? "used" : "unavailable", v_dev, v_need);

	Engine.External.SetSkinningMode();

	// disasm
	o.disasm = Core.ParamsData.test(ECoreParams::disasm);
	o.forceskinw = Core.ParamsData.test(ECoreParams::skinw);
	o.no_detail_textures = !ps_r2_ls_flags.test(R1FLAG_DETAIL_TEXTURES);
	c_ldynamic_props = "L_dynamic_props";

	o.no_ram_textures = Core.ParamsData.test(ECoreParams::noramtex) ? TRUE : ps_r__common_flags.test(RFLAG_NO_RAM_TEXTURES);
	if (o.no_ram_textures)
		Msg("* Managed textures disabled");
	else
		Msg("* Managed textures enabled");

	m_bMakeAsyncSS = false;

	//---------
	Target = xr_new<CRenderTarget>();
	//---------
	//
	Models = xr_new<CModelPool>();
	L_Dynamic = xr_new<CLightR_Manager>();
	PSLibrary.OnCreate();
	//.	HWOCC.occq_create			(occq_size);

	GMBase.initialize();
	Device.ModelDefferClear = xr_make_delegate(Models, &CModelPool::DeleteQueuedDeffer);
}

void CRender::destroy()
{
	m_bMakeAsyncSS = false;
	GMBase.destroy();
	//.	HWOCC.occq_destroy			();
	PSLibrary.OnDestroy();

	xr_delete(L_Dynamic);
	xr_delete(Models);

	//*** Components
	xr_delete(Target);
	Device.seqFrame.Remove(this);

	Device.ModelDefferClear = nullptr;
}

void CRender::reset_begin()
{
	//AVO: let's reload details while changed details options on vid_restart
	if (b_loaded && ((dm_current_size != dm_size) || (ps_r__Detail_density != ps_current_detail_density) || (
		ps_r__Detail_height != ps_current_detail_height)))
	{
		Details->Unload();
		xr_delete(Details);
	}
	xr_delete(Target);
	//.	HWOCC.occq_destroy			();
}

void CRender::reset_end()
{
	//.	HWOCC.occq_create			(occq_size);
	Target = xr_new<CRenderTarget>();
	if (L_Projector) L_Projector->invalidate();

	// let's reload details while changed details options on vid_restart
	if (b_loaded && (dm_current_size != dm_size || ps_r__Detail_density != ps_current_detail_density))
	{
		Details = xr_new<CDetailManager>();
		Details->Load();
	}

	// Set this flag true to skip the first render frame,
	// that some data is not ready in the first frame (for example device camera position)
	m_bFirstFrameAfterReset = true;
}

void CRender::OnFrame()
{
	Models->DeleteQueue();

	//Lights Delete queue
	for (light* L : v_all_lights_dque)
		xr_delete(L);

	v_all_lights_dque.clear();
}

// Implementation
IRender_ObjectSpecific* CRender::ros_create(IRenderable* parent) { return xr_new<CROS_impl>(); }
void CRender::ros_destroy(IRender_ObjectSpecific* & p) { xr_delete(p); }
IRenderVisual* CRender::model_Create(LPCSTR name, IReader* data) { return Models->Create(name, data); }
IRenderVisual* CRender::model_CreateChild(LPCSTR name, IReader* data) { return Models->CreateChild(name, data); }
IRenderVisual* CRender::model_Duplicate(IRenderVisual* V) { return Models->Instance_Duplicate((dxRender_Visual*)V); }

void CRender::model_Delete(IRenderVisual* & V, BOOL bDiscard)
{
	dxRender_Visual* pVisual = (dxRender_Visual*)V;
    if (Models)
        Models->Delete(pVisual, bDiscard);
	V = 0;
}

void CRender::model_Delete_Deffered(IRenderVisual* & V)
{
	dxRender_Visual* pVisual = (dxRender_Visual*)V;
	Models->DeleteDeffered(pVisual);
	V = 0;
}

IRender_DetailModel* CRender::model_CreateDM(IReader* F)
{
	CDetail* D = xr_new<CDetail>();
	D->Load(F);
	return D;
}

void CRender::model_Delete(IRender_DetailModel* & F)
{
	if (F)
	{
		CDetail* D = (CDetail*)F;
		D->Unload();
		xr_delete(D);
		F = NULL;
	}
}

IRenderVisual* CRender::model_CreatePE(LPCSTR name)
{
	PS::CPEDef* SE = PSLibrary.FindPED(name);
	R_ASSERT3(SE, "Particle effect doesn't exist", name);
	return Models->CreatePE(SE);
}

IRenderVisual* CRender::model_CreateParticles(LPCSTR name)
{
	PS::CPEDef* SE = PSLibrary.FindPED(name);
	if (SE) return Models->CreatePE(SE);
	else
	{
		PS::CPGDef* SG = PSLibrary.FindPGD(name);
		R_ASSERT3(SG, "Particle effect or group doesn't exist", name);
		return Models->CreatePG(SG);
	}
}

void CRender::models_Prefetch() { Models->Prefetch(); }
void CRender::models_PrefetchOne(LPCSTR name, bool assert) { Models->Prefetch_One(name, assert); }
void CRender::models_Clear(BOOL b_complete) { Models->ClearPool(b_complete); }
bool CRender::models_Exists(LPCSTR name) { return Models->Exists(name); }

ref_shader CRender::getShader(int id)
{
	VERIFY(id<int(Shaders.size()));
	return Shaders[id];
}

IRender_Portal* CRender::getPortal(int id)
{
	VERIFY(id<int(Portals.size()));
	return Portals[id];
}

IRender_Sector* CRender::getSector(int id)
{
	VERIFY(id<int(Sectors.size()));
	return Sectors[id];
}

IRender_Sector* CRender::getSectorActive() { return pLastSector; }

IRenderVisual* CRender::getVisual(int id)
{
	VERIFY(id<int(Visuals.size()));
	return Visuals[id];
}

D3DVERTEXELEMENT9* CRender::getVB_Format(int id)
{
	VERIFY(id<int(DCL.size()));
	return DCL[id].begin();
}

IDirect3DVertexBuffer9* CRender::getVB(int id)
{
	VERIFY(id<int(VB.size()));
	return VB[id];
}

IDirect3DIndexBuffer9* CRender::getIB(int id)
{
	VERIFY(id<int(IB.size()));
	return IB[id];
}

IRender_Target* CRender::getTarget() { return Target; }

FSlideWindowItem* CRender::getSWI(int id)
{
	VERIFY(id<int(SWIs.size()));
	return &SWIs[id];
}

IRender_Light* CRender::light_create() { return L_DB->Create(); }

IRender_Glow* CRender::glow_create() { return xr_new<CGlow>(); }

void CRender::flush() { RImplementation.GMBase.r_dsgraph_render_graph(0); }

BOOL CRender::occ_visible(vis_data& P) { return HOM.visible(P); }
BOOL CRender::occ_visible(sPoly& P) { return HOM.visible(P); }
BOOL CRender::occ_visible(Fbox& P) { return HOM.visible(P); }

// demonized: add user defined rotation to wallmark
void CRender::add_StaticWallmark(ref_shader& S, const Fvector& P, float s, CDB::TRI* T, Fvector* verts, float ttl, bool ignore_opt, bool random_rotation)
{
	add_StaticWallmark(S, P, s, T, verts, ttl, ignore_opt, random_rotation ? ::Random.randF(-20.f, 20.f) : 0.f);
}

void CRender::add_StaticWallmark(ref_shader& S, const Fvector& P, float s, CDB::TRI* T, Fvector* verts, float ttl, bool ignore_opt, float rotation)
{
	if (T->suppress_wm) return;
	VERIFY2(_valid(P) && _valid(s) && T && verts && (s > EPS_L), "Invalid static wallmark params");
	Wallmarks->AddStaticWallmark(T, verts, P, &*S, s, ttl, ignore_opt, rotation);
}

void CRender::add_StaticWallmark(IWallMarkArray* pArray, const Fvector& P, float s, CDB::TRI* T, Fvector* V, float ttl, bool ignore_opt, bool random_rotation)
{
	add_StaticWallmark(pArray, P, s, T, V, ttl, ignore_opt, random_rotation ? ::Random.randF(-20.f, 20.f) : 0.f);
}

void CRender::add_StaticWallmark(IWallMarkArray* pArray, const Fvector& P, float s, CDB::TRI* T, Fvector* V, float ttl, bool ignore_opt, float rotation)
{
	dxWallMarkArray* pWMA = (dxWallMarkArray*)pArray;
	ref_shader* pShader = pWMA->dxGenerateWallmark();
	if (pShader) add_StaticWallmark(*pShader, P, s, T, V, ttl, ignore_opt, rotation);
}

void CRender::add_StaticWallmark(const wm_shader& S, const Fvector& P, float s, CDB::TRI* T, Fvector* V)
{
	dxUIShader* pShader = (dxUIShader*)&*S;
	add_StaticWallmark(pShader->hShader, P, s, T, V, 0.0f, false, true);
}

void CRender::clear_static_wallmarks()
{
	if (Wallmarks)
		Wallmarks->clear();
}

void CRender::add_SkeletonWallmark(intrusive_ptr<CSkeletonWallmark> wm)
{
	Wallmarks->AddSkeletonWallmark(std::move(wm));
}

void CRender::add_SkeletonWallmark(const Fmatrix* xf, CKinematics* obj, ref_shader& sh, const Fvector& start,
                                   const Fvector& dir, float size, float ttl, bool ignore_opt)
{
	Wallmarks->AddSkeletonWallmark(xf, obj, sh, start, dir, size, ttl, ignore_opt);
}

void CRender::add_SkeletonWallmark(const Fmatrix* xf, IKinematics* obj, IWallMarkArray* pArray, const Fvector& start,
                                   const Fvector& dir, float size, float ttl, bool ignore_opt)
{
	dxWallMarkArray* pWMA = (dxWallMarkArray *)pArray;
	ref_shader* pShader = pWMA->dxGenerateWallmark();
	if (pShader) add_SkeletonWallmark(xf, (CKinematics*)obj, *pShader, start, dir, size, ttl, ignore_opt);
}

void CRender::remove_SkeletonWallmarksFromObject(IKinematics* obj)
{
    Wallmarks->RemoveSkeletonWallmarksFromObject(static_cast<CKinematics*>(obj));
}

void CRender::update_Wallmarks()
{
    Wallmarks->UpdateWallmarks();
}

void CRender::add_Occluder(Fbox2& bb_screenspace)
{
	VERIFY(_valid(bb_screenspace));
	HOM.occlude(bb_screenspace);
}

#include "../../xrEngine/PS_instance.h"

void CRender::apply_object(IRenderable* O)
{
	if (0 == O) return;
	if (O->renderable_ROS())
	{
		CROS_impl& LT = *((CROS_impl*)O->renderable.pROS);
		float o_hemi = 0.5f * LT.get_hemi();
		float o_sun = 0.5f * LT.get_sun();
		RCache.set_c(c_ldynamic_props, o_sun, o_sun, o_sun, o_hemi);
		// shadowing
		if ((LT.shadow_recv_frame == Device.dwFrame) && O->renderable_ShadowReceive())
			L_Projector->setup(LT.shadow_recv_slot);
	}
}

// Misc
float g_fSCREEN;
static BOOL gm_Nearer = 0;

IC void gm_SetNearer(BOOL bNearer)
{
	if (bNearer != gm_Nearer)
	{
		gm_Nearer = bNearer;
		if (gm_Nearer) RImplementation.rmNear();
		else RImplementation.rmNormal();
	}
}

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////
CRender::CRender()
	: m_bFirstFrameAfterReset(false)
{
}

CRender::~CRender()
{
	for (auto& it : SWIs) {
		xr_free(it.sw);
		it.sw = nullptr;
		it.count = 0;
	}
	SWIs.clear();
}

extern float r_ssaDISCARD;
extern float r_ssaDONTSORT;
extern float r_ssaLOD_A, r_ssaLOD_B;
extern float r_ssaGLOD_start, r_ssaGLOD_end;
extern float r_ssaHZBvsTEX;

void CRender::Calculate()
{
#ifdef _GPA_ENABLED
	TAL_SCOPED_TASK_NAMED("CRender::Calculate()");
#endif // _GPA_ENABLED

	Device.Statistic->RenderCALC.Begin();

	// Transfer to global space to avoid deep pointer access
	IRender_Target* T = getTarget();
	float fov_factor = _sqr(90.f / Device.fFOV);
	g_fSCREEN = float(T->get_width() * T->get_height()) * fov_factor * (EPS_S + ps_r__LOD);
	r_ssaDISCARD = _sqr(ps_r__ssaDISCARD) / g_fSCREEN;
	r_ssaDONTSORT = _sqr(ps_r__ssaDONTSORT / 3) / g_fSCREEN;
	r_ssaLOD_A = _sqr(ps_r1_ssaLOD_A / 3) / g_fSCREEN;
	r_ssaLOD_B = _sqr(ps_r1_ssaLOD_B / 3) / g_fSCREEN;
	r_ssaGLOD_start = _sqr(ps_r__GLOD_ssa_start / 3) / g_fSCREEN;
	r_ssaGLOD_end = _sqr(ps_r__GLOD_ssa_end / 3) / g_fSCREEN;
	r_ssaHZBvsTEX = _sqr(ps_r__ssaHZBvsTEX / 3) / g_fSCREEN;

	// Frustum & HOM rendering
	ViewBase.CreateFromMatrix(Device.mFullTransform, FRUSTUM_P_LRTB | FRUSTUM_P_FAR);
    HOM.MT_RENDER();
	gm_SetNearer(FALSE);
	phase = PHASE_NORMAL;

	// Detect camera-sector
	if (!vLastCameraPos.similar(Device.vCameraPosition, EPS_S))
	{
		CSector* pSector = (CSector*)detectSector(Device.vCameraPosition);
		if (pSector && (pSector != pLastSector))
			g_pGamePersistent->OnSectorChanged(translateSector(pSector));

		if (0 == pSector) pSector = pLastSector;
		pLastSector = pSector;
		vLastCameraPos.set(Device.vCameraPosition);
	}

	//
	if (L_DB)
		L_DB->Update();

	// Main process
	GMBase.traverse(pLastSector, ViewBase, Device.vCameraPosition, Device.mFullTransform);
	GMBase.r_dsgraph_capture(true, true);

	// Calculate miscelaneous stuff
	L_Shadows->calculate();
	L_Projector->calculate();

	// End calc
	Device.Statistic->RenderCALC.End();
}

void CRender::rmNear()
{
	IRender_Target* T = getTarget();
	D3DVIEWPORT9 VP = {0, 0, T->get_width(), T->get_height(), 0, 0.02f};
	CHK_DX(HW.pDevice->SetViewport(&VP));
}

void CRender::rmFar()
{
	IRender_Target* T = getTarget();
	D3DVIEWPORT9 VP = {0, 0, T->get_width(), T->get_height(), 0.99999f, 1.f};
	CHK_DX(HW.pDevice->SetViewport(&VP));
}

void CRender::rmNormal()
{
	IRender_Target* T = getTarget();
	D3DVIEWPORT9 VP = {0, 0, T->get_width(), T->get_height(), 0, 1.f};
	CHK_DX(HW.pDevice->SetViewport(&VP));
}


void CRender::Render()
{
#ifdef _GPA_ENABLED
		TAL_SCOPED_TASK_NAMED( "CRender::Render()" );
#endif // _GPA_ENABLED

	if (m_bFirstFrameAfterReset)
	{
		m_bFirstFrameAfterReset = false;
		return;
	}

	Device.Statistic->RenderDUMP.Begin();
	// Begin
	Target->Begin();
	phase = PHASE_NORMAL;
	GMBase.r_dsgraph_render_hud(); // hud
	GMBase.r_dsgraph_render_graph(0); // normal level
	if (Details)Details->Render(); // grass / details
	GMBase.r_dsgraph_render_lods(true, false); // lods - FB

	CEnvironment* Env = &g_pGamePersistent->Environment();
	Env->RenderSky(); // sky / sun
	Env->RenderClouds(); // clouds

	L_Dynamic->render(0); // addititional light sources
	if (Wallmarks)Wallmarks->Render(); // wallmarks has priority as normal geometry

	if (g_hud)
	{
		g_hud->Render_R1_Attachment_UI();

		if (g_hud->RenderActiveItemUIQuery())
			GMBase.r_dsgraph_render_hud_ui();
		if (g_hud->RenderCamAttachedUIQuery())
			GMBase.r_dsgraph_render_cam_ui();
	}

	phase = PHASE_NORMAL;
	if (L_Shadows)L_Shadows->render(); // ... and shadows
	GMBase.r_dsgraph_render_lods(false, true); // lods - FB
	GMBase.r_dsgraph_render_static(1); // normal level, secondary priority
	CParticlesAsync::Wait();
	GMBase.r_dsgraph_render_dynamic(1, true); // normal level, secondary priority
	L_Dynamic->render(1); // addititional light sources, secondary priority
	GMBase.fade_render(); // faded-portals
	GMBase.r_dsgraph_render_sorted(); // strict-sorted geoms
	if (L_Glows)L_Glows->Render(); // glows
	if (ps_r2_anomaly_flags.test(R2_AN_FLAG_FLARES))Env->RenderFlares(); // lens-flares
	Env->RenderLast(); // rain/thunder-bolts

#if DEBUG
	for (int _priority=0; _priority<2; ++_priority)
	{
		for ( u32 iPass = 0; iPass<SHADER_PASSES_MAX; ++iPass)
		{
			R_ASSERT( GMBase.RGraph.mapStaticPasses[_priority][iPass].size() == 0);
			R_ASSERT( GMBase.RGraph.mapDynamicPasses[_priority][iPass].size() == 0);
		}
	}

#endif
	// Postprocess, if necessary
	Target->End();
	if (L_Projector) L_Projector->finalize();

	// HUD
	Device.Statistic->RenderDUMP.End();
}

void CRender::ApplyBlur4(FVF::TL4uv* pv, u32 w, u32 h, float k)
{
	float _w = float(w);
	float _h = float(h);
	float kw = (1.f / _w) * k;
	float kh = (1.f / _h) * k;
	Fvector2 p0, p1;
	p0.set(.5f / _w, .5f / _h);
	p1.set((_w + .5f) / _w, (_h + .5f) / _h);
	u32 _c = 0xffffffff;

	// Fill vertex buffer
	pv->p.set(EPS, float(_h + EPS), EPS, 1.f);
	pv->color = _c;
	pv->uv[0].set(p0.x - kw, p1.y - kh);
	pv->uv[1].set(p0.x + kw, p1.y + kh);
	pv->uv[2].set(p0.x + kw, p1.y - kh);
	pv->uv[3].set(p0.x - kw, p1.y + kh);
	pv++;
	pv->p.set(EPS, EPS, EPS, 1.f);
	pv->color = _c;
	pv->uv[0].set(p0.x - kw, p0.y - kh);
	pv->uv[1].set(p0.x + kw, p0.y + kh);
	pv->uv[2].set(p0.x + kw, p0.y - kh);
	pv->uv[3].set(p0.x - kw, p0.y + kh);
	pv++;
	pv->p.set(float(_w + EPS), float(_h + EPS), EPS, 1.f);
	pv->color = _c;
	pv->uv[0].set(p1.x - kw, p1.y - kh);
	pv->uv[1].set(p1.x + kw, p1.y + kh);
	pv->uv[2].set(p1.x + kw, p1.y - kh);
	pv->uv[3].set(p1.x - kw, p1.y + kh);
	pv++;
	pv->p.set(float(_w + EPS), EPS, EPS, 1.f);
	pv->color = _c;
	pv->uv[0].set(p1.x - kw, p0.y - kh);
	pv->uv[1].set(p1.x + kw, p0.y + kh);
	pv->uv[2].set(p1.x + kw, p0.y - kh);
	pv->uv[3].set(p1.x - kw, p0.y + kh);
	pv++;
}

#include "../../xrEngine/GameFont.h"

void CRender::Statistics(CGameFont* _F)
{
	CGameFont& F = *_F;
	F.OutNext(" **** Occ-Q(%03.1f) **** ", 100.f * f32(stats.o_culled) / f32(stats.o_queries ? stats.o_queries : 1));
	F.OutNext(" total  : %2d", stats.o_queries);
	stats.o_queries = 0;
	F.OutNext(" culled : %2d", stats.o_culled);
	stats.o_culled = 0;
	F.OutSkip();
#ifdef DEBUG
	HOM.stats	();
#endif
}

#pragma comment(lib,"d3dx9.lib")



static inline bool match_shader_id(LPCSTR const debug_shader_id, LPCSTR const full_shader_id,
                                   FS_FileSet const& file_set, string_path& result);

//--------------------------------------------------------------------------------------------------------------
class includer : public ID3DXInclude
{
public:
	HRESULT __stdcall Open(D3DXINCLUDE_TYPE IncludeType, LPCSTR pFileName, LPCVOID pParentData, LPCVOID* ppData,
	                       UINT* pBytes)
	{
		string_path pname;
		strconcat(sizeof(pname), pname, ::Render->getShaderPath(), pFileName);
		IReader* R = FS.r_open("$game_shaders$", pname);
		if (0 == R)
		{
			// possibly in shared directory or somewhere else - open directly
			R = FS.r_open("$game_shaders$", pFileName);
			if (0 == R) return E_FAIL;
		}

		// duplicate and zero-terminate
		u32 size = R->length();
		u8* data = xr_alloc<u8>(size + 1);
		CopyMemory(data, R->pointer(), size);
		data[size] = 0;
		FS.r_close(R);

		*ppData = data;
		*pBytes = size;
		return D3D_OK;
	}

	HRESULT __stdcall Close(LPCVOID pData)
	{
		xr_free(pData);
		return D3D_OK;
	}
};

static HRESULT create_shader(
	LPCSTR const pTarget,
	DWORD const* buffer,
	u32 const buffer_size,
	LPCSTR const file_name,
	void*& result,
	bool const disasm
)
{
	HRESULT _result = E_FAIL;
	if (pTarget[0] == 'p')
	{
		SPS* sps_result = (SPS*)result;
		_result = HW.pDevice->CreatePixelShader(buffer, &sps_result->ps);
		if (!SUCCEEDED(_result))
		{
			Log("! PS: ", file_name);
			Msg("! CreatePixelShader hr == 0x%08x", _result);
			return E_FAIL;
		}

		LPCVOID data = NULL;
		_result = D3DXFindShaderComment(buffer,MAKEFOURCC('C', 'T', 'A', 'B'), &data,NULL);
		if (SUCCEEDED(_result) && data)
		{
			LPD3DXSHADER_CONSTANTTABLE pConstants = LPD3DXSHADER_CONSTANTTABLE(data);
			sps_result->constants.parse(pConstants, 0x1);
		}
		else
		{
			Log("! PS: ", file_name);
			Msg("! D3DXFindShaderComment hr == 0x%08x", _result);
		}
	}
	else
	{
		SVS* svs_result = (SVS*)result;
		_result = HW.pDevice->CreateVertexShader(buffer, &svs_result->vs);
		if (!SUCCEEDED(_result))
		{
			Log("! VS: ", file_name);
			Msg("! CreatePixelShader hr == 0x%08x", _result);
			return E_FAIL;
		}

		LPCVOID data = NULL;
		_result = D3DXFindShaderComment(buffer,MAKEFOURCC('C', 'T', 'A', 'B'), &data,NULL);
		if (SUCCEEDED(_result) && data)
		{
			LPD3DXSHADER_CONSTANTTABLE pConstants = LPD3DXSHADER_CONSTANTTABLE(data);
			svs_result->constants.parse(pConstants, 0x2);
		}
		else
		{
			Log("! VS: ", file_name);
			Msg("! D3DXFindShaderComment hr == 0x%08x", _result);
		}
	}

	if (disasm)
	{
		ID3DXBuffer* disasm = 0;
		D3DXDisassembleShader(LPDWORD(buffer), FALSE, 0, &disasm);
		string_path dname;
		strconcat(sizeof(dname), dname, "disasm\\", file_name, ('v' == pTarget[0]) ? ".vs" : ".ps");
		IWriter* W = FS.w_open("$logs$", dname);
		W->w(disasm->GetBufferPointer(), disasm->GetBufferSize());
		FS.w_close(W);
		_RELEASE(disasm);
	}

	return _result;
}

HRESULT CRender::shader_compile(
	LPCSTR name,
	DWORD const* pSrcData,
	UINT SrcDataLen,
	LPCSTR pFunctionName,
	LPCSTR pTarget,
	DWORD Flags,
	void*& result
)
{
	const int m_skinning = Engine.External.GetSkinningMode();
	
	D3DXMACRO defines [128];
	int def_it = 0;

	char sh_name[MAX_PATH] = "";
	u32 len = 0;

	// options
	if (o.forceskinw)
	{
		defines[def_it].Name = "SKIN_COLOR";
		defines[def_it].Definition = "1";
		def_it ++;
	}
	sh_name[len] = '0' + char(o.forceskinw);
	++len;

	if (m_skinning < 0)
	{
		defines[def_it].Name = "SKIN_NONE";
		defines[def_it].Definition = "1";
		def_it ++;
		sh_name[len] = '1';
		++len;
	}
	else
	{
		sh_name[len] = '0';
		++len;
	}

	if (0 == m_skinning)
	{
		defines[def_it].Name = "SKIN_0";
		defines[def_it].Definition = "1";
		def_it ++;
	}
	sh_name[len] = '0' + char(0 == m_skinning);
	++len;

	if (1 == m_skinning)
	{
		defines[def_it].Name = "SKIN_1";
		defines[def_it].Definition = "1";
		def_it ++;
	}
	sh_name[len] = '0' + char(1 == m_skinning);
	++len;

	if (2 == m_skinning)
	{
		defines[def_it].Name = "SKIN_2";
		defines[def_it].Definition = "1";
		def_it ++;
	}
	sh_name[len] = '0' + char(2 == m_skinning);
	++len;

	if (3 == m_skinning)
	{
		defines[def_it].Name = "SKIN_3";
		defines[def_it].Definition = "1";
		def_it ++;
	}
	sh_name[len] = '0' + char(3 == m_skinning);
	++len;

	if (4 == m_skinning)
	{
		defines[def_it].Name = "SKIN_4";
		defines[def_it].Definition = "1";
		def_it ++;
	}
	sh_name[len] = '0' + char(4 == m_skinning);
	++len;

	// finish
	defines[def_it].Name = 0;
	defines[def_it].Definition = 0;
	def_it ++;
	R_ASSERT(def_it<128);

	HRESULT _result = E_FAIL;

	string_path folder_name, folder;
	xr_strcpy(folder, "r1\\objects\\r1\\");
	xr_strcat(folder, name);
	xr_strcat(folder, ".");

	char extension[3];
	strncpy_s(extension, pTarget, 2);
	xr_strcat(folder, extension);

	FS.update_path(folder_name, "$game_shaders$", folder);
	xr_strcat(folder_name, "\\");

	m_file_set.clear();
	FS.file_list(m_file_set, folder_name, FS_ListFiles | FS_RootOnly, "*");

	string_path temp_file_name, file_name;
	bool const useGeneratedShaderCache =
		psDeviceFlags2.test(rsPrecompiledShaders) || !match_shader_id(name, sh_name, m_file_set, temp_file_name);
	if (useGeneratedShaderCache)
	{
		string_path file;
		xr_strcpy(file, "shaders_cache\\r1\\");
		xr_strcat(file, name);
		xr_strcat(file, ".");
		xr_strcat(file, extension);
		xr_strcat(file, "\\");
		xr_strcat(file, sh_name);
		FS.update_path(file_name, "$app_data_root$", file);
	}
	else
	{
		xr_strcpy(file_name, folder_name);
		xr_strcat(file_name, temp_file_name);
	}

	u32 source_crc = 0;
	if (useGeneratedShaderCache)
		source_crc = getShaderSourceCrc32(pSrcData, SrcDataLen, ::Render->getShaderPath());

	if (FS.exist(file_name))
	{
		IReader* file = FS.r_open(file_name);
		if (useGeneratedShaderCache)
		{
			if (file->length() > 8)
			{
				u32 const saved_source_crc = file->r_u32();
				if (saved_source_crc == source_crc)
				{
					u32 const crc = file->r_u32();
					u32 const real_crc = crc32(file->pointer(), file->elapsed());

					if (real_crc == crc)
					{
						_result = create_shader(pTarget, (DWORD*)file->pointer(), file->elapsed(), file_name, result, o.disasm);
					}
				}
				else
				{
					Msg("! Shader cache source CRC mismatch for '%s' (%s): cached=0x%08x current=0x%08x, recompiling",
						name, file_name, saved_source_crc, source_crc);
				}
			}
		}
		else if (file->length() > 4)
		{
			u32 const crc = file->r_u32();

			u32 const real_crc = crc32(file->pointer(), file->elapsed());

			if (real_crc == crc)
			{
				_result = create_shader(pTarget, (DWORD*)file->pointer(), file->elapsed(), file_name, result, o.disasm);
			}
		}
		file->close();
	}

	if (FAILED(_result))
	{
		includer Includer;
		LPD3DXBUFFER pShaderBuf = NULL;
		LPD3DXBUFFER pErrorBuf = NULL;
		LPD3DXCONSTANTTABLE pConstants = NULL;
		LPD3DXINCLUDE pInclude = (LPD3DXINCLUDE)&Includer;

		_result = D3DXCompileShader((LPCSTR)pSrcData, SrcDataLen, defines, pInclude, pFunctionName, pTarget,
		                            Flags | D3DXSHADER_USE_LEGACY_D3DX9_31_DLL, &pShaderBuf, &pErrorBuf, &pConstants);
		if (SUCCEEDED(_result))
		{
			IWriter* file = FS.w_open(file_name);

			u32 const crc = crc32(pShaderBuf->GetBufferPointer(), pShaderBuf->GetBufferSize());

			if (useGeneratedShaderCache)
				file->w_u32(source_crc);

			file->w_u32(crc);
			file->w(pShaderBuf->GetBufferPointer(), (u32)pShaderBuf->GetBufferSize());
			FS.w_close(file);

			_result = create_shader(pTarget, (DWORD*)pShaderBuf->GetBufferPointer(), pShaderBuf->GetBufferSize(),
			                        file_name, result, o.disasm);
		}
		else
		{
			Log("! ", file_name);
			if (pErrorBuf)
				Log("! error: ", (LPCSTR)pErrorBuf->GetBufferPointer());
			else
				Msg("Can't compile shader hr=0x%08x", _result);
		}
	}

	return _result;
}

static inline bool match_shader(LPCSTR const debug_shader_id, LPCSTR const full_shader_id, LPCSTR const mask,
                                size_t const mask_length)
{
	u32 const full_shader_id_length = xr_strlen(full_shader_id);
	R_ASSERT2(
		full_shader_id_length == mask_length,
		make_string(
			"bad cache for shader %s, [%s], [%s]",
			debug_shader_id,
			mask,
			full_shader_id
		)
	);
	char const* i = full_shader_id;
	char const* const e = full_shader_id + full_shader_id_length;
	char const* j = mask;
	for (; i != e; ++i, ++j)
	{
		if (*i == *j)
			continue;

		if (*j == '_')
			continue;

		return false;
	}

	return true;
}

static inline bool match_shader_id(LPCSTR const debug_shader_id, LPCSTR const full_shader_id,
                                   FS_FileSet const& file_set, string_path& result)
{
#if 0
	strcpy_s					( result, "" );
	return						false;
#else // #if 1
#ifdef DEBUG
	LPCSTR temp					= "";
	bool found					= false;
	FS_FileSet::const_iterator	i = file_set.begin();
	FS_FileSet::const_iterator	const e = file_set.end();
	for ( ; i != e; ++i ) {
		if ( match_shader(debug_shader_id, full_shader_id, (*i).name.c_str(), (*i).name.size() ) ) {
			VERIFY				( !found );
			found				= true;
			temp				= (*i).name.c_str();
		}
	}

	xr_strcpy					( result, temp );
	return						found;
#else // #ifdef DEBUG
	FS_FileSet::const_iterator i = file_set.begin();
	FS_FileSet::const_iterator const e = file_set.end();
	for (; i != e; ++i)
	{
		if (match_shader(debug_shader_id, full_shader_id, (*i).name.c_str(), (*i).name.size()))
		{
			xr_strcpy(result, (*i).name.c_str());
			return true;
		}
	}

	return false;
#endif // #ifdef DEBUG
#endif// #if 1
}

void CRender::RenderToTarget(RRT target)
{
	ref_rt* RT = nullptr;

	switch (target)
	{
	case rtPDA:
		RT = &Target->rt_ui_pda;
		break;
	case rtSVP:
		RT = &Target->rt_secondVP;
		break;
	default:
		Debug.fatal(DEBUG_INFO, "None or wrong Target specified: %i", target);
		break;
	}

	IDirect3DSurface9* pBackBuffer = nullptr;
	HW.pDevice->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &pBackBuffer);
	D3DXLoadSurfaceFromSurface((*RT)->pRT, 0, 0, pBackBuffer, 0, 0, D3DX_DEFAULT, 0);
	pBackBuffer->Release();
}
