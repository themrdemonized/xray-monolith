////////////////////////////////////////////////////////////////////////////
// Module : ai_script_lua_debug.cpp
// Created : 19.09.2003
// Modified : 19.09.2003
// Author : Dmitriy Iassenev
// Description : XRay Script debugging system
////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "ai_script_space.h"
#include "ai_script_lua_extension.h"

using namespace Script;

bool Script::bfPrintOutput(CLuaVirtualMachine* tpLuaVirtualMachine, LPCSTR caScriptFileName, int iErorCode)
{
	for (int i = -1;; --i)
		if (lua_isstring(tpLuaVirtualMachine, i))
		{
			LPCSTR S = lua_tostring(tpLuaVirtualMachine, i);
			if (!xr_strcmp(S, "cannot resume dead coroutine"))
			{
				LuaOut(Lua::eLuaMessageTypeInfo, "Script %s is finished", caScriptFileName);
				return (true);
			}
			else
			{
				if (!i && !iErorCode)
					LuaOut(Lua::eLuaMessageTypeInfo, "Output from %s", caScriptFileName);
				LuaOut(iErorCode ? Lua::eLuaMessageTypeError : Lua::eLuaMessageTypeMessage, "%s", S);
			}
		}
		else
		{
			for (i = 0;; ++i)
				if (lua_isstring(tpLuaVirtualMachine, i))
				{
					LPCSTR S = lua_tostring(tpLuaVirtualMachine, i);
					if (!xr_strcmp(S, "cannot resume dead coroutine"))
					{
						LuaOut(Lua::eLuaMessageTypeInfo, "Script %s is finished", caScriptFileName);
						return (true);
					}
					else
					{
						if (!i && !iErorCode)
							LuaOut(Lua::eLuaMessageTypeInfo, "Output from %s", caScriptFileName);
						LuaOut(iErorCode ? Lua::eLuaMessageTypeError : Lua::eLuaMessageTypeMessage, "%s", S);
					}
				}
				else
					return (false);
		}
}

void Script::vfPrintError(CLuaVirtualMachine* tpLuaVirtualMachine, int iErrorCode)
{
	switch (iErrorCode)
	{
	case LUA_ERRRUN:
		{
			Msg("! SCRIPT RUNTIME ERROR");
			break;
		}
	case LUA_ERRMEM:
		{
			Msg("! SCRIPT ERROR (memory allocation)");
			break;
		}
	case LUA_ERRERR:
		{
			Msg("! SCRIPT ERROR (while running the error handler function)");
			break;
		}
	case LUA_ERRFILE:
		{
			Msg("! SCRIPT ERROR (while running file)");
			break;
		}
	case LUA_ERRSYNTAX:
		{
			Msg("! SCRIPT SYNTAX ERROR");
			break;
		}
	default:
		NODEFAULT;
	}

	for (int i = 0;; ++i)
	{
		Msg("! Stack level %d", i);
		if (!bfListLevelVars(tpLuaVirtualMachine, i))
			return;
	}
}

LPCSTR Script::cafEventToString(int iEventCode)
{
	switch (iEventCode)
	{
	case LUA_HOOKCALL:
		return ("hook call");
	case LUA_HOOKRET:
		return ("hook return");
	case LUA_HOOKLINE:
		return ("hook line");
	case LUA_HOOKCOUNT:
		return ("hook count");
	case LUA_HOOKTAILRET:
		return ("hook tail return");
	default:
		NODEFAULT;
	}
#ifdef DEBUG
    return(0);
#endif
}

bool Script::bfListLevelVars(CLuaVirtualMachine* tpLuaVirtualMachine, int iStackLevel)
{
	return (false);
}
