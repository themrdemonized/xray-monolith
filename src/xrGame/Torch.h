#pragma once

#include "inventory_item_object.h"
#include "hudsound.h"
#include "script_export_space.h"

class CLAItem;
class CNightVisionEffector;

class CTorch : public CInventoryItemObject
{
private:
	typedef CInventoryItemObject inherited;

protected:
	float fBrightness;
	CLAItem* lanim;
	LPCSTR def_lanim;
	Fcolor def_clr;

	u16 guid_bone;
	shared_str light_trace_bone;

	float m_delta_h;
	Fvector2 m_prev_hp;
	bool m_switched_on;
	ref_light light_render;
	ref_light light_omni;
	ref_glow glow_render;
	shared_str m_light_section;
	Fvector m_torch_offset;
	Fvector m_omni_offset;
	float m_torch_inertion_speed_max;
	float m_torch_inertion_speed_min;
	float m_torch_inertion_clamp;
	bool m_bUseInertion;
private:
	inline bool can_use_dynamic_lights();
	bool isFlickering;
	bool lightRenderState;
	float lastFlicker;
	int l_flickerChance;
	float l_flickerDelay;
public:
	CTorch();
	virtual ~CTorch();

	virtual void Load(LPCSTR section);
	virtual BOOL net_Spawn(CSE_Abstract* DC);
	
	virtual void net_Destroy();
	virtual void net_Export(NET_Packet& P); // export to server
	virtual void net_Import(NET_Packet& P); // import from server

	virtual void OnH_A_Chield();
	virtual void OnH_B_Independent(bool just_before_destroy);

	virtual void OnMoveToSlot(const SInvItemPlace& prev);
	virtual void OnMoveToRuck(const SInvItemPlace& prev);
	virtual void UpdateCL();

	void Switch();
	void Switch(bool light_on);
	void SwitchLightOnly();
	void SetLanim(LPCSTR name, bool bFlicker, int flickerChance, float flickerDelay, float framerate);
	void ResetLanim();
	void LoadLightParams();
	bool torch_active() const;

	virtual bool can_be_attached() const;

	//CAttachableItem
	virtual void enable(bool value);
protected:
	HUD_SOUND_COLLECTION_LAYERED m_sounds;

	enum EStats
	{
		eTorchActive = (1 << 0),
		eNightVisionActive = (1 << 1),
		eAttached = (1 << 2)
	};

public:

	virtual bool use_parent_ai_locations() const
	{
		return (!H_Parent());
	}

	virtual void create_physic_shell();
	virtual void activate_physic_shell();
	virtual void setup_physic_shell();

	virtual void afterAttach();
	virtual void afterDetach();
	virtual void renderable_Render();

DECLARE_SCRIPT_REGISTER_FUNCTION
};

add_to_type_list(CTorch)
#undef script_type_list
#define script_type_list save_type_list(CTorch)
