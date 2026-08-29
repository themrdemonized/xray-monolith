#pragma once

#include "uiwindow.h"
#include "../../xrServerEntities/script_export_space.h"
#include "UIOptionsItem.h"

class CUITabButton;
class CUITabScrollArrows;
class CUIScrollArrowButton;

DEF_VECTOR(TABS_VECTOR, CUITabButton*)

class CUITabControl : public CUIWindow, public CUIOptionsItem
{
	typedef CUIWindow inherited;
public:
	CUITabControl();
	virtual ~CUITabControl();

	// options item
	virtual void SetCurrentOptValue(); // opt->current
	virtual void SaveBackUpOptValue(); // current->backup
	virtual void SaveOptValue(); // current->opt
	virtual void UndoOptValue(); // backup->current
	virtual bool IsChangedOptValue() const; // backup!=current

	virtual bool OnKeyboardAction(int dik, EUIMessages keyboard_action);
	virtual bool OnMouseAction(float x, float y, EUIMessages mouse_action);
	virtual void Draw();
	virtual void Update();
	virtual void OnTabChange(const shared_str& sCur, const shared_str& sPrev);
	virtual void OnStaticFocusReceive(CUIWindow* pWnd);
	virtual void OnStaticFocusLost(CUIWindow* pWnd);

	// Добавление кнопки-закладки в список закладок контрола
	bool AddItem(LPCSTR pItemName, LPCSTR pTexName, Fvector2 pos, Fvector2 size);
	bool AddItem(CUITabButton* pButton);

	void RemoveAll();

	// Dynamic, id-aware tab add (the legacy 4-arg AddItem leaves m_btn_id empty). Inserts after the
	// tab named by after_id (NULL/""/unknown -> appended); geometry and art copy the strip's own
	// tabs, so it fails on a control that has no tabs yet. Tab width is fixed, so a too-long caption
	// may clip. Call RecalcScroll after.
	bool AddTab(LPCSTR id, LPCSTR caption, LPCSTR after_id);
	// Remove the AddTab-added tabs, restoring the XML positions. Call RecalcScroll afterwards.
	void RemoveDynamicTabs();

	// Hand over a skin-authored scroll arrow; side is CUITabScrollArrows::eLeft/eRight. That side is then
	// used as-is instead of being built from the strip's art.
	void SetScrollArrow(int side, CUIScrollArrowButton* arrow);

	void SetIconLayout(const Fvector2& pos, const Fvector2& box, const Fvector2& anchor)
	{
		m_icon_pos = pos;
		m_icon_box = box;
		m_icon_anchor = anchor;
	}

	bool SetTabIcon(LPCSTR id, LPCSTR art);

	void RecalcScroll();
	void ScrollBy(float dx);
	void EnsureVisible(const shared_str& id);
	bool CanScroll() const;

	virtual void SendMessage(CUIWindow* pWnd, s16 msg, void* pData);
	virtual void Enable(bool status);

	const shared_str& GetActiveId() const { return m_sPushedId; }
	LPCSTR GetActiveId_script();
	const shared_str& GetPrevActiveId() { return m_sPrevPushedId; }
	void SetActiveTab(const shared_str& sNewTab);
	void SetActiveTab_script(LPCSTR sNewTab) { SetActiveTab(sNewTab); };
	const u32 GetTabsCount() const { return m_TabsArr.size(); }

	// Режим клавилатурных акселераторов (вкл/выкл)
	IC bool GetAcceleratorsMode() const { return m_bAcceleratorsEnable; }
	void SetAcceleratorsMode(bool bEnable) { m_bAcceleratorsEnable = bEnable; }


	TABS_VECTOR* GetButtonsVector() { return &m_TabsArr; }
	CUITabButton* GetButtonById(const shared_str& id);
	CUITabButton* GetButtonById_script(LPCSTR s) { return GetButtonById(s); }

	void ResetTab();
protected:
	// Список кнопок - переключателей закладок
	TABS_VECTOR m_TabsArr;

	shared_str m_sPushedId;
	shared_str m_sPrevPushedId;

	// Цвет неактивных элементов
	u32 m_cGlobalTextColor;
	u32 m_cGlobalButtonColor;

	// Цвет надписи на активном элементе
	u32 m_cActiveTextColor;
	u32 m_cActiveButtonColor;

	bool m_bAcceleratorsEnable;
	shared_str m_opt_backup_value;

	float m_content_w;
	float m_strip_w;

	CUITabScrollArrows* m_arrows;

	float m_view_left;
	float m_view_right;

	float m_origin_x;

	Fvector2 m_icon_pos;
	Fvector2 m_icon_box;
	Fvector2 m_icon_anchor;

	Fvector2 IconBox(const CUITabButton* b) const;
	bool InsertItem(CUITabButton* pButton, u32 at);
	void RebuildTabOverlaps();
	CUITabButton* FirstStripTab() const;
	void DrawTabsClipped();
	void ApplyStripHitClips();
	float CurrentScroll() const;
	float MaxScroll() const;
	void ApplyScroll(float scroll, float squeeze = 1.0f);
	void ClampScroll(float scroll);
	float ScrollStep() const;

DECLARE_SCRIPT_REGISTER_FUNCTION
};
