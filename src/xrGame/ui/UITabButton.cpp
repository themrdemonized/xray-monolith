#include "StdAfx.h"
#include "UITabButton.h"
#include "UIStatic.h"

CUITabButton::CUITabButton()
{
}

CUITabButton::~CUITabButton()
{
}

bool CUITabButton::OnMouseAction(float x, float y, EUIMessages mouse_action)
{
	return CUIWindow::OnMouseAction(x, y, mouse_action);
}

bool CUITabButton::OnMouseDown(int mouse_btn)
{
	if (mouse_btn == MOUSE_1)
	{
		GetMessageTarget()->SendMessage(this, TAB_CHANGED, NULL);
		return true;
	}
	else
		return false;
}

void CUITabButton::SendMessage(CUIWindow* pWnd, s16 msg, void* pData)
{
	if (!IsEnabled())
		return;

	switch (msg)
	{
	case TAB_CHANGED:
		if (this == pWnd)
		{
			SetButtonState(BUTTON_PUSHED);
			OnClick();
		}
		else
		{
			SetButtonState(BUTTON_NORMAL);
		}
		break;
	default:
		;
	}
}

float CUITabButton::CapWidthUI() const
{
	const Frect art = ArtRegion();
	return (art.width() > 0.0f) ? art.height() * GetWndSize().x / art.width() : 0.0f;
}

float CUITabButton::CapOverlapUI() const
{
	return CapWidthUI();
}

Frect CUITabButton::ArtRegion() const
{
	if (m_background)
	{
		CUIStatic* bg = m_background->Get(S_Enabled);
		if (bg)
			return bg->GetTextureRect();
	}
	Frect r;
	r.set(0.0f, 0.0f, 0.0f, 0.0f);
	return r;
}

void CUITabButton::InitTexture(LPCSTR tex_name)
{
	m_art_base = tex_name;
	inherited::InitTexture(tex_name);
}

CUITabButton* CUITabButton::Clone(const Fvector2& pos, const Fvector2& size) const
{
	CUITabButton* b = xr_new<CUITabButton>();
	b->SetAutoDelete(true);
	b->InitButton(pos, size);
	if (m_art_base.size())
		b->InitTexture(m_art_base.c_str());
	b->SetOverlap(m_overlap);
	for (int st = S_Enabled; st <= S_Touched; ++st)
	{
		b->m_dwTextColor[st] = m_dwTextColor[st];
		b->m_bUseTextColor[st] = m_bUseTextColor[st];
	}
	return b;
}

u32 CUITabButton::SlantPoly(Fvector2* out, u32 max, const Fvector2& pos, const Fvector2& size, float slant)
{
	if (max < 4)
		return 0;
	const float xl = pos.x;
	const float xr = pos.x + size.x - slant;
	const float yt = pos.y;
	const float yb = pos.y + size.y;
	out[0].set(xl,         yb);
	out[1].set(xr,         yb);
	out[2].set(xr + slant, yt);
	out[3].set(xl + slant, yt);
	return 4;
}

u32 CUITabButton::HitPoly(Fvector2* out, u32 max) const
{
	return SlantPoly(out, max, GetWndPos(), GetWndSize(), Overlap());
}
