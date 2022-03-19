#pragma once
#include "stdafx.h"
#include "../xrEngine/LightAnimLibrary.h"

class CLAItem;

class ScriptGlow
{
private:
	ref_glow m_glow;
	CLAItem* m_light_anim;

	float fRange;
	float fBrightness;
	LPCSTR texture;
	Fcolor color;
public:
	ScriptGlow()
	{
		m_glow = ::Render->glow_create();

		SetRange(10.f);
		SetColor(Fcolor().set(1, 1, 1, 1));
		Enable(true);

		m_light_anim = nullptr;
		texture = nullptr;
	}

	virtual ~ScriptGlow()
	{
		m_glow.destroy();
	}
	IC void Enable(bool state)				{ m_glow->set_active(state); }
	IC const bool IsEnabled() const			{ return m_glow->get_active(); }
	IC const float GetRange() const			{ return fRange; }
	IC void SetPosition(Fvector pos)		{ m_glow->set_position(pos); }
	IC void SetDirection(Fvector dir)		{ m_glow->set_direction(dir); }
	IC const Fcolor GetColor() const		{ return color; }
	IC const LPCSTR GetTexture() const		{ return texture; }
	IC void SetBrightness(float val)		{ fBrightness = val; }
	IC const float GetBrightness() const	{ return fBrightness; }
	IC void SetLanim(LPCSTR name) { m_light_anim = LALib.FindItem(name); }
	IC LPCSTR GetLanim() const { return m_light_anim != nullptr ? *m_light_anim->cName : nullptr; }

	IC void SetRange(float range)
	{
		m_glow->set_radius(range);
		fRange = range;
	}

	IC void SetTexture(LPCSTR name)
	{
		m_glow->set_texture(name);
		texture = name;
	}

	IC void SetColor(Fcolor col)
	{
		m_light_anim = nullptr;
		m_glow->set_color(col);
		color = col;
	}

	IC void Update()
	{
		if (m_light_anim)
		{
			int frame;

			u32 clr = m_light_anim->CalculateBGR(Device.fTimeGlobal, frame);

			Fcolor fclr;
			fclr.set((float)color_get_B(clr), (float)color_get_G(clr), (float)color_get_R(clr), 1.f);
			fclr.mul_rgb(fBrightness);
			m_glow->set_color(fclr);
		}
	}
 };

class ScriptLight
{
private:
	ref_light m_light;
	CLAItem* m_light_anim;

	bool bShadow;
	bool bHudMode;
	float fBrightness;
	float fRange;
	Fcolor color;
	LPCSTR texture;
	int iType;

	// Volumetric light
	bool bVolEnable;
	float fVolDistance;
	float fVolQuality;
	float fVolIntensity;
public:
	ScriptLight() 
	{
		m_light = ::Render->light_create();

		SetType(IRender_Light::POINT);
		SetShadow(false);
		SetRange(10.f);
		SetColor(Fcolor().set(1, 1, 1, 1));
		SetHudMode(false);
		Enable(true);

		SetVolumetric(false);
		SetVolumetricDistance(.5f);
		SetVolumetricIntensity(.5f);
		SetVolumetricQuality(.5f);

		m_light_anim = nullptr;
		fBrightness = 0.f;
		texture = nullptr;
	}

	virtual ~ScriptLight()
	{
		m_light.destroy();
	}

	IC void SetLanim(LPCSTR name)						{ m_light_anim = LALib.FindItem(name); }
	IC LPCSTR GetLanim() const							{ return m_light_anim != nullptr ? *m_light_anim->cName : nullptr; }
	IC void SetPosition(Fvector pos)					{ m_light->set_position(pos); }
	IC void SetDirection(Fvector dir)					{ m_light->set_rotation(dir, Fvector().set(1, 0, 0)); }
	IC void SetDirection(Fvector dir, Fvector right)	{ m_light->set_rotation(dir, right); }
	IC void SetCone(float angle)						{ m_light->set_cone(angle); }
	IC void Enable(bool state)							{ m_light->set_active(state); }
	IC const bool IsEnabled() const						{ return m_light->get_active(); }
	IC void SetHudMode(bool b)							{ m_light->set_hud_mode(b); }
	IC const bool GetHudMode() const					{ return m_light->get_hud_mode(); }
	
	IC void SetBrightness(float br)						{ fBrightness = br; }
	IC const float GetBrightness() const				{ return fBrightness; }
	IC const bool GetVolumetric() const					{ return bVolEnable; }
	IC const float GetVolumetricDistance() const		{ return fVolDistance; }
	IC const float GetVolumetricIntensity() const		{ return fVolIntensity; }
	IC const float GetVolumetricQuality() const			{ return fVolQuality; }
	IC const bool GetShadow() const						{ return bShadow; }
	IC const float GetRange() const						{ return fRange; }
	IC const Fcolor GetColor() const					{ return color; }
	IC const LPCSTR GetTexture() const					{ return texture; }
	IC const int GetType() const						{ return iType; }

	IC void SetShadow(bool state) 
	{ 
		m_light->set_shadow(state); 
		bShadow = state;
	}

	IC void SetRange(float range) 
	{
		m_light->set_range(range); 
		fRange = range;
	}

	IC void SetType(int type) 
	{
		m_light->set_type((IRender_Light::LT) type); 
		iType = type;
	}

	IC void SetTexture(LPCSTR name) 
	{
		m_light->set_texture(name); 
		texture = name;
	}

	IC void SetVolumetric(bool state) 
	{ 
		m_light->set_volumetric(state);
		bVolEnable = state;

		if (state) SetShadow(true);
	}

	IC void SetVolumetricDistance(float val) 
	{
		m_light->set_volumetric_distance(val);
		fVolDistance = val; 
	}

	IC void SetVolumetricIntensity(float val)
	{ 
		m_light->set_volumetric_intensity(val); 
		fVolIntensity = val; 
	}

	IC void SetVolumetricQuality(float val) 
	{ 
		m_light->set_volumetric_quality(val);
		fVolQuality = val;
	}

	IC void SetColor(Fcolor col)
	{
		m_light_anim = nullptr;
		m_light->set_color(col);
		color = col;
	}

	IC void Update()
	{
		if (m_light_anim)
		{
			int frame;

			u32 clr = m_light_anim->CalculateBGR(Device.fTimeGlobal, frame);

			Fcolor fclr;
			fclr.set((float)color_get_B(clr), (float)color_get_G(clr), (float)color_get_R(clr), 1.f);
			fclr.mul_rgb(fBrightness);
			m_light->set_color(fclr);
		}
	}
};
