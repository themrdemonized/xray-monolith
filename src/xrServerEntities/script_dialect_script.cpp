#include "stdafx.h"
#include "pch_script.h"
#include "script_dialect.h"

using namespace luabind;

#pragma optimize("s",on)
void CScriptDialect::script_register(lua_State* L)
{
	module(L)
		[
			class_<CScriptDialect>("CScriptDialect")
			.def("recognize", &CScriptDialect::recognize)
			.def("unlocalize", &CScriptDialect::unlocalize)
			.def("lift", &CScriptDialect::lift)
		];
}
