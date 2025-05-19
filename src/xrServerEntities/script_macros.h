#pragma once

#include "stdafx.h"
#include "script_storage.h"
#include "script_macro.h"
#include "script_macro_wua.h"

namespace luabind
{
	template <typename return_type>
	class functor;

	class object;
} // namespace luabind

struct CScriptMacros {
	CWuaMacro wua;

	std::string lift(
		std::string caString,
		LPCSTR caScriptName,
		LPCSTR caNameSpaceName = 0,
		Unlocalizers* unlocalizers = 0
	) const;
};

static CScriptMacros macros;
static const CScriptMacros& ScriptMacros()
{
	return macros;
}