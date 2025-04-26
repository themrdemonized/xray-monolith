// HUDCrosshair.cpp:  крестик прицела, отображающий текущую дисперсию
// 
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"

#include "HUDCrosshair.h"
#include "../xrEngine/CustomHUD.h"
#include "../xrEngine/igame_persistent.h"
#include "ui_base.h"

string32 crosshair_shader = "hud\\cursor";
string32 crosshair_texture = "ui\\cursor";
float crosshair_near_size = 1.f;

CHUDCrosshair::CHUDCrosshair()
{
	strcpy(lastCrosshairShader, "");
	strcpy(lastCrosshairTexture, "");
	shaderWire->create("hud\\crosshair");
	transform = Fmatrix().identity();
	minRadius = 0.001f;
	maxRadius = 0.004f;
	crossColor = 0;
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
	return a * (1 - t) + b * t;
}

void CHUDCrosshair::DeinitShaderCrosshair()
{
	if (shaderCrosshair->inited())
	{
		shaderCrosshair->destroy();
		strcpy(lastCrosshairShader, "");
		strcpy(lastCrosshairTexture, "");
	}
}

bool CHUDCrosshair::InitShaderCrosshair()
{
	if (strcmp(lastCrosshairShader, crosshair_shader) || strcmp(lastCrosshairTexture, crosshair_texture))
	{
		DeinitShaderCrosshair();

		shaderCrosshair->create(crosshair_shader, crosshair_texture);
		strcpy(lastCrosshairShader, crosshair_shader);
		strcpy(lastCrosshairTexture, crosshair_texture);
	}

	return shaderCrosshair->inited();
}

void CHUDCrosshair::RenderShaderCrosshair()
{
	UIRender->StartPrimitive(4, IUIRender::ptTriStrip, UI().m_currentPointType);

	Fvector4 pt;
	Device.mFullTransform.transform(pt, transform.c);
	pt.y = -pt.y;

	Fvector2 scr_size = { float(Device.dwWidth), float(Device.dwHeight) };

	float min = minRadius;
	float max = maxRadius;

	Fvector verts[4] = {
		{-max, -max},
		{-max, max},
		{max, -max},
		{max, max},
	};

	Fvector2 uvs[4] = {
		{0, 1},
		{0, 0},
		{1, 1},
		{1, 0},
	};

	// Bring our transform into view space
	Fvector pos = Fvector();
	transform.transform_tiny(pos);
	Device.mView.transform_tiny(pos);

	// Project without W-divide to retrieve linear depth
	Fvector pos_ = Fvector().set(pos);
	Device.mProject.transform_tiny(pos_);

	// Calculate size from linear depth
	float zNear = Device.ViewportNear;
	float zFar = g_pGamePersistent->Environment().CurrentEnv->far_plane;
	float size = lerp(zNear + (crosshair_near_size - 1), zFar, (pos_.z - zNear) / (zFar - zNear)) + (zFar * dispersionRadius);

	// Transform and push vertices
	for (int i = 0; i < 4; i++)
	{
		Fvector vert = verts[i];
		vert.mul(size);
		transform.transform_tiny(vert);
		Device.mView.transform_tiny(vert);
		Device.mProject.transform(vert);
		vert.x = (vert.x + 1.f) * 0.5f * scr_size.x;
		vert.y = (-vert.y + 1.f) * 0.5f * scr_size.y;
		Fvector2 uv = uvs[i];
		UIRender->PushPoint(vert.x, vert.y, 0, crossColor, uv.x, uv.y);
	}

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

	// Transform into view space
	Fvector pos = Fvector();
	transform.transform_tiny(pos);
	Device.mView.transform_tiny(pos);

	// Project without W-divide to retrieve linear depth
	Fvector pos_ = Fvector().set(pos);
	Device.mProject.transform_tiny(pos_);

	// Calculate size from linear depth
	float zNear = Device.ViewportNear;
	float zFar = g_pGamePersistent->Environment().CurrentEnv->far_plane;
	float size = lerp(zNear + (crosshair_near_size - 1), zFar, (pos_.z - zNear) / (zFar - zNear)) + (zFar * dispersionRadius);

	// Project into NDC with W-divide
	Device.mProject.transform(pos);

	// Project vertices for accurate scaling
	UIRender->StartPrimitive(8, IUIRender::ptLineList, UI().m_currentPointType);
	for (int i = 0; i < 8; i++)
	{
		Fvector vert = verts[i];
		vert.mul(size);
		transform.transform_tiny(vert);
		Device.mView.transform_tiny(vert);
		Device.mProject.transform(vert);
		vert.x = (vert.x + 1.f) * 0.5f * scr_size.x;
		vert.y = (-vert.y + 1.f) * 0.5f * scr_size.y;
		UIRender->PushPoint(vert.x, vert.y, 0, crossColor, 0, 0);
	}

	// Apply perspective divide to aim point and transform into screen space
	pos.x = (pos.x + 1.f) * 0.5f * scr_size.x;
	pos.y = (-pos.y + 1.f) * 0.5f * scr_size.y;

	// Render a 1px wide line  for the center dot
	UIRender->PushPoint(pos.x - 0.5f, pos.y, 0, crossColor, 0, 0);
	UIRender->PushPoint(pos.x + 0.5f, pos.y, 0, crossColor, 0, 0);

	UIRender->SetShader(*shaderWire);
	UIRender->FlushPrimitive();
}

void CHUDCrosshair::OnRender()
{
	VERIFY(g_bRendering);

	if (psHUD_Flags.is(HUD_SHADER_CROSSHAIR))
	{
		if (InitShaderCrosshair())
			RenderShaderCrosshair();
	}
	else
	{
		DeinitShaderCrosshair();
		RenderWireCrosshair();
	}
}