#include "stdafx.h"
#include "script_dialect_lisp_macro.h"
#include "lua_macros.h"

const std::string TAG_LISP_MACRO = ";dialect lisp macro";

bool CLispMacroDialect::recognize(const std::string& src, LPCSTR caNameSpaceName) const
{
    return src.compare(0, TAG_LISP_MACRO.length(), TAG_LISP_MACRO) == 0;
}

std::string CLispMacroDialect::lift(const std::string& src, LPCSTR caNameSpaceName) const
{
    return string_format(
        R"(
local fennel = require("fennel")
table.insert(
    fennel["macro-searchers"],
    function(module_name)
        if module_name ~= "%s" then
            return
        end
        return function()
            return fennel.eval(
                [=[
                    %s
                ]=],
                {
                    correlate = true,
                    env = "_COMPILER",
                    useBitLib = true,
                    ["error-pinpoint"] = false,
                }
            )
        end
    end
)
package.loaded["%s"] = {}
        )",
        caNameSpaceName,
        src,
        caNameSpaceName
    );
}