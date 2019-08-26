#include "pch_script.h"
#include "eatable_item.h"
#include "inventory_item.h"

using namespace luabind;

#pragma optimize("s",on)
void CEatableItem::script_register(lua_State *L)
{
	module(L)
		[
			class_<CEatableItem, CInventoryItem>("CEatableItem")
			//.def(constructor<>())
			.def("Empty", &CEatableItem::Empty)
			.def("CanDelete", &CEatableItem::CanDelete)
			.def("GetMaxUses", &CEatableItem::GetMaxUses)
			.def("GetRemainingUses", &CEatableItem::GetRemainingUses)
			.def("SetRemainingUses", &CEatableItem::SetRemainingUses)
			.def_readwrite("m_bRemoveAfterUse", &CEatableItem::m_bRemoveAfterUse)
			.def_readwrite("m_fWeightFull", &CEatableItem::m_fWeightFull)
			.def_readwrite("m_fWeightEmpty", &CEatableItem::m_fWeightEmpty)
			.def("Weight", &CEatableItem::Weight)
			.def("Cost", &CEatableItem::Cost)
		];
}