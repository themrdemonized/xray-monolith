#include "StdAfx.h"
#include "UITabScrollArrows.h"
#include "UIStatic.h"
#include "UILines.h"

static const float TAB_ARROW_MIN_W = 20.0f;

bool CUIScrollArrowButton::OnMouseDown(int mouse_btn)
{
	return CUI3tButton::OnMouseDown(mouse_btn);
}

bool CUIScrollArrowButton::OnMouseAction(float x, float y, EUIMessages mouse_action)
{
	return CUIButton::OnMouseAction(x, y, mouse_action);
}

static shared_str StateArt(const shared_str& base, IBState ib_state)
{
	LPCSTR suffix;
	switch (ib_state)
	{
	case S_Touched:     suffix = "_t"; break;
	case S_Highlighted: suffix = "_h"; break;
	default:            suffix = "_e"; break;
	}
	string256 buf;
	strconcat(sizeof(buf), buf, base.c_str(), suffix);
	return shared_str(buf);
}

CUIScrollArrowButton::CUIScrollArrowButton()
	: m_applied_state(S_Enabled)
{
	m_half[0] = m_half[1] = NULL;
}

void CUIScrollArrowButton::SetupHalves(const shared_str& art_base)
{
	m_half_base = art_base;
	if (!m_half[0])
	{
		for (int i = 0; i < 2; ++i)
		{
			m_half[i] = xr_new<CUIStatic>();
			m_half[i]->SetAutoDelete(true);
			AttachChild(m_half[i]);
		}
	}
	m_applied_state = S_Enabled;
	ApplyHalfArt(S_Enabled);
}

void CUIScrollArrowButton::LayoutHalves()
{
	if (!m_half[0])
		return;
	const float half_w = GetWndSize().x * 0.5f;
	const float h = GetWndSize().y;
	for (int i = 0; i < 2; ++i)
	{
		m_half[i]->SetWndPos(Fvector2().set(i * half_w, 0.0f));
		m_half[i]->SetWndSize(Fvector2().set(half_w, h));
	}
}

void CUIScrollArrowButton::ApplyHalfArt(IBState ib_state)
{
	shared_str art = StateArt(m_half_base, ib_state);
	for (int i = 0; i < 2; ++i)
	{
		CUIStatic* half = m_half[i];
		half->InitTexture(art.c_str());
		Frect r = half->GetTextureRect();
		const float cap = CapWidthTexels(r);
		if (i == 0)
			r.x2 = r.x1 + cap;
		else
			r.x1 = r.x2 - cap;
		half->SetTextureRect(r);
		half->SetStretchTexture(true);
	}
}

void CUIScrollArrowButton::Update()
{
	inherited::Update();
	if (!m_half[0])
		return;
	const IBState state = CurrentIBState();
	if (state == m_applied_state)
		return;
	ApplyHalfArt(state);
	m_applied_state = state;
}

CUITabScrollArrows::CUITabScrollArrows()
	: m_parent(NULL), m_msg_target(NULL)
{
	m_arrow[0] = m_arrow[1] = NULL;
	m_pin[0].set(0.0f, 0.0f);
	m_pin[1].set(0.0f, 0.0f);
}

CUITabScrollArrows::~CUITabScrollArrows()
{
	for (int s = 0; s < 2; ++s)
		if (m_arrow[s] && m_parent)
			m_parent->DetachChild(m_arrow[s]);
}

void CUITabScrollArrows::Init(CUIWindow* parent, CUIWindow* msg_target)
{
	m_parent = parent;
	m_msg_target = msg_target;
}

static bool CanSplice(CUITabButton* ref)
{
	if (!ref->ArtBase().size())
		return false;
	const float w = 2.0f * ref->CapWidthUI();
	return w >= TAB_ARROW_MIN_W && ref->GetWndSize().x - w >= TAB_ARROW_MIN_W;
}

void CUITabScrollArrows::Adopt(int side, CUIScrollArrowButton* arrow)
{
	static const LPCSTR name[2] = {"tab_scroll_left", "tab_scroll_right"};

	arrow->SetAutoDelete(true);
	arrow->SetWindowName(name[side]);
	arrow->Show(false);
	m_pin[side] = arrow->GetWndPos();
	if (m_parent)
		m_parent->AttachChild(arrow);
	arrow->SetMessageTarget(m_msg_target);
	m_arrow[side] = arrow;
}

CUIScrollArrowButton* CUITabScrollArrows::NewArrow(int side, const Fvector2& size, CUITabButton* ref)
{
	static const LPCSTR glyph[2] = {"<", ">"};

	CUIScrollArrowButton* arrow = xr_new<CUIScrollArrowButton>();
	arrow->InitButton(Fvector2().set(0.0f, 0.0f), size);
	arrow->TextItemControl()->SetText(glyph[side]);

	CUILines* style = ref->TextItemControl();
	if (style->GetFont())
		arrow->TextItemControl()->SetFont(style->GetFont());

	Adopt(side, arrow);
	return arrow;
}

void CUITabScrollArrows::EnsureBuilt(CUITabButton* ref)
{
	if (!ref)
		return;

	const bool splice = CanSplice(ref);
	Fvector2 size;
	size.set(splice ? 2.0f * ref->CapWidthUI() : TAB_ARROW_MIN_W, ref->GetWndSize().y);

	for (int s = 0; s < 2; ++s)
	{
		if (m_arrow[s])
			continue;
		CUIScrollArrowButton* arrow = NewArrow(s, size, ref);
		if (splice)
		{
			arrow->SetOverlap(ref->Overlap());
			arrow->SetupHalves(ref->ArtBase());
			arrow->LayoutHalves();
			for (int st = S_Enabled; st <= S_Touched; ++st)
			{
				arrow->m_dwTextColor[st] = ref->m_dwTextColor[st];
				arrow->m_bUseTextColor[st] = ref->m_bUseTextColor[st];
			}
		}
		else
		{
			arrow->m_dwTextColor[S_Enabled] = ref->m_bUseTextColor[S_Touched]
				                                  ? ref->m_dwTextColor[S_Touched]
				                                  : ref->m_dwTextColor[S_Enabled];
		}
	}
}

void CUITabScrollArrows::Layout(float view_left, float view_right, float strip_y)
{
	for (int s = 0; s < 2; ++s)
	{
		if (!m_arrow[s])
			continue;
		Fvector2 p;
		p.set((s == eLeft) ? view_left : view_right, strip_y);
		if (m_pin[s].x != 0.0f)
			p.x = m_pin[s].x;
		if (m_pin[s].y != 0.0f)
			p.y = m_pin[s].y;
		m_arrow[s]->SetWndPos(p);
	}
}

void CUITabScrollArrows::Show(bool visible)
{
	for (int s = 0; s < 2; ++s)
		if (m_arrow[s])
			m_arrow[s]->Show(visible);
}

void CUITabScrollArrows::Draw()
{
	for (int s = 0; s < 2; ++s)
		if (m_arrow[s] && m_arrow[s]->IsShown())
			m_arrow[s]->Draw();
}

void CUITabScrollArrows::ApplyHitClips(const Fvector2& origin)
{
	for (int s = 0; s < 2; ++s)
	{
		CUIScrollArrowButton* arrow = m_arrow[s];
		if (!arrow || !arrow->IsShown())
			continue;
		Fvector2 poly[4];
		u32 n = arrow->HitPoly(poly, 4);
		for (u32 k = 0; k < n; ++k)
		{
			poly[k].x += origin.x;
			poly[k].y += origin.y;
		}
		arrow->SetHitClip(poly, n);
	}
}

int CUITabScrollArrows::SideOf(const CUIWindow* clicked) const
{
	if (clicked == m_arrow[eLeft])
		return eLeft;
	if (clicked == m_arrow[eRight])
		return eRight;
	return eNone;
}
