#include "pch_script.h"
#include "InventoryOwner.h"
#include "GameObject.h"

using namespace luabind;

#pragma optimize("s",on)
void CInventoryOwner::script_register(lua_State *L)
{
	module(L)
		[
			class_<CInventoryOwner>("CInventoryOwner")
			//.def(constructor<>())
			.def("IconName", &CInventoryOwner::IconName)
			.def("get_money", &CInventoryOwner::get_money)
			.def("EnableTalk", &CInventoryOwner::EnableTalk)
			.def("DisableTalk", &CInventoryOwner::DisableTalk)
			.def("IsTalkEnabled", &CInventoryOwner::IsTalkEnabled)
			.def("EnableTrade", &CInventoryOwner::EnableTrade)
			.def("DisableTrade", &CInventoryOwner::DisableTrade)
			.def("IsTradeEnabled", &CInventoryOwner::IsTradeEnabled)
			.def("EnableInvUpgrade", &CInventoryOwner::EnableInvUpgrade)
			.def("DisableInvUpgrade", &CInventoryOwner::DisableInvUpgrade)
			.def("IsInvUpgradeEnabled", &CInventoryOwner::IsInvUpgradeEnabled)
			.def("GetTalkPartner", &CInventoryOwner::GetTalkPartner)
			.def("OfferTalk", &CInventoryOwner::OfferTalk)
			.def("StartTalk", &CInventoryOwner::StartTalk)
			.def("StopTalk", &CInventoryOwner::StopTalk)
			.def("IsTalking", &CInventoryOwner::IsTalking)
			.def("deadbody_can_take", &CInventoryOwner::deadbody_can_take)
			.def("deadbody_can_take_status", &CInventoryOwner::deadbody_can_take_status)
			.def("deadbody_closed", &CInventoryOwner::deadbody_closed)
			.def("deadbody_closed_status", &CInventoryOwner::deadbody_closed_status)
		];
}