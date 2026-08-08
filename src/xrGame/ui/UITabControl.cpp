#include "StdAfx.h"
#include "UITabControl.h"
#include "UITabButton.h"
#include "UITabScrollArrows.h"
#include "UI3tButton.h"
#include "UIStatic.h"

CUITabControl::CUITabControl()
	: m_cGlobalTextColor(0xFFFFFFFF),
	  m_cActiveTextColor(0xFFFFFFFF),
	  m_cActiveButtonColor(0xFFFFFFFF),
	  m_cGlobalButtonColor(0xFFFFFFFF),
	  m_bAcceleratorsEnable(true),
	  m_content_w(0.0f),
	  m_strip_w(0.0f),
	  m_view_left(0.0f),
	  m_view_right(0.0f),
	  m_margin(0.0f),
	  m_arrows(NULL)
{
	m_arrows = xr_new<CUITabScrollArrows>();
	m_arrows->Init(this, this);
}

CUITabControl::~CUITabControl()
{
	xr_delete(m_arrows); // detaches the arrow widgets while this parent is still valid
	RemoveAll();
}

void CUITabControl::SetCurrentOptValue()
{
	CUIOptionsItem::SetCurrentOptValue();
	shared_str v = GetOptStringValue();
	CUITabButton* b = GetButtonById(v);
	if (NULL == b)
	{
#ifndef MASTER_GOLD
		Msg("! tab named [%s] doesnt exist", v.c_str());
#endif // #ifndef MASTER_GOLD
		v = m_TabsArr[0]->m_btn_id;
	}
	SetActiveTab(v);
}

void CUITabControl::SaveOptValue()
{
	CUIOptionsItem::SaveOptValue();
	SaveOptStringValue(GetActiveId().c_str());
}

void CUITabControl::UndoOptValue()
{
	SetActiveTab(m_opt_backup_value);
	CUIOptionsItem::UndoOptValue();
}

void CUITabControl::SaveBackUpOptValue()
{
	CUIOptionsItem::SaveBackUpOptValue();
	m_opt_backup_value = GetActiveId();
}

bool CUITabControl::IsChangedOptValue() const
{
	return GetActiveId() != m_opt_backup_value;
}

// добавление кнопки-закладки в список закладок контрола
bool CUITabControl::AddItem(LPCSTR pItemName, LPCSTR pTexName, Fvector2 pos, Fvector2 size)
{
	CUITabButton* pNewButton = xr_new<CUITabButton>();
	pNewButton->SetAutoDelete(true);
	pNewButton->InitButton(pos, size);
	pNewButton->InitTexture(pTexName);
	pNewButton->TextItemControl()->SetText(pItemName);
	pNewButton->TextItemControl()->SetTextColor(m_cGlobalTextColor);
	pNewButton->SetTextureColor(m_cGlobalButtonColor);

	return AddItem(pNewButton);
}

bool CUITabControl::AddItem(CUITabButton* pButton)
{
	return InsertItem(pButton, m_TabsArr.size());
}

bool CUITabControl::InsertItem(CUITabButton* pButton, u32 at)
{
	pButton->SetAutoDelete(true);
	pButton->Show(true);
	pButton->Enable(true);
	pButton->SetButtonAsSwitch(true);

	AttachChild(pButton);
	m_TabsArr.insert(m_TabsArr.begin() + at, pButton);
	// Seam geometry (pitch = width - overlap + margin) is authored: overlap is the drawn end cap (from the
	// art), the leftover pitch is the strip margin, fixed once from the XML rest positions before any scroll.
	// Seed from the first two non-parked non-dynamic tabs
	// (index-agnostic, so a parked zero-width stub anywhere in the strip can't defeat it); every later strip
	// tab inherits it; dynamic tabs arrive pre-seeded (AddTab copies the reference tab's). Non-dynamic tabs
	// are only ever appended, so `pButton` is the second such tab when the count first reaches 2. The strip
	// is assumed to be single-overlap packed from layout x 0 -- laying it out that way is the caller's
	// responsibility. Each tab then owns its shape.
	if (!pButton->m_dynamic && pButton->GetWndSize().x > 0.0f)
	{
		CUITabButton* first = NULL;
		u32 strip_n = 0;
		for (u32 i = 0; i < m_TabsArr.size(); ++i)
		{
			CUITabButton* t = m_TabsArr[i];
			if (t->m_dynamic || t->GetWndSize().x <= 0.0f)
				continue;
			if (!first)
				first = t;
			++strip_n;
		}
		if (strip_n == 2)
		{
			// Split the authored pitch: overlap is the drawn end cap (from the art); the leftover is the strip
			// margin. Deriving overlap as width - pitch would fold the gap in (that value is overlap - margin).
			const float overlap = first->CapOverlapUI();
			const float pitch = pButton->GetWndPos().x - first->GetWndPos().x;
			m_margin = pitch - (first->GetWndSize().x - overlap);
			first->SetOverlap(overlap);
			pButton->SetOverlap(overlap);
		}
		else if (strip_n > 2)
			pButton->SetOverlap(first->Overlap()); // inherit the strip overlap
	}
	R_ASSERT(pButton->m_btn_id.size());
	return true;
}

void CUITabControl::RemoveAll()
{
	TABS_VECTOR_it it = m_TabsArr.begin();
	for (; it != m_TabsArr.end(); ++it)
	{
		DetachChild(*it);
	}
	m_TabsArr.clear();
	m_content_w = 0.0f;
	m_margin = 0.0f;
	if (m_arrows)
		m_arrows->Show(false);
}

void CUITabControl::SendMessage(CUIWindow* pWnd, s16 msg, void* pData)
{
	// Scroll-arrow clicks (handled before the accelerator gate so they work regardless of mode)
	if (BUTTON_CLICKED == msg)
	{
		const int side = m_arrows->SideOf(pWnd);
		if (side == CUITabScrollArrows::eLeft)
		{
			ScrollBy(-ScrollStep());
			return;
		}
		if (side == CUITabScrollArrows::eRight)
		{
			ScrollBy(ScrollStep());
			return;
		}
	}

	if (!GetAcceleratorsMode())
		return;

	if (TAB_CHANGED == msg)
	{
		for (u32 i = 0; i < m_TabsArr.size(); ++i)
		{
			if (m_TabsArr[i] == pWnd)
			{
				m_sPushedId = m_TabsArr[i]->m_btn_id;
				if (m_sPrevPushedId == m_sPushedId)
					return;

				OnTabChange(m_sPushedId, m_sPrevPushedId);
				m_sPrevPushedId = m_sPushedId;
				break;
			}
		}
	}

	else if (WINDOW_FOCUS_RECEIVED == msg ||
		WINDOW_FOCUS_LOST == msg)
	{
		for (u8 i = 0; i < m_TabsArr.size(); ++i)
		{
			if (pWnd == m_TabsArr[i])
			{
				if (msg == WINDOW_FOCUS_RECEIVED)
					OnStaticFocusReceive(pWnd);
				else
					OnStaticFocusLost(pWnd);
			}
		}
	}
	else
	{
		inherited::SendMessage(pWnd, msg, pData);
	}
}

void CUITabControl::OnStaticFocusReceive(CUIWindow* pWnd)
{
	GetMessageTarget()->SendMessage(this, WINDOW_FOCUS_RECEIVED, static_cast<void*>(pWnd));
}

void CUITabControl::OnStaticFocusLost(CUIWindow* pWnd)
{
	GetMessageTarget()->SendMessage(this, WINDOW_FOCUS_LOST, static_cast<void*>(pWnd));
}

void CUITabControl::OnTabChange(const shared_str& sCur, const shared_str& sPrev)
{
	CUITabButton* tb_cur = GetButtonById(sCur);
	CUITabButton* tb_prev = GetButtonById(sPrev);
	if (tb_prev)
		tb_prev->SendMessage(tb_cur, TAB_CHANGED, NULL);

	tb_cur->SendMessage(tb_cur, TAB_CHANGED, NULL);

	if (GetAcceleratorsMode())
		GetMessageTarget()->SendMessage(this, TAB_CHANGED, NULL);
}

void CUITabControl::SetActiveTab(const shared_str& sNewTab)
{
	if (m_sPushedId == sNewTab)
		return;

	m_sPushedId = sNewTab;
	OnTabChange(m_sPushedId, m_sPrevPushedId);

	m_sPrevPushedId = m_sPushedId;
	EnsureVisible(m_sPushedId);
}

bool CUITabControl::OnKeyboardAction(int dik, EUIMessages keyboard_action)
{
	if (GetAcceleratorsMode() && WINDOW_KEY_PRESSED == keyboard_action)
	{
		for (u32 i = 0; i < m_TabsArr.size(); ++i)
		{
			if (m_TabsArr[i]->IsAccelerator(dik))
			{
				SetActiveTab(m_TabsArr[i]->m_btn_id);
				return true;
			}
		}
	}
	return false;
}

bool operator ==(const CUITabButton* btn, const shared_str& id)
{
	return (btn->m_btn_id == id);
}

CUITabButton* CUITabControl::GetButtonById(const shared_str& id)
{
	TABS_VECTOR::const_iterator it = std::find(m_TabsArr.begin(), m_TabsArr.end(), id);
	if (it != m_TabsArr.end())
		return *it;
	else
		return NULL;
}

/*
const shared_str CUITabControl::GetCommandName(const shared_str& id)
{ 
	CUITabButton* tb			= GetButtonById(id);
	R_ASSERT2					(tb, id.c_str());

	return (GetButtonByIndex(i))->WindowName();
};

CUIButton* CUITabControl::GetButtonByCommand(const shared_str& n)
{
	for(u32 i = 0; i<m_TabsArr.size(); ++i)
		if(m_TabsArr[i]->WindowName() == n)
			return m_TabsArr[i];

	return NULL;
}*/

void CUITabControl::ResetTab()
{
	for (u32 i = 0; i < m_TabsArr.size(); ++i)
	{
		m_TabsArr[i]->SetButtonState(CUIButton::BUTTON_NORMAL);
	}
	m_sPushedId = "";
	m_sPrevPushedId = "";
}

LPCSTR CUITabControl::GetActiveId_script()
{
	LPCSTR res = GetActiveId().c_str();
	return res;
}

void CUITabControl::Enable(bool status)
{
	for (u32 i = 0; i < m_TabsArr.size(); ++i)
		m_TabsArr[i]->Enable(status);

	//	m_sPushedId		= "";
	//	m_sPrevPushedId	= "";
	inherited::Enable(status);
}
