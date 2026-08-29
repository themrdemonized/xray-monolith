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
	float Seam() const { return (m_overlap > 0.0f && m_overlap < GetWndSize().x) ? m_overlap : 0.0f; }

	// The one place that assumes a 45-degree cap. Tab art has slanted ends, but nothing declares how
	// wide the slant is: the atlas rect carries no cap boundary, and the layout cannot recover one
	// either -- pitch = width - cap + margin is one equation in two unknowns. So we take the cap to be
	// as wide as the art is tall. That is a hack fitted to the vanilla art; getting it exact requires
	// the caps to be declared, i.e. tabs rebuilt as CUIFrameLineWnd, whose m_tex_rect[flFirst] and
	// [flSecond] state the cap widths outright.
	static float CapWidthTexels(const Frect& art) { return art.height(); }

	float CapWidthUI() const;

	const shared_str& ArtBase() const { return m_art_base; }
	using inherited::InitTexture;
	virtual void InitTexture(LPCSTR tex_name);

	bool SetIcon(LPCSTR base_name);
	void ClearIcon();
	void FitIcon(const Fvector2& box, const Fvector2& pos, const Fvector2& anchor);
	bool IconHasStateArt() const;

	virtual void DrawTexture();
	virtual void Update();

	// Reproduce this tab at (pos,size): background kind, art, seam overlap, text style and caption placement.
	// Caller fills id/caption.
	CUITabButton* Clone(const Fvector2& pos, const Fvector2& size);

	static u32 SlantPoly(Fvector2* out, u32 max, const Fvector2& pos, const Fvector2& size, float slant);
	u32 HitPoly(Fvector2* out, u32 max) const;

protected:
	float m_overlap = 0.0f;
	shared_str m_art_base;
	CUI_IB_Static* m_icon = nullptr;

private:
	Frect ArtRegion() const; // background art rect, in atlas texels (internal to CapWidthUI)
};
