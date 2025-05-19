#include "stdafx.h"
#include "script_dialect.h"
#include "lua_macros.h"

bool CScriptDialect::recognize(const std::string& src, LPCSTR caNameSpaceName) const
{
    return false;
}

std::string CScriptDialect::lift(const std::string& src, LPCSTR caNameSpaceName) const
{
    return src;
}

std::string CScriptDialect::unlocalize(Unlocalizer& unlocalizer, const std::string& src, LPCSTR caNameSpaceName) const
{
    return src;
}
