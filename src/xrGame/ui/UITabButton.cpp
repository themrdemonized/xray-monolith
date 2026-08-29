#include "StdAfx.h"
#include "UITabButton.h"
#include "UIStatic.h"
#include "UITextureMaster.h"

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
	return (art.width() > 0.0f) ? CapWidthTexels(art) * GetWndSize().x / art.width() : 0.0f;
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

static bool ArtExists(LPCSTR name)
{
	return CUITextureMaster::FindItem(name).file.size() != 0;
}

void CUITabButton::ClearIcon()
{
	if (m_icon)
	{
		DetachChild(m_icon);
		m_icon = NULL;
	}
}

bool CUITabButton::SetIcon(LPCSTR base_name)
{
	if (!base_name || !xr_strlen(base_name))
		return false;

	string256 buf;
	const bool staged = ArtExists(strconcat(sizeof(buf), buf, base_name, "_e"));
	if (!staged && !ArtExists(base_name))
		return false;

	ClearIcon();

	m_icon = xr_new<CUI_IB_Static>();
	m_icon->SetAutoDelete(true);
	m_icon->SetCustomDraw(true);
	AttachChild(m_icon);

	if (staged)
	{
		static const LPCSTR suffix[4] = {"_e", "_d", "_h", "_t"};
		static const IBState slot[4] = {S_Enabled, S_Disabled, S_Highlighted, S_Touched};
		for (int i = 0; i < 4; ++i)
		{
			strconcat(sizeof(buf), buf, base_name, suffix[i]);
			if (ArtExists(buf))
				m_icon->InitState(slot[i], buf);
		}
	}
	else
		m_icon->InitState(S_Enabled, base_name);

	for (int i = 0; i < S_Current; ++i)
		if (CUIStatic* art = m_icon->Get((IBState)i))
			art->SetStretchTexture(true);

	m_icon->SetCurrentState(CurrentIBState());
	return true;
}

bool CUITabButton::IconHasStateArt() const
{
	return m_icon && (m_icon->Get(S_Disabled) || m_icon->Get(S_Highlighted) || m_icon->Get(S_Touched));
}

void CUITabButton::FitIcon(const Fvector2& box, const Fvector2& pos, const Fvector2& anchor)
{
	if (!m_icon)
		return;

	Fvector2 size = box;
	if (CUIStatic* art = m_icon->Get(S_Enabled))
	{
		const Frect& native = art->GetTextureRect();
		if (native.width() > 0.0f && native.height() > 0.0f)
		{
			const float scale = _min(box.x / native.width(), box.y / native.height());
			size.set(native.width() * scale, native.height() * scale);
		}
	}

	const Fvector2 tab = GetWndSize();
	m_icon->SetWndPos(Fvector2().set((tab.x - size.x) * anchor.x + pos.x,
	                                 (tab.y - size.y) * anchor.y + pos.y));
	m_icon->SetWndSize(size);
	m_icon->SetWidth(size.x);
	m_icon->SetHeight(size.y);
}

void CUITabButton::DrawTexture()
{
	inherited::DrawTexture();
	if (m_icon && GetWndSize().x > 0.0f)
		m_icon->Draw();
}

void CUITabButton::Update()
{
	inherited::Update();

	if (!m_icon)
		return;

	const IBState state = CurrentIBState();
	m_icon->SetCurrentState(state);
	if (CUIStatic* art = m_icon->Get(S_Current))
		art->SetTextureColor(IconHasStateArt() ? 0xFFFFFFFF : StateTextColor(state));
}

CUITabButton* CUITabButton::Clone(const Fvector2& pos, const Fvector2& size)
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
	CUILines* src = TextItemControl();
	CUILines* dst = b->TextItemControl();
	dst->SetFont(src->GetFont());
	dst->SetTextAlignment(src->GetTextAlignment());
	dst->SetVTextAlignment(src->GetVTextAlignment());
	dst->m_TextOffset = src->m_TextOffset;
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
	return SlantPoly(out, max, GetWndPos(), GetWndSize(), Seam());
}
