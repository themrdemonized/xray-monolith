#include "stdafx.h"
#include "script_macro.h"
#include "lua_macros.h"

std::string CScriptMacro::lift(const std::string& src, LPCSTR caNameSpaceName) const
{
    return src;
}

std::string CScriptMacro::unlocalize(const std::string& src, LPCSTR caNameSpaceName, Unlocalizer& unlocalizer) const
{
    return src;
}
