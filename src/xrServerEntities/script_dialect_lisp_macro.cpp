#include "stdafx.h"
#include "script_dialect_lisp_macro.h"
#include "lua_macros.h"

LPCSTR LISP_MACRO_TAG = ";dialect lisp-macro";

LPCSTR CLispMacroDialect::tag() const
{
    return LISP_MACRO_TAG;
}

std::string CLispMacroDialect::wrap_body(const std::string& src, LPCSTR caNameSpaceName) const
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
        )",
        caNameSpaceName,
        src
    );
}