////////////////////////////////////////////////////////////////////////////
//	Module 		: script_engine.h
//	Created 	: 01.04.2004
//  Modified 	: 01.04.2004
//	Author		: Dmitriy Iassenev
//	Description : XRay Script Engine
////////////////////////////////////////////////////////////////////////////

#pragma once

#include "script_storage_space.h"
#include "script_export_space.h"
#include "script_space_forward.h"
#include "associative_vector.h"

//AVO: lua re-org
#include "lua.hpp"
/*extern "C" {
#include <lua/lua.h>
}*/
//-AVO

//#define DBG_DISABLE_SCRIPTS

#include "script_engine_space.h"

#ifndef MASTER_GOLD
#	define USE_DEBUGGER
#	define USE_LUA_STUDIO
#endif //-!MASTER_GOLD

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

using namespace ScriptStorage;

class CScriptProcess;
class CScriptThread;
struct lua_State;
struct lua_Debug;

#ifdef USE_DEBUGGER
#	ifndef USE_LUA_STUDIO
		class CScriptDebugger;
#	else // #ifndef USE_LUA_STUDIO
		namespace cs {
			namespace lua_studio {
				struct world;
			} // namespace lua_studio
		} // namespace cs

		class lua_studio_engine;
#	endif // #ifndef USE_LUA_STUDIO
#endif

class CScriptEngine
{
private:
	lua_State* m_virtual_machine;
	CScriptThread* m_current_thread;
	BOOL m_jit;

#ifdef DEBUG
public:
	bool						m_stack_is_ready;
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

protected:
	static int vscript_log(ScriptStorage::ELuaMessageType tLuaMessageType, LPCSTR caFormat, va_list marker);
	bool do_file(LPCSTR caScriptName, LPCSTR caNameSpaceName);
	void reinit();

public:
	//#ifdef PRINT_CALL_STACK
	void print_stack();
	//AVO: added to stop duplicate stack output prints in log
	static int __cdecl script_log_no_stack(ScriptStorage::ELuaMessageType tLuaMessageType, LPCSTR caFormat, ...);
	//-AVO
	//#endif //-PRINT_CALL_STACK

public:
	CScriptEngine();
	~CScriptEngine();
	IC lua_State* lua();
	IC void current_thread(CScriptThread* thread);
	IC CScriptThread* current_thread() const;
	int compile_buffer(
		lua_State* L,
		std::string caString,
		LPCSTR caScriptName,
		LPCSTR caNameSpaceName = 0
	);
	int load_buffer(
		lua_State* L,
		LPCSTR caBuffer,
		size_t tSize,
		LPCSTR caScriptName,
		LPCSTR caNameSpaceName = 0
	);
	bool load_file_into_namespace(LPCSTR caScriptName, LPCSTR caNamespaceName);
	bool namespace_loaded(LPCSTR caName, bool remove_from_stack = true);
	luabind::object name_space(LPCSTR namespace_name);
	int error_log(LPCSTR caFormat, ...);
	static int __cdecl script_log(ELuaMessageType message, LPCSTR caFormat, ...);
	static bool print_output(lua_State* L, LPCSTR caScriptName, int iErorCode = 0);
	static void print_error(lua_State* L, int iErrorCode);
	void on_error(lua_State* L);

#ifdef LUA_DEBUG_PRINT //DEBUG
public:
	void flush_log();
#endif //-LUA_DEBUG_PRINT DEBUG

public:
	typedef ScriptEngine::EScriptProcessors EScriptProcessors;
	typedef associative_vector<EScriptProcessors, CScriptProcess*> CScriptProcessStorage;

protected:
	CScriptProcessStorage m_script_processes;
	int m_stack_level;
	shared_str m_class_registrators;

protected:
#ifdef USE_DEBUGGER
#	ifndef USE_LUA_STUDIO
		CScriptDebugger			*m_scriptDebugger;
#	else // #ifndef USE_LUA_STUDIO
		cs::lua_studio::world*	m_lua_studio_world;
		lua_studio_engine*		m_lua_studio_engine;
#	endif // #ifndef USE_LUA_STUDIO
#endif // #ifdef USE_DEBUGGER

private:
	string128 m_last_no_file;
	u32 m_last_no_file_length;

	bool no_file_exists(LPCSTR file_name, u32 string_length);
	void add_no_file(LPCSTR file_name, u32 string_length);

public:
	void init();
	void unload();
	static int lua_panic(lua_State* L);
	static void lua_error(lua_State* L);
	static int lua_pcall_failed(lua_State* L);
#ifdef DEBUG
	static	void				lua_hook_call				(lua_State *L, lua_Debug *dbg);
#endif // #ifdef DEBUG
	void setup_callbacks();
	void load_common_scripts();
	IC CScriptProcess* script_process(const EScriptProcessors& process_id) const;
	IC void add_script_process(const EScriptProcessors& process_id, CScriptProcess* script_process);
	void remove_script_process(const EScriptProcessors& process_id);
	void setup_auto_load();
	bool load_package(LPCSTR file_name, bool warn_if_not_exist = true);
	void unload_package(LPCSTR package);
protected:
	bool object(LPCSTR caIdentifier, int type);
	bool object(LPCSTR caNamespaceName, LPCSTR caIdentifier, int type);
public:
	bool function_object(LPCSTR function_to_call, luabind::object& object, int type = LUA_TFUNCTION);
	void register_script_classes();
	IC void parse_script_namespace(LPCSTR function_to_call, LPSTR name_space, u32 const namespace_size, LPSTR function,
	                               u32 const function_size);

	template <typename _result_type>
	IC bool functor(LPCSTR function_to_call, luabind::functor<_result_type>& lua_function);

#ifdef USE_DEBUGGER
#	ifndef USE_LUA_STUDIO
			void				stopDebugger				();
			void				restartDebugger				();
			CScriptDebugger		*debugger					();
#	else // ifndef USE_LUA_STUDIO
			void				try_connect_to_debugger		();
			void				disconnect_from_debugger	();
	inline cs::lua_studio::world* debugger					() const { return m_lua_studio_world; }
#	endif // ifndef USE_LUA_STUDIO
#endif
	void collect_all_garbage();

DECLARE_SCRIPT_REGISTER_FUNCTION
};

add_to_type_list(CScriptEngine)
#undef script_type_list
#define script_type_list save_type_list(CScriptEngine)

#include "script_engine_inline.h"
