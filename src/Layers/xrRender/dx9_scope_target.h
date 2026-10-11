#pragma once

#include "ResourceManager.h"
#include "dxRenderDeviceRender.h"
#include "../../xrEngine/IGame_Persistent.h"

bool CRenderTarget::IsScopeThermalActive(bool require_lens) const
{
    extern Fvector4 ps_s3ds_param_3;
    extern int ps_markswitch_current;
    const int type = int(ps_s3ds_param_3.x);
    return g_pGameLevel && g_pGamePersistent && scope_3D_fake_enabled &&
        g_pGamePersistent->m_pGShaderConstants->hud_params.x > EPS_S &&
        !Device.m_SecondViewport.IsSVPActive() && (!require_lens || RImplementation.mapScopeHUDSorted.size()) &&
        (type == 2 || type == 3) && ps_markswitch_current < 2;
}

void CRenderTarget::ClearScopeHeat()
{
    scope_heat_shaders.clear();
    rt_scope_heat.destroy();
}

ShaderElement* CRenderTarget::GetScopeHeatShader(ShaderElement* source)
{
    SPass* original = source->passes[0]._get();
    if (!original->vs || !original->vs->vs)
        return nullptr;
    const auto cached = scope_heat_shaders.find(original);
    if (cached != scope_heat_shaders.end())
        return cached->second.second._get();

    // Retain the actual VS, including its skinning variant and per-material constants.
    CResourceManager& resources = *dxRenderDeviceRender::Instance().Resources;
    SPass pass = *original;
    pass.dwFlags = 0;
    pass.ps = resources._CreatePS("models_scope_heat_mask");
    R_constant_table constants;
    constants.merge(&original->vs->constants);
    constants.merge(&pass.ps->constants);
    for (auto& constant : constants.table)
    {
        R_constant* binding = original->constants->get(*constant->name);
        if (binding)
            constant->handler = binding->handler;
    }
    pass.constants = resources._CreateConstantTable(constants);

    SimulatorStates states = original->state->state_code;
    states.set_RS(D3DRS_ZENABLE, D3DZB_TRUE);
    states.set_RS(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
    states.set_RS(D3DRS_ZWRITEENABLE, FALSE);
    states.set_RS(D3DRS_ALPHABLENDENABLE, FALSE);
    states.set_RS(D3DRS_FOGENABLE, FALSE);
    states.set_RS(D3DRS_STENCILENABLE, FALSE);
    states.set_RS(D3DRS_COLORWRITEENABLE, 0xF);

    R_constant* base = original->constants->get("s_base");
    const u32 sampler = base && base->type == RC_sampler ? base->samp.index : 0;
    STextureList textures;
    if (original->T)
    {
        for (const auto& texture : *original->T)
            if (texture.first == sampler)
                textures.push_back(mk_pair(0u, texture.second));
    }
    if (textures.empty())
        return nullptr;
    pass.T = resources._CreateTextureList(textures);

    // The diffuse sampler is not necessarily s0 in the original material.
    RCache.set_Element(source);
    for (u32 state = D3DSAMP_ADDRESSU; state <= D3DSAMP_SRGBTEXTURE; ++state)
    {
        DWORD value;
        CHK_DX(HW.pDevice->GetSamplerState(sampler, D3DSAMPLERSTATETYPE(state), &value));
        states.set_SAMP(0, state, value);
    }
    pass.state = resources._CreateState(states);
    ShaderElement element;
    element.passes.push_back(resources._CreatePass(pass));
    ref_selement heat;
    heat = resources._CreateElement(element);
    scope_heat_shaders.emplace(original, mk_pair(ref_pass(original), heat));
    return heat._get();
}

void CRenderTarget::CaptureScopeHeat()
{
    VERIFY(IsScopeThermalActive() && RImplementation.o.defer_hud);
    IDirect3DSurface9* color = RCache.get_RT();
    IDirect3DSurface9* depth = RCache.get_ZB();
    D3DSURFACE_DESC desc;
    CHK_DX(color->GetDesc(&desc));
    if (!rt_scope_heat || rt_scope_heat->dwWidth != desc.Width || rt_scope_heat->dwHeight != desc.Height)
    {
        rt_scope_heat.destroy();
        rt_scope_heat.create("$user$3dss_heat", desc.Width, desc.Height, desc.Format);
        Msg("* R1 scope heat target: %ux%u", desc.Width, desc.Height);
    }
    RCache.set_RT(rt_scope_heat->pRT);
    RCache.set_ZB(depth);
    CHK_DX(HW.pDevice->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1.f, 0));
    RCache.set_xform_view(Device.mView);
    RCache.set_xform_project(Device.mProject);
    RImplementation.rmNormal();
    RImplementation.RenderScopeHeat();
    RCache.set_RT(color);
    RCache.set_ZB(depth);
}
