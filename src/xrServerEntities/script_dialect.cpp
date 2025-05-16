#include "stdafx.h"
#include "script_dialect.h"

size_t CScriptDialect::tag_length() const
{
    return xr_strlen(tag());
}

bool CScriptDialect::parse(LPCSTR src) const
{
    return strncmp(tag(), src, tag_length()) == 0;
}
