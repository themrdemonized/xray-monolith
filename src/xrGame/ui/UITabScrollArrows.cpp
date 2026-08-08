#include "StdAfx.h"
#include "UITabScrollArrows.h"
#include "UIStatic.h"
#include "UILines.h"

bool CUIScrollArrowButton::OnMouseDown(int mouse_btn)
{
	return CUI3tButton::OnMouseDown(mouse_btn);
}

bool CUIScrollArrowButton::OnMouseAction(float x, float y, EUIMessages mouse_action)
{
	return CUIButton::OnMouseAction(x, y, mouse_action);
}

static shared_str StateArt(const shared_str& base, int ib_state)
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

int CUIScrollArrowButton::CurrentIBState()
{
	if (GetButtonState() == CUIButton::BUTTON_PUSHED)
		return S_Touched;
	return CursorOverWindow() ? S_Highlighted : S_Enabled;
}

void CUIScrollArrowButton::ApplyHalfArt(int ib_state)
{
	shared_str art = StateArt(m_half_base, ib_state);
	for (int i = 0; i < 2; ++i)
	{
		CUIStatic* half = m_half[i];
		half->InitTexture(art.c_str());
		Frect r = half->GetTextureRect();
		const float cap = r.height();
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
	const int state = CurrentIBState();
	if (state == m_applied_state)
		return;
	ApplyHalfArt(state);
	m_applied_state = state;
}

CUITabScrollArrows::CUITabScrollArrows()
	: m_parent(NULL), m_msg_target(NULL)
{
	m_arrow[0] = m_arrow[1] = NULL;
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

void CUITabScrollArrows::EnsureBuilt(CUITabButton* ref)
{
	if (m_arrow[0] || !ref)
		return;

	Build(ref);
}

void CUITabScrollArrows::Build(CUITabButton* ref)
{
	static const LPCSTR glyph[2] = {"<", ">"};
	static const LPCSTR name[2] = {"tab_scroll_left", "tab_scroll_right"};

	const float height = ref->GetWndSize().y;
	const float overlap = ref->Overlap();
	const float width = 2.0f * ref->CapWidthUI();

	CUILines* style = ref->TextItemControl();
	for (int s = 0; s < 2; ++s)
	{
		CUIScrollArrowButton* arrow = xr_new<CUIScrollArrowButton>();
		arrow->SetAutoDelete(true);
		arrow->InitButton(Fvector2().set(0.0f, 0.0f), Fvector2().set(width, height));
		arrow->SetOverlap(overlap);
		arrow->SetupHalves(ref->ArtBase());
		arrow->LayoutHalves();
		arrow->TextItemControl()->SetText(glyph[s]);
		arrow->SetWindowName(name[s]);

		if (style && style->GetFont())
			arrow->TextItemControl()->SetFont(style->GetFont());
		for (int st = S_Enabled; st <= S_Touched; ++st)
		{
			arrow->m_dwTextColor[st] = ref->m_dwTextColor[st];
			arrow->m_bUseTextColor[st] = ref->m_bUseTextColor[st];
		}
		if (m_parent)
			m_parent->AttachChild(arrow);
		arrow->SetMessageTarget(m_msg_target);
		m_arrow[s] = arrow;
	}
}

void CUITabScrollArrows::Layout(float view_right, float strip_y)
{
	for (int s = 0; s < 2; ++s)
		if (m_arrow[s])
			m_arrow[s]->SetWndPos(Fvector2().set((s == eLeft) ? 0.0f : view_right, strip_y));
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
