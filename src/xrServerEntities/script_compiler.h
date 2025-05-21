#pragma once

#include "stdafx.h"
#include "script_storage.h"
#include "script_macro.h"

namespace luabind
{
	template <typename return_type>
	class functor;

	class object;
} // namespace luabind

struct CScriptCompiler {
public:
	int compile(
		lua_State* L,
		std::string caString,
		LPCSTR caScriptName,
		LPCSTR caNameSpaceName = 0
	);
};

static CScriptCompiler compiler;
static CScriptCompiler& ScriptCompiler()
{
	return compiler;
}