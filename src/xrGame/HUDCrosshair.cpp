// HUDCrosshair.cpp:  крестик прицела, отображающий текущую дисперсию
// 
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "HUDCrosshair.h"
#include "HUDTarget.h"

#include "../xrEngine/CustomHUD.h"
#include "../xrEngine/igame_persistent.h"
#include "ui_base.h"

string32 crosshair_shader = "hud\\cursor";
string32 crosshair_texture = "ui\\cursor";
float crosshair_near_size = 1.f;
float crosshair_far_size = 1.f;
float crosshair_depth_begin = 0.f;
float crosshair_depth_end = 100.f;

u32 C_CROSS D3DCOLOR_RGBA(0xff, 0xff, 0xff, 0xff);

CHUDCrosshair::CHUDCrosshair()
{
	crosshairShader = NULL;
	crosshairTexture = NULL;
	strcpy(lastCrosshairShader, "");
	strcpy(lastCrosshairTexture, "");
	transform = Fmatrix().identity();
	minRadius = 0.001f;
	maxRadius = 0.004f;
	crossColor = C_CROSS;
	dispersionRadius = 0.f;
}

CHUDCrosshair::~CHUDCrosshair()
{
}

void CHUDCrosshair::Load()
{
	minRadius = pSettings->r_float(HUD_CURSOR_SECTION, "min_radius");
	maxRadius = pSettings->r_float(HUD_CURSOR_SECTION, "max_radius");
	crossColor = pSettings->r_fcolor(HUD_CURSOR_SECTION, "cross_color").get();
}

void CHUDCrosshair::SetTransform(const Fmatrix& m)
{
	transform.set(m);
}

void CHUDCrosshair::SetScale(float s)
{
	scale = s;
}

void CHUDCrosshair::SetColor(u32 c)
{
	crossColor = c;
}

void CHUDCrosshair::SetDispersion(float d)
{
	dispersionRadius = d;
}

extern ENGINE_API BOOL g_bRendering;

static float lerp(float a, float b, float t)
{
	clamp(t, 0.f, 1.f);
	return a * (1 - t) + b * t;
}

void CHUDCrosshair::InitShaderWire()
{
	if (!shaderWire->inited())
		shaderWire->create("hud\\crosshair");
}

void CHUDCrosshair::DeinitShaderCrosshair()
{
    //if (shaderCrosshair->inited())
    //{
    //	shaderCrosshair->destroy();
    //	strcpy(lastCrosshairShader, "");
    //	strcpy(lastCrosshairTexture, "");
    //}
    strcpy(lastCrosshairShader, "");
    strcpy(lastCrosshairTexture, "");
}

bool CHUDCrosshair::InitShaderCrosshair()
{
	if (!crosshairShader || !crosshairTexture)
		return false;

	if (strcmp(lastCrosshairShader, *crosshairShader) || strcmp(lastCrosshairTexture, *crosshairTexture))
	{
		DeinitShaderCrosshair();

		shaderCrosshair->create(*crosshairShader, *crosshairTexture);
		strcpy(lastCrosshairShader, *crosshairShader);
		strcpy(lastCrosshairTexture, *crosshairTexture);
	}

	return shaderCrosshair->inited();
}

void CHUDCrosshair::PushVerts(Fvector* verts, Fvector* uvs, int count, Fmatrix mat, Fvector4 pos) const
{
	Fvector2 scr_size = {
		float(::Render->getTarget()->get_width()),
		float(::Render->getTarget()->get_height())
	};

	for (int i = 0; i < count; i++)
	{
		Fvector vert = verts[i];
		Fvector uv = Fvector();
		if (uvs)
			uv = uvs[i];

		vert.mul(scale);
		mat.transform(vert);
		vert.x *= scr_size.x / scr_size.y;
		vert.y *= -1;
		vert.x += pos.x;
		vert.y += pos.y;
		UIRender->PushPoint(vert.x, vert.y, 0, crossColor, uv.x, uv.y);
	}
}

void CHUDCrosshair::RenderShaderCrosshair()
{
	// Fetch the render target size
	Fvector2 scr_size = {
		float(::Render->getTarget()->get_width()),
		float(::Render->getTarget()->get_height())
	};

	float max = maxRadius;

	Fvector verts[4] = {
		{-max, -max},
		{-max, max},
		{max, -max},
		{max, max},
	};

	Fvector uvs[4] = {
		{0, 1},
		{0, 0},
		{1, 1},
		{1, 0},
	};

	Fmatrix mat = Fmatrix().mul(Device.mFullTransform, transform);
	Fvector4 pos = Fvector4().set(mat._41, mat._42, mat._43, mat._44);

	// Apply perspective divide to aim point and transform into screen space
	pos.x = ((pos.x / pos.w) + 1.f) * 0.5f * scr_size.x;
	pos.y = (-(pos.y / pos.w) + 1.f) * 0.5f * scr_size.y;

	UIRender->StartPrimitive(4, IUIRender::ptTriStrip, UI().m_currentPointType);
	PushVerts(verts, uvs, 4, mat, pos);

	// Draw
	UIRender->SetShader(*shaderCrosshair);
	UIRender->FlushPrimitive();
}

void CHUDCrosshair::RenderWireCrosshair()
{
	// Fetch the render target size
	Fvector2 scr_size = {
		float(::Render->getTarget()->get_width()),
		float(::Render->getTarget()->get_height())
	};

	float min = minRadius;
	float max = maxRadius;

	// Create vertices from our size metrics
	Fvector verts[8] = {
		{ min, 0 },
		{ max, 0 },
		{ -min, 0 },
		{ -max, 0 },
		{ 0, min },
		{ 0, max },
		{ 0, -min },
		{ 0, -max },
	};

	Fmatrix mat = Fmatrix().mul(Device.mFullTransform, transform);
	Fvector4 pos = Fvector4().set(mat._41, mat._42, mat._43, mat._44);

	// Apply perspective divide to aim point and transform into screen space
	pos.x = ((pos.x / pos.w) + 1.f) * 0.5f * scr_size.x;
	pos.y = (-(pos.y / pos.w) + 1.f) * 0.5f * scr_size.y;

	// Project vertices for accurate scaling
	UIRender->StartPrimitive(8, IUIRender::ptLineList, UI().m_currentPointType);
	PushVerts(verts, NULL, 8, mat, pos);

	// Render a 1px wide line  for the center dot
	UIRender->PushPoint(pos.x - 0.5f, pos.y, 0, crossColor, 0, 0);
	UIRender->PushPoint(pos.x + 0.5f, pos.y, 0, crossColor, 0, 0);

	UIRender->SetShader(*shaderWire);
	UIRender->FlushPrimitive();
}

void CHUDCrosshair::OnRender(bool use_shader)
{
	VERIFY(g_bRendering);

	if (use_shader)
	{
		if (InitShaderCrosshair())
			RenderShaderCrosshair();
	}
	else
	{
		DeinitShaderCrosshair();
		InitShaderWire();
		RenderWireCrosshair();
	}
}
