#include "stdafx.h"
#include "script_dialect.h"
#include "lua_macros.h"

size_t CScriptDialect::tag_length() const
{
    return xr_strlen(tag());
}

bool CScriptDialect::parse(const std::string& src) const
{
    return strncmp(tag(), src.c_str(), tag_length()) == 0;
}

std::string CScriptDialect::wrap_namespace(const std::string& src, LPCSTR caNameSpaceName) const
{
    return lines(
        script_name_getter(caNameSpaceName),
        assign_local("this", "{}"),
        string_format(
            R"(
setmetatable(
    this,
    {
        __index = function(_, key)
            local gv = _G[key]
            if gv ~= nil then
                return gv
            end
        end
    }
)
            )"
        ),
        assign_path("_G", caNameSpaceName, "this"),
        scope_to("this"),
        src
    );
}

std::string CScriptDialect::wrap_body(const std::string& src, LPCSTR caNameSpaceName) const
{
    return src;
}

std::string CScriptDialect::unlocalize(Unlocalizer& unlocalizer, const std::string& src, LPCSTR caNameSpaceName) const
{
    return src;
}
