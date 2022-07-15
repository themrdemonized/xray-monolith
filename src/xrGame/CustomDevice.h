#pragma once
#include "hud_item_object.h"
#include "ui/ArtefactDetectorUI.h"

class CCustomDevice : public CHudItemObject
{
	typedef CHudItemObject inherited;
protected:
	bool m_bFastAnimMode;
	bool m_bNeedActivation;
	bool m_bCanBeZoomed;
	bool m_bThrowAnm;
	bool m_bWorking;
	bool m_bOldZoom;

public:
	CCustomDevice();
	virtual ~CCustomDevice();

	virtual BOOL net_Spawn(CSE_Abstract* DC);
	virtual void Load(LPCSTR section);

	virtual void OnH_B_Independent(bool just_before_destroy);

	virtual void shedule_Update(u32 dt);
	virtual void UpdateCL();

	enum EDeviceStates
	{
		eIdleZoom = 5,
		eIdleZoomIn,
		eIdleZoomOut,
		eIdleThrowStart,
		eIdleThrow,
		eIdleThrowEnd
	};

	virtual bool IsWorking();

	virtual void OnMoveToRuck(const SInvItemPlace& prev);
	virtual void on_a_hud_attach();

	virtual void OnActiveItem();
	virtual void OnHiddenItem();
	virtual void OnStateSwitch(u32 S, u32 oldState);
	virtual void OnAnimationEnd(u32 state);
	virtual void UpdateXForm();
	virtual void UpdateHudAdditional(Fmatrix& trans);

	virtual void ToggleDevice(bool bFastMode);
	virtual void HideDevice(bool bFastMode);
	virtual void ShowDevice(bool bFastMode);
	virtual void ForceHide();
	virtual bool CheckCompatibility(CHudItem* itm);

	virtual void render_item_3d_ui();
	virtual bool render_item_3d_ui_query();

	bool m_bZoomed;
	float m_fZoomfactor;

protected:
	virtual bool CheckCompatibilityInt(CHudItem* itm, u16* slot_to_activate);
	virtual void TurnDeviceInternal(bool b);
	virtual void UpdateVisibility();
	virtual void UpdateWork();
	CUICustomDeviceBase* m_ui;

	virtual void CreateUI(){};
	virtual void ResetUI(){};
};