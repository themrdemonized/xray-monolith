#include "pch_script.h"
#include "inventory_item.h"

using namespace luabind;

#pragma optimize("s",on)
void CInventoryItem::script_register(lua_State *L)
{
	module(L)
		[
			class_<CInventoryItem>("CInventoryItem")
			//.def(constructor<>())
		];
}