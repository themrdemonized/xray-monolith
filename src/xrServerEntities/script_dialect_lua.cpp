#include "stdafx.h"
#include "script_dialect_lua.h"
#include "lua_macros.h"

#include <sstream>
#include <regex>
#include "../xrCore/mezz_stringbuffer.h"

const std::string TAG_LUA = "--dialect lua";

bool CLuaDialect::recognize(const std::string& src, LPCSTR caNameSpaceName) const
{
    return src.compare(0, TAG_LUA.length(), TAG_LUA) == 0;
}

std::string CLuaDialect::lift(const std::string& src, LPCSTR caNameSpaceName) const
{
    return lines(
        string_format(
            R"(
local function f()
    local function script_name()
        return "%s"
    end

    %s
end

package.loaded["%s"] = f()
            )",
            caNameSpaceName,
            src,
            caNameSpaceName
        )
    );
}
