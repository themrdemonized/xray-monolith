#pragma once
#include "UIStatic.h"
#include "UIWndCallback.h"

class CUIGlobalMapSpot;
class CUIMapWnd;

class CUICustomMap : public CUIStatic, public CUIWndCallback
{
protected:
	shared_str m_name;

	Frect m_BoundRect_; // real map size (meters)
	Flags16 m_flags;

	enum EFlags 
	{
		eLocked = (1 << 0),
		eRounded = (1 << 1),
		eRotate = (1 << 2)
	};

	float m_pointer_dist;
	Frect m_workingArea;
public:
	Frect& WorkingArea() { return m_workingArea; }
	Frect m_prevRect;
	shared_str m_texture;
	shared_str m_shader_name;

	CUICustomMap();
	virtual ~CUICustomMap();

	virtual void SetActivePoint(const Fvector& vNewPoint);

	void Initialize(shared_str name, LPCSTR sh_name);
	virtual Fvector2 ConvertRealToLocal(const Fvector2& src, bool for_drawing);
	// meters->pixels (relatively own left-top pos)
	Fvector2 ConvertLocalToReal(const Fvector2& src, Frect const& bound_rect);
	Fvector2 ConvertRealToLocalNoTransform(const Fvector2& src, Frect const& bound_rect);
	// meters->pixels (relatively own left-top pos)

	virtual bool GetPointerTo(const Fvector2& src, float item_radius, Fvector2& pos, float& heading);
	//position and heading for drawing pointer to src pos

	void FitToWidth(float width);
	void FitToHeight(float height);

	Fvector2 GetCurrentZoom() const
	{
		return Fvector2().set(GetWndRect().height() / BoundRect().height(), GetWndRect().width() / BoundRect().width());
	}

	const Frect& BoundRect() const { return m_BoundRect_; }
	virtual void OptimalFit(const Frect& r);

	const shared_str& MapName() { return m_name; }
	virtual CUIGlobalMapSpot* GlobalMapSpot() { return NULL; }

	virtual void Draw();
	virtual void Update();
	virtual void SendMessage(CUIWindow* pWnd, s16 msg, void* pData);
	virtual bool IsRectVisible(Frect r);
	virtual bool NeedShowPointer(Frect r);
	bool Locked() { return !!m_flags.test(eLocked); }
	void SetLocked(bool b) { m_flags.set(eLocked, b); }
	bool IsRounded() { return m_flags.test(eRounded); }
	void SetRounded(bool b) { m_flags.set(eRounded, b); }
	bool Rotate() { return m_flags.test(eRotate); }
	void SetRotate(bool b) { m_flags.set(eRotate, b); }
	void SetPointerDistance(float d) { m_pointer_dist = d; };
	float GetPointerDistance() { return m_pointer_dist; };

	virtual CUIWindow* ui_cast_window() { return this; }
	virtual CUIStatic* ui_cast_static() { return this; }
protected:
	virtual void Init_internal(const shared_str& name, CInifile& pLtx, const shared_str& sect_name, LPCSTR sh_name);

	virtual void UpdateSpots()
	{
	};
};


class CUIGlobalMap : public CUICustomMap
{
	typedef CUICustomMap inherited;

	shared_str m_prev_active_map;
	CUIMapWnd* m_mapWnd;
	float m_minZoom;
	float m_max_zoom;
public:

	virtual Fvector2 ConvertRealToLocal(const Fvector2& src, bool for_drawing);
	// pixels->pixels (relatively own left-top pos)

	CUIGlobalMap(CUIMapWnd* pMapWnd);
	virtual ~CUIGlobalMap();

	IC void SetMinZoom(float zoom) { m_minZoom = zoom; }
	IC float GetMinZoom() { return m_minZoom; }
	IC float GetMaxZoom() { return m_max_zoom; }
	IC void SetMaxZoom(float zoom) { m_max_zoom = zoom; }

	virtual bool OnMouseAction(float x, float y, EUIMessages mouse_action);

	CUIMapWnd* MapWnd() { return m_mapWnd; }
	void MoveWndDelta(const Fvector2& d);

	float CalcOpenRect(const Fvector2& center_point, Frect& map_desired_rect, float tgt_zoom);

	void ClipByVisRect();
	virtual void Update();
	void Initialize();

	// demonized: pointer to hovered map on PDA
	CUICustomMap* hoveredMap;
protected:
	virtual void Init_internal(const shared_str& name, CInifile& pLtx, const shared_str& sect_name, LPCSTR sh_name);
};

class CUITextWnd;

class CUILevelMap : public CUICustomMap
{
	typedef CUICustomMap inherited;

	CUIMapWnd* m_mapWnd;
	Frect m_GlobalRect; // virtual map size (meters)
	CUITextWnd* m_label; // owned and freed in dtor — do not enable autodelete
	float m_label_scale_max; // hide label when global zoom >= this; 0 = always show
	Fvector2 m_label_offset; // global_rect units from rect centre; +x right, +y up (scaled by zoom at apply)
	CUILevelMap(const CUILevelMap& obj)
	{
	}

	CUILevelMap& operator=(const CUILevelMap& obj)
	{
	}

public:
	CUILevelMap(CUIMapWnd*);
	virtual ~CUILevelMap();
	const Frect& GlobalRect() const { return m_GlobalRect; }
	virtual void Draw();
	virtual void Show(bool status);
	virtual void Update();
	virtual bool OnMouseAction(float x, float y, EUIMessages mouse_action);
	virtual void SendMessage(CUIWindow* pWnd, s16 msg, void* pData);

	Frect CalcWndRectOnGlobal();
	CUIMapWnd* MapWnd() { return m_mapWnd; }

	virtual void OnFocusLost();

protected:
	virtual void UpdateSpots();
	virtual void Init_internal(const shared_str& name, CInifile& pLtx, const shared_str& sect_name, LPCSTR sh_name);
};

class CUIMiniMap : public CUICustomMap
{
	typedef CUICustomMap inherited;

public:
	CUIMiniMap();
	virtual ~CUIMiniMap();
	virtual void Draw();
	virtual bool GetPointerTo(const Fvector2& src, float item_radius, Fvector2& pos, float& heading);
	//position and heading for drawing pointer to src pos
	virtual bool NeedShowPointer(Frect r);
	virtual bool IsRectVisible(Frect r);
protected:
	virtual void UpdateSpots();
	virtual void Init_internal(const shared_str& name, CInifile& pLtx, const shared_str& sect_name, LPCSTR sh_name);
};

class CMapLocation;
class CMiniMapSpot;
class CMapSpotPointer;

class CUIMiniMapWidget : public CUIWindow
{
	typedef CUIWindow inherited;

	struct SPoolEntry
	{
		CMiniMapSpot* spot;
		CMapSpotPointer* pointer;
		bool pointer_path;
		u32 base_color;
		u32 color;
		u32 stamp;
	};

	struct SIconOverride
	{
		shared_str texture;
		shared_str above;
		shared_str below;
		float width = 0.f;
		float height = 0.f;
	};

	struct SPointerOverride
	{
		shared_str texture;
		Frect rect;
	};

	CUICustomMap* m_map;
	CUIStatic* m_global;

	xr_map<CMapLocation*, SPoolEntry> m_pool;
	xr_map<shared_str, SIconOverride> m_icons;
	xr_map<shared_str, SPointerOverride> m_pointer_icons;
	shared_str m_spot_shader;
	shared_str m_global_texture;
	shared_str m_global_default;
	xr_map<u16, u32> m_spot_colors;
	xr_set<shared_str> m_hidden_types;
	bool m_pointers_visible = true;
	xr_set<shared_str> m_pointer_types;
	u16 m_pointer_target = u16(-1);
	u32 m_pointer_count = 0;
	shared_str m_pointer_texture;
	Frect m_pointer_rect;

	Frect m_global_canvas;
	Frect m_global_rect;
	bool m_global_ready;
	bool m_global_visible;

	bool m_has_map;
	bool m_rotate;
	float m_heading;
	float m_zoom_span;
	float m_spot_scale;
	float m_pointer_scale;
	u32 m_texture_color;
	u32 m_update_interval;
	u32 m_last_update;
	u32 m_spot_count;
	u32 m_stamp;
	shared_str m_shader;
	shared_str m_map_texture;

public:
	CUIMiniMapWidget();
	virtual ~CUIMiniMapWidget();

	bool init(LPCSTR level_name);
	bool init(LPCSTR level_name, LPCSTR shader);
	LPCSTR map_texture();
	void set_map_texture(LPCSTR texture);
	bool has_map() const { return m_has_map; }
	void set_zoom_span(float meters);
	bool set_active_point(const Fvector& pos);
	void set_heading(float radians);
	void set_rotate(bool b);
	void set_texture_color(u32 color);
	void set_spot_scale(float scale);
	void set_pointer_scale(float scale);
	void set_spot_color(u16 object_id, u32 color);
	void set_update_interval(u32 ms);
	Fvector2 local_of(const Fvector& pos);
	u32 spot_count() const { return m_spot_count; }
	u32 pointer_count() const { return m_pointer_count; }
	void set_global_visible(bool b);
	void set_global_texture(LPCSTR texture);
	void set_spot_shader(LPCSTR shader);
	void set_spot_texture(LPCSTR spot_type, LPCSTR texture, float width, float height);
	void set_spot_height_textures(LPCSTR spot_type, LPCSTR above, LPCSTR below);
	void clear_spot_textures();
	void set_spot_type_visible(LPCSTR spot_type, bool visible);
	void clear_spot_type_filter();
	void set_pointers_visible(bool visible);
	void set_pointer_type(LPCSTR spot_type, bool point);
	void clear_pointer_types();
	void set_pointer_target(u16 object_id);
	void clear_pointer_target();
	void set_pointer_texture(LPCSTR texture, float x, float y, float w, float h);
	void set_pointer_texture(LPCSTR spot_type, LPCSTR texture, float x, float y, float w, float h);
	void clear_pointer_texture();
	u16 spot_at(float x, float y);

	virtual void Update();
	virtual void Draw();

protected:
	bool level_ready();
	void clear_pool();
	void release(SPoolEntry& e);
	void prune_vanished();
	SPoolEntry* acquire(CMapLocation* loc);
	bool pointer_allowed(CMapLocation* loc) const;
	void refit_pointers();
	LPCSTR spot_shader() const { return m_spot_shader.size() ? m_spot_shader.c_str() : m_shader.c_str(); }
	void scale_spot(CUIStatic* sp, float scale);
	void init_global(const shared_str& level);
	void place_global();
};
