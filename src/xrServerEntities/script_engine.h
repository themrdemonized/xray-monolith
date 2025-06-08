////////////////////////////////////////////////////////////////////////////
//	Module 		: script_engine.h
//	Created 	: 01.04.2004
//  Modified 	: 01.04.2004
//	Author		: Dmitriy Iassenev
//	Description : XRay Script Engine
////////////////////////////////////////////////////////////////////////////

#pragma once

#include "script_engine_space.h"
#include "script_export_space.h"
#include "script_space_forward.h"
#include "script_processes.h"
#include "associative_vector.h"

//AVO: lua re-org
#include "lua.hpp"
/*extern "C" {
#include <lua/lua.h>
}*/
//-AVO

//#define DBG_DISABLE_SCRIPTS

#ifdef XRGAME_EXPORTS
#	ifndef MASTER_GOLD
#		define PRINT_CALL_STACK
#	endif //-!MASTER_GOLD
#else //!XRGAME_EXPORTS
#	ifndef NDEBUG
#		define PRINT_CALL_STACK
#	endif // #ifndef NDEBUG
#endif //-XRGAME_EXPORTS

//AVO: allow LUA debug prints (i.e.: ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError, "CWeapon : cannot access class member Weapon_IsScopeAttached!");)
#include "..\build_config_defines.h"
#ifndef DEBUG
#   ifdef LUA_DEBUG_PRINT
#       define PRINT_CALL_STACK
#   endif
#endif //-!DEBUG
//-AVO

using namespace ScriptEngine;

struct lua_State;
struct lua_Debug;

class CScriptEngine
{
private:
	lua_State* m_virtual_machine;

protected:
	int m_stack_level;

#ifdef DEBUG
public:
	bool m_stack_is_ready;
#endif //-DEBUG

#ifdef LUA_DEBUG_PRINT//PRINT_CALL_STACK
protected:
	CMemoryWriter m_output;
#else
#   ifdef DEBUG
protected:
	CMemoryWriter m_output;
#   endif //-DEBUG
#endif //-LUA_DEBUG_PRINT PRINT_CALL_STACK

public:
	CScriptEngine();
	~CScriptEngine();

	void init();
	void setup_callbacks();
	void unload();

	IC lua_State* lua() { return m_virtual_machine; }
	CScriptProcesses script_processes();

#ifdef DEBUG
	static void lua_hook_call(lua_State* L, lua_Debug* dbg);
#endif // #ifdef DEBUG

	int load_string(
		LPCSTR caString,
		LPCSTR caScriptName,
		LPCSTR caNameSpaceName = 0
	);

	int do_string(
		LPCSTR caString,
		LPCSTR caScriptName,
		LPCSTR caNameSpaceName = 0
	);

	int error_log(LPCSTR caFormat, ...);
	static int __cdecl script_log(ELuaMessageType message, LPCSTR caFormat, ...);
	static bool print_output(lua_State* L, LPCSTR caScriptName, int iErorCode = 0);
	static void print_error(lua_State* L, int iErrorCode);
	void on_error(lua_State* L);

#ifdef LUA_DEBUG_PRINT //DEBUG
	void flush_log();
#endif //-LUA_DEBUG_PRINT DEBUG

	bool function_object(LPCSTR function_to_call, luabind::object& object, int type = LUA_TFUNCTION);

	template <typename _result_type>
	bool functor(LPCSTR function_to_call, luabind::functor<_result_type>& lua_function);

	//#ifdef PRINT_CALL_STACK
	void print_stack();
	//AVO: added to stop duplicate stack output prints in log
	static int __cdecl script_log_no_stack(ELuaMessageType tLuaMessageType, LPCSTR caFormat, ...);
	//-AVO
	//#endif //-PRINT_CALL_STACK

	void collect_all_garbage();

protected:
	void reinit();
	static int vscript_log(ELuaMessageType tLuaMessageType, LPCSTR caFormat, va_list marker);

DECLARE_SCRIPT_REGISTER_FUNCTION
};

add_to_type_list(CScriptEngine)
#undef script_type_list
#define script_type_list save_type_list(CScriptEngine)

#include "script_engine_inline.h"
