// HUDCrosshair.h:  крестик прицела, отображающий текущую дисперсию
// 
//////////////////////////////////////////////////////////////////////

#pragma once

#define HUD_CURSOR_SECTION "hud_cursor"

#include "ui_defs.h"


class CHUDCrosshair
{
private:
	Fmatrix transform;
	float minRadius;
	float maxRadius;
	u32 crossColor;

	ui_shader shaderWire;
	ui_shader shaderCrosshair;

	string32 lastCrosshairShader;
	string32 lastCrosshairTexture;

	float dispersionRadius;

	void DeinitShaderCrosshair();
	bool InitShaderCrosshair();
	void RenderShaderCrosshair();
	void RenderWireCrosshair();

public:
	CHUDCrosshair();
	~CHUDCrosshair();

	void Load();
	void SetTransform(const Fmatrix& m);
	void SetColor(u32 c);
	void SetDispersion(float d);
	void OnRender();
};
