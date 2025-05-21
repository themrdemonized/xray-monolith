#include "stdafx.h"
#include "pch_script.h"
#include "script_macro.h"
#include "macro_wua.h"

using namespace luabind;

#pragma optimize("s",on)
void CScriptMacro::script_register(lua_State* L)
{
	module(L)
		[
			def("compile_wua", &compile_wua)
		];
}
