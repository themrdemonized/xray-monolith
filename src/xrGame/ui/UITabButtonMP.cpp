#include "StdAfx.h"
#include "UITabButtonMP.h"

CUITabButtonMP::CUITabButtonMP()
{
	m_orientationVertical = true;
	m_text_ident_cursor_over.set(0, 0);
	m_text_ident_normal.set(0, 0);
	m_hint = NULL;
}

void CUITabButtonMP::CreateHint()
{
	m_hint = xr_new<CUIStatic>();
	m_hint->SetAutoDelete(true);
	m_hint->SetCustomDraw(true);
	AttachChild(m_hint);
}

void CUITabButtonMP::SendMessage(CUIWindow* pWnd, s16 msg, void* pData)
{
	if (this == pWnd)
		m_bIsEnabled = true;

	CUITabButton::SendMessage(pWnd, msg, pData);
}

void CUITabButtonMP::UpdateTextAlign()
{
	if (m_bCursorOverWindow)
	{
		TextItemControl()->m_TextOffset = m_text_ident_cursor_over;
	}
	else
	{
		TextItemControl()->m_TextOffset = m_text_ident_normal;
	}
}


void CUITabButtonMP::Update()
{
	bool tempEnabled = m_bIsEnabled;
	m_bIsEnabled = m_bCursorOverWindow ? true : m_bIsEnabled;
	inherited::Update();
	m_bIsEnabled = tempEnabled;
	UpdateTextAlign();

	if (m_hint)
		m_hint->TextItemControl()->SetTextColor(StateTextColor(CurrentIBState()));
}

void CUITabButtonMP::Draw()
{
	CUITabButton::Draw();

	if (m_hint)
		m_hint->Draw();
}
