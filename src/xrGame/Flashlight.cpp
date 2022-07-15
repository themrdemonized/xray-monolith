#include "stdafx.h"
#include "Flashlight.h"
#include "inventory.h"
#include "player_hud.h"
#include "weapon.h"
#include "hudsound.h"
#include "ai_sounds.h"
#include "../xrEngine/LightAnimLibrary.h"
#include "../xrEngine/camerabase.h"
#include "../xrengine/xr_collide_form.h"
#include "Level.h"
#include "Actor.h"

CFlashlight::CFlashlight()
{
	light_render = ::Render->light_create();
	light_render->set_type(IRender_Light::SPOT);
	light_render->set_shadow(true);
	light_omni = ::Render->light_create();
	light_omni->set_type(IRender_Light::POINT);
	light_omni->set_shadow(false);

	glow_render = ::Render->glow_create();
	lanim = 0;
	fBrightness = 1.f;

	m_light_section = "torch_definition";

	def_lanim = "";
	light_trace_bone = "";
	lightRenderState = false;
	isFlickering = false;
	l_flickerChance = 0;
	l_flickerDelay = 0.0f;
	lastFlicker = 0.0f;
}

CFlashlight::~CFlashlight()
{
	light_render.destroy();
	light_omni.destroy();
	glow_render.destroy();
}

BOOL CFlashlight::net_Spawn(CSE_Abstract* DC)
{
	if (!inherited::net_Spawn(DC))
		return FALSE;

	bool b_r2 = !!psDeviceFlags.test(rsR2);
	b_r2 |= !!psDeviceFlags.test(rsR3);
	b_r2 |= !!psDeviceFlags.test(rsR4);

	def_lanim = pSettings->r_string(m_light_section, "color_animator");
	lanim = LALib.FindItem(def_lanim);

	Fcolor clr = pSettings->r_fcolor(m_light_section, (b_r2) ? "color_r2" : "color");
	def_color = clr;
	fBrightness = clr.intensity();
	float range = pSettings->r_float(m_light_section, (b_r2) ? "range_r2" : "range");
	light_render->set_color(clr);
	light_render->set_range(range);
	light_render->set_hud_mode(true);

	Fcolor clr_o = pSettings->r_fcolor(m_light_section, (b_r2) ? "omni_color_r2" : "omni_color");
	float range_o = pSettings->r_float(m_light_section, (b_r2) ? "omni_range_r2" : "omni_range");
	light_omni->set_color(clr_o);
	light_omni->set_range(range_o);
	light_omni->set_hud_mode(true);

	light_render->set_cone(deg2rad(pSettings->r_float(m_light_section, "spot_angle")));
	light_render->set_texture(READ_IF_EXISTS(pSettings, r_string, m_light_section, "spot_texture", (0)));

	glow_render->set_texture(pSettings->r_string(m_light_section, "glow_texture"));
	glow_render->set_color(clr);
	glow_render->set_radius(pSettings->r_float(m_light_section, "glow_radius"));

	light_render->set_volumetric(!!READ_IF_EXISTS(pSettings, r_bool, m_light_section, "volumetric", 0));
	light_render->
		set_volumetric_quality(READ_IF_EXISTS(pSettings, r_float, m_light_section, "volumetric_quality", 1.f));
	light_render->set_volumetric_intensity(READ_IF_EXISTS(pSettings, r_float, m_light_section, "volumetric_intensity",
	                                                      1.f));
	light_render->set_volumetric_distance(READ_IF_EXISTS(pSettings, r_float, m_light_section, "volumetric_distance",
	                                                     1.f));
	light_render->set_type((IRender_Light::LT)(READ_IF_EXISTS(pSettings, r_u8, m_light_section, "type", 2)));
	light_omni->set_type((IRender_Light::LT)(READ_IF_EXISTS(pSettings, r_u8, m_light_section, "omni_type", 1)));

	return TRUE;
}

void CFlashlight::Load(LPCSTR section)
{
	inherited::Load(section);

	light_trace_bone = READ_IF_EXISTS(pSettings, r_string, section, "light_trace_bone", "");
	m_light_section = READ_IF_EXISTS(pSettings, r_string, section, "light_section", "torch_definition");
}

//Switch light render on/off without touching torch state
void CFlashlight::SwitchLightOnly()
{
	lightRenderState = !lightRenderState;

	if (can_use_dynamic_lights())
	{
		light_render->set_active(lightRenderState);
		light_omni->set_active(lightRenderState);
	}

	glow_render->set_active(lightRenderState);
}

void CFlashlight::UpdateCL()
{
	inherited::UpdateCL();

	CActor* actor = smart_cast<CActor*>(H_Parent());
	if (!actor)
		return;

	if (!IsWorking())
		return;

	if (!HudItemData())
	{
		TurnDeviceInternal(false);
		return;
	}

	firedeps dep;
	HudItemData()->setup_firedeps(dep);

	light_render->set_position(dep.vLastFP);
	light_omni->set_position(dep.vLastFP);
	glow_render->set_position(dep.vLastFP);

	Fvector dir = dep.m_FireParticlesXForm.k;

	if (Actor()->cam_freelook != eflDisabled)
	{
		dir.setHP(-angle_normalize_signed(Actor()->old_torso_yaw), dir.getP() > 0.f ? dir.getP() * .6f : dir.getP() * .8f);
		dir.lerp(dep.m_FireParticlesXForm.k, dir, Actor()->freelook_cam_control);
	}

	light_render->set_rotation(dir, dep.m_FireParticlesXForm.i);
	light_omni->set_rotation(dir, dep.m_FireParticlesXForm.i);
	glow_render->set_direction(dir);

	// Update flickering
	if (isFlickering)
	{
		float tg = Device.fTimeGlobal;
		if (lastFlicker == 0) lastFlicker = tg;
		if (tg - lastFlicker >= l_flickerDelay)
		{
			int rando = rand() % 100 + 1;
			if (rando >= l_flickerChance)
			{
				SwitchLightOnly();
			}
			lastFlicker = tg;
		}
	}

	// calc color animator
	if (!lanim)
		return;

	int frame;

	u32 clr = lanim->CalculateBGR(Device.fTimeGlobal, frame);

	Fcolor fclr;
	fclr.set((float)color_get_B(clr), (float)color_get_G(clr), (float)color_get_R(clr), 1.f);
	fclr.mul_rgb(fBrightness / 255.f);
	if (can_use_dynamic_lights())
	{
		light_render->set_color(fclr);
		light_omni->set_color(fclr);
	}
	glow_render->set_color(fclr);
}

void CFlashlight::SetLanim(LPCSTR name, bool bFlicker, int flickerChance, float flickerDelay, float framerate)
{
	lanim = LALib.FindItem(name);
	if (lanim && framerate)
		lanim->SetFramerate(framerate);
	isFlickering = bFlicker;
	if (isFlickering)
	{
		l_flickerChance = flickerChance;
		l_flickerDelay = flickerDelay;
	}
}

void CFlashlight::ResetLanim()
{
	if (lanim)
	{
		lanim->ResetFramerate();
		if (lanim->cName != def_lanim)
			lanim = LALib.FindItem(def_lanim);
	}
	
	if (can_use_dynamic_lights())
	{
		light_render->set_color(def_color);
		light_omni->set_color(def_color);
	}
	glow_render->set_color(def_color);

	isFlickering = false;
}

inline bool CFlashlight::can_use_dynamic_lights()
{
	if (!H_Parent())
		return (true);

	CInventoryOwner* owner = smart_cast<CInventoryOwner*>(H_Parent());
	if (!owner)
		return (true);

	return (owner->can_use_dynamic_lights());
}

void CFlashlight::TurnDeviceInternal(bool b)
{
	inherited::TurnDeviceInternal(b);

	if (can_use_dynamic_lights())
	{
		light_render->set_active(b);
		light_omni->set_active(b);
	}
	glow_render->set_active(b);
}

bool CFlashlight::torch_active() const
{
	return m_bWorking;
}


void CFlashlight::OnStateSwitch(u32 S, u32 oldState)
{
	if (S == eShowing)
	{
		CHudItemObject::OnStateSwitch(S, oldState);

		g_player_hud->attach_item(this);

		m_sounds.PlaySound("sndShow", Fvector().set(0, 0, 0), this, true, false);

		bool need_zoom = false;
		attachable_hud_item* i0 = g_player_hud->attached_item(0);
		if (m_bCanBeZoomed && i0)
		{
			CWeapon* wpn = smart_cast<CWeapon*>(i0->m_parent_hud_item);
			if (wpn && wpn->IsZoomed())
				need_zoom = true;
		}

		if (HudAnimationExist("anm_zoom_show") && need_zoom)
		{
			PlayHUDMotion("anm_zoom_show", FALSE, this, GetState(), 1.f, 0.f, false);
			m_bZoomed = true;
		}
		else
			PlayHUDMotion(m_bFastAnimMode ? "anm_show_fast" : "anm_show", FALSE, this, GetState(), 1.f, 0.f, false);
		SetPending(TRUE);
	}
	else
		inherited::OnStateSwitch(S, oldState);
}

void CFlashlight::OnAnimationEnd(u32 state)
{
	if (state == eShowing)
	{
		if (!IsUsingCondition() || (IsUsingCondition() && GetCondition() >= m_fLowestBatteryCharge))
			TurnDeviceInternal(true);

		if (m_bCanBeZoomed)
		{
			attachable_hud_item* i0 = g_player_hud->attached_item(0);
			if (i0)
			{
				CWeapon* wpn = smart_cast<CWeapon*>(i0->m_parent_hud_item);
				if (wpn && wpn->IsZoomed())
				{
					SwitchState(eIdleZoom);
					return;
				}
			}
		}
		SwitchState(eIdle);
	}

	inherited::OnAnimationEnd(state);
}