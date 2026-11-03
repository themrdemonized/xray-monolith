#pragma once
// shader bus mask target, shared by the HUD redraw and the object mask pass
#if defined(USE_DX10) || defined(USE_DX11)
#include "ResourceManager.h"
#include "dxRenderDeviceRender.h"
#include "../xrRenderDX10/StateManager/dx10StateCache.h"
#include "../../xrEngine/xr_object.h"
#include "../../Include/xrRender/Kinematics.h"
#include "../../xrEngine/bone.h"
#include "FBasicVisual.h"

// compiles the hidden depth resolve over the stock screen space vertex shader, reading the position target
class CBlender_bus_hidden_resolve : public IBlender
{
public:
	virtual LPCSTR getComment() { return "INTERNAL: shader bus hidden depth resolve"; }
	virtual BOOL canBeDetailed() { return FALSE; }
	virtual BOOL canBeLMAPped() { return FALSE; }
	virtual void Compile(CBlender_Compile& C)
	{
		IBlender::Compile(C);
		C.r_Pass("stub_screen_space", "bus_hidden_resolve", FALSE, TRUE, TRUE);
		C.r_dx10Texture("s_position", r2_RT_P);
		C.r_End();
	}
};

// creates and clears the shader bus mask target, true when the HUD lists redraw into it
static bool bus_mask_begin()
{
	if (!ShaderBus::mask_enabled())
		return false;

	static const u32 lane = ShaderBus::register_engine("engine_hud_mask",
		"x is 1 while the mask target holds this frame's HUD bits, else 0");
	static const u32 ids_lane = ShaderBus::register_engine("engine_mask_ids",
		"x is 1 while the id target holds this frame's object ids, else 0");
	const bool hud = ShaderBus::declared(lane);
	// an engine target exists only while a compiled pass samples its texture
	const bool ids = ShaderBus::samples_object_ids();
	const u32 particles_lane = ShaderBus::particle_lane();
	const bool particles = particles_lane && ShaderBus::samples_particles();
	u32 layer = 0, hidden = 0;
	const bool world = ShaderBus::mask_live_bits(&layer, &hidden) != 0;
	const bool held = hud || world;

	CRenderTarget* T = RImplementation.Target;
	const bool msaa = !ShaderBus::mask_drawable();
	static bool msaa_logged = false;
	if (held && msaa && !msaa_logged)
	{
		Msg("! [SHADER-BUS] mask target refused, it needs MSAA off");
		msaa_logged = true;
	}

	if (!held || msaa)
		T->rt_bus_mask.destroy();
	else if (!T->rt_bus_mask)
	{
		T->rt_bus_mask.create("$user$bus_engine_mask", Device.dwWidth, Device.dwHeight, D3DFMT_R32F);
		if (!T->s_bus_mask)
			T->s_bus_mask = DEV->_CreatePS("bus_mask");
		if (!T->s_bus_mask_particle)
			T->s_bus_mask_particle = DEV->_CreatePS("bus_mask_particle");
		if (!T->s_bus_mask_cutout)
			T->s_bus_mask_cutout = DEV->_CreatePS("bus_mask_cutout");
		if (!T->s_bus_mask_blend)
			T->s_bus_mask_blend = DEV->_CreatePS("bus_mask_blend");
		if (!T->bs_bus_mask)
		{
			SimulatorStates bs;
			bs.set_RS(D3DRS_ALPHABLENDENABLE, TRUE);
			bs.set_RS(D3DRS_SRCBLEND, D3DBLEND_BLENDFACTOR);
			bs.set_RS(D3DRS_DESTBLEND, D3DBLEND_ZERO);
			T->bs_bus_mask = BSManager.GetState(bs);
		}
	}

	// the id target follows the mask target while a mod holds a live bit and a shader reads the ids
	if (!ids || !world || !T->rt_bus_mask)
		T->rt_bus_object_id.destroy();
	else if (!T->rt_bus_object_id)
		T->rt_bus_object_id.create("$user$bus_engine_object_id", Device.dwWidth, Device.dwHeight, D3DFMT_R32F);

	// the layer target follows the mask target while a bit on the layer is live
	if (!layer || !T->rt_bus_mask)
		T->rt_bus_mask_layer.destroy();
	else if (!T->rt_bus_mask_layer)
		T->rt_bus_mask_layer.create("$user$bus_engine_mask_layer", Device.dwWidth, Device.dwHeight, D3DFMT_R32F);

	// the hidden depth follows the mask target while a hidden bit is live
	if (!hidden || !T->rt_bus_mask)
		T->rt_bus_hidden_depth.destroy();
	else if (!T->rt_bus_hidden_depth)
	{
		T->rt_bus_hidden_depth.create("$user$bus_hidden_depth", Device.dwWidth, Device.dwHeight, D3DFMT_D24S8);
		if (!T->s_bus_hidden_resolve)
		{
			CBlender_bus_hidden_resolve resolve;
			T->s_bus_hidden_resolve.create(&resolve, "bus_hidden_resolve");
		}
	}

	// the particle target follows the mask target while a mod holds a live bit and a pass samples the particle tags
	if (!particles || !world || !T->rt_bus_mask)
		T->rt_bus_particle_mask.destroy();
	else if (!T->rt_bus_particle_mask)
		T->rt_bus_particle_mask.create("$user$bus_engine_particle_mask", Device.dwWidth, Device.dwHeight, D3DFMT_R32F);

	if (T->rt_bus_mask)
	{
		const FLOAT zero[4] = { 0.f, 0.f, 0.f, 0.f };
		HW.pContext->ClearRenderTargetView(T->rt_bus_mask->pRT, zero);
		if (T->rt_bus_object_id)
			HW.pContext->ClearRenderTargetView(T->rt_bus_object_id->pRT, zero);
		if (T->rt_bus_mask_layer)
			HW.pContext->ClearRenderTargetView(T->rt_bus_mask_layer->pRT, zero);
		if (T->rt_bus_particle_mask)
			HW.pContext->ClearRenderTargetView(T->rt_bus_particle_mask->pRT, zero);
	}

	if (hud)
		ShaderBus::set(lane, T->rt_bus_mask ? 1.f : 0.f, 0.f, 0.f, 0.f);
	if (ShaderBus::declared(ids_lane))
		ShaderBus::set(ids_lane, T->rt_bus_object_id ? 1.f : 0.f, 0.f, 0.f, 0.f);
	if (particles_lane)
		ShaderBus::set(particles_lane, T->rt_bus_particle_mask ? 1.f : 0.f, 0.f, 0.f, 0.f);
	return hud && T->rt_bus_mask;
}

// which target the world mask walk draws, the tags, the object ids, the layer bits or the particle tags
enum { bus_pass_tag, bus_pass_id, bus_pass_layer, bus_pass_particle };
static u32 bus_mask_pass = bus_pass_tag;
static u32 bus_mask_layer = 0;
// hidden tagged objects drawn into the mask per frame, the nearest kept
static constexpr u32 bus_hidden_max = 16;

// the id + 1 of the game object a draw belongs to, 0 for a level root attachment or particles
static u32 bus_object_id(IRenderable* object)
{
	CObject* o = object ? fast_dynamic_cast<CObject*>(object) : nullptr;
	return o ? u32(o->ID()) + 1 : 0;
}

// what a tagged world draw writes in the walk that runs, 0 draws nothing
static u32 bus_mask_value(IRenderable* object, u32 tag)
{
	if (bus_mask_pass == bus_pass_id)
		return tag ? bus_object_id(object) : 0;
	if (bus_mask_pass == bus_pass_layer)
		return tag & bus_mask_layer;
	return tag;
}

// mask pixel shader and states over the draw's own, after set_Element or set_States
static void bus_mask_apply(u32 tag)
{
	CRenderTarget* T = RImplementation.Target;
	RCache.set_PS(T->s_bus_mask);
	RCache.set_ZFunc(D3DCMP_EQUAL);
	RCache.set_Stencil(FALSE);
	StateManager.SetBlendState(T->bs_bus_mask);
	StateManager.SetBlendFactor(tag * (1.f / 16777216.f));
}

// mask pixel shader and states for a particle, which keeps its own depth test
static void bus_mask_apply_particle(u32 tag)
{
	CRenderTarget* T = RImplementation.Target;
	RCache.set_PS(T->s_bus_mask_particle);
	RCache.set_Stencil(FALSE);
	StateManager.SetBlendState(T->bs_bus_mask);
	StateManager.SetBlendFactor(tag * (1.f / 16777216.f));
}

// whether a forward or hidden mesh element draws into the mask, an additive glow draws nothing
static bool bus_mask_mesh_draws(const SPass* pass)
{
	return !pass->state->bus_additive;
}

// the pass's base texture and sampler also at slot 0, where the mask shaders sample them
static void bus_mask_base_to_slot0(const SPS* ps)
{
	if (ps->bus_base_t)
		if (CTexture* base = RCache.get_ActiveTexture(ps->bus_base_t))
			base->bind(0);
	if (ps->bus_base_s)
	{
		ID3DSamplerState* s = nullptr;
		HW.pContext->PSGetSamplers(ps->bus_base_s, 1, &s);
		HW.pContext->PSSetSamplers(0, 1, &s);
		_RELEASE(s);
	}
}

// slot 0 back to the texture the backend has on record for it
static void bus_mask_slot0_restore()
{
	if (CTexture* t = RCache.get_ActiveTexture(0))
		t->bind(0);
	else
		SRVSManager.SetPSResource(0, nullptr);
}

// mask pixel shader and states for a forward or hidden mesh, its own depth test with the depth write off
// an aref pass clips where its own shader clips, a blended one drops fully transparent texels
static bool bus_mask_apply_mesh(u32 tag, const SPass* pass)
{
	CRenderTarget* T = RImplementation.Target;
	const bool base = pass->vs->bus_uv_first && pass->ps->bus_base;
	const bool cutout = base && pass->bus_aref;
	const bool blend = base && !cutout && pass->state->bus_alpha_blend;
	RCache.set_PS(cutout ? T->s_bus_mask_cutout : blend ? T->s_bus_mask_blend : T->s_bus_mask);
	if (cutout || blend)
		bus_mask_base_to_slot0(pass->ps._get());
	RCache.set_Stencil(FALSE);
	StateManager.SetDepthWriteEnable(FALSE);
	StateManager.SetBlendState(T->bs_bus_mask);
	StateManager.SetBlendFactor(tag * (1.f / 16777216.f));
	return (cutout || blend) && pass->ps->bus_base_t;
}

// model space box of a visual in its current pose, a skeleton's from its visible bones as the engine builds it
static Fbox3 bus_hidden_box(dxRender_Visual* V)
{
	IKinematics* K = V->dcast_PKinematics();
	if (!K)
		return V->vis.box;
	Fbox3 box;
	box.invalidate();
	bool any = false;
	for (u16 b = 0; b < K->LL_BoneCount(); ++b)
	{
		if (!K->LL_GetBoneVisible(b))
			continue;
		const Fobb& obb = K->GetBoneData(b).get_obb();
		Fmatrix to_box, X;
		obb.xform_get(to_box);
		X.mul_43(K->LL_GetTransform(b), to_box);
		const Fvector& s = obb.m_halfsize;
		for (int i = 0; i < 8; ++i)
		{
			Fvector a, p;
			a.set((i & 1) ? s.x : -s.x, (i & 2) ? s.y : -s.y, (i & 4) ? s.z : -s.z);
			X.transform_tiny(p, a);
			box.modify(p);
		}
		any = true;
	}
	return any ? box : V->vis.box;
}

// the screen rectangle in pixels a world box covers, the whole viewport when the box reaches past the near plane
static Fvector4 bus_hidden_rect(const Fbox3& box)
{
	const float w = float(Device.dwWidth);
	const float h = float(Device.dwHeight);
	Fvector4 r = { w, h, 0.f, 0.f };
	for (int i = 0; i < 8; ++i)
	{
		Fvector p;
		box.getpoint(i, p);
		const Fvector4 v = { p.x, p.y, p.z, 1.f };
		Fvector4 c;
		Device.mFullTransform.transform(c, v);
		if (c.z < 0.f)
			return { 0.f, 0.f, w, h };
		const float x = (c.x / c.w + 1.f) * 0.5f * w;
		const float y = (1.f - c.y / c.w) * 0.5f * h;
		r.set(_min(r.x, x), _min(r.y, y), _max(r.z, x), _max(r.w, y));
	}
	return { _max(floorf(r.x), 0.f), _max(floorf(r.y), 0.f), _min(ceilf(r.z), w), _min(ceilf(r.w), h) };
}

// the scene depth into the hidden depth target inside the rectangles, the near plane elsewhere
// one quad per rectangle, or their bounding rectangle when it covers less than their sum
static void bus_hidden_depth_resolve(const xr_vector<Fvector4>& rects)
{
	CRenderTarget* T = RImplementation.Target;
	Fvector4 all = rects[0];
	float sum = 0.f;
	for (const Fvector4& r : rects)
	{
		all.set(_min(all.x, r.x), _min(all.y, r.y), _max(all.z, r.z), _max(all.w, r.w));
		sum += (r.z - r.x) * (r.w - r.y);
	}
	const bool one = (all.z - all.x) * (all.w - all.y) <= sum;
	const u32 quads = one ? 1 : rects.size();
	const float sw = float(Device.dwWidth);
	const float sh = float(Device.dwHeight);
	u32 offset = 0;
	FVF::TL* pv = (FVF::TL*)RCache.Vertex.Lock(quads * 4, T->bus_quad()->vb_stride, offset);
	for (u32 i = 0; i < quads; ++i)
	{
		const Fvector4& r = one ? all : rects[i];
		pv->set(r.x, r.w, EPS_S, 1.0f, 0, r.x / sw, r.w / sh); pv++;
		pv->set(r.x, r.y, EPS_S, 1.0f, 0, r.x / sw, r.y / sh); pv++;
		pv->set(r.z, r.w, EPS_S, 1.0f, 0, r.z / sw, r.w / sh); pv++;
		pv->set(r.z, r.y, EPS_S, 1.0f, 0, r.z / sw, r.y / sh); pv++;
	}
	RCache.Vertex.Unlock(quads * 4, T->bus_quad()->vb_stride);
	HW.pContext->ClearDepthStencilView(T->rt_bus_hidden_depth->pZRT, D3D_CLEAR_DEPTH, 0.f, 0);
	RCache.set_RT(nullptr, 0);
	RCache.set_ZB(T->rt_bus_hidden_depth->pZRT);
	RCache.set_Element(T->s_bus_hidden_resolve->E[0]);
	RCache.set_ZFunc(D3DCMP_ALWAYS);
	RCache.set_Stencil(FALSE);
	RCache.set_CullMode(CULL_NONE);
	RCache.set_Geometry(T->bus_quad());
	RCache.Render(D3DPT_TRIANGLELIST, offset, 0, quads * 4, 0, quads * 2);
	RCache.set_CullMode(CULL_CCW);
}

// states over a hidden element's own for its depth draw, stencil off and depth written where it passes
static void bus_hidden_depth_apply()
{
	RCache.set_Stencil(FALSE);
	RCache.set_ZFunc(D3DCMP_LESSEQUAL);
	StateManager.SetDepthEnable(TRUE);
	StateManager.SetDepthWriteEnable(TRUE);
}

// a hidden pass whose own shader sets its coverage through the depth draw, it tests and writes depth and does not blend
static bool bus_hidden_writes_depth(const SPass* pass)
{
	return pass->state->bus_zwrite && !pass->state->bus_alpha_blend && bus_mask_mesh_draws(pass);
}

// binds the mask target alone, keeping the bound targets in saved
static void bus_mask_bind(ID3DRenderTargetView* saved[4])
{
	for (u32 i = 0; i < 4; ++i)
		saved[i] = RCache.get_RT(i);
	RCache.set_RT(RImplementation.Target->rt_bus_mask->pRT, 0);
	RCache.set_RT(nullptr, 1);
	RCache.set_RT(nullptr, 2);
	RCache.set_RT(nullptr, 3);
}

// points the world mask walk at another target, the layer pass with no depth buffer bound
static void bus_mask_pass_begin(u32 pass, ID3DRenderTargetView* rt, u32 layer)
{
	bus_mask_pass = pass;
	RCache.set_RT(rt, 0);
	if (pass == bus_pass_layer)
	{
		bus_mask_layer = layer;
		RCache.set_ZB(nullptr);
	}
}

static void bus_mask_unbind(ID3DRenderTargetView* saved[4])
{
	bus_mask_pass = bus_pass_tag;
	StateManager.SetBlendFactor(0.f);
	for (u32 i = 0; i < 4; ++i)
		RCache.set_RT(saved[i], i);
}
#else
static bool bus_mask_begin() { return false; }
#endif
