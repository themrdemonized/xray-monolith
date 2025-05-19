#include "stdafx.h"
#include "pch_script.h"
#include "script_macro.h"

using namespace luabind;

#pragma optimize("s",on)
void CScriptMacro::script_register(lua_State* L)
{
	module(L)
		[
			class_<CScriptMacro>("CScriptMacro")
			.def("unlocalize", &CScriptMacro::unlocalize)
			.def("lift", &CScriptMacro::lift)
		];
}
