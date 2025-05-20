#pragma once

#include "stdafx.h"
#include "script_storage.h"
#include "script_macro.h"
#include "script_macro_wua.h"

typedef std::set<std::string> Unlocalizer;
typedef xr_unordered_map<std::string, Unlocalizer> Unlocalizers;

namespace luabind
{
	template <typename return_type>
	class functor;

	class object;
} // namespace luabind

struct CScriptCompiler {
public:
	void load_unlocalizers();
	Unlocalizer* CScriptCompiler::get_unlocalizer(std::string name);

	std::string lift(
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