#include "stdafx.h"
#include "script_dialect_lisp.h"
#include <iostream>

LPCSTR LISP_TAG = ";dialect lisp";

LPCSTR LISP_WRAPPER = R"(
require("fennel").eval(
    [=[
%s
    ]=],
    {
        allowedGlobals = false,
        correlate = true,
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

LPCSTR LISP_NAMESPACE_WRAPPER = R"(
local function script_name()
    return "%s"
end

%s %s
)";

LPCSTR CLispDialect::tag() const
{
    return LISP_TAG;
}

std::string CLispDialect::wrap_namespace(const std::string& src, LPCSTR caNameSpaceName) const
{
    return string_format(
        LISP_NAMESPACE_WRAPPER,
        caNameSpaceName,
        parse_namespace(caNameSpaceName),
        src
    );
}

std::string CLispDialect::wrap_body(const std::string& src, LPCSTR caNameSpaceName) const
{
    return string_format(LISP_WRAPPER, src);
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
