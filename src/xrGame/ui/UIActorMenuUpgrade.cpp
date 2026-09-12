//#include "stdafx.h"
#include "pch_script.h"
#include "UIActorMenu.h"
#include "UIInventoryUpgradeWnd.h"
#include "UIInvUpgradeInfo.h"

#include "UIDragDropListEx.h"
#include "UIDragDropReferenceList.h"
#include "UICharacterInfo.h"

#include "../inventory_item.h"
#include "UICellItem.h"
#include "UICellItemFactory.h"
#include "../InventoryOwner.h"
#include "../Inventory.h"
#include "../actor.h"
#include "../uigamesp.h"
#include "UI3tButton.h"

#include "inventory_upgrade.h"
#include "UITalkWnd.h"

void CUIActorMenu::InitUpgradeMode()
{
	m_PartnerCharacterInfo->Show(true);
	m_PartnerMoney->Show(false);
	m_pUpgradeWnd->Show(true);
	m_pQuickSlot->Show(true);

	InitInventoryContents(m_pInventoryBagList);
	VERIFY(m_pPartnerInvOwner);
	m_pPartnerInvOwner->StartTrading();
	//-	UpdateUpgradeItem();
}

void CUIActorMenu::DeInitUpgradeMode()
{
	m_PartnerCharacterInfo->Show(false);
	m_pUpgradeWnd->Show(false);
	m_pUpgradeWnd->set_info_cur_upgrade(NULL);
	m_pUpgradeWnd->m_btn_repair->Enable(false);

	if (m_upgrade_selected)
	{
		m_upgrade_selected->Mark(false);
		m_upgrade_selected = NULL;
	}
	if (m_pPartnerInvOwner)
	{
		m_pPartnerInvOwner->StopTrading();
	}

	if (!CurrentGameUI())
		return;
	//только если находимся в режиме single
	CUIGameSP* pGameSP = smart_cast<CUIGameSP*>(CurrentGameUI());
	if (!pGameSP) return;

	if (pGameSP->TalkMenu->IsShown())
	{
		pGameSP->TalkMenu->NeedUpdateQuestions();
	}
}

void CUIActorMenu::SetupUpgradeItem()
{
	if (m_upgrade_selected)
	{
		m_upgrade_selected->Mark(false);
	}

	bool can_upgrade = false;
	PIItem item = CurrentIItem();
	if (item)
	{
		m_upgrade_selected = CurrentItem();
		m_upgrade_selected->Mark(true);
		can_upgrade = CanUpgradeItem(item);
	}

	m_pUpgradeWnd->InitInventory(item, can_upgrade);
	if (m_upgrade_info)
	{
		m_upgrade_info->Show(false);
	}

	UpdateUpgradeItem();
}

void CUIActorMenu::UpdateUpgradeItem()
{
	//	m_pUpgradeWnd->InitInventory( CurrentIItem() );
}

void CUIActorMenu::TrySetCurUpgrade()
{
	if (!m_upgrade_info) return;
	Upgrade_type const* upgr = m_upgrade_info->get_upgrade();
	if (!upgr) return;
	m_pUpgradeWnd->DBClickOnUIUpgrade(upgr);
}

bool CUIActorMenu::SetInfoCurUpgrade(Upgrade_type* upgrade_type, CInventoryItem* inv_item)
{
	if (!m_upgrade_info) return false;
	bool res = m_upgrade_info->init_upgrade(upgrade_type, inv_item);

	if (!upgrade_type)
	{
		return false;
	}

	fit_in_rect(m_upgrade_info, Frect().set(0.0f, 0.0f, UI_BASE_WIDTH, UI_BASE_HEIGHT), 0.0f, GetWndRect().left);
	return res;
}

PIItem CUIActorMenu::get_upgrade_item()
{
	return (m_upgrade_selected) ? (PIItem)m_upgrade_selected->m_pData : NULL;
}

void CUIActorMenu::SeparateUpgradeItem()
{
	VERIFY(m_upgrade_selected);
	if (!m_upgrade_selected || !m_upgrade_selected->m_pData)
	{
		return;
	}
	CUIDragDropListEx* list_owner = m_upgrade_selected->OwnerList();
	if (!list_owner)
	{
		return;
	}

	m_upgrade_selected->Mark(false);
	CUICellItem* ci = list_owner->RemoveItem(m_upgrade_selected, false);
	m_upgrade_selected = NULL;
	m_pCurrentCellItem = NULL;

	// Remove using the old grid size, then create a cell with the upgraded size.
	PIItem item = (PIItem)ci->m_pData;
	xr_delete(ci);
	ci = create_cell_item(item);
	list_owner->SetItem(ci);

	// SetItem may have merged the new cell into an existing stack.
	for (u32 i = 0; i < list_owner->ItemsCount(); ++i)
	{
		CUICellItem* root = list_owner->GetItemIdx(i);
		if (root == ci || root->HasChild(ci))
		{
			SetCurrentItem(root);
			InfoCurItem(root);
			break;
		}
	}
}
