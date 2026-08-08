#pragma once


#include "uiframewindow.h"
#include "uilistbox.h"

#include "../../xrServerEntities/script_export_space.h"

class CUIPropertiesBox :
	public CUIFrameWindow,
	public CUIWndCallback
{
private:
	typedef CUIFrameWindow inherited;
public:
	CUIPropertiesBox();
	virtual ~CUIPropertiesBox();

	void InitPropertiesBox(Fvector2 pos, Fvector2 size);

	virtual void SendMessage(CUIWindow* pWnd, s16 msg, void* pData);
	virtual bool OnMouseAction(float x, float y, EUIMessages mouse_action);
	virtual bool OnKeyboardAction(int dik, EUIMessages keyboard_action);

	bool AddItem(LPCSTR str, void* pData = NULL, u32 tag_value = 0);
	bool AddItem_script(LPCSTR str) { return AddItem(str); };
	bool AddItem_script_tag(LPCSTR str, u32 tag_value) { return AddItem(str, NULL, tag_value); };
	void AddHeader_script(LPCSTR str) { AddHeader(str); };
	u32 GetItemsCount() { return m_UIListWnd.GetSize(); };
	CUIListBoxItem* GetItemByIDX(int idx) { return m_UIListWnd.GetItemByIDX(idx); }
	const xr_map<CUIListBoxItem*, CUIPropertiesBox*>& GetItemSubmenus() const { return m_item_submenus; }
	CUIListBoxItem* AddHeader(LPCSTR str);
	void RemoveItem(CUIListBoxItem* itm);
	void RemoveItemByTAG(u32 tag_value);
	void RemoveAll();

	virtual void Show(const Frect& parent_rect, const Fvector2& point);
	virtual void Hide();

	virtual void Update();
	virtual void Draw();

	CUIListBoxItem* GetClickedItem();

	void AutoUpdateSize();

	// Hover-expanding submenus, Lua-fed. Creates and owns a child box under `label`,
	// returns a borrowed pointer for Lua to populate (see AddSubmenu in the .cpp).
	CUIPropertiesBox* AddSubmenu(LPCSTR label);
protected:
	CUIListBox m_UIListWnd;
private:
	Frect m_last_show_rect;

	// per-item hover submenus
	xr_map<CUIListBoxItem*, CUIPropertiesBox*> m_item_submenus;
	CUIPropertiesBox* m_active_submenu;
	CUIListBoxItem* m_active_sub_item;
	CUIPropertiesBox* m_parent_menu;
	u32 m_submenu_close_at;

	void ShowSubMenuForItem(CUIListBoxItem* item);
	void HideActiveSubmenu();
	void ClearSubmenus();
	bool CursorOverTree();

DECLARE_SCRIPT_REGISTER_FUNCTION
};
