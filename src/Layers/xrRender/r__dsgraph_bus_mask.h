#pragma once
// shader bus mask target, shared by the HUD redraw and the object mask pass
#if defined(USE_DX10) || defined(USE_DX11)
#include "ResourceManager.h"
#include "dxRenderDeviceRender.h"
#include "../xrRenderDX10/StateManager/dx10StateCache.h"

// creates and clears the shader bus mask target, true when the HUD lists redraw into it
static bool bus_mask_begin()
{
	if (!ShaderBus::mask_enabled())
		return false;

	static const u32 lane = ShaderBus::register_engine("engine_hud_mask",
		"x is 1 while the mask target holds this frame's HUD bits, else 0");
	const bool hud = ShaderBus::declared(lane);
	const bool held = hud || ShaderBus::mask_live_bits() != 0;

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
		if (!T->bs_bus_mask)
		{
			SimulatorStates bs;
			bs.set_RS(D3DRS_ALPHABLENDENABLE, TRUE);
			bs.set_RS(D3DRS_SRCBLEND, D3DBLEND_BLENDFACTOR);
			bs.set_RS(D3DRS_DESTBLEND, D3DBLEND_ZERO);
			T->bs_bus_mask = BSManager.GetState(bs);
		}
	}

	if (T->rt_bus_mask)
	{
		const FLOAT zero[4] = { 0.f, 0.f, 0.f, 0.f };
		HW.pContext->ClearRenderTargetView(T->rt_bus_mask->pRT, zero);
	}

	if (hud)
		ShaderBus::set(lane, T->rt_bus_mask ? 1.f : 0.f, 0.f, 0.f, 0.f);
	return hud && T->rt_bus_mask;
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

static void bus_mask_unbind(ID3DRenderTargetView* saved[4])
{
	StateManager.SetBlendFactor(0.f);
	for (u32 i = 0; i < 4; ++i)
		RCache.set_RT(saved[i], i);
}
#else
static bool bus_mask_begin() { return false; }
#endif
