#pragma once
#include "customdevice.h"

class CLAItem;

class CFlashlight : public CCustomDevice
{
private:
	typedef CCustomDevice inherited;
	inline bool can_use_dynamic_lights();

protected:
	float fBrightness;
	CLAItem* lanim;
	LPCSTR def_lanim;
	Fcolor def_color;

	shared_str light_trace_bone;

	bool lightRenderState;
	bool isFlickering;
	int l_flickerChance;
	float l_flickerDelay;
	float lastFlicker;

	ref_light light_render;
	ref_light light_omni;
	ref_glow glow_render;
	shared_str m_light_section;

	void SwitchLightOnly();

public:
	CFlashlight();
	virtual ~CFlashlight();

	virtual CFlashlight* cast_flashlight() { return this; }

	virtual BOOL net_Spawn(CSE_Abstract* DC);
	virtual void Load(LPCSTR section);
	virtual void UpdateCL();
	virtual void OnStateSwitch(u32 S, u32 oldState);
	virtual void OnAnimationEnd(u32 state);
	
	void SetLanim(LPCSTR name, bool bFlicker, int flickerChance, float flickerDelay, float framerate);
	void ResetLanim();
	bool torch_active() const;

protected:
	virtual void TurnDeviceInternal(bool b);
};
