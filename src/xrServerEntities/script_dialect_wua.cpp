#include "stdafx.h"
#include "script_dialect_wua.h"
#include "lua_macros.h"

#include <sstream>
#include <regex>
#include "../xrCore/mezz_stringbuffer.h"

const std::string TAG_WUA = "--dialect wua";

bool CWuaDialect::recognize(const std::string& src, LPCSTR caNameSpaceName) const
{
    return src.compare(0, TAG_WUA.length(), TAG_WUA) == 0;
}

std::string CWuaDialect::lift(const std::string& src, LPCSTR caNameSpaceName) const
{
    return lines(
        wua_environment("G"),
        R"(
local this = setmetatable(
    { _G = G },
    { __index = G }
)
        )",
        string_format(
            R"(
package.loaded["%s"] = this
setfenv(1, this)

local function script_name()
    return "%s"
end
            )",
            caNameSpaceName,
            caNameSpaceName
        ),
        src
    );
}
