#pragma once
#include "UIStatic.h"
#include "../../xrServerEntities/script_export_space.h"

class CObject;
class CUIBinocularsVision;


enum
{
	flVisObjNotValid =(1 << 0),
	flTargetLocked =(1 << 1),
};

class CUIBinocularsVisionObject : public CUIWindow
{
	typedef CUIWindow inherited;
public:
	CUIBinocularsVisionObject()
	{
	};
	u16 m_id;
	const CUIBinocularsVision* m_vision;
	CUIStatic m_lt;
	CUIStatic m_lb;
	CUIStatic m_rt;
	CUIStatic m_rb;
	Frect cur_rect;

	Flags8 m_flags;
	void create(const CUIBinocularsVision* vision);
	virtual void Draw();
	virtual void Update();

	bool operator <(const CUIBinocularsVisionObject& other) const
	{
		return m_flags.test(flVisObjNotValid) < other.m_flags.test(flVisObjNotValid);
	} //move non-actual to tail
};

class CUIBinocularsVision : public CUIWindow
{
	friend class CUIBinocularsVisionObject;
	typedef CUIWindow inherited;
	typedef xr_vector<CUIBinocularsVisionObject*> VIS_OBJECTS;
	typedef VIS_OBJECTS::iterator VIS_OBJECTS_IT;
	VIS_OBJECTS m_active_objects;
public:
	CUIBinocularsVision(LPCSTR lt, LPCSTR rt, LPCSTR lb, LPCSTR rb, const Fvector2& frame_size);
	CUIBinocularsVision(const shared_str& sect);
	virtual void Update();
	virtual void Show(bool status);

	void SetFrameColor(u32 color) { m_frame_color = color; }
	void SetSpeed(float speed) { m_rotating_speed = speed; }
	void SetRelationColors(u32 friend_color, u32 neutral_color, u32 enemy_color);
	void SetFoundSound(LPCSTR path);
	void SetCatchSound(LPCSTR path);
	void SetScreenZoom(float zoom, float radius) { m_screen_zoom = zoom; m_screen_zoom_radius = radius; }

protected :
	float m_screen_zoom; // magnification of the screen around its center, applied after the world is rendered
	float m_screen_zoom_radius; // radius of the magnified circle, in screen heights
	u32 m_frame_color;
	float m_rotating_speed;
	u32 m_friend_color;
	u32 m_neutral_color;
	u32 m_enemy_color;
	shared_str m_frame_textures[4]; // lt, rt, lb, rb
	Fvector2 m_frame_size;
	void Load(const shared_str& section);
	ref_sound m_found_snd;
	ref_sound m_catch_snd;

DECLARE_SCRIPT_REGISTER_FUNCTION
};
