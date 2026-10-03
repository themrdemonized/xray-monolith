// dxRender_Visual.cpp: implementation of the dxRender_Visual class.
//
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#pragma hdrstop

#ifndef _EDITOR
#	include "../../xrEngine/render.h"
#endif // #ifndef _EDITOR

#include "fbasicvisual.h"
#include "../../xrEngine/fmesh.h"
#include "dxRenderDeviceRender.h"

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

IRender_Mesh::~IRender_Mesh()
{
	_RELEASE(p_rm_Vertices);
	_RELEASE(p_rm_Indices);
}

dxRender_Visual::dxRender_Visual()
{
	Type = 0;
	shader = 0;
	vis.clear();
    dbg_id = 1;
    skinning = 1;
    shader_params = nullptr;
}

dxRender_Visual::~dxRender_Visual()
{
    xr_delete(shader_params);
}

void dxRender_Visual::Release()
{
}

//CStatTimer						tscreate;

void dxRender_Visual::Load(const char* N, IReader* data, u32)
{
	dbg_name = N;
	dbg_id = 1;
	skinning = ::Render->m_skinning;
    hud = ::Render->hud_loading;

	// header
	VERIFY(data);
	ogf_header hdr;
	if (data->r_chunk_safe(OGF_HEADER, &hdr, sizeof(hdr)))
	{
		R_ASSERT2(hdr.format_version==xrOGF_FormatVersion, "Invalid visual version");
		Type = hdr.type;
		//if (hdr.shader_id)	shader	= ::Render->getShader	(hdr.shader_id);
		if (hdr.shader_id) shader = ::RImplementation.getShader(hdr.shader_id);
		vis.box.set(hdr.bb.min, hdr.bb.max);
		vis.sphere.set(hdr.bs.c, hdr.bs.r);
	}
	else
	{
		FATAL("Invalid visual");
	}

	// Shader
	if (data->find_chunk(OGF_TEXTURE))
	{
		string256 fnT, fnS;
		data->r_stringZ(fnT, sizeof(fnT));
		data->r_stringZ(fnS, sizeof(fnS));
		dbg_shader_def = fnS;
		dbg_texture_def = fnT;
		ResetShaderTexture();
	}

	// desc
#ifdef _EDITOR
    if (data->find_chunk(OGF_S_DESC)) 
	    desc.Load		(*data);
#endif
}

void dxRender_Visual::SetShaderTexture(LPCSTR s_shader, LPCSTR s_texture)
{
	if (s_shader && strlen(s_shader))
	{
		char* shader = xr_strdup(s_shader);
		char* no_shadow = strstr(shader, "$no_shadows");

		if (no_shadow)
		{
			flags.set(IRenderVisualFlags::eNoShadow, TRUE);
			*no_shadow = 0;
		}
		else
			flags.set(IRenderVisualFlags::eNoShadow, FALSE);

		dbg_shader = shader;
		xr_delete(shader);
	}

	if (s_texture && strlen(s_texture))
	{
		dbg_texture = s_texture;
	}

	::Render->m_skinning = skinning;
    bool prev_hud = ::Render->hud_loading;
    ::Render->hud_loading = hud;
    shader.create(*dbg_shader, *dbg_texture);
    ::Render->hud_loading = prev_hud;
}

void dxRender_Visual::ResetShaderTexture()
{
	if (!dbg_shader.equal(dbg_shader_def) || !dbg_texture.equal(dbg_texture_def))
		SetShaderTexture(*dbg_shader_def, *dbg_texture_def);
}

void dxRender_Visual::SetShaderParam(int param, float x, float y, float z, float w)
{
    if (param < 1 || param > 8) return;

    if (!shader_params)
    {
        shader_params = xr_alloc<Fvector4>(8);
        for (int i = 0; i < 8; ++i)
            shader_params[i].set(0, 0, 0, 0);
    }

    shader_params[param - 1].set(x, y, z, w);
}

void dxRender_Visual::SetShaderParams(Fvector4* params)
{
    if (!shader_params) shader_params = xr_alloc<Fvector4>(8);
    CopyMemory(shader_params, params, sizeof(Fvector4) * 8);
}

void dxRender_Visual::ResetShaderParams()
{
    if (shader_params) xr_delete(shader_params);
}

#define PCOPY(a)	a = pFrom->a

void dxRender_Visual::Copy(dxRender_Visual* pFrom)
{
	PCOPY(Type);
	PCOPY(shader);
	PCOPY(vis);
#ifdef _EDITOR
	PCOPY(desc);
#endif
	PCOPY(flags);
	PCOPY(dbg_id);
	PCOPY(dbg_name);
	PCOPY(dbg_shader);
	PCOPY(dbg_shader_def);
	PCOPY(dbg_texture);
	PCOPY(dbg_texture_def);
	PCOPY(skinning);
    PCOPY(hud);
}
