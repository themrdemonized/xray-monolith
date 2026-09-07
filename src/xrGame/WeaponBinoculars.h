#pragma once

#include "WeaponCustomPistol.h"
#include "script_export_space.h"

class CUIFrameWindow;
class CUIStatic;
class CBinocularsVision;

class CWeaponBinoculars : public CWeaponCustomPistol
{
private:
	typedef CWeaponCustomPistol inherited;
protected:
	bool m_bVision;
public:
	CWeaponBinoculars();
	virtual ~CWeaponBinoculars();

	void Load(LPCSTR section);

	virtual bool NeedBlendAnm();
	virtual bool MovingAnimAllowedNow();

	virtual Fmatrix RayTransform();

	virtual void OnZoomIn();
	virtual void OnZoomOut();
	virtual void ZoomInc();
	virtual void ZoomDec();
	// pip binocs are flat-window optics with their own zoom chain, keep the legacy fov derivation
	virtual bool SvpDetentBase() const override { return false; }
	// pip binocs keep the legacy zoom derivation, authored mags never apply
	virtual bool SvpMagsEligible() const override { return false; }
	virtual void net_Destroy();
	virtual BOOL net_Spawn(CSE_Abstract* DC);
	bool can_kill() const;
	virtual void save(NET_Packet& output_packet);
	virtual void load(IReader& input_packet);

	virtual bool Action(u16 cmd, u32 flags);
	virtual void UpdateCL();
	virtual void render_item_ui();
	virtual bool render_item_ui_query();
	virtual bool use_crosshair() const { return false; }
	virtual CWeaponBinoculars* cast_weapon_binoculars() { return this; }
	virtual bool GetBriefInfo(II_BriefInfo& info);
	virtual void net_Relcase(CObject* object);
protected:
	CBinocularsVision* m_binoc_vision;

DECLARE_SCRIPT_REGISTER_FUNCTION
};
