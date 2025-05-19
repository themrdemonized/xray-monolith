#include "stdafx.h"
#include "script_dialect_lisp.h"
#include "lua_macros.h"
#include <iostream>

const std::string TAG_LISP = ";dialect lisp";

bool CLispDialect::recognize(const std::string& src, LPCSTR caNameSpaceName) const
{
    return src.compare(0, TAG_LISP.length(), TAG_LISP) == 0;
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
    return string_format(
        R"(
(import-macros {: unlocalize} :lisp_unlocalize)
(unlocalize
  [%s]
  %s)
        )",
        unlocs,
        src
    );
}

std::string CLispDialect::lift(const std::string& src, LPCSTR caNameSpaceName) const
{
    return string_format(
        R"(
package.loaded["%s"] = require("fennel").eval(
    [=[
(fn script_name []
  "%s")
%s
    ]=],
    {
        allowedGlobals = false,
        correlate = true,
        useBitLib = true,
        ["error-pinpoint"] = false,
    }
)
        )",
        caNameSpaceName,
        caNameSpaceName,
        src
    );
}
