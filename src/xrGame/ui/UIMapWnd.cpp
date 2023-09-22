#include "stdafx.h"
#include "UIMapWnd.h"
#include "UIMap.h"
#include "UIXmlInit.h"

#include "../Actor.h"
#include "../map_manager.h"
#include "UIInventoryUtilities.h"
#include "../map_spot.h"
#include "../map_location.h"

#include "UIFixedScrollBar.h"
#include "UIFrameWindow.h"
#include "UIFrameLineWnd.h"
#include "UITabControl.h"
#include "UI3tButton.h"
#include "UIMapWndActions.h"
#include "UIMapWndActionsSpace.h"
#include "UIHint.h"
#include "map_hint.h"
#include "uicursor.h"

#include "UIPropertiesBox.h"
#include "UIListBoxItem.h"

#include "../../xrEngine/xr_input.h"		//remove me !!!

CUIMapWnd* g_map_wnd = NULL; // quick temporary solution -(
CUIMapWnd* GetMapWnd()
{
	return g_map_wnd;
}

CUIMapWnd::CUIMapWnd()
{
	m_tgtMap = NULL;
	m_GlobalMap = NULL;
	m_view_actor = false;
	m_prev_actor_pos.set(0, 0);
	m_currentZoom = 1.0f;
	m_map_location_hint = NULL;
	m_map_move_step = 10.0f;
	/*
	#ifdef DEBUG
	//	m_dbg_text_hint			= NULL;
	//	m_dbg_info				= NULL;
	#endif // DEBUG /**/

	//	UIMainMapHeader			= NULL;
	m_scroll_mode = false;
	m_nav_timing = Device.dwTimeGlobal;
	hint_wnd = NULL;
	g_map_wnd = this;
}

CUIMapWnd::~CUIMapWnd()
{
	delete_data(m_ActionPlanner);
	delete_data(m_GameMaps);
	delete_data(m_map_location_hint);
	/*
	#ifdef DEBUG
		delete_data( m_dbg_text_hint );
		delete_data( m_dbg_info );
	#endif // DEBUG/**/
	g_map_wnd = NULL;
}


void CUIMapWnd::Init(LPCSTR xml_name, LPCSTR start_from)
{
	CUIXml uiXml;
	uiXml.Load(CONFIG_PATH, UI_PATH, xml_name);

	string512 pth;
	CUIXmlInit xml_init;
	strconcat(sizeof(pth), pth, start_from, ":main_wnd");
	xml_init.InitWindow(uiXml, pth, 0, this);

	m_map_move_step = uiXml.ReadAttribFlt(start_from, 0, "map_move_step", 10.0f);

	m_UILevelFrame = xr_new<CUIWindow>();
	m_UILevelFrame->SetAutoDelete(true);
	strconcat(sizeof(pth), pth, start_from, ":level_frame");
	xml_init.InitWindow(uiXml, pth, 0, m_UILevelFrame);
	//	m_UIMainFrame->AttachChild		(m_UILevelFrame);
	AttachChild(m_UILevelFrame);

	m_UIMainFrame = xr_new<CUIFrameWindow>();
	m_UIMainFrame->SetAutoDelete(true);
	AttachChild(m_UIMainFrame);
	strconcat(sizeof(pth), pth, start_from, ":main_map_frame");
	xml_init.InitFrameWindow(uiXml, pth, 0, m_UIMainFrame);

	m_scroll_mode = (uiXml.ReadAttribInt(start_from, 0, "scroll_enable", 0) == 1) ? true : false;
	if (m_scroll_mode)
	{
		float dx, dy, sx, sy;
		strconcat(sizeof(pth), pth, start_from, ":main_map_frame");
		dx = uiXml.ReadAttribFlt(pth, 0, "dx", 0.0f);
		dy = uiXml.ReadAttribFlt(pth, 0, "dy", 0.0f);
		sx = uiXml.ReadAttribFlt(pth, 0, "sx", 5.0f);
		sy = uiXml.ReadAttribFlt(pth, 0, "sy", 5.0f);

		CUIWindow* rect_parent = m_UIMainFrame; //m_UILevelFrame;
		Frect r = rect_parent->GetWndRect();

		m_UIMainScrollH = xr_new<CUIFixedScrollBar>();
		m_UIMainScrollH->SetAutoDelete(true);
		m_UIMainScrollH->InitScrollBar(Fvector2().set(r.left + dx, r.bottom - sy), true);
		m_UIMainScrollH->SetStepSize(_max(1, (int)(m_UILevelFrame->GetWidth() * 0.1f)));
		m_UIMainScrollH->SetPageSize((int)m_UILevelFrame->GetWidth()); // iFloor
		AttachChild(m_UIMainScrollH);
		Register(m_UIMainScrollH);
		AddCallback(m_UIMainScrollH, SCROLLBAR_HSCROLL, CUIWndCallback::void_function(this, &CUIMapWnd::OnScrollH));

		m_UIMainScrollV = xr_new<CUIFixedScrollBar>();
		m_UIMainScrollV->SetAutoDelete(true);
		m_UIMainScrollV->InitScrollBar(Fvector2().set(r.right - sx, r.top + dy), false);
		m_UIMainScrollV->SetStepSize(_max(1, (int)(m_UILevelFrame->GetHeight() * 0.1f)));
		m_UIMainScrollV->SetPageSize((int)m_UILevelFrame->GetHeight());
		AttachChild(m_UIMainScrollV);
		Register(m_UIMainScrollV);
		AddCallback(m_UIMainScrollV, SCROLLBAR_VSCROLL, CUIWndCallback::void_function(this, &CUIMapWnd::OnScrollV));
	}

	m_map_location_hint = xr_new<CUIMapLocationHint>();
	strconcat(sizeof(pth), pth, start_from, ":map_hint_item");
	m_map_location_hint->Init(uiXml, pth);
	m_map_location_hint->SetAutoDelete(false);

	// Load maps

	m_GlobalMap = xr_new<CUIGlobalMap>(this);
	m_GlobalMap->SetAutoDelete(true);
	m_GlobalMap->Initialize();

	m_UILevelFrame->AttachChild(m_GlobalMap);
	m_GlobalMap->OptimalFit(m_UILevelFrame->GetWndRect());
	m_GlobalMap->SetMinZoom(m_GlobalMap->GetCurrentZoom().x);
	m_currentZoom = m_GlobalMap->GetCurrentZoom().x;

	init_xml_nav(uiXml);

	// initialize local maps
	xr_string sect_name;
	if (IsGameTypeSingle())
		sect_name = "level_maps_single";
	else
		sect_name = "level_maps_mp";

	if (pGameIni->section_exist(sect_name.c_str()))
	{
		CInifile::Sect& S = pGameIni->r_section(sect_name.c_str());
		CInifile::SectCIt it = S.Data.begin(), end = S.Data.end();
		for (; it != end; it++)
		{
			shared_str map_name = it->first;
			xr_strlwr(map_name);
			R_ASSERT2(m_GameMaps.end() == m_GameMaps.find(map_name), "Duplicate level name not allowed");

			CUICustomMap*& l = m_GameMaps[map_name];

			l = xr_new<CUILevelMap>(this);
			R_ASSERT2(pGameIni->section_exist(map_name), map_name.c_str());
			l->Initialize(map_name, "hud\\default");

			l->OptimalFit(m_UILevelFrame->GetWndRect());
		}
	}

#ifdef DEBUG
	GameMaps::iterator it = m_GameMaps.begin();
	GameMaps::iterator it2;
	for(;it!=m_GameMaps.end();++it){
		CUILevelMap* l = smart_cast<CUILevelMap*>(it->second);VERIFY(l);
		for(it2=it; it2!=m_GameMaps.end();++it2){
			if(it==it2) continue;
			CUILevelMap* l2 = smart_cast<CUILevelMap*>(it2->second);VERIFY(l2);
			if(l->GlobalRect().intersected(l2->GlobalRect())){
				Msg(" --error-incorrect map definition global rect of map [%s] intersects with [%s]", *l->MapName(), *l2->MapName());
			}
		}
		if(FALSE == l->GlobalRect().intersected(GlobalMap()->BoundRect())){
			Msg(" --error-incorrect map definition map [%s] places outside global map", *l->MapName());
		}

	}
#endif

	Register(m_GlobalMap);
	m_ActionPlanner = xr_new<CMapActionPlanner>();
	m_ActionPlanner->setup(this);
	m_view_actor = true;

	m_UIPropertiesBox = xr_new<CUIPropertiesBox>();
	m_UIPropertiesBox->SetAutoDelete(true);
	m_UIPropertiesBox->InitPropertiesBox(Fvector2().set(0, 0), Fvector2().set(300, 300));
	AttachChild(m_UIPropertiesBox);
	m_UIPropertiesBox->Hide();
	m_UIPropertiesBox->SetWindowName("property_box");
}

void CUIMapWnd::Show(bool status)
{
	inherited::Show(status);
	Activated();
	if (GlobalMap())
	{
		m_GlobalMap->DetachAll();
		m_GlobalMap->Show(false);
	}
	GameMaps::iterator it = m_GameMaps.begin();
	for (; it != m_GameMaps.end(); ++it)
	{
		it->second->DetachAll();
	}

	if (status)
	{
		m_GlobalMap->Show(true);
		m_GlobalMap->WorkingArea().set(ActiveMapRect());
		GameMaps::iterator it = m_GameMaps.begin();
		GameMaps::iterator it_e = m_GameMaps.end();
		for (; it != it_e; ++it)
		{
			m_GlobalMap->AttachChild(it->second);
			it->second->Show(true);
			it->second->WorkingArea().set(ActiveMapRect());
		}

		if (m_view_actor)
		{
			inherited::Update(); // only maps, not action planner
			ViewActor();
			m_view_actor = false;
		}
		InventoryUtilities::SendInfoToActor("ui_pda_map_local");
	}
	HideCurHint();
}

void CUIMapWnd::Activated()
{
	Fvector v = Level().CurrentEntity()->Position();
	Fvector2 v2;
	v2.set(v.x, v.z);
	if (v2.distance_to(m_prev_actor_pos) > 3.0f)
	{
		ViewActor();
	}
}

void CUIMapWnd::AddMapToRender(CUICustomMap* m)
{
	Register(m);
	m_UILevelFrame->AttachChild(m);
	m->Show(true);
	m->WorkingArea().set(ActiveMapRect());
}

void CUIMapWnd::RemoveMapToRender(CUICustomMap* m)
{
	if (m != GlobalMap())
		m_UILevelFrame->DetachChild(smart_cast<CUIWindow*>(m));
}

void CUIMapWnd::SetTargetMap(const shared_str& name, const Fvector2& pos, bool bZoomIn)
{
	u16 idx = GetIdxByName(name);
	if (idx != u16(-1))
	{
		CUICustomMap* lm = GetMapByIdx(idx);
		SetTargetMap(lm, pos, bZoomIn);
	}
}

void CUIMapWnd::SetTargetMap(const shared_str& name, bool bZoomIn)
{
	u16 idx = GetIdxByName(name);
	if (idx != u16(-1))
	{
		CUICustomMap* lm = GetMapByIdx(idx);
		SetTargetMap(lm, bZoomIn);
	}
}

void CUIMapWnd::SetTargetMap(CUICustomMap* m, bool bZoomIn)
{
	m_tgtMap = m;
	Fvector2 pos;
	Frect r = m->BoundRect();
	r.getcenter(pos);
	SetTargetMap(m, pos, bZoomIn);
}

void CUIMapWnd::SetTargetMap(CUICustomMap* m, const Fvector2& pos, bool bZoomIn)
{
	m_tgtMap = m;

	if (m == GlobalMap())
	{
		CUIGlobalMap* gm = GlobalMap();
		SetZoom(gm->GetMinZoom());
		Frect vis_rect = ActiveMapRect();
		vis_rect.getcenter(m_tgtCenter);
		Fvector2 _p;
		gm->GetAbsolutePos(_p);
		m_tgtCenter.sub(_p);
		m_tgtCenter.div(gm->GetCurrentZoom());
	}
	else
	{
		if (bZoomIn/* && fsimilar(GlobalMap()->GetCurrentZoom(), GlobalMap()->GetMinZoom(),EPS_L )*/)
			SetZoom(GlobalMap()->GetMaxZoom());

		//		m_tgtCenter						= m->ConvertRealToLocalNoTransform(pos, m->BoundRect());
		m_tgtCenter = m->ConvertRealToLocal(pos, true);
		m_tgtCenter.add(m->GetWndPos()).div(GlobalMap()->GetCurrentZoom());
	}
	ResetActionPlanner();
}

void CUIMapWnd::MoveMap(Fvector2 const& pos_delta)
{
	GlobalMap()->MoveWndDelta(pos_delta);
	UpdateScroll();
	HideCurHint();
}

void CUIMapWnd::Draw()
{
	inherited::Draw();
	/*
	#ifdef DEBUG
		m_dbg_text_hint->Draw	();
		m_dbg_info->Draw		();
	#endif // DEBUG/**/

	m_btn_nav_parent->Draw();
}

void CUIMapWnd::MapLocationRelcase(CMapLocation* ml)
{
	CUIWindow* owner = m_map_location_hint->GetOwner();
	if (owner)
	{
		CMapSpot* ms = smart_cast<CMapSpot*>(owner);
		if (ms && ms->MapLocation() == ml) //CUITaskItem also can be a HintOwner
			m_map_location_hint->SetOwner(NULL);
	}
}

void CUIMapWnd::DrawHint()
{
	CUIWindow* owner = m_map_location_hint->GetOwner();
	if (owner)
	{
		CMapSpot* ms = smart_cast<CMapSpot*>(owner);
		if (ms)
		{
			if (ms->MapLocation() && ms->MapLocation()->HintEnabled())
			{
				m_map_location_hint->Draw_();
			}
		}
		else
		{
			m_map_location_hint->Draw_();
		}
	}
}

bool CUIMapWnd::OnKeyboardHold(int dik)
{
	switch (dik)
	{
	case DIK_UP:
	case DIK_DOWN:
	case DIK_LEFT:
	case DIK_RIGHT:
		{
			Fvector2 pos_delta;
			pos_delta.set(0.0f, 0.0f);

			if (dik == DIK_UP) pos_delta.y += m_map_move_step;
			if (dik == DIK_DOWN) pos_delta.y -= m_map_move_step;
			if (dik == DIK_LEFT) pos_delta.x += m_map_move_step;
			if (dik == DIK_RIGHT) pos_delta.x -= m_map_move_step;
			MoveMap(pos_delta);
			return true;
		}
		break;
	}
	return inherited::OnKeyboardHold(dik);
}

bool CUIMapWnd::OnKeyboardAction(int dik, EUIMessages keyboard_action)
{
	switch (dik)
	{
	case DIK_NUMPADMINUS:
		{
			//SetZoom(GetZoom()/1.5f);
			UpdateZoom(false);
			//ResetActionPlanner();
			return true;
		}
		break;
	case DIK_NUMPADPLUS:
		{
			//SetZoom(GetZoom()*1.5f);
			UpdateZoom(true);
			//ResetActionPlanner();
			return true;
		}
		break;
	}

	return inherited::OnKeyboardAction(dik, keyboard_action);
}

bool CUIMapWnd::OnMouseAction(float x, float y, EUIMessages mouse_action)
{
	if (inherited::OnMouseAction(x, y, mouse_action) /*|| m_btn_nav_parent->OnMouseAction(x,y,mouse_action)*/)
	{
		return true;
	}

	Fvector2 cursor_pos1 = GetUICursor().GetCursorPosition();

	if (GlobalMap() && !GlobalMap()->Locked() && ActiveMapRect().in(cursor_pos1))
	{
		switch (mouse_action)
		{
		case WINDOW_RBUTTON_UP:
			ActivatePropertiesBox(NULL);
			break;
		case WINDOW_MOUSE_MOVE:
			if (pInput->iGetAsyncBtnState(0))
			{
				GlobalMap()->MoveWndDelta(GetUICursor().GetCursorPositionDelta());
				UpdateScroll();
				HideCurHint();
				return true;
			}
			break;

		case WINDOW_MOUSE_WHEEL_DOWN:
			UpdateZoom(true, true);
			return true;
			break;
		case WINDOW_MOUSE_WHEEL_UP:
			UpdateZoom(false, true);
			return true;
			break;
		} //switch	
	};

	return false;
}

// demonized: Zoom towards mouse cursor instead of map center
BOOL pda_map_zoom_in_to_mouse = TRUE;
BOOL pda_map_zoom_out_to_mouse = TRUE;
bool CUIMapWnd::UpdateZoom(bool b_zoom_in, bool b_scroll_wheel)
{
	auto before_mouse_pos = GetGlobalMapCoordsForMouse();
	before_mouse_pos.mul(-1);
	float prev_zoom = GetZoom();
	float z = 0.0f;
	if (b_zoom_in)
	{
		z = GetZoom() * 1.2f;
		SetZoom(z);
	}
	else
	{
		z = GetZoom() / 1.2f;
		SetZoom(z);
	}

	if (b_scroll_wheel && (pda_map_zoom_in_to_mouse || pda_map_zoom_out_to_mouse))
	{
		if (!fsimilar(prev_zoom, GetZoom()))
		{
			Frect vis_rect = ActiveMapRect();
			vis_rect.getcenter(m_tgtCenter);

			Fvector2 pos;
			CUIGlobalMap* gm = GlobalMap();
			gm->GetAbsolutePos(pos);
			m_tgtCenter.sub(pos);
			m_tgtCenter.div(gm->GetCurrentZoom());

			// Zoom towards mouse
			if (b_zoom_in && pda_map_zoom_in_to_mouse) {
				auto temp = m_tgtCenter;
				temp.add(before_mouse_pos).div(2).sub(m_tgtCenter);
				m_tgtCenter.add(temp);
			} else if (!b_zoom_in && pda_map_zoom_out_to_mouse) {
				auto temp = m_tgtCenter;
				temp.add(before_mouse_pos).div(2).sub(m_tgtCenter).mul(-1);
				m_tgtCenter.add(temp);
			}

			ResetActionPlanner();
			HideCurHint();
			return false;
		}
	} else {
		if (!fsimilar(prev_zoom, GetZoom()))
		{
			//		m_tgtCenter.set( 0, 0 );// = cursor_pos;
			Frect vis_rect = ActiveMapRect();
			vis_rect.getcenter(m_tgtCenter);

			Fvector2 pos;
			CUIGlobalMap* gm = GlobalMap();
			gm->GetAbsolutePos(pos);
			m_tgtCenter.sub(pos);
			m_tgtCenter.div(gm->GetCurrentZoom());

			ResetActionPlanner();
			HideCurHint();
			return false;
		}
	}
	return true;
}

void CUIMapWnd::SendMessage(CUIWindow* pWnd, s16 msg, void* pData)
{
	//	inherited::SendMessage( pWnd, msg, pData);
	CUIWndCallback::OnEvent(pWnd, msg, pData);
	if (pWnd == m_UIPropertiesBox && msg == PROPERTY_CLICKED && m_UIPropertiesBox->GetClickedItem())
	{
		luabind::functor<void> funct;
		if (ai().script_engine().functor("pda.property_box_clicked", funct))
			funct(m_UIPropertiesBox);
	}
}

// demonized: get global map coords under mouse cursor
Fvector2 CUIMapWnd::GetGlobalMapCoordsForMouse()
{
	auto gm = GlobalMap();

	// 1. Get cursor position in map space
	// Normalize mouse coordinates in map canvas
	Fvector2 pos_abs = { 0, 0 };
	auto cursor_pos = GetUICursor().GetCursorPosition();
	cursor_pos.sub(ActiveMapRect().lt);

	// Invert mouse coords
	cursor_pos.mul(-1);

	// Get absolute left top of the current area of the map
	Fvector2 map_abs = { 0, 0 };
	Fvector2& current_zoom = gm->GetCurrentZoom();
	gm->GetAbsolutePos(map_abs);
	map_abs.sub(gm->WorkingArea().lt);
	map_abs.div(current_zoom);

	// Increment to mouse coordinates
	pos_abs.add(map_abs);
	pos_abs.add(cursor_pos.div(current_zoom));
	return pos_abs;
}

void CUIMapWnd::ActivatePropertiesBox(CUIWindow* w)
{
	m_UIPropertiesBox->RemoveAll();
	luabind::functor<void> funct;
	CMapSpot* sp = nullptr;
	if (ai().script_engine().functor("pda.property_box_add_properties", funct))
	{
		sp = smart_cast<CMapSpot*>(w);
		if (sp)
			funct(m_UIPropertiesBox, sp->MapLocation()->ObjectID(), (LPCSTR)sp->MapLocation()->GetLevelName().c_str(),
			      (LPCSTR)sp->MapLocation()->GetHint());
	}

	// demonized: possibility to click trigger properties box anywhere on the map with right click
	luabind::functor<void> rcFunct;
	if (ai().script_engine().functor("_G.COnRightClickMap", rcFunct))
	{
		auto gm = GlobalMap();

		// 1. Get cursor position in map space
		// Normalize mouse coordinates in map canvas
		Fvector2 cursor_pos = GetUICursor().GetCursorPosition();
		cursor_pos.sub(ActiveMapRect().lt);

		// Invert mouse coords
		cursor_pos.mul(-1);

		// Divide by zoom level
		Fvector2 current_zoom = gm->GetCurrentZoom();
		cursor_pos.div(current_zoom);

		// Get absolute left top of the current area of the map
		Fvector2 map_abs = { 0, 0 };
		gm->GetAbsolutePos(map_abs);
		map_abs.sub(gm->WorkingArea().lt).div(current_zoom);

		// Increment to mouse coordinates
		Fvector2 pos_abs = { 0, 0 };
		pos_abs.add(map_abs).add(cursor_pos);

		// 2. Get map position from global position
		if (gm->hoveredMap) {

			// Get position of the local map on global map
			auto lm = gm->hoveredMap;
			Frect lm_rect;
			lm_rect.set(0, 0, 0, 0);
			lm->GetAbsoluteRect(lm_rect);

			// Normalize local map coordinates
			Frect lm_wa = lm->WorkingArea();
			lm_rect.lt.sub(lm_wa.lt);
			lm_rect.rb.sub(lm_wa.rb);

			// Divide by local map zoom level
			Fvector2 lm_zoom = lm->GetCurrentZoom();
			lm_rect.lt.div(lm_zoom);
			lm_rect.rb.div(lm_zoom);

			// Get cursor coordinates
			Fvector2 lm_cursor_pos = GetUICursor().GetCursorPosition();
			lm_cursor_pos.sub(ActiveMapRect().lt);

			// Invert cursor coords
			lm_cursor_pos.mul(-1);

			// Divide by local map zoom level
			lm_cursor_pos.div(lm_zoom);

			// Get local map bounding rect
			Frect lm_bound_rect = lm->BoundRect();
			Fvector2 lm_bound_size = { 0, 0 };
			lm_bound_rect.getsize(lm_bound_size);

			// Get mouse coordinates relative to local map left top
			Fvector2 lm_mouse_pos = { 0, 0 };
			lm_mouse_pos.add(lm_rect.lt).add(lm_cursor_pos);
			
			// Adjust local map rect based on bounding rect
			lm_rect.rb.set(Fvector2().set(lm_rect.lt).sub(lm_bound_size));

			// Adjust mouse coordinates for getting real world coordinates
			Fvector2 lm_real_mouse_pos = { 0, 0 };
			Fvector2 lm_adjusted_mouse_pos = { 0, 0 };
			lm_adjusted_mouse_pos.add(lm_mouse_pos).mul(-1).mul(lm_zoom);
			
			// Get real world coordinates
			lm_real_mouse_pos.x = lm_bound_rect.lt.x + lm_adjusted_mouse_pos.x / lm_zoom.x;
			lm_real_mouse_pos.y = lm_bound_rect.height() + lm_bound_rect.lt.y - lm_adjusted_mouse_pos.y / lm_zoom.x;
			lm_real_mouse_pos.x /= UI().get_current_kx();

			// Get map name
			LPCSTR lm_name = lm->MapName().c_str();

			// Get Y component of real world coordinates - raycast. Only on current level
			// On other levels y will be taken from lvid
			// Get accurate lvid as well
			Fvector lm_real_pos = { lm_real_mouse_pos.x, 0, lm_real_mouse_pos.y };
			u32 lm_lvid = u32(-1);
			if (0 == xr_strcmp(g_pGameLevel->name().c_str(), lm_name)) {

				// perform ray cast to get actual position
				collide::rq_result R;
				CObject* ignore = Actor();
				Fbox lm_bv = g_pGameLevel->ObjectSpace.GetBoundingVolume();
				Fvector start = { lm_real_mouse_pos.x, lm_bv.max.y, lm_real_mouse_pos.y };
				Fvector dir = { 0, -1, 0 };
				float range = lm_bv.max.y - lm_bv.min.y;
				Fvector res;
				if (Level().ObjectSpace.RayPick(start, dir, range, collide::rqtStatic, R, ignore))
				{
					res.mad(start, dir, R.range);
					lm_real_pos.y = res.y;
					lm_lvid = ai().level_graph().vertex_id(lm_real_pos);
				}
			}

			bool lvid_not_set = lm_lvid == u32(-1);
			u16 lm_gvid = u16(-1);

			// Get gvid and set lvid if wasnt set
			auto& gg = ai().game_graph();
			u32 current_gvid = 0;
			float dist = FLT_MAX;
			while (gg.valid_vertex_id(current_gvid)) {
				auto vertex = gg.vertex(current_gvid);
				if (!vertex) {
					current_gvid++;
					continue;
				}
				GameGraph::_LEVEL_ID level_id = vertex->level_id();
				const GameGraph::LEVEL_MAP& levels = ai().game_graph().header().levels();
				GameGraph::LEVEL_MAP::const_iterator I = levels.find(level_id);
				if (I == levels.end()) {
					current_gvid++;
					continue;
				}
				LPCSTR level_name = I->second.name().c_str();
				if (0 == xr_strcmp(level_name, lm_name)) {
					Fvector3 pos = vertex->level_point();
					float d = lm_real_pos.distance_to_xz_sqr(pos);
					if (d < dist) {
						dist = d;
						lm_gvid = current_gvid;
						if (lvid_not_set) {
							lm_lvid = vertex->level_vertex_id();
							Fvector3 lvid_pos = ai().level_graph().vertex_position(lm_lvid);
							lm_real_pos.y = lvid_pos.y;
						}
					}
				}
				current_gvid++;
			}

			luabind::object table = luabind::newtable(ai().script_engine().lua());
			/*table["gm_cursor_pos"] = cursor_pos;
			table["gm_map_abs"] = map_abs;
			table["lm_bound_rect"] = lm_bound_rect;
			table["lm_bound_size"] = lm_bound_size;
			table["gm_zoom"] = current_zoom;
			table["lm_rect"] = lm_rect;
			table["lm_zoom"] = lm_zoom;
			table["lm_mouse_pos"] = lm_mouse_pos;
			table["lm_mouse_pos_adjusted"] = lm_adjusted_mouse_pos;
			table["lm_mouse_pos_real"] = lm_real_mouse_pos;*/
			table["global_map_pos"] = pos_abs;
			table["pos"] = lm_real_pos;
			table["level_name"] = lm_name;
			table["object_id"] = sp ? sp->MapLocation()->ObjectID() : 65535;
			table["hint"] = sp ? sp->MapLocation()->GetHint() : NULL;
			table["lvid"] = lm_lvid;
			table["gvid"] = lm_gvid;

			// 3. Lua
			rcFunct(m_UIPropertiesBox, table);
		}
	}


	if (m_UIPropertiesBox->GetItemsCount() > 0)
	{
		m_UIPropertiesBox->AutoUpdateSize();

		Fvector2 cursor_pos;
		Frect vis_rect;

		GetAbsoluteRect(vis_rect);
		cursor_pos = GetUICursor().GetCursorPosition();
		cursor_pos.sub(vis_rect.lt);
		m_UIPropertiesBox->Show(vis_rect, cursor_pos);
	}
}

CUICustomMap* CUIMapWnd::GetMapByIdx(u16 idx)
{
	VERIFY(idx!=u16(-1));
	GameMapsPairIt it = m_GameMaps.begin();
	std::advance(it, idx);
	return it->second;
}

u16 CUIMapWnd::GetIdxByName(const shared_str& map_name)
{
	GameMapsPairIt it = m_GameMaps.find(map_name);
	if (it == m_GameMaps.end())
	{
		Msg("~ Level Map '%s' not registered", map_name.c_str());
		return u16(-1);
	}
	return (u16)std::distance(m_GameMaps.begin(), it);
}

void CUIMapWnd::UpdateScroll()
{
	if (m_scroll_mode)
	{
		Fvector2 w_pos = GlobalMap()->GetWndPos();
		m_UIMainScrollV->SetRange(m_UIMainScrollV->GetMinRange(), iFloor(GlobalMap()->GetHeight()));
		m_UIMainScrollH->SetRange(m_UIMainScrollV->GetMinRange(), iFloor(GlobalMap()->GetWidth()));

		m_UIMainScrollV->SetScrollPos(iFloor(-w_pos.y));
		m_UIMainScrollH->SetScrollPos(iFloor(-w_pos.x));
	}
}

void CUIMapWnd::OnScrollV(CUIWindow*, void*)
{
	if (m_scroll_mode && GlobalMap())
	{
		MoveScrollV(-1.0f * float(m_UIMainScrollV->GetScrollPos()));
	}
}

void CUIMapWnd::OnScrollH(CUIWindow*, void*)
{
	if (m_scroll_mode && GlobalMap())
	{
		MoveScrollH(-1.0f * float(m_UIMainScrollH->GetScrollPos()));
	}
}

void CUIMapWnd::MoveScrollV(float dy)
{
	Fvector2 w_pos = GlobalMap()->GetWndPos();
	GlobalMap()->SetWndPos(Fvector2().set(w_pos.x, dy));
}

void CUIMapWnd::MoveScrollH(float dx)
{
	Fvector2 w_pos = GlobalMap()->GetWndPos();
	GlobalMap()->SetWndPos(Fvector2().set(dx, w_pos.y));
}

void CUIMapWnd::Update()
{
	if (m_GlobalMap)
		m_GlobalMap->WorkingArea().set(ActiveMapRect());
	inherited::Update();
	m_ActionPlanner->update();
	UpdateNav();
}

void CUIMapWnd::SetZoom(float value)
{
	m_currentZoom = value;
	clamp(m_currentZoom, GlobalMap()->GetMinZoom(), GlobalMap()->GetMaxZoom());
}

void CUIMapWnd::ViewGlobalMap()
{
	if (GlobalMap()->Locked()) return;
	SetTargetMap(GlobalMap());
}

void CUIMapWnd::ResetActionPlanner()
{
	m_ActionPlanner->m_storage.set_property(1, false);
	m_ActionPlanner->m_storage.set_property(2, false);
	m_ActionPlanner->m_storage.set_property(3, false);
}

void CUIMapWnd::ViewZoomIn()
{
	if (GlobalMap()->Locked()) return;
	UpdateZoom(true);
}

void CUIMapWnd::ViewZoomOut()
{
	if (GlobalMap()->Locked()) return;
	UpdateZoom(false);
}

void CUIMapWnd::ViewActor()
{
	if (GlobalMap()->Locked()) return;

	Fvector v = Level().CurrentEntity()->Position();
	m_prev_actor_pos.set(v.x, v.z);

	CUICustomMap* lm = NULL;
	u16 idx = GetIdxByName(Level().name());
	if (idx != u16(-1))
	{
		lm = GetMapByIdx(idx);
	}
	else
	{
		lm = GlobalMap();
	}

	SetTargetMap(lm, m_prev_actor_pos, true);
}

void CUIMapWnd::ShowHintStr(CUIWindow* parent, LPCSTR text) //map name
{
	if (m_map_location_hint->GetOwner())
		return;

	m_map_location_hint->SetInfoStr(text);
	m_map_location_hint->SetOwner(parent);
	ShowHint();
}

void CUIMapWnd::ShowHintSpot(CMapSpot* spot)
{
	CUIWindow* owner = m_map_location_hint->GetOwner();
	if (!owner)
	{
		m_map_location_hint->SetInfoMSpot(spot);
		m_map_location_hint->SetOwner(spot);
		ShowHint();
		return;
	}

	CMapSpot* prev_spot = smart_cast<CMapSpot*>(owner);
	if (prev_spot && (prev_spot->get_location_level() < spot->get_location_level()))
	{
		m_map_location_hint->SetInfoMSpot(spot);
		m_map_location_hint->SetOwner(spot);
		ShowHint();
		return;
	}
}

void CUIMapWnd::ShowHintTask(CGameTask* task, CUIWindow* owner)
{
	if (task)
	{
		m_map_location_hint->SetInfoTask(task);
		m_map_location_hint->SetOwner(owner);
		ShowHint(true);
		return;
	}
	HideCurHint();
}

void CUIMapWnd::ShowHint(bool extra)
{
	Frect vis_rect;
	if (extra)
	{
		vis_rect.set(Frect().set(0.0f, 0.0f, UI_BASE_WIDTH, UI_BASE_HEIGHT));
	}
	else
	{
		vis_rect = ActiveMapRect();
	}

	bool is_visible = fit_in_rect(m_map_location_hint, vis_rect);
	if (!is_visible)
	{
		HideCurHint();
	}
}

void CUIMapWnd::HideHint(CUIWindow* parent)
{
	if (m_map_location_hint->GetOwner() == parent)
	{
		HideCurHint();
	}
}

void CUIMapWnd::HideCurHint()
{
	m_map_location_hint->SetOwner(NULL);
}

void CUIMapWnd::Hint(const shared_str& text)
{
	/*
#ifdef DEBUG
	m_dbg_text_hint->SetTextST( *text );
#endif // DEBUG/**/
}

void CUIMapWnd::Reset()
{
	inherited::Reset();
	ResetActionPlanner();
}

#include "../gametaskmanager.h"
#include "../actor.h"
#include "../map_spot.h"
#include "../gametask.h"

void CUIMapWnd::SpotSelected(CUIWindow* w)
{
	CMapSpot* sp = smart_cast<CMapSpot*>(w);
	if (!sp)
	{
		return;
	}

	CGameTask* t = Level().GameTaskManager().HasGameTask(sp->MapLocation(), true);
	if (t)
	{
		Level().GameTaskManager().SetActiveTask(t);
	}
}
