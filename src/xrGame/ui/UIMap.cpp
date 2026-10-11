#include "stdafx.h"
#include "../level.h"
#include "../map_location.h"
#include "../map_manager.h"
#include "../map_spot.h"
#include "UIMap.h"
#include "UIMapWnd.h"
#include "UIStatic.h"
#include "UIXmlInit.h"
#include "../string_table.h"
#include "../../xrEngine/xr_input.h"		//remove me !!!

const u32 activeLocalMapColor = 0xffffffff; //0xffc80000;
const u32 inactiveLocalMapColor = 0xffffffff; //0xff438cd1;
const u32 ourLevelMapColor = 0xffffffff;

BOOL pda_show_map_labels = TRUE;


CUICustomMap::CUICustomMap()
{
	m_BoundRect_.set(0, 0, 0, 0);
	SetWindowName("map");
	m_flags.zero();
	SetPointerDistance(0.0f);
}

void CUICustomMap::Initialize(shared_str name, LPCSTR sh_name)
{
	CInifile* levelIni = NULL;
	if (name == g_pGameLevel->name())
		levelIni = g_pGameLevel->pLevel;
	else
	{
		string_path map_cfg_fn;
		string_path fname;
		strconcat(sizeof(fname), fname, name.c_str(), "\\level.ltx");
		FS.update_path(map_cfg_fn, "$game_levels$", fname);
		levelIni = xr_new<CInifile>(map_cfg_fn);
	}

	if (levelIni->section_exist("level_map"))
	{
		Init_internal(name, *levelIni, "level_map", sh_name);
	}
	else
	{
		Msg("! default LevelMap used for level[%s]", name.c_str());
		Init_internal(name, *pGameIni, "def_map", sh_name);
		m_name = name;
	}
	if (levelIni != g_pGameLevel->pLevel)
		xr_delete(levelIni);
}

CUICustomMap::~CUICustomMap()
{
}

void CUICustomMap::Update()
{
	SetPointerDistance(0.0f);
	if (!Locked())
		UpdateSpots();

	CUIStatic::Update();
}

void CUICustomMap::Draw()
{
	UI().PushScissor(WorkingArea());
	CUIStatic::Draw();
	UI().PopScissor();
}


void CUICustomMap::Init_internal(const shared_str& name, CInifile& pLtx, const shared_str& sect_name, LPCSTR sh_name)
{
	m_name = name;
	Fvector4 tmp;

	m_texture = pLtx.r_string(sect_name, "texture");
	m_shader_name = sh_name;
	tmp = pLtx.r_fvector4(sect_name, "bound_rect");

	if (!Heading())
	{
		tmp.x *= UI().get_current_kx();
		tmp.z *= UI().get_current_kx();
	}

	m_BoundRect_.set(tmp.x, tmp.y, tmp.z, tmp.w);

	Fvector2 sz;
	m_BoundRect_.getsize(sz);
	CUIStatic::SetWndSize(sz);
	CUIStatic::SetWndPos(Fvector2().set(0, 0));
	CUIStatic::InitTextureEx(m_texture.c_str(), m_shader_name.c_str());

	SetStretchTexture(true);
}

void rotation_(float x, float y, const float angle, float& x_, float& y_, float kx)
{
	float _sc = _cos(angle);
	float _sn = _sin(angle);
	x_ = x * _sc + y * _sn;
	y_ = y * _sc - x * _sn;
	x_ *= kx;
}

Fvector2 CUICustomMap::ConvertLocalToReal(const Fvector2& src, Frect const& bound_rect)
{
	Fvector2 res;
	res.x = bound_rect.lt.x + src.x / GetCurrentZoom().x;
	res.y = bound_rect.height() + bound_rect.lt.y - src.y / GetCurrentZoom().x;

	return res;
}

Fvector2 CUICustomMap::ConvertRealToLocal(const Fvector2& src, bool for_drawing)
// meters->pixels (relatively own left-top pos)
{
	Fvector2 res;
	if (!Heading())
	{
		Frect bound_rect = BoundRect();
		bound_rect.x1 /= UI().get_current_kx();
		bound_rect.x2 /= UI().get_current_kx();
		res = ConvertRealToLocalNoTransform(src, bound_rect);
		res.x *= UI().get_current_kx();
	}
	else
	{
		Fvector2 heading_pivot = GetStaticItem()->GetHeadingPivot();

		res = ConvertRealToLocalNoTransform(src, BoundRect());
		res.sub(heading_pivot);
		rotation_(res.x, res.y, GetHeading(), res.x, res.y, for_drawing ? UI().get_current_kx() : 1.0f);

		res.add(heading_pivot);
	};
	return res;
}

Fvector2 CUICustomMap::ConvertRealToLocalNoTransform(const Fvector2& src, Frect const& bound_rect)
// meters->pixels (relatively own left-top pos)
{
	Fvector2 res;
	res.x = (src.x - bound_rect.lt.x) * GetCurrentZoom().x;
	res.y = (bound_rect.height() - (src.y - bound_rect.lt.y)) * GetCurrentZoom().x;

	return res;
}

//position and heading for drawing pointer to src pos
bool CUICustomMap::GetPointerTo(const Fvector2& src, float item_radius, Fvector2& pos, float& heading)
{
	Frect clip_rect_abs = WorkingArea(); //absolute rect coords
	Frect map_rect_abs;
	GetAbsoluteRect(map_rect_abs);

	Frect rect;
	BOOL res = rect.intersection(clip_rect_abs, map_rect_abs);
	if (!res) return false;

	rect = clip_rect_abs;
	rect.sub(map_rect_abs.lt.x, map_rect_abs.lt.y);

	Fbox2 f_clip_rect_local;
	f_clip_rect_local.set(rect.x1, rect.y1, rect.x2, rect.y2);

	Fvector2 f_center;
	f_clip_rect_local.getcenter(f_center);

	Fvector2 f_dir, f_src;

	f_src.set(src.x, src.y);
	f_dir.sub(f_center, f_src);
	f_dir.normalize_safe();
	Fvector2 f_intersect_point;
	res = f_clip_rect_local.Pick2(f_src, f_dir, f_intersect_point);
	if (!res)
		return false;


	heading = -f_dir.getH();

	f_intersect_point.mad(f_intersect_point, f_dir, item_radius);

	pos.set(iFloor(f_intersect_point.x), iFloor(f_intersect_point.y));
	return true;
}


void CUICustomMap::FitToWidth(float width)
{
	float k = m_BoundRect_.width() / m_BoundRect_.height();
	float w = width;
	float h = width / k;
	SetWndRect(Frect().set(0.0f, 0.0f, w, h));
}

void CUICustomMap::FitToHeight(float height)
{
	float k = m_BoundRect_.width() / m_BoundRect_.height();
	float h = height;
	float w = k * height;

	SetWndRect(Frect().set(0.0f, 0.0f, w, h));
}


void CUICustomMap::OptimalFit(const Frect& r)
{
	if ((BoundRect().height() / r.height()) < (BoundRect().width() / r.width()))
		FitToHeight(r.height());
	else
		FitToWidth(r.width());
}

// try to positioning clipRect center to vNewPoint
void CUICustomMap::SetActivePoint(const Fvector& vNewPoint)
{
	Fvector2 pos;
	pos.set(vNewPoint.x, vNewPoint.z);
	Frect bound = BoundRect();
	if (FALSE == bound.in(pos))return;

	Fvector2 pos_on_map = ConvertRealToLocalNoTransform(pos, BoundRect());
	Frect map_abs_rect;
	GetAbsoluteRect(map_abs_rect);
	Fvector2 pos_abs;

	pos_abs.set(map_abs_rect.lt);
	pos_abs.add(pos_on_map);

	Fvector2 clip_center;
	WorkingArea().getcenter(clip_center);
	clip_center.sub(pos_abs);
	MoveWndDelta(clip_center);
	SetHeadingPivot(pos_on_map, Fvector2().set(0, 0), false);
}

bool CUICustomMap::IsRectVisible(Frect r)
{
	Fvector2 pos;
	GetAbsolutePos(pos);
	r.add(pos.x, pos.y);

	return !!WorkingArea().intersected(r);
}

bool CUICustomMap::NeedShowPointer(Frect r)
{
	Frect map_visible_rect = WorkingArea();
	map_visible_rect.shrink(5, 5);
	Fvector2 pos;
	GetAbsolutePos(pos);
	r.add(pos.x, pos.y);

	return !map_visible_rect.intersected(r);
}

void CUICustomMap::SendMessage(CUIWindow* pWnd, s16 msg, void* pData)
{
	CUIWndCallback::OnEvent(pWnd, msg, pData);
}

bool CUIGlobalMap::OnMouseAction(float x, float y, EUIMessages mouse_action)
{
	if (inherited::OnMouseAction(x, y, mouse_action)) return true;
	if (mouse_action == WINDOW_MOUSE_MOVE && (FALSE == pInput->iGetAsyncBtnState(0)))
	{
		if (MapWnd())
		{
			MapWnd()->Hint(MapName());
			return true;
		}
	}
	return false;
}


CUIGlobalMap::CUIGlobalMap(CUIMapWnd* pMapWnd)
{
	m_mapWnd = pMapWnd;
	m_minZoom = 1.f;
	Show(false);
}


CUIGlobalMap::~CUIGlobalMap()
{
}

void CUIGlobalMap::Initialize()
{
	Init_internal("global_map", *pGameIni, "global_map", "hud\\default");
}

void CUIGlobalMap::Init_internal(const shared_str& name, CInifile& pLtx, const shared_str& sect_name, LPCSTR sh_name)
{
	inherited::Init_internal(name, pLtx, sect_name, sh_name);
	//	Fvector2 size = CUIStatic::GetWndSize();
	SetMaxZoom(pLtx.r_float(m_name, "max_zoom"));
}

void CUIGlobalMap::Update()
{
	xrCriticalSectionGuard guard(csUi);
	for (WINDOW_LIST_it it = m_ChildWndList.begin(); m_ChildWndList.end() != it; ++it)
	{
		CUICustomMap* m = smart_cast<CUICustomMap*>(*it);
		if (!m) continue;
		m->DetachAll();
	}
	inherited::Update();
}


void CUIGlobalMap::ClipByVisRect()
{
	Frect r = GetWndRect();
	Frect clip = WorkingArea();
	if (r.x2 < clip.width()) r.x1 += clip.width() - r.x2;
	if (r.y2 < clip.height()) r.y1 += clip.height() - r.y2;
	if (r.x1 > 0.0f) r.x1 = 0.0f;
	if (r.y1 > 0.0f) r.y1 = 0.0f;
	SetWndPos(r.lt);
}

Fvector2 CUIGlobalMap::ConvertRealToLocal(const Fvector2& src, bool for_drawing)
// pixels->pixels (relatively own left-top pos)
{
	Fvector2 res;
	res.x = (src.x - BoundRect().lt.x) * GetCurrentZoom().x;
	res.y = (src.y - BoundRect().lt.y) * GetCurrentZoom().x;
	return res;
}

void CUIGlobalMap::MoveWndDelta(const Fvector2& d)
{
	inherited::MoveWndDelta(d);
	ClipByVisRect();
	m_mapWnd->UpdateScroll();
}

float CUIGlobalMap::CalcOpenRect(const Fvector2& center_point, Frect& map_desired_rect, float tgt_zoom)
{
	Fvector2 new_center_pt;
	// calculate desired rect in new zoom
	map_desired_rect.set(0.0f, 0.0f, BoundRect().width() * tgt_zoom, BoundRect().height() * tgt_zoom);

	// calculate center point in new zoom (center_point is in identity global map space)
	new_center_pt.set(center_point.x * tgt_zoom, center_point.y * tgt_zoom);
	// get vis width & height
	Frect vis_abs_rect = m_mapWnd->ActiveMapRect();
	float vis_w = vis_abs_rect.width();
	float vis_h = vis_abs_rect.height();
	// calculate center delta from vis rect
	Fvector2 delta_pos;
	delta_pos.set(new_center_pt.x - vis_w * 0.5f, new_center_pt.y - vis_h * 0.5f);

	// correct desired rect
	map_desired_rect.sub(delta_pos.x, delta_pos.y);
	// clamp pos by vis rect
	const Frect& r = map_desired_rect;
	Fvector2 np = r.lt;
	if (r.x2 < vis_w) np.x += vis_w - r.x2;
	if (r.y2 < vis_h) np.y += vis_h - r.y2;
	if (r.x1 > 0.0f) np.x = 0.0f;
	if (r.y1 > 0.0f) np.y = 0.0f;
	np.sub(r.lt);
	map_desired_rect.add(np.x, np.y);
	// calculate max way dist
	float dist = 0.f;

	Frect s_rect, t_rect;
	s_rect.div(GetWndRect(), GetCurrentZoom().x, GetCurrentZoom().x);
	t_rect.div(map_desired_rect, tgt_zoom, tgt_zoom);

	Fvector2 cpS, cpT;
	s_rect.getcenter(cpS);
	t_rect.getcenter(cpT);

	dist = cpS.distance_to(cpT);

	return dist;
}

CUILevelMap::CUILevelMap(CUIMapWnd* p)
{
	m_mapWnd = p;
	m_label = nullptr;
	m_label_scale_max = 0.0f;
	m_label_offset.set(0.0f, 0.0f);
	Show(false);
}

CUILevelMap::~CUILevelMap()
{
	if (m_label)
	{
		if (m_label->GetParent() == this)
			DetachChild(m_label);
		xr_delete(m_label);
		m_label = nullptr;
	}
}

void CUILevelMap::Draw()
{
	if (MapWnd())
	{
		float gmz = MapWnd()->GlobalMap()->GetCurrentZoom().x;
		xrCriticalSectionGuard guard(csUi);
		if (m_label && m_label_scale_max > 0.0f)
			m_label->SetVisible(!!pda_show_map_labels && (gmz < m_label_scale_max));

		for (WINDOW_LIST_it it = m_ChildWndList.begin(); m_ChildWndList.end() != it; ++it)
		{
			CMapSpot* sp = smart_cast<CMapSpot*>((*it));
			if (sp)
			{
				if (sp->m_bScale)
				{
					Fvector2 sz = sp->m_originSize;
					float k = gmz;

					if (gmz > sp->m_scale_bounds.y)
						k = sp->m_scale_bounds.y;
					else if (gmz < sp->m_scale_bounds.x)
						k = sp->m_scale_bounds.x;

					sz.mul(k);
					sp->SetWndSize(sz);
				}
				else if (sp->m_scale_bounds.x > 0.0f)
					sp->SetVisible(sp->m_scale_bounds.x < gmz);
			}
		}
	}
	inherited::Draw();
}

void CUILevelMap::Init_internal(const shared_str& name, CInifile& pLtx, const shared_str& sect_name, LPCSTR sh_name)
{
	inherited::Init_internal(name, pLtx, sect_name, sh_name);
	Fvector4 tmp = pGameIni->r_fvector4(MapName(), "global_rect");

	tmp.x *= UI().get_current_kx();
	tmp.z *= UI().get_current_kx();
	m_GlobalRect.set(tmp.x, tmp.y, tmp.z, tmp.w);

	if (pGameIni->line_exist(MapName(), "label"))
	{
		LPCSTR label_id = pGameIni->r_string(MapName(), "label");
		m_label_scale_max = pGameIni->line_exist(MapName(), "label_scale_max")
			? pGameIni->r_float(MapName(), "label_scale_max")
			: 0.0f;

		// label_color = R, G, B, A — ints 0-255. Alpha omitted defaults to 255
		// (per r_color/sscanf), not the engine default of 180, so authors who
		// want partial transparency must spell out the 4th component.
		u32 label_color = color_argb(180, 230, 220, 200);
		if (pGameIni->line_exist(MapName(), "label_color"))
			label_color = pGameIni->r_color(MapName(), "label_color");

		if (pGameIni->line_exist(MapName(), "label_offset_x"))
			m_label_offset.x = pGameIni->r_float(MapName(), "label_offset_x");
		if (pGameIni->line_exist(MapName(), "label_offset_y"))
			m_label_offset.y = pGameIni->r_float(MapName(), "label_offset_y");

		CStringTable str_tbl;
		m_label = xr_new<CUITextWnd>();
		m_label->SetFont(UI().Font().pFontLetterica18Russian);
		m_label->SetTextColor(label_color);
		m_label->SetTextAlignment(CGameFont::alCenter);
		m_label->SetText(*str_tbl.translate(label_id));
		m_label->SetWidth(180.0f);
		m_label->AdjustHeightToText();
		AttachChild(m_label);
	}

#ifdef DEBUG
	float kw = m_GlobalRect.width	()	/	BoundRect().width	();
	float kh = m_GlobalRect.height	()	/	BoundRect().height	();

	if(FALSE==fsimilar(kw,kh,EPS_L))
	{
		Msg(" --incorrect global rect definition for map [%s]  kw=%f kh=%f",*MapName(),kw,kh);
		Msg(" --try x2=%f or  y2=%f",m_GlobalRect.x1+kh*BoundRect().width(), m_GlobalRect.y1+kw*BoundRect().height());
	}
#endif
}


void CUILevelMap::UpdateSpots()
{
	DetachAll();

	//.	if( fsimilar(MapWnd()->GlobalMap()->GetCurrentZoom(),MapWnd()->GlobalMap()->GetMinZoom(),EPS_L ) ) return;

	Frect _r;
	GetAbsoluteRect(_r);

	if (FALSE == MapWnd()->ActiveMapRect().intersected(_r)) return;

	Locations& ls = Level().MapManager().Locations();
	Locations_it it = ls.begin();
	Locations_it it_e = ls.end();

	for (u32 idx = 0; it != it_e; ++it, ++idx)
	{
		if ((*it).actual && MapName() == (*it).location->GetLevelName())
		{
			(*it).location->UpdateLevelMap(this);
		}
	}

	std::stable_sort(m_ChildWndList.begin(), m_ChildWndList.end(),
		[](CUIWindow* a, CUIWindow* b) {
			CMapSpot* sa = smart_cast<CMapSpot*>(a);
			CMapSpot* sb = smart_cast<CMapSpot*>(b);
			int la = sa ? sa->get_location_level() : 0;
			int lb = sb ? sb->get_location_level() : 0;
			return la < lb;
		}
	);
}

Frect CUILevelMap::CalcWndRectOnGlobal()
{
	Frect res;
	CUIGlobalMap* globalMap = MapWnd()->GlobalMap();

	res.lt = globalMap->ConvertRealToLocal(GlobalRect().lt, false);
	res.rb = globalMap->ConvertRealToLocal(GlobalRect().rb, false);
	res.add(globalMap->GetWndPos().x, globalMap->GetWndPos().y);

	return res;
}

void CUILevelMap::Show(bool status)
{
	inherited::Show(status);
}

void CUILevelMap::Update()
{
	CUIGlobalMap* w = MapWnd()->GlobalMap();
	Frect rect;
	Fvector2 tmp;

	tmp = w->ConvertRealToLocal(GlobalRect().lt, false);
	rect.lt = tmp;
	tmp = w->ConvertRealToLocal(GlobalRect().rb, false);
	rect.rb = tmp;

	SetWndRect(rect);

	inherited::Update();

	// UpdateSpots() calls DetachAll() each frame, so the label must be
	// re-attached after inherited::Update() runs or it won't draw.
	if (m_label)
	{
		float lw = m_label->GetWidth();
		float lh = m_label->GetHeight();
		// Offset is in global_rect units (scaled by zoom so it stays anchored);
		// X also scaled by UI kx since global_rect X is kx-scaled at load.
		// Y inverted because engine screen Y grows downward.
		float gmz = MapWnd()->GlobalMap()->GetCurrentZoom().x;
		float kx  = UI().get_current_kx();
		m_label->SetWndPos(Fvector2().set(
			(rect.width()  - lw) * 0.5f + m_label_offset.x * gmz * kx,
			(rect.height() - lh) * 0.5f - m_label_offset.y * gmz));
		if (!m_label->GetParent())
			AttachChild(m_label);
	}

	if (m_bCursorOverWindow)
	{
		// demonized: send pointer to this map into GlobalMap
		MapWnd()->GlobalMap()->hoveredMap = this;

		VERIFY(m_dwFocusReceiveTime>=0);
		if (Device.dwTimeGlobal > (m_dwFocusReceiveTime + 500))
		{
			if (fsimilar(MapWnd()->GlobalMap()->GetCurrentZoom().x, MapWnd()->GlobalMap()->GetMinZoom(), EPS_L))
				MapWnd()->ShowHintStr(this, MapName().c_str());
			else
				MapWnd()->HideHint(this);
		}
	}
}

bool CUILevelMap::OnMouseAction(float x, float y, EUIMessages mouse_action)
{
	if (inherited::OnMouseAction(x, y, mouse_action)) return true;
	if (MapWnd()->GlobalMap()->Locked()) return true;

	if (mouse_action == WINDOW_MOUSE_MOVE && (FALSE == pInput->iGetAsyncBtnState(0)))
	{
		if (MapWnd())
		{
			MapWnd()->Hint(MapName());
			return true;
		}
	}
	return false;
}

void CUILevelMap::SendMessage(CUIWindow* pWnd, s16 msg, void* pData)
{
	inherited::SendMessage(pWnd, msg, pData);

	if (msg == MAP_SHOW_HINT)
	{
		CMapSpot* sp = smart_cast<CMapSpot*>(pWnd);
		VERIFY(sp);
		if (sp)
		{
			MapWnd()->ShowHintSpot(sp);
		}
	}
	else if (msg == MAP_HIDE_HINT)
	{
		MapWnd()->HideHint(pWnd);
	}
	else if (msg == MAP_SELECT_SPOT)
	{
		MapWnd()->SpotSelected(pWnd);
	}
	else if (msg == MAP_SELECT_SPOT2)
	{
		MapWnd()->ActivatePropertiesBox(pWnd);
	}
}

void CUILevelMap::OnFocusLost()
{
	inherited::OnFocusLost();
	MapWnd()->HideHint(this);
}

CUIMiniMap::CUIMiniMap()
{
}

CUIMiniMap::~CUIMiniMap()
{
}

void CUIMiniMap::Init_internal(const shared_str& name, CInifile& pLtx, const shared_str& sect_name, LPCSTR sh_name)
{
	inherited::Init_internal(name, pLtx, sect_name, sh_name);
	//CUIStatic::SetTextureColor(0x7fffffff);
}

void CUIMiniMap::UpdateSpots()
{
	DetachAll();
	Locations& ls = Level().MapManager().Locations();
	for (Locations_it it = ls.begin(); it != ls.end(); ++it)
		(*it).location->UpdateMiniMap(this);
}

void CUIMiniMap::Draw()
{
	if (!IsRounded())
	{
		inherited::Draw();
		return;
	}

	u32 segments_count = 20;

	UIRender->SetShader(*m_UIStaticItem.GetShader());
	UIRender->StartPrimitive(segments_count * 3, IUIRender::ptTriList, UI().m_currentPointType);

	u32 color = m_UIStaticItem.GetTextureColor();
	float angle = GetHeading();


	float kx = UI().get_current_kx();

	// clip poly
	sPoly2D S;
	S.resize(segments_count);
	float segment_ang = PI_MUL_2 / segments_count;
	float pt_radius = WorkingArea().width() / 2.0f;
	Fvector2 center;
	WorkingArea().getcenter(center);

	float tt_radius = pt_radius / GetWidth();
	float k_tt_height = GetWidth() / GetHeight();

	Fvector2 tt_offset;
	tt_offset.set(m_UIStaticItem.vHeadingPivot);
	tt_offset.x /= GetWidth();
	tt_offset.y /= GetHeight();

	Fvector2 m_scale_;
	m_scale_.set(float(Device.dwWidth) / UI_BASE_WIDTH, float(Device.dwHeight) / UI_BASE_HEIGHT);

	for (u32 idx = 0; idx < segments_count; ++idx)
	{
		float cosPT = _cos(segment_ang * idx + angle);
		float sinPT = _sin(segment_ang * idx + angle);

		float cosTX = _cos(segment_ang * idx);
		float sinTX = _sin(segment_ang * idx);

		S[idx].pt.set(pt_radius * cosPT * kx, -pt_radius * sinPT);
		S[idx].uv.set(tt_radius * cosTX, -tt_radius * sinTX * k_tt_height);
		S[idx].uv.add(tt_offset);
		S[idx].pt.add(center);

		S[idx].pt.x *= m_scale_.x;
		S[idx].pt.y *= m_scale_.y;
	}

	for (u32 idx = 0; idx < segments_count - 2; ++idx)
	{
		UIRender->PushPoint(S[0 + 0].pt.x, S[0 + 0].pt.y, 0, color, S[0 + 0].uv.x, S[0 + 0].uv.y);
		UIRender->PushPoint(S[idx + 2].pt.x, S[idx + 2].pt.y, 0, color, S[idx + 2].uv.x, S[idx + 2].uv.y);
		UIRender->PushPoint(S[idx + 1].pt.x, S[idx + 1].pt.y, 0, color, S[idx + 1].uv.x, S[idx + 1].uv.y);
	}

	UIRender->FlushPrimitive();


	//------------
	CUIWindow::Draw(); //draw childs
}

bool CUIMiniMap::GetPointerTo(const Fvector2& src, float item_radius, Fvector2& pos, float& heading)
{
	if (!IsRounded())
		return inherited::GetPointerTo(src, item_radius, pos, heading);

	Fvector2 clip_center = GetStaticItem()->GetHeadingPivot();
	float map_radius = WorkingArea().width() / 2.0f;
	Fvector2 direction;

	direction.sub(clip_center, src);
	heading = -direction.getH();

	float kx = UI().get_current_kx();
	float cosPT = _cos(heading);
	float sinPT = _sin(heading);
	pos.set(-map_radius * sinPT * kx, -map_radius * cosPT);
	pos.add(clip_center);

	return true;
}

bool CUIMiniMap::NeedShowPointer(Frect r)
{
	if (!IsRounded())
		return inherited::NeedShowPointer(r);

	Fvector2 clip_center = GetStaticItem()->GetHeadingPivot();

	Fvector2 spot_pos;
	r.getcenter(spot_pos);
	float dist = clip_center.distance_to(spot_pos);
	float spot_radius = r.width() / 2.0f;
	return (dist + spot_radius > WorkingArea().width() / 2.0f);
}

bool CUIMiniMap::IsRectVisible(Frect r)
{
	if (!IsRounded())
		return inherited::IsRectVisible(r);

	Fvector2 clip_center = GetStaticItem()->GetHeadingPivot();
	float vis_radius = WorkingArea().width() / 2.0f;
	Fvector2 rect_center;
	r.getcenter(rect_center);
	float spot_radius = r.width() / 2.0f;
	return clip_center.distance_to(rect_center) + spot_radius < vis_radius; //assume that all minimap spots are circular
}

// the pulse rests on the size seen when armed, so arming follows the scale
static void arm_spot_anim(CUIStatic* sp, CUIXml* xml, LPCSTR path)
{
	LPCSTR anim = xml->ReadAttrib(path, 0, "xform_anim", "");
	int cyclic = xml->ReadAttribInt(path, 0, "xform_anim_cyclic", 0);
	sp->SetXformLightAnim(anim, cyclic != 0);
}

// the widget drives its own spot pool, so the map child never collects spots itself
class CUIWidgetMap : public CUICustomMap
{
protected:
	virtual void UpdateSpots()
	{
	}

public:
	// the frame is a few hundredths of a unit wide, so the edge margin is a fraction of its size instead of a pixel count
	virtual bool NeedShowPointer(Frect r)
	{
		Frect map_visible_rect = WorkingArea();
		map_visible_rect.shrink(map_visible_rect.width() * 0.02f, map_visible_rect.height() * 0.02f);
		Fvector2 pos;
		GetAbsolutePos(pos);
		r.add(pos.x, pos.y);

		return !map_visible_rect.intersected(r);
	}

	// same as the base pointer placement without the whole unit rounding of the result
	virtual bool GetPointerTo(const Fvector2& src, float item_radius, Fvector2& pos, float& heading)
	{
		Frect clip_rect_abs = WorkingArea();
		Frect map_rect_abs;
		GetAbsoluteRect(map_rect_abs);

		Frect rect;
		BOOL res = rect.intersection(clip_rect_abs, map_rect_abs);
		if (!res) return false;

		rect = clip_rect_abs;
		rect.sub(map_rect_abs.lt.x, map_rect_abs.lt.y);

		Fbox2 f_clip_rect_local;
		f_clip_rect_local.set(rect.x1, rect.y1, rect.x2, rect.y2);

		Fvector2 f_center;
		f_clip_rect_local.getcenter(f_center);

		Fvector2 f_dir, f_src;
		f_src.set(src.x, src.y);
		f_dir.sub(f_center, f_src);
		f_dir.normalize_safe();
		Fvector2 f_intersect_point;
		res = f_clip_rect_local.Pick2(f_src, f_dir, f_intersect_point);
		if (!res) return false;

		heading = -f_dir.getH();
		f_intersect_point.mad(f_intersect_point, f_dir, item_radius);
		pos.set(f_intersect_point.x, f_intersect_point.y);
		return true;
	}
};

CUIMiniMapWidget::CUIMiniMapWidget()
{
	m_global_canvas.set(0.0f, 0.0f, 0.0f, 0.0f);
	m_global_rect.set(0.0f, 0.0f, 0.0f, 0.0f);
	m_global_ready = false;
	m_global_visible = true;

	m_has_map = false;
	m_rotate = false;
	m_heading = 0.0f;
	m_zoom_span = 100.0f;
	m_spot_scale = 1.0f;
	m_pointer_scale = 0.0f;
	m_shader = "hud\\default";
	m_texture_color = 0xffffffff;
	m_update_interval = 0;
	m_last_update = 0;
	m_spot_count = 0;
	m_stamp = 0;

	SetWindowName("minimap_widget");
	EnableClip(true);

	m_global = xr_new<CUIStatic>();
	m_global->SetAutoDelete(true);
	m_global->Show(false);
	AttachChild(m_global);

	m_map = xr_new<CUIWidgetMap>();
	m_map->SetAutoDelete(true);
	AttachChild(m_map);
}

CUIMiniMapWidget::~CUIMiniMapWidget()
{
	clear_pool();
}

void CUIMiniMapWidget::clear_pool()
{
	m_map->DetachAll();

	for (auto it = m_pool.begin(); it != m_pool.end(); ++it)
		release(it->second);
	m_pool.clear();
	m_spot_count = 0;
	m_pointer_count = 0;
}

void CUIMiniMapWidget::release(SPoolEntry& e)
{
	if (e.spot && e.spot->GetParent() == m_map)
		m_map->DetachChild(e.spot);
	if (e.pointer && e.pointer->GetParent() == m_map)
		m_map->DetachChild(e.pointer);
	xr_delete(e.spot);
	xr_delete(e.pointer);
}

// drops the entries whose location left the registry before a draw can touch them
void CUIMiniMapWidget::prune_vanished()
{
	++m_stamp;
	Locations& ls = Level().MapManager().Locations();
	for (Locations_it it = ls.begin(); it != ls.end(); ++it)
	{
		auto found = m_pool.find((*it).location);
		if (found != m_pool.end())
			found->second.stamp = m_stamp;
	}

	for (auto it = m_pool.begin(); it != m_pool.end();)
	{
		if (it->second.stamp == m_stamp)
		{
			++it;
			continue;
		}
		release(it->second);
		it = m_pool.erase(it);
	}
	m_spot_count = (u32)m_map->GetChildNum();
}

bool CUIMiniMapWidget::level_ready()
{
	if (!m_has_map) return false;
	if (!g_pGameLevel) return false;

	return g_pGameLevel->name() == m_map->MapName();
}

bool CUIMiniMapWidget::init(LPCSTR level_name)
{
	shared_str keep = m_shader;
	return init(level_name, keep.c_str());
}

// every quad takes the screen's own shader so the world space pass transforms it with the page
bool CUIMiniMapWidget::init(LPCSTR level_name, LPCSTR shader)
{
	m_shader = (shader && xr_strlen(shader)) ? shader : "hud\\default";
	m_has_map = false;
	m_global_ready = false;
	m_global->Show(false);
	clear_pool();

	if (!g_pGameLevel || !g_pGameLevel->pLevel)
	{
		Msg("! minimap widget: no map for level %s", (level_name) ? level_name : "");
		return false;
	}

	shared_str level = (level_name && xr_strlen(level_name)) ? shared_str(level_name) : g_pGameLevel->name();

	bool has_section = pGameIni && pGameIni->section_exist(level);
	if (!has_section && level == g_pGameLevel->name())
		has_section = !!g_pGameLevel->pLevel->section_exist("level_map");

	if (!has_section)
	{
		Msg("! minimap widget: no map for level %s", level.c_str());
		return false;
	}

	// the bound rect leaves the aspect factor out only while the heading flag is already on
	m_map->EnableHeading(true);
	m_map->Initialize(level, m_shader.c_str());
	if (m_map_texture.size())
		m_map->InitTextureEx(m_map_texture.c_str(), m_shader.c_str());
	m_map->SetTextureColor(m_texture_color);
	m_map->SetHeading(m_rotate ? m_heading : 0.0f);
	m_has_map = true;

	set_zoom_span(m_zoom_span);
	init_global(level);

	return true;
}

void CUIMiniMapWidget::init_global(const shared_str& level)
{
	m_global_ready = false;
	m_global->Show(false);

	if (!pGameIni || !pGameIni->section_exist("global_map")) return;
	if (!pGameIni->line_exist("global_map", "texture") || !pGameIni->line_exist("global_map", "bound_rect")) return;
	if (!pGameIni->section_exist(level) || !pGameIni->line_exist(level, "global_rect")) return;

	Fvector4 canvas = pGameIni->r_fvector4("global_map", "bound_rect");
	Fvector4 rect = pGameIni->r_fvector4(level, "global_rect");
	m_global_canvas.set(canvas.x, canvas.y, canvas.z, canvas.w);
	m_global_rect.set(rect.x, rect.y, rect.z, rect.w);

	if (m_global_rect.width() <= EPS_L || m_global_rect.height() <= EPS_L) return;

	m_global_default = pGameIni->r_string("global_map", "texture");
	m_global->InitTextureEx(m_global_texture.size() ? m_global_texture.c_str() : m_global_default.c_str(), m_shader.c_str());
	m_global->SetStretchTexture(true);
	m_global->SetTextureColor(m_texture_color);
	m_global_ready = true;
}

void CUIMiniMapWidget::place_global()
{
	if (!m_global_ready || !m_global_visible)
	{
		m_global->Show(false);
		return;
	}

	Fvector2 map_size = m_map->GetWndSize();
	float sx = map_size.x / m_global_rect.width();
	float sy = map_size.y / m_global_rect.height();

	Fvector2 canvas_size;
	m_global_canvas.getsize(canvas_size);
	m_global->SetWndSize(Fvector2().set(canvas_size.x * sx, canvas_size.y * sy));

	Fvector2 map_pos = m_map->GetWndPos();
	Fvector2 global_pos;
	global_pos.set(map_pos.x - (m_global_rect.x1 - m_global_canvas.x1) * sx,
	               map_pos.y - (m_global_rect.y1 - m_global_canvas.y1) * sy);
	m_global->SetWndPos(global_pos);

	m_global->EnableHeading(m_rotate);
	if (m_rotate)
	{
		m_global->SetHeading(m_map->GetHeading());
		Fvector2 pivot = m_map->GetStaticItem()->GetHeadingPivot();
		pivot.add(Fvector2().set(map_pos.x - global_pos.x, map_pos.y - global_pos.y));
		m_global->SetHeadingPivot(pivot, Fvector2().set(0.0f, 0.0f), false);
	}
	else
		m_global->SetHeading(0.0f);

	m_global->Show(true);
}

void CUIMiniMapWidget::set_zoom_span(float meters)
{
	m_zoom_span = meters;

	if (!m_has_map || meters <= EPS_L) return;

	float frame_height = GetHeight();
	if (frame_height <= EPS_L) return;

	float ppm = frame_height / meters;
	Fvector2 bound_size;
	m_map->BoundRect().getsize(bound_size);
	m_map->SetWndSize(Fvector2().set(bound_size.x * ppm, bound_size.y * ppm));
}

bool CUIMiniMapWidget::set_active_point(const Fvector& pos)
{
	if (!m_has_map) return false;

	Fvector2 p;
	p.set(pos.x, pos.z);

	Frect bound = m_map->BoundRect();
	bool inside = !!bound.in(p);

	const float inset = 0.1f;
	clamp(p.x, bound.x1 + inset, bound.x2 - inset);
	clamp(p.y, bound.y1 + inset, bound.y2 - inset);

	m_map->SetActivePoint(Fvector().set(p.x, pos.y, p.y));
	return inside;
}

void CUIMiniMapWidget::set_heading(float radians)
{
	m_heading = radians;
	if (m_has_map)
		m_map->SetHeading(m_rotate ? m_heading : 0.0f);
}

void CUIMiniMapWidget::set_rotate(bool b)
{
	m_rotate = b;
	if (m_has_map)
		m_map->SetHeading(m_rotate ? m_heading : 0.0f);
}

void CUIMiniMapWidget::set_texture_color(u32 color)
{
	m_texture_color = color;
	m_map->SetTextureColor(color);
	m_global->SetTextureColor(color);
}

void CUIMiniMapWidget::set_spot_scale(float scale)
{
	if (scale <= 0.f || fsimilar(scale, m_spot_scale)) return;

	m_spot_scale = scale;
	clear_pool();
}

LPCSTR CUIMiniMapWidget::map_texture()
{
	return m_has_map ? m_map->m_texture.c_str() : "";
}

// a mod may hand over its own art for the level, registered under any texture id
void CUIMiniMapWidget::set_map_texture(LPCSTR texture)
{
	m_map_texture = (texture && xr_strlen(texture)) ? texture : "";
	if (!m_has_map) return;

	LPCSTR tex = m_map_texture.size() ? m_map_texture.c_str() : m_map->m_texture.c_str();
	m_map->InitTextureEx(tex, m_shader.c_str());
}

// a hidden type drops out of the pool on the next update and comes back the same way
void CUIMiniMapWidget::set_spot_type_visible(LPCSTR spot_type, bool visible)
{
	if (!spot_type || !xr_strlen(spot_type)) return;

	if (visible)
		m_hidden_types.erase(shared_str(spot_type));
	else
		m_hidden_types.insert(shared_str(spot_type));
}

void CUIMiniMapWidget::clear_spot_type_filter()
{
	m_hidden_types.clear();
}

// crops the pointer art to the arrow itself, in texture pixels
void CUIMiniMapWidget::set_pointer_texture(LPCSTR texture, float x, float y, float w, float h)
{
	if (!texture || !xr_strlen(texture) || w <= 0.f || h <= 0.f) return;

	m_pointer_texture = texture;
	m_pointer_rect.set(x, y, x + w, y + h);
	clear_pool();
}

void CUIMiniMapWidget::clear_pointer_texture()
{
	if (!m_pointer_texture.size() && m_pointer_icons.empty()) return;

	m_pointer_texture = "";
	m_pointer_icons.clear();
	clear_pool();
}

void CUIMiniMapWidget::set_pointers_visible(bool visible)
{
	if (m_pointers_visible == visible) return;

	m_pointers_visible = visible;
	refit_pointers();
}

void CUIMiniMapWidget::set_pointer_type(LPCSTR spot_type, bool point)
{
	if (!spot_type || !xr_strlen(spot_type)) return;

	shared_str type(spot_type);
	bool listed = m_pointer_types.find(type) != m_pointer_types.end();
	if (listed == point) return;

	if (point)
		m_pointer_types.insert(type);
	else
		m_pointer_types.erase(type);
	refit_pointers();
}

void CUIMiniMapWidget::clear_pointer_types()
{
	if (m_pointer_types.empty()) return;

	m_pointer_types.clear();
	refit_pointers();
}

// the target keeps its pointer from any level, listed types point within the level
void CUIMiniMapWidget::set_pointer_target(u16 object_id)
{
	if (m_pointer_target == object_id) return;

	m_pointer_target = object_id;
	refit_pointers();
}

void CUIMiniMapWidget::clear_pointer_target()
{
	set_pointer_target(u16(-1));
}

bool CUIMiniMapWidget::pointer_allowed(CMapLocation* loc) const
{
	if (!m_pointers_visible) return false;
	if (loc->ObjectID() == m_pointer_target) return true;

	LPCSTR type = loc->CurrentSpotType();
	return type && m_pointer_types.find(shared_str(type)) != m_pointer_types.end();
}

// drops the pointers the rule no longer allows and forgets the entries that now need one
void CUIMiniMapWidget::refit_pointers()
{
	for (auto it = m_pool.begin(); it != m_pool.end();)
	{
		SPoolEntry& e = it->second;
		bool want = pointer_allowed(it->first);
		if (e.pointer && !want)
		{
			if (e.pointer->GetParent() == m_map)
				m_map->DetachChild(e.pointer);
			xr_delete(e.pointer);
		}
		else if (!e.pointer && want && e.pointer_path)
		{
			release(e);
			it = m_pool.erase(it);
			continue;
		}
		++it;
	}
}

// frame local point in, object id of the spot under it out
u16 CUIMiniMapWidget::spot_at(float x, float y)
{
	Fvector2 map_pos = m_map->GetWndPos();
	for (auto it = m_pool.begin(); it != m_pool.end(); ++it)
	{
		CMiniMapSpot* sp = it->second.spot;
		if (!sp || sp->GetParent() != m_map) continue;

		Frect r = sp->GetWndRect();
		r.add(map_pos.x, map_pos.y);
		if (r.in(x, y))
			return it->first->ObjectID();
	}
	return u16(-1);
}

void CUIMiniMapWidget::set_pointer_scale(float scale)
{
	if (fsimilar(scale, m_pointer_scale)) return;

	m_pointer_scale = scale;
	clear_pool();
}

void CUIMiniMapWidget::set_spot_color(u16 object_id, u32 color)
{
	if (color)
		m_spot_colors[object_id] = color;
	else
		m_spot_colors.erase(object_id);
}

void CUIMiniMapWidget::set_update_interval(u32 ms)
{
	m_update_interval = ms;
}

void CUIMiniMapWidget::set_global_visible(bool b)
{
	m_global_visible = b;
	if (!b)
		m_global->Show(false);
}

void CUIMiniMapWidget::set_spot_texture(LPCSTR spot_type, LPCSTR texture, float width, float height)
{
	if (!spot_type || !texture) return;

	SIconOverride& ov = m_icons[shared_str(spot_type)];
	ov.texture = texture;
	ov.width = width;
	ov.height = height;
	clear_pool();
}

void CUIMiniMapWidget::set_spot_height_textures(LPCSTR spot_type, LPCSTR above, LPCSTR below)
{
	if (!spot_type || !xr_strlen(spot_type)) return;

	SIconOverride& ov = m_icons[shared_str(spot_type)];
	ov.above = above ? above : "";
	ov.below = below ? below : "";
	clear_pool();
}

// icons and pointers may draw through their own shader while the map keeps the one init took
void CUIMiniMapWidget::set_spot_shader(LPCSTR shader)
{
	shared_str next = (shader && xr_strlen(shader)) ? shader : "";
	if (next == m_spot_shader) return;

	m_spot_shader = next;
	clear_pool();
}

// the world map under the level, "" restores the texture the game ini names
void CUIMiniMapWidget::set_global_texture(LPCSTR texture)
{
	m_global_texture = (texture && xr_strlen(texture)) ? texture : "";
	if (!m_global_ready) return;

	m_global->InitTextureEx(m_global_texture.size() ? m_global_texture.c_str() : m_global_default.c_str(), m_shader.c_str());
	m_global->SetStretchTexture(true);
	m_global->SetTextureColor(m_texture_color);
}

void CUIMiniMapWidget::set_pointer_texture(LPCSTR spot_type, LPCSTR texture, float x, float y, float w, float h)
{
	if (!spot_type || !xr_strlen(spot_type) || !texture || !xr_strlen(texture) || w <= 0.f || h <= 0.f) return;

	SPointerOverride& ov = m_pointer_icons[shared_str(spot_type)];
	ov.texture = texture;
	ov.rect.set(x, y, x + w, y + h);
	clear_pool();
}

void CUIMiniMapWidget::clear_spot_textures()
{
	if (m_icons.empty()) return;

	m_icons.clear();
	clear_pool();
}

Fvector2 CUIMiniMapWidget::local_of(const Fvector& pos)
{
	Fvector2 res;
	res.set(0.0f, 0.0f);
	if (!m_has_map) return res;

	Fvector2 src;
	src.set(pos.x, pos.z);
	UI().m_bAspectNeutral = true;
	res = m_map->ConvertRealToLocal(src, true);
	UI().m_bAspectNeutral = false;
	res.add(m_map->GetWndPos());

	return res;
}

void CUIMiniMapWidget::scale_spot(CUIStatic* sp, float scale)
{
	if (scale <= 0.f || fsimilar(scale, 1.0f)) return;

	Fvector2 sz = sp->GetWndSize();
	sp->SetWndSize(Fvector2().set(sz.x * scale, sz.y * scale));
}

CUIMiniMapWidget::SPoolEntry* CUIMiniMapWidget::acquire(CMapLocation* loc)
{
	auto found = m_pool.find(loc);
	if (found != m_pool.end()) return &found->second;

	LPCSTR type = loc->CurrentSpotType();
	if (!type) return NULL;

	string512 spot_path, pointer_path;
	if (!GetMiniMapSpotPaths(type, spot_path, pointer_path)) return NULL;

	SPoolEntry e;
	e.pointer = NULL;
	e.pointer_path = xr_strlen(pointer_path) != 0;
	e.color = 0;
	e.stamp = m_stamp;

	e.spot = xr_new<CMiniMapSpot>(loc);
	e.spot->Load(GetSpotXml(), spot_path);
	e.spot->m_stat_hint_text = "";
	// a spot with a heading would keep its native pixel size, the screen needs the scaled window
	e.spot->SetStretchTexture(true);

	e.spot->SetIconShader(spot_shader());
	auto ov = m_icons.find(shared_str(type));
	if (ov != m_icons.end())
	{
		if (ov->second.texture.size())
		{
			e.spot->SetNormalIcon(ov->second.texture.c_str(), spot_shader());
			e.spot->SetWndSize(Fvector2().set(ov->second.width, ov->second.height));
		}
		if (ov->second.above.size() || ov->second.below.size())
			e.spot->SetHeightIcons(ov->second.above.c_str(), ov->second.below.c_str(), spot_shader());
	}
	scale_spot(e.spot, m_spot_scale);
	arm_spot_anim(e.spot, GetSpotXml(), spot_path);

	if (e.pointer_path && pointer_allowed(loc))
	{
		e.pointer = xr_new<CMapSpotPointer>(loc);
		e.pointer->Load(GetSpotXml(), pointer_path);
		e.pointer->m_stat_hint_text = "";
		e.pointer->SetStretchTexture(true);
		auto pov = m_pointer_icons.find(shared_str(type));
		if (pov != m_pointer_icons.end())
		{
			e.pointer->InitTextureEx(pov->second.texture.c_str(), spot_shader());
			e.pointer->SetTextureRect(pov->second.rect);
			e.pointer->SetWndSize(Fvector2().set(pov->second.rect.width(), pov->second.rect.height()));
		}
		else if (m_pointer_texture.size())
		{
			e.pointer->InitTextureEx(m_pointer_texture.c_str(), spot_shader());
			e.pointer->SetTextureRect(m_pointer_rect);
			e.pointer->SetWndSize(Fvector2().set(m_pointer_rect.width(), m_pointer_rect.height()));
		}
		else if (!e.pointer->m_TextureName.empty())
			e.pointer->InitTextureEx(e.pointer->m_TextureName.c_str(), spot_shader());
		scale_spot(e.pointer, (m_pointer_scale > 0.f) ? m_pointer_scale : m_spot_scale);
		arm_spot_anim(e.pointer, GetSpotXml(), pointer_path);
	}

	e.base_color = e.spot->GetTextureColor();

	return &m_pool.insert(std::make_pair(loc, e)).first->second;
}

void CUIMiniMapWidget::Update()
{
	if (!level_ready())
	{
		clear_pool();
		CUIWindow::Update();
		return;
	}

	if (m_update_interval && (Device.dwTimeGlobal - m_last_update) < m_update_interval)
	{
		prune_vanished();
		CUIWindow::Update();
		return;
	}
	m_last_update = Device.dwTimeGlobal;

	Frect frame;
	GetAbsoluteRect(frame);
	m_map->WorkingArea() = frame;
	if (frame.width() > 0.0f && frame.height() > 0.0f)
		SetClipRect(frame);

	m_map->DetachAll();

	UI().m_bAspectNeutral = true;
	++m_stamp;
	Locations& ls = Level().MapManager().Locations();
	for (Locations_it it = ls.begin(); it != ls.end(); ++it)
	{
		CMapLocation* loc = (*it).location;
		if (!loc) continue;

		// only the target is followed from another level, its pointer needs a graph path
		if (loc->GetLevelName() != m_map->MapName() && !(m_pointers_visible && loc->ObjectID() == m_pointer_target))
			continue;

		LPCSTR current_type = loc->CurrentSpotType();
		if (!m_hidden_types.empty() && current_type && m_hidden_types.find(shared_str(current_type)) != m_hidden_types.end())
			continue;

		SPoolEntry* e = acquire(loc);
		if (!e) continue;

		e->stamp = m_stamp;

		u32 want = e->base_color;
		auto clr = m_spot_colors.find(loc->ObjectID());
		if (clr != m_spot_colors.end())
			want = clr->second;

		if (want != e->color)
		{
			e->color = want;
			e->spot->SetTextureColor(want);
			if (e->pointer)
				e->pointer->SetTextureColor(want);
		}
	}

	for (auto it = m_pool.begin(); it != m_pool.end();)
	{
		if (it->second.stamp == m_stamp)
		{
			++it;
			continue;
		}
		release(it->second);
		it = m_pool.erase(it);
	}

	m_pointer_count = 0;
	for (auto it = m_pool.begin(); it != m_pool.end(); ++it)
	{
		it->first->UpdateMiniMap(m_map, it->second.spot, it->second.pointer);
		if (it->second.pointer && it->second.pointer->GetParent() == m_map)
			++m_pointer_count;
	}

	m_spot_count = (u32)m_map->GetChildNum();
	UI().m_bAspectNeutral = false;

	place_global();

	CUIWindow::Update();
}

void CUIMiniMapWidget::Draw()
{
	if (!level_ready())
	{
		clear_pool();
		return;
	}

	CUIWindow::Draw();
}
