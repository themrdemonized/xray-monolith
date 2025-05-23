#include "stdafx.h"

#include "luabind/luabind.hpp"
#include "script_storage.h"
#include "ai_space.h"
#include "script_engine.h"
#include "object_item_script.h"

using namespace luabind;

#pragma optimize("s",on)
void CScriptStorage::script_register(lua_State* L)
{
	module(L)
		[
			class_<CScriptStorage>("CScriptStorage")
				.def("get", &CScriptStorage::get)
				.def("set", &CScriptStorage::set)
		];
}