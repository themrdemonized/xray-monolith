#include "StdAfx.h"
#include "UITabControl.h"
#include "UITabButton.h"
#include "UITabScrollArrows.h"
#include "UI3tButton.h"
#include "UIStatic.h"
#include "../ui_base.h"

static const float TAB_SCROLL_WHEEL_STEP = 1.0f / 3.0f;

// A zero-width tab is "parked": an invisible off-strip dispatch target excluded from strip geometry and scrolling.
static bool TabParked(CUITabButton* t)
{
	return t->GetWndSize().x <= 0.0f;
}

CUITabButton* CUITabControl::FirstStripTab() const
{
	for (u32 i = 0; i < m_TabsArr.size(); ++i)
		if (!TabParked(m_TabsArr[i]))
			return m_TabsArr[i];
	return NULL;
}

bool CUITabControl::AddTab(LPCSTR id, LPCSTR caption, LPCSTR after_id)
{
	CUITabButton* first_tab = FirstStripTab();
	if (!first_tab)
	{
		Msg("! [CUITabControl] AddTab(%s): control has no tabs to derive geometry from", id);
		return false;
	}

	u32 at = 0;
	for (u32 i = 0; i < m_TabsArr.size(); ++i)
		if (!TabParked(m_TabsArr[i]))
			at = i + 1;

	if (after_id && xr_strlen(after_id))
	{
		const shared_str anchor(after_id);
		u32 i = 0;
		for (; i < m_TabsArr.size(); ++i)
		{
			if (m_TabsArr[i]->m_btn_id == anchor && !TabParked(m_TabsArr[i]))
			{
				at = i + 1;
				break;
			}
		}
		if (i == m_TabsArr.size())
			Msg("! [CUITabControl] AddTab(%s): anchor tab [%s] not found, appending", id, after_id);
	}

	CUITabButton* prev = m_TabsArr[at - 1];
	Fvector2 size = first_tab->GetWndSize();

	CGameFont* font = first_tab->TextItemControl()->GetFont();
	if (!font)
		font = UI().Font().pFontLetterica16Russian;

	Fvector2 pos;
	pos.set(prev->GetWndPos().x + StripPitch(prev), prev->GetWndPos().y);

	CUITabButton* pNewButton = first_tab->Clone(pos, size);
	pNewButton->TextItemControl()->SetText(caption);
	pNewButton->TextItemControl()->SetFont(font);
	pNewButton->SetTextureColor(m_cGlobalButtonColor);
	pNewButton->m_btn_id = id;
	pNewButton->m_dynamic = true;

	return InsertItem(pNewButton, at);
}

void CUITabControl::RemoveDynamicTabs()
{
	u32 dst = 0;
	for (u32 i = 0; i < m_TabsArr.size(); ++i)
	{
		if (m_TabsArr[i]->m_dynamic)
			DetachChild(m_TabsArr[i]);
		else
			m_TabsArr[dst++] = m_TabsArr[i];
	}
	m_TabsArr.resize(dst);
}

float CUITabControl::StripPitch(const CUITabButton* t) const
{
	return t->Pitch() + m_margin;
}

float CUITabControl::Tuck(const CUITabButton* t) const
{
	return t->Overlap() - m_margin;
}

bool CUITabControl::CanScroll() const
{
	return m_content_w > m_strip_w + 0.5;
}

void CUITabControl::RecalcScroll()
{
	// Two independent left-to-right layouts. content_w spans ALL strip tabs (what must fit); strip_w spans
	// the STATIC tabs alone (the viewport): the legacy PDA layout ships a wrong control width, so the vanilla
	// static tabs define how wide the visible strip is, and the dynamic (AddTab) tabs overflow it and scroll.
	// GetWidth() is only a floor.
	m_content_w = 0.0f;
	m_strip_w = GetWidth();
	float content_x = 0.0f;
	float static_x = 0.0f;
	for (u32 i = 0; i < m_TabsArr.size(); ++i)
	{
		if (TabParked(m_TabsArr[i]))
			continue;
		const float w = m_TabsArr[i]->GetWndSize().x;
		if (content_x + w > m_content_w)
			m_content_w = content_x + w;
		if (!m_TabsArr[i]->m_dynamic)
		{
			if (static_x + w > m_strip_w)
				m_strip_w = static_x + w;
			static_x += StripPitch(m_TabsArr[i]);
		}
		content_x += StripPitch(m_TabsArr[i]);
	}

	CUITabButton* first_tab = FirstStripTab();
	if (CanScroll())
		m_arrows->EnsureBuilt(first_tab);

	m_view_left = m_arrows->Width(CUITabScrollArrows::eLeft) - (first_tab ? Tuck(first_tab) : 0.0f);
	m_view_right = m_strip_w - m_arrows->Width(CUITabScrollArrows::eRight);

	m_arrows->Layout(m_view_right, first_tab ? first_tab->GetWndPos().y : 0.0f);
	m_arrows->Show(CanScroll());

	ApplyScroll(0.0f);
}

float CUITabControl::ScrollStep() const
{
	CUITabButton* t = FirstStripTab();
	return t ? StripPitch(t) : 0.0f;
}

float CUITabControl::CurrentScroll() const
{
	CUITabButton* t = FirstStripTab();
	return t ? m_view_left - t->GetWndPos().x : 0.0f;
}

float CUITabControl::MaxScroll() const
{
	CUITabButton* first = FirstStripTab();
	if (!first)
		return 0.0f;
	const float max_scroll = m_content_w + m_view_left - m_view_right - Tuck(first);
	return max_scroll > 0.0f ? max_scroll : 0.0f;
}

void CUITabControl::ApplyScroll(float scroll)
{
	const bool can_scroll = CanScroll();
	const float view_left = can_scroll ? m_view_left : 0.0f;
	const float viewport_w = m_strip_w;
	float layout_x = 0.0f;
	for (u32 i = 0; i < m_TabsArr.size(); ++i)
	{
		if (TabParked(m_TabsArr[i]))
			continue;
		const float w = m_TabsArr[i]->GetWndSize().x;
		Fvector2 p = m_TabsArr[i]->GetWndPos();
		p.x = layout_x - scroll + view_left;
		m_TabsArr[i]->SetWndPos(p);

		if (can_scroll)
		{
			bool vis = (p.x + w > 0.0f) && (p.x < viewport_w);
			m_TabsArr[i]->SetVisible(vis);
		}
		layout_x += StripPitch(m_TabsArr[i]);
	}
}

void CUITabControl::ClampScroll(float scroll)
{
	if (!CanScroll() || m_TabsArr.empty())
		return;
	clamp(scroll, 0.0f, MaxScroll());
	ApplyScroll(scroll);
}

void CUITabControl::ScrollBy(float dx)
{
	if (!CanScroll())
		return;
	ClampScroll(CurrentScroll() + dx);
}

void CUITabControl::EnsureVisible(const shared_str& id)
{
	if (!CanScroll())
		return;
	float scroll = CurrentScroll();
	float layout_x = 0.0f;
	for (u32 i = 0; i < m_TabsArr.size(); ++i)
	{
		if (m_TabsArr[i]->m_btn_id == id)
		{
			if (TabParked(m_TabsArr[i]))
				return;
			float tab_w = m_TabsArr[i]->GetWndSize().x;
			if (layout_x - scroll < 0.0f)
				scroll = layout_x;
			else
			{
				float need = layout_x + m_view_left + tab_w - m_view_right - Tuck(m_TabsArr[i]);
				if (scroll < need)
					scroll = need;
			}
			ClampScroll(scroll);
			return;
		}
		if (!TabParked(m_TabsArr[i]))
			layout_x += StripPitch(m_TabsArr[i]);
	}
}

void CUITabControl::Draw()
{
	if (!CanScroll())
	{
		inherited::Draw();
		return;
	}
	DrawTabsClipped();
}

void CUITabControl::DrawTabsClipped()
{
	Frect abs_rect;
	GetAbsoluteRect(abs_rect);
	CUITabButton* ref = FirstStripTab();
	const float overlap = ref->Overlap();
	const float tab_h = ref->GetWndSize().y;
	const float strip_top = abs_rect.y1 + ref->GetWndPos().y;
	const float strip_bot = strip_top + tab_h;

	const float cut_l = m_view_left;
	const float cut_r = m_view_right - m_margin;

	Fvector2 lb, lt, rb, rt;
	UI().ClientToScreenScaled(lb, abs_rect.x1 + cut_l, strip_bot);
	UI().ClientToScreenScaled(lt, abs_rect.x1 + cut_l + overlap, strip_top);
	UI().ClientToScreenScaled(rb, abs_rect.x1 + cut_r, strip_bot);
	UI().ClientToScreenScaled(rt, abs_rect.x1 + cut_r + overlap, strip_top);

	C2DFrustum frustum = UI().ScreenFrustum();
	frustum.AddEdgePlane(lb, lt);
	frustum.AddEdgePlane(rt, rb);

	Frect clip_bg = abs_rect;
	clip_bg.x2 = abs_rect.x1 + m_strip_w;

	UI().PushScissor(clip_bg);
	for (u32 i = 0; i < m_TabsArr.size(); ++i)
	{
		CUITabButton* t = m_TabsArr[i];
		if (!t->IsShown() || TabParked(t))
			continue;
		const float tab_lb = t->GetWndPos().x;
		const float tab_rb = tab_lb + t->GetWndSize().x - overlap;
		if (tab_lb < cut_l || tab_rb > cut_r)
		{
			UI().PushClipFrustum(&frustum);
			t->DrawTexture();
			UI().PopClipFrustum();
		}
		else
			t->DrawTexture();
	}
	UI().PopScissor();

	Frect clip_txt = abs_rect;
	clip_txt.x1 = abs_rect.x1 + m_arrows->Width(CUITabScrollArrows::eLeft);
	clip_txt.x2 = abs_rect.x1 + m_view_right;

	UI().PushScissor(clip_txt);
	for (u32 i = 0; i < m_TabsArr.size(); ++i)
		if (m_TabsArr[i]->IsShown())
			m_TabsArr[i]->DrawText();
	UI().PopScissor();

	m_arrows->Draw();
}

void CUITabControl::Update()
{
	ApplyStripHitClips();
	inherited::Update();
}

// Slide a tab's hit parallelogram (BL,BR,TR,TL) inside the viewport walls left/right, gauged on its bottom
// edge (BL,BR); the top vertices follow, not a vertical clamp: each slanted side moves as a unit
static void SlidePolyIntoBand(Fvector2* poly, float left, float right)
{
	if (poly[0].x < left)       { const float d = left - poly[0].x;  poly[0].x += d; poly[3].x += d; }
	else if (poly[0].x > right) { const float d = right - poly[0].x; poly[0].x += d; poly[3].x += d; }
	if (poly[1].x > right)      { const float d = right - poly[1].x; poly[1].x += d; poly[2].x += d; }
	else if (poly[1].x < left)  { const float d = left - poly[1].x;  poly[1].x += d; poly[2].x += d; }
}

void CUITabControl::ApplyStripHitClips()
{
	CUITabButton* ref = FirstStripTab();
	if (!ref)
		return;
	Frect abs_rect;
	GetAbsoluteRect(abs_rect);
	const float band_left = m_view_left;
	const float band_right = m_view_right - m_margin;
	const bool can_scroll = CanScroll();
	for (u32 i = 0; i < m_TabsArr.size(); ++i)
	{
		CUITabButton* t = m_TabsArr[i];
		if (TabParked(t))
			continue;
		Fvector2 poly[4];
		u32 n = t->HitPoly(poly, 4);
		if (can_scroll && n == 4)
			SlidePolyIntoBand(poly, band_left, band_right);
		for (u32 k = 0; k < n; ++k)
		{
			poly[k].x += abs_rect.x1;
			poly[k].y += abs_rect.y1;
		}
		t->SetHitClip(poly, n);
	}
	m_arrows->ApplyHitClips(Fvector2().set(abs_rect.x1, abs_rect.y1));
}

bool CUITabControl::OnMouseAction(float x, float y, EUIMessages mouse_action)
{
	if (CanScroll())
	{
		if (mouse_action == WINDOW_MOUSE_WHEEL_UP)
		{
			ScrollBy(-ScrollStep() * TAB_SCROLL_WHEEL_STEP);
			return true;
		}
		if (mouse_action == WINDOW_MOUSE_WHEEL_DOWN)
		{
			ScrollBy(ScrollStep() * TAB_SCROLL_WHEEL_STEP);
			return true;
		}
	}
	return inherited::OnMouseAction(x, y, mouse_action);
}
