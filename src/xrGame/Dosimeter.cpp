#include "stdafx.h"
#include "Dosimeter.h"
#include "UIDosimeter.h"

void CDosimeter::CreateUI()
{
	R_ASSERT(nullptr == m_ui);
	m_ui = xr_new<CUIDosimeter>();
	ui().construct(this);
}

CUIDosimeter& CDosimeter::ui()
{
	return *((CUIDosimeter*)m_ui);
}

void CDosimeter::render_item_3d_ui()
{
	R_ASSERT(HudItemData());
	ui().Draw();

	//	Restore cull mode
	inherited::render_item_3d_ui();
}
