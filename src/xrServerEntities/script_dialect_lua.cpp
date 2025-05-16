#include "stdafx.h"
#include "script_dialect_lua.h"

LPCSTR LUA_TAG = "--dialect lua";

LPCSTR LUA_WRAPPER = "setfenv(1, this)\n%s";

LPCSTR CLuaDialect::tag() const
{
    return LUA_TAG;
}

size_t CLuaDialect::wrap_ofs() const
{
    return xr_strlen(LUA_WRAPPER) - 1;
}

size_t CLuaDialect::wrap(LPSTR dest, LPCSTR src, size_t tSize) const
{
    size_t wrapper_size = wrap_ofs();
    size_t out_size = wrapper_size + tSize;
    xr_sprintf(dest, out_size, LUA_WRAPPER, src);
    return out_size - 1;
}
