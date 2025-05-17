#include "stdafx.h"
#include "script_dialect_lisp.h"
#include <iostream>

LPCSTR LISP_TAG = ";dialect lisp";

LPCSTR LISP_WRAPPER = R"(
local function script_name()
   return "%s"
end

local this = {}
%s this %s
setmetatable(this, {__index = _G})

require("fennel").eval(
    [=[
%s
    ]=],
    {
        allowedGlobals = false,
        correlate = true,
        env = this,
        useBitLib = true,
        ["error-pinpoint"] = false,
    }
)
)";

LPCSTR LISP_UNLOCALIZE_WRAPPER = R"(
(import-macros {: unlocalize} :lisp_unlocalize)
(unlocalize
  [%s]
  %s)
)";

LPCSTR CLispDialect::tag() const
{
    return LISP_TAG;
}

std::string CLispDialect::wrap(const std::string& src, LPCSTR caNameSpaceName) const
{
    string512 a, b;
    if (!parse_namespace(caNameSpaceName, a, sizeof(a), b, sizeof(b)))
        return (false);

    return string_format(LISP_WRAPPER, caNameSpaceName, a, b, src);
}

std::string CLispDialect::unlocalize(Unlocalizer& unlocalizer, const std::string& src, LPCSTR caNameSpaceName) const
{
    std::string unlocs;
    for (auto unloc : unlocalizer)
    {
        if (unlocs.length() > 0)
            unlocs += " ";
        unlocs += unloc;
    }
    return string_format(LISP_UNLOCALIZE_WRAPPER, unlocs, src);
}
