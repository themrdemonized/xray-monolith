#include "stdafx.h"
#include "script_dialect.h"

LPCSTR NAMESPACE_WRAPPER = R"(
local function script_name()
    return "%s"
end

local this = {}
%s this
setmetatable(this, {__index = _G})
setfenv(1, this)

%s
)";

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
    return string_format(
        NAMESPACE_WRAPPER,
        caNameSpaceName,
        parse_namespace(caNameSpaceName),
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
