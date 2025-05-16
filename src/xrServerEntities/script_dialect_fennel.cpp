#include "stdafx.h"
#include "script_dialect_fennel.h"

LPCSTR FENNEL_TAG = ";dialect fennel";

LPCSTR FENNEL_WRAPPER = "\
require(\"fennel\").eval(\n\
    [=[\n\
%s\n\
    ]=],\n\
    {\n\
        allowedGlobals = false,\n\
        correlate = true,\n\
        env = this,\n\
        useBitLib = true,\n\
    }\n\
)\n";

LPCSTR CFennelDialect::tag() const
{
    return FENNEL_TAG;
}

size_t CFennelDialect::wrap_ofs() const
{
    return xr_strlen(FENNEL_WRAPPER) - 1;
}

size_t CFennelDialect::wrap(LPSTR dest, LPCSTR src, size_t tSize) const
{
    size_t wrapper_size = wrap_ofs();
    size_t out_size = wrapper_size + tSize;
    xr_sprintf(dest, out_size, FENNEL_WRAPPER, src);
    return out_size - 1;
}
