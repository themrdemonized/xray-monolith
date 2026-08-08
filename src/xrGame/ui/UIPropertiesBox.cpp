#include "stdafx.h"
#include "UIPropertiesBox.h"
#include "../level.h"
#include "UIListBoxItem.h"
#include "UIXmlInit.h"
#include "uicursor.h"

#define OFFSET_X (5.0f)
#define OFFSET_Y (5.0f)
#define ITEM_HEIGHT (GetFont()->CurrentHeight()+2.0f)

CUIPropertiesBox::CUIPropertiesBox()
{
	m_UIListWnd.SetFont(UI().Font().pFontArial14);
	m_UIListWnd.SetImmediateSelection(true);

	m_active_submenu = NULL;
	m_active_sub_item = NULL;
	m_parent_menu = NULL;
	m_submenu_close_at = 0;
}

CUIPropertiesBox::~CUIPropertiesBox()
{
	m_item_submenus.clear();
}


void CUIPropertiesBox::InitPropertiesBox(Fvector2 pos, Fvector2 size)
{
	inherited::SetWndPos(pos);
	inherited::SetWndSize(size);

	AttachChild(&m_UIListWnd);

	CUIXml xml_doc;
	xml_doc.Load(CONFIG_PATH, UI_PATH, "actor_menu.xml");

	LPCSTR t = xml_doc.Read("properties_box:texture", 0, "");
	R_ASSERT(t);
	InitTexture(t);

	CUIXmlInit::InitListBox(xml_doc, "properties_box:list", 0, &m_UIListWnd);

	m_UIListWnd.SetWndPos(Fvector2().set(OFFSET_X, OFFSET_Y));
	m_UIListWnd.SetWndSize(Fvector2().set(size.x - OFFSET_X * 2, size.y - OFFSET_Y * 2));
}

void CUIPropertiesBox::SendMessage(CUIWindow* pWnd, s16 msg, void* pData)
{
	if (pWnd == &m_UIListWnd)
	{
		if (msg == LIST_ITEM_CLICKED)
		{
			CUIListBoxItem* sel = m_UIListWnd.GetSelectedItem();
			if (sel && m_item_submenus.count(sel))
				return;
			GetMessageTarget()->SendMessage(this, PROPERTY_CLICKED);
			Hide();
		}
	}
	else if (msg == PROPERTY_CLICKED)
	{
		for (auto& kv : m_item_submenus)
		{
			if (kv.second == pWnd)
			{
				GetMessageTarget()->SendMessage(this, PROPERTY_CLICKED);
				Hide();
				break;
			}
		}
	}
	CUIWndCallback::OnEvent(pWnd, msg, pData);
	inherited::SendMessage(pWnd, msg, pData);
}

CUIPropertiesBox* CUIPropertiesBox::AddSubmenu(LPCSTR label)
{
	R_ASSERT2(GetParent(), "AddSubmenu: menu must be attached before adding submenus");

	string512 buff;
	xr_sprintf(buff, "%s  >", label);
	CUIListBoxItem* itm = m_UIListWnd.AddTextItem(buff);

	CUIPropertiesBox* sub = xr_new<CUIPropertiesBox>();
	sub->SetAutoDelete(true);
	sub->InitPropertiesBox(Fvector2().set(0, 0), Fvector2().set(GetWidth(), 300.0f));
	sub->m_parent_menu = this;
	sub->SetMessageTarget(this);
	GetParent()->AttachChild(sub);
	sub->Hide();

	m_item_submenus[itm] = sub;

	return sub;
}

void CUIPropertiesBox::ShowSubMenuForItem(CUIListBoxItem* item)
{
	auto it = m_item_submenus.find(item);
	if (it == m_item_submenus.end())
		return;
	CUIPropertiesBox* sub = it->second;

	Frect item_rect, parent_rect;
	item->GetAbsoluteRect(item_rect);
	GetParent()->GetAbsoluteRect(parent_rect);

	float my_left = GetWndPos().x;
	float my_right = my_left + GetWidth();
	float top = item_rect.y1 - parent_rect.y1;

	float anchor_x = (my_right + sub->GetWidth() <= m_last_show_rect.x2)
		                 ? my_right
		                 : my_left - sub->GetWidth();

	Fvector2 point;
	point.set(anchor_x, top);

	Frect clip = m_last_show_rect;
	clip.x1 = anchor_x;
	clip.y1 = top;

	sub->Show(clip, point);

	m_active_submenu = sub;
	m_active_sub_item = item;
	m_submenu_close_at = 0;
}

void CUIPropertiesBox::HideActiveSubmenu()
{
	if (m_active_submenu)
	{
		if (m_active_submenu->IsShown())
			m_active_submenu->Hide();
		m_active_submenu = NULL;
		m_active_sub_item = NULL;
	}
	m_submenu_close_at = 0;
}

bool CUIPropertiesBox::CursorOverTree()
{
	Frect r;
	GetAbsoluteRect(r);
	if (r.in(GetUICursor().GetCursorPosition()))
		return true;
	if (m_active_submenu && m_active_submenu->IsShown())
		return m_active_submenu->CursorOverTree();
	return false;
}

void CUIPropertiesBox::ClearSubmenus()
{
	HideActiveSubmenu();
	for (auto& kv : m_item_submenus)
	{
		CUIPropertiesBox* sub = kv.second;
		sub->ClearSubmenus();
		if (sub->GetParent())
			sub->GetParent()->DetachChild(sub);
	}
	m_item_submenus.clear();
}

bool CUIPropertiesBox::AddItem(LPCSTR str, void* pData, u32 tag_value)
{
	CUIListBoxItem* itm = m_UIListWnd.AddTextItem(str);
	itm->SetTAG(tag_value);
	itm->SetData(pData);
	return true;
}

CUIListBoxItem* CUIPropertiesBox::AddHeader(LPCSTR str)
{
	CUIListBoxItem* itm = m_UIListWnd.AddTextItem(str);
	itm->SetTextColor(0xffffd27f);
	itm->Enable(false);
	return itm;
}

void CUIPropertiesBox::RemoveItem(CUIListBoxItem* itm)
{
	auto it = m_item_submenus.find(itm);
	if (it != m_item_submenus.end())
	{
		CUIPropertiesBox* sub = it->second;
		if (m_active_submenu == sub)
			HideActiveSubmenu();
		sub->ClearSubmenus();
		if (sub->GetParent())
			sub->GetParent()->DetachChild(sub);
		m_item_submenus.erase(it);
	}
	m_UIListWnd.RemoveWindow(itm);
}

void CUIPropertiesBox::RemoveItemByTAG(u32 tag)
{
	m_UIListWnd.RemoveWindow(m_UIListWnd.GetItemByTAG(tag));
}

void CUIPropertiesBox::RemoveAll()
{
	ClearSubmenus();
	m_UIListWnd.Clear();
}

void CUIPropertiesBox::Show(const Frect& parent_rect, const Fvector2& point)
{
	float room_below = parent_rect.y2 - point.y;
	float room_above = point.y - parent_rect.y1;
	float max_h = _max(room_above, room_below) - OFFSET_Y * 2.0f;
	if (GetHeight() > max_h)
	{
		Fvector2 sz = GetWndSize();
		sz.y = max_h;
		sz.x += m_UIListWnd.ScrollBar()->GetWidth();
		SetWndSize(sz);
		m_UIListWnd.SetWndSize(sz);
		m_UIListWnd.InitScrollView();
		m_UIListWnd.UpdateChildrenLenght();
		m_UIListWnd.ForceUpdate();
	}

	Fvector2 prop_pos;
	Fvector2 prop_size = GetWndSize();
	m_last_show_rect = parent_rect;

	if (point.x - prop_size.x > parent_rect.x1 && point.y + prop_size.y < parent_rect.y2)
	{
		prop_pos.set(point.x - prop_size.x, point.y);
	}
	else if (point.x - prop_size.x > parent_rect.x1 && point.y - prop_size.y > parent_rect.y1)
	{
		prop_pos.set(point.x - prop_size.x, point.y - prop_size.y);
	}
	else if (point.x + prop_size.x < parent_rect.x2 && point.y - prop_size.y > parent_rect.y1)
	{
		prop_pos.set(point.x, point.y - prop_size.y);
	}
	else
		prop_pos.set(point.x, point.y);

	SetWndPos(prop_pos);

	inherited::Show(true);
	inherited::Enable(true);

	ResetAll();

	GetParent()->SetCapture(this, true);
	m_UIListWnd.Reset();

	float pad_h = m_UIListWnd.GetPadSize().y;
	if (pad_h > m_UIListWnd.GetHeight())
		m_UIListWnd.ScrollBar()->SetRange(0, iFloor(pad_h));
}

void CUIPropertiesBox::Hide()
{
	CUIWindow::Show(false);
	CUIWindow::Enable(false);

	m_pMouseCapturer = NULL;

	if (GetParent()->GetMouseCapturer() == this)
	{
		if (m_parent_menu && m_parent_menu->IsShown())
			GetParent()->SetCapture(m_parent_menu, true);
		else
			GetParent()->SetCapture(this, false);
	}

	HideActiveSubmenu();
}

bool CUIPropertiesBox::OnMouseAction(float x, float y, EUIMessages mouse_action)
{
	bool cursor_on_box;


	if (x >= 0 && x < GetWidth() && y >= 0 && y < GetHeight())
		cursor_on_box = true;
	else
		cursor_on_box = false;


	CUIPropertiesBox* root = this;
	while (root->m_parent_menu)
		root = root->m_parent_menu;

	if (mouse_action == WINDOW_LBUTTON_DOWN && !cursor_on_box)
	{
		root->Hide();
		return true;
	}
	if (mouse_action == WINDOW_RBUTTON_DOWN && !cursor_on_box)
	{
		root->Hide();
	}
	if (mouse_action == WINDOW_MOUSE_WHEEL_DOWN || mouse_action == WINDOW_MOUSE_WHEEL_UP)
	{
		Fvector2 cur = GetUICursor().GetCursorPosition();
		CUIPropertiesBox* target = this;
		for (CUIPropertiesBox* b = root; b;
		     b = (b->m_active_submenu && b->m_active_submenu->IsShown()) ? b->m_active_submenu : NULL)
		{
			Frect r;
			b->GetAbsoluteRect(r);
			if (r.in(cur))
				target = b;
		}
		if (target->m_UIListWnd.GetPadSize().y > target->m_UIListWnd.GetHeight())
			target->m_UIListWnd.OnMouseAction(x, y, mouse_action);
		return true;
	}

	bool res = inherited::OnMouseAction(x, y, mouse_action);

	if (IsShown() && GetParent() && GetParent()->GetMouseCapturer() != this)
		GetParent()->SetCapture(this, true);

	return res;
}

void CUIPropertiesBox::AutoUpdateSize()
{
	Fvector2 sz = GetWndSize();
	sz.y = m_UIListWnd.GetItemHeight() * m_UIListWnd.GetSize() + m_UIListWnd.GetVertIndent();
	sz.x = float(m_UIListWnd.GetLongestLength() + m_UIListWnd.GetHorizIndent()) + 2;
	SetWndSize(sz);
	m_UIListWnd.SetWndSize(GetWndSize());
	m_UIListWnd.UpdateChildrenLenght();
}

CUIListBoxItem* CUIPropertiesBox::GetClickedItem()
{
	if (m_active_submenu && m_active_submenu->IsShown())
		return m_active_submenu->GetClickedItem();
	return m_UIListWnd.GetSelectedItem();
}

void CUIPropertiesBox::Update()
{
	inherited::Update();

	if (!m_item_submenus.empty())
	{
		Fvector2 cur = GetUICursor().GetCursorPosition();
		for (auto& kv : m_item_submenus)
		{
			Frect r;
			kv.first->GetAbsoluteRect(r);
			if (r.in(cur))
			{
				if (m_active_submenu != kv.second)
				{
					HideActiveSubmenu();
					ShowSubMenuForItem(kv.first);
				}
				break;
			}
		}
	}

	if (m_active_submenu && m_active_submenu->IsShown() && m_active_sub_item)
	{
		Frect ir;
		m_active_sub_item->GetAbsoluteRect(ir);
		bool inside = ir.in(GetUICursor().GetCursorPosition()) || m_active_submenu->CursorOverTree();
		if (inside)
		{
			m_submenu_close_at = 0;
		}
		else if (m_submenu_close_at == 0)
		{
			m_submenu_close_at = Device.dwTimeGlobal + 300;
		}
		else if (Device.dwTimeGlobal >= m_submenu_close_at)
		{
			HideActiveSubmenu();
		}
	}
}

void CUIPropertiesBox::Draw()
{
	inherited::Draw();
}

bool CUIPropertiesBox::OnKeyboardAction(int dik, EUIMessages keyboard_action)
{
	return true;
}
