#pragma once
#include "UITabButton.h"

class CUIStatic;
class CUIWindow;

// Scroll arrow for the tab strip. Its face is either skin-authored, or a tab-style one built from two
// mirrored halves of the strip tabs' own cap art, or -- when the art affords neither -- a bare glyph.
class CUIScrollArrowButton : public CUITabButton
{
	typedef CUITabButton inherited;
public:
	CUIScrollArrowButton();

	virtual bool OnMouseDown(int mouse_btn);
	virtual bool OnMouseAction(float x, float y, EUIMessages mouse_action);

	void SetupHalves(const shared_str& art_base);
	void LayoutHalves();

	virtual void Update();

protected:
	void ApplyHalfArt(IBState ib_state);

	CUIStatic* m_half[2];
	shared_str m_half_base;
	IBState    m_applied_state;
};

class CUITabScrollArrows
{
public:
	enum { eNone = -1, eLeft = 0, eRight = 1 };

	CUITabScrollArrows();
	~CUITabScrollArrows();

	void Init(CUIWindow* parent, CUIWindow* msg_target);

	void  Adopt(int side, CUIScrollArrowButton* arrow);
	void  EnsureBuilt(CUITabButton* ref);
	void  Layout(float view_left, float view_right, float strip_y);
	void  Show(bool visible);
	void  Draw();
	void  ApplyHitClips(const Fvector2& origin);
	int   SideOf(const CUIWindow* clicked) const;
	float Width(int side) const { return (m_arrow[side] && m_pin[side].x == 0.0f) ? m_arrow[side]->GetWndSize().x : 0.0f; }

private:
	CUIScrollArrowButton* NewArrow(int side, const Fvector2& size, CUITabButton* ref);

	CUIWindow* m_parent;
	CUIWindow* m_msg_target;

	CUIScrollArrowButton* m_arrow[2];
	Fvector2              m_pin[2];
};
