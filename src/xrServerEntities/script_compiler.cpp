#include "stdafx.h"
#include "script_compiler.h"
#include "ai_space.h"
#include "script_engine.h"
#include "../xrCore/mezz_stringbuffer.h"
#include "lua_macros.h"
#include "luabind/luabind.hpp"
#include <regex>
#include <lua.h>

int CScriptCompiler::compile(
    lua_State* L,
    std::string caString,
    LPCSTR caScriptName,
    LPCSTR caNameSpaceName
)
{
    luabind::functor<luabind::object> compile;
    if (ai().script_engine().namespace_loaded("script_compiler", true))
    {
        if (ai().script_engine().functor("script_compiler.compile", compile))
        {
            luabind::object result = compile(caString.c_str(), caScriptName, caNameSpaceName);
            result.pushvalue();
            return 0;
        }
    }

    Msg("script_compiler not available, loading as raw Lua...");
    return luaL_loadbuffer(L, caString.c_str(), caString.length(), caScriptName);
}
