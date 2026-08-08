#pragma once
#include "UI3tButton.h"

class CUITabButton : public CUI3tButton
{
	typedef CUI3tButton inherited;
public:
	shared_str m_btn_id;
	bool m_dynamic = false;

	CUITabButton();
	virtual ~CUITabButton();

	virtual void SendMessage(CUIWindow* pWnd, s16 msg, void* pData = 0);
	virtual bool OnMouseAction(float x, float y, EUIMessages mouse_action);
	virtual bool OnMouseDown(int mouse_btn);

	float Overlap() const { return m_overlap; }
	void  SetOverlap(float v) { m_overlap = v; }
	float Pitch() const { return GetWndSize().x - m_overlap; }

	float CapWidthUI() const;
	float CapOverlapUI() const;

	const shared_str& ArtBase() const { return m_art_base; }
	using inherited::InitTexture;
	virtual void InitTexture(LPCSTR tex_name);

	// Reproduce this tab at (pos,size): background kind, art, seam overlap, text colours. Caller fills id/caption.
	CUITabButton* Clone(const Fvector2& pos, const Fvector2& size) const;

	static u32 SlantPoly(Fvector2* out, u32 max, const Fvector2& pos, const Fvector2& size, float slant);
	u32 HitPoly(Fvector2* out, u32 max) const;

protected:
	float m_overlap = 0.0f;
	shared_str m_art_base;

private:
	Frect ArtRegion() const; // background art rect, in atlas texels (internal to CapWidthUI)
};
