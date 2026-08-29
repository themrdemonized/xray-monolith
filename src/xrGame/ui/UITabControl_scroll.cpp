#include "StdAfx.h"
#include "UITabControl.h"
#include "UITabButton.h"
#include "UITabScrollArrows.h"
#include "UI3tButton.h"
#include "UIStatic.h"
#include "../ui_base.h"

static const float TAB_SCROLL_WHEEL_STEP = 1.0f / 3.0f;

static const float TAB_MIN_GAP = 3.0f;

// A zero-width tab is "parked": an invisible off-strip dispatch target excluded from strip geometry and scrolling.
static bool TabParked(CUITabButton* t)
{
	return t->GetWndSize().x <= 0.0f;
}

static float TabSlack(const CUITabButton* t)
{
	const float gap = -t->Overlap();
	return gap > TAB_MIN_GAP ? gap - TAB_MIN_GAP : 0.0f;
}

CUITabButton* CUITabControl::FirstStripTab() const
{
	for (u32 i = 0; i < m_TabsArr.size(); ++i)
		if (!TabParked(m_TabsArr[i]))
			return m_TabsArr[i];
	return NULL;
}

void CUITabControl::RebuildTabOverlaps()
{
	CUITabButton* prev = NULL;
	float overlap = 0.0f;
	for (u32 i = 0; i < m_TabsArr.size(); ++i)
	{
		CUITabButton* t = m_TabsArr[i];
		if (t->m_dynamic || TabParked(t))
			continue;
		if (prev)
		{
			overlap = prev->GetWndPos().x + prev->GetWndSize().x - t->GetWndPos().x;
			prev->SetOverlap(overlap);
		}
		else
			m_origin_x = t->GetWndPos().x;
		prev = t;
	}
	if (prev)
		prev->SetOverlap(overlap);
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

	Fvector2 pos;
	pos.set(prev->GetWndPos().x + prev->Pitch(), prev->GetWndPos().y);

	CUITabButton* pNewButton = first_tab->Clone(pos, size);
	pNewButton->SetOverlap(prev->Overlap());
	pNewButton->TextItemControl()->SetText(caption);
	if (!pNewButton->TextItemControl()->GetFont())
		pNewButton->TextItemControl()->SetFont(UI().Font().pFontLetterica16Russian);
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

static float LaidOverlap(const CUITabButton* t, float squeeze = 1.0f)
{
	return t->Overlap() + TabSlack(t) * squeeze;
}

static float LaidPitch(const CUITabButton* t, float squeeze = 1.0f)
{
	return t->GetWndSize().x - LaidOverlap(t, squeeze);
}

bool CUITabControl::CanScroll() const
{
	return m_content_w > m_strip_w + 0.5f;
}

void CUITabControl::RecalcScroll()
{
	// Two left-to-right spans measured from the strip origin. content_w covers ALL tabs (what must fit);
	// strip_w covers the STATIC tabs alone and is the frame the strip must stay inside -- the authored tabs
	// define it, not GetWidth(), which the legacy PDA layout ships wrong. Anchoring the frame at both
	// authored ends is what keeps the last tab's right edge put while the interior redistributes.
	m_content_w = 0.0f;
	m_strip_w = 0.0f;
	float content_x = 0.0f;
	float static_x = 0.0f;
	float total_slack = 0.0f;
	CUITabButton* prev = NULL;
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
			static_x += m_TabsArr[i]->Pitch();
		}
		content_x += m_TabsArr[i]->Pitch();
		if (prev)
			total_slack += TabSlack(prev);
		prev = m_TabsArr[i];
	}

	const float needed = m_content_w - m_strip_w;
	float squeeze = 0.0f;
	if (needed > 0.0f && total_slack > 0.0f)
	{
		squeeze = (needed < total_slack) ? needed / total_slack : 1.0f;
		m_content_w -= total_slack * squeeze;
	}

	CUITabButton* first_tab = FirstStripTab();
	if (CanScroll())
		m_arrows->EnsureBuilt(first_tab);

	m_view_left = m_origin_x + m_arrows->Width(CUITabScrollArrows::eLeft)
		- (first_tab ? LaidOverlap(first_tab) : 0.0f);
	m_view_right = m_origin_x + m_strip_w - m_arrows->Width(CUITabScrollArrows::eRight);

	if (m_view_right < m_view_left)
		m_view_right = m_view_left;

	m_arrows->Layout(m_origin_x, m_view_right, first_tab ? first_tab->GetWndPos().y : 0.0f);
	m_arrows->Show(CanScroll());

	ApplyScroll(0.0f, squeeze);
}

float CUITabControl::ScrollStep() const
{
	CUITabButton* t = FirstStripTab();
	return t ? LaidPitch(t) : 0.0f;
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
	const float max_scroll = m_content_w + m_view_left - m_view_right - LaidOverlap(first);
	return max_scroll > 0.0f ? max_scroll : 0.0f;
}

void CUITabControl::ApplyScroll(float scroll, float squeeze)
{
	const bool can_scroll = CanScroll();
	const float view_left = can_scroll ? m_view_left : m_origin_x;
	const float frame_r = m_origin_x + m_strip_w;
	float layout_x = 0.0f;
	for (u32 i = 0; i < m_TabsArr.size(); ++i)
	{
		if (TabParked(m_TabsArr[i]))
			continue;
		const float w = m_TabsArr[i]->GetWndSize().x;
		Fvector2 p = m_TabsArr[i]->GetWndPos();
		p.x = layout_x - scroll + view_left;
		m_TabsArr[i]->SetWndPos(p);

		m_TabsArr[i]->SetVisible(!can_scroll || ((p.x + w > m_origin_x) && (p.x < frame_r)));
		layout_x += LaidPitch(m_TabsArr[i], squeeze);
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
				float need = layout_x + m_view_left + tab_w - m_view_right - LaidOverlap(m_TabsArr[i]);
				if (scroll < need)
					scroll = need;
			}
			ClampScroll(scroll);
			return;
		}
		if (!TabParked(m_TabsArr[i]))
			layout_x += LaidPitch(m_TabsArr[i]);
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
	const float seam = ref->Seam();

	C2DFrustum frustum = UI().ScreenFrustum();
	if (seam > 0.0f)
	{
		const float strip_top = abs_rect.y1 + ref->GetWndPos().y;
		const float strip_bot = strip_top + ref->GetWndSize().y;

		Fvector2 lb, lt, rb, rt;
		UI().ClientToScreenScaled(lb, abs_rect.x1 + m_view_left, strip_bot);
		UI().ClientToScreenScaled(lt, abs_rect.x1 + m_view_left + seam, strip_top);
		UI().ClientToScreenScaled(rb, abs_rect.x1 + m_view_right, strip_bot);
		UI().ClientToScreenScaled(rt, abs_rect.x1 + m_view_right + seam, strip_top);

		frustum.AddEdgePlane(lb, lt);
		frustum.AddEdgePlane(rt, rb);
	}

	const float laid = LaidOverlap(ref);
	const float tuck = laid > 0.0f ? laid : 0.0f;

	Frect clip_bg = abs_rect;
	clip_bg.x1 = abs_rect.x1 + m_view_left;
	clip_bg.x2 = abs_rect.x1 + m_view_right + laid;

	Frect clip_txt = clip_bg;
	clip_txt.x1 += tuck;
	clip_txt.x2 -= tuck;

	UI().PushScissor(clip_bg);
	for (u32 i = 0; i < m_TabsArr.size(); ++i)
	{
		CUITabButton* t = m_TabsArr[i];
		if (!t->IsShown() || TabParked(t))
			continue;
		const float tab_lb = t->GetWndPos().x;
		const float tab_rb = tab_lb + t->GetWndSize().x - seam;
		if (seam > 0.0f && (tab_lb < m_view_left || tab_rb > m_view_right))
		{
			UI().PushClipFrustum(&frustum);
			t->DrawTexture();
			UI().PopClipFrustum();
		}
		else
			t->DrawTexture();
	}
	UI().PopScissor();

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
	Frect abs_rect;
	GetAbsoluteRect(abs_rect);
	const bool can_scroll = CanScroll();
	for (u32 i = 0; i < m_TabsArr.size(); ++i)
	{
		CUITabButton* t = m_TabsArr[i];
		if (TabParked(t))
			continue;
		if (!can_scroll && t->Seam() <= 0.0f)
		{
			t->SetHitClip(NULL, 0);
			continue;
		}
		Fvector2 poly[4];
		u32 n = t->HitPoly(poly, 4);
		if (can_scroll && n == 4)
			SlidePolyIntoBand(poly, m_view_left, m_view_right);
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
