#pragma once

#include "HUDCrosshair.h"
#include "../xrcdb/xr_collide_defs.h"


class CHUDManager;
class CLAItem;

class CHUDTarget
{
private:
	float fuzzyShowInfo;

	Fvector crosshairPos;
	float crosshairOpacity;
	float occludedOpacity;
	float occlusionFadeRate;
	float distanceLerpRate;

	bool m_bShowCrosshair;
	CHUDCrosshair HUDCrosshair;

private:
	float GetUIDist() const;
	float GetTargetOpacity() const;
	void IntegratePosition();
	void IntegrateOpacity();

public:
	CHUDTarget();
	~CHUDTarget();
	void Render();
	void Load();
	CHUDCrosshair& GetHUDCrosshair() { return HUDCrosshair; }
	void ShowCrosshair(bool b);
};
