////////////////////////////////////////////////////////////////////////////
//	Module 		: script_storage.h
//	Created 	: 01.04.2004
//  Modified 	: [1/14/2015 Andrey]
//	Author		: Dmitriy Iassenev
//	Description : XRay Script Storage
////////////////////////////////////////////////////////////////////////////

#pragma once

#include "script_storage_space.h"
#include "script_space_forward.h"
#include <unordered_map>
#include <string>
#include <set>

struct lua_State;
class CScriptThread;

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

class CScriptDialect;

typedef std::set<std::string> Unlocalizer;
typedef xr_unordered_map<std::string, Unlocalizer> Unlocalizers;

/**
 * Convert all std::strings to const char* using constexpr if (C++17)
 */
template<typename T>
auto convert(T&& t) {
	if constexpr (std::is_same<std::remove_cv_t<std::remove_reference_t<T>>, std::string>::value) {
		return std::forward<T>(t).c_str();
	}
	else {
		return std::forward<T>(t);
	}
}

/**
 * printf like formatting for C++ with std::string
 * Original source: https://stackoverflow.com/a/26221725/11722
 */
template<typename ... Args>
std::string string_format_internal(const std::string& format, Args&& ... args)
{
	const auto size = snprintf(nullptr, 0, format.c_str(), std::forward<Args>(args) ...) + 1;
	if (size <= 0) { throw std::runtime_error("Error during formatting."); }
	std::unique_ptr<char[]> buf(new char[size]);
	snprintf(buf.get(), size, format.c_str(), args ...);
	return std::string(buf.get(), buf.get() + size - 1);
}

template<typename ... Args>
std::string string_format(std::string fmt, Args&& ... args) {
	return string_format_internal(fmt, convert(std::forward<Args>(args))...);
}

static bool parse_namespace(LPCSTR caNamespaceName, LPSTR b, u32 const b_size, LPSTR c, u32 const c_size)
{
	*b = 0;
	*c = 0;
	LPSTR S2;
	STRCONCAT(S2, caNamespaceName);
	LPSTR S = S2;
	for (int i = 0;; ++i)
	{
		if (!xr_strlen(S))
		{
			Msg("the namespace name %s is incorrect!", caNamespaceName);
			return (false);
		}
		LPSTR S1 = strchr(S, '.');
		if (S1)
			*S1 = 0;

		if (i)
			xr_strcat(b, b_size, "{");
		xr_strcat(b, b_size, S);
		xr_strcat(b, b_size, "=");
		if (i)
			xr_strcat(c, c_size, "}");
		if (S1)
			S = ++S1;
		else
			break;
	}

	return (true);
}

using namespace ScriptStorage;

class CScriptStorage
{
private:
	lua_State* m_virtual_machine;
	CScriptThread* m_current_thread;
	BOOL m_jit;

#ifdef DEBUG
public:
    bool						m_stack_is_ready	;
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
	CScriptStorage();
	virtual ~CScriptStorage();
	IC lua_State* lua();
	IC void current_thread(CScriptThread* thread);
	IC CScriptThread* current_thread() const;
	bool load_buffer(
		lua_State* L,
		Unlocalizers* unlocalizers,
		LPCSTR caBuffer,
		size_t tSize,
		LPCSTR caScriptName,
		LPCSTR caNameSpaceName = 0
	);
	bool load_file_into_namespace(LPCSTR caScriptName, LPCSTR caNamespaceName);
	bool namespace_loaded(LPCSTR caName, bool remove_from_stack = true);
	bool object(LPCSTR caIdentifier, int type);
	bool object(LPCSTR caNamespaceName, LPCSTR caIdentifier, int type);
	luabind::object name_space(LPCSTR namespace_name);
	int error_log(LPCSTR caFormat, ...);
	static int __cdecl script_log(ELuaMessageType message, LPCSTR caFormat, ...);
	static bool print_output(lua_State* L, LPCSTR caScriptName, int iErorCode = 0);
	static void print_error(lua_State* L, int iErrorCode);
	virtual void on_error(lua_State* L) = 0;

#ifdef LUA_DEBUG_PRINT //DEBUG
public:
    void flush_log();
#endif //-LUA_DEBUG_PRINT DEBUG
};

#include "script_storage_inline.h"
