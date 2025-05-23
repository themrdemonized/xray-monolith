////////////////////////////////////////////////////////////////////////////
//	Module 		: script_engine.cpp
//	Created 	: 01.04.2004
//  Modified 	: [1/14/2015 Andrey]
//	Author		: Dmitriy Iassenev
//	Description : XRay Script Engine
////////////////////////////////////////////////////////////////////////////

#include "pch_script.h"
#include "script_engine.h"
#include "ai_space.h"
#include "object_factory.h"
#include "script_process.h"
#include "../build_config_defines.h"
#include "script_thread.h"
#include "../xrCore/mezz_stringbuffer.h"
#include <stdarg.h>
#include <unordered_map>
#include <set>
#include <boost/noncopyable.hpp>

#if !defined(DEBUG) && defined(USE_LUAJIT_ONE)
#	include "opt.lua.h"
#	include "opt_inline.lua.h"
#endif //!DEBUG && USE_LUAJIT_ONE
#ifndef USE_LUAJIT_ONE
#include "lua.hpp"
#endif

#ifndef ENGINE_BUILD
#	include "script_engine.h"
#	include "ai_space.h"
#else //ENGINE_BUILD
#	define NO_XRGAME_SCRIPT_ENGINE
#endif //!ENGINE_BUILD

#ifndef XRGAME_EXPORTS
#	define NO_XRGAME_SCRIPT_ENGINE
#endif //!XRGAME_EXPORTS

#ifndef NO_XRGAME_SCRIPT_ENGINE
#	include "ai_debug.h"
#endif //!NO_XRGAME_SCRIPT_ENGINE

#ifdef USE_DEBUGGER
#	include "script_debugger.h"
#endif

#ifndef PURE_ALLOC
//#	ifndef USE_MEMORY_MONITOR
//#		define USE_DL_ALLOCATOR
//#	endif //!USE_MEMORY_MONITOR
#endif //!PURE_ALLOC

struct raii_guard : private boost::noncopyable
{
    int m_error_code;
    LPCSTR const& m_error_description;

    raii_guard(int error_code, LPCSTR const& m_description) : m_error_code(error_code),
        m_error_description(m_description)
    {
    }

    ~raii_guard()
    {
#ifdef DEBUG
        bool lua_studio_connected = !!ai().script_engine().debugger();
        if (!lua_studio_connected)
#endif //-DEBUG
        {
#ifdef DEBUG
            static bool const break_on_assert = !!strstr(Core.Params, "-break_on_assert");
#else //!DEBUG
            static bool const break_on_assert = false; //Alundaio: Can't get a proper stack trace with this enabled
#endif //-DEBUG
            if (!m_error_code)
                return;

            if (break_on_assert)
                R_ASSERT2(!m_error_code, m_error_description);
            else
                Msg("! [SCRIPT ERROR]: %s", m_error_description);
        }
    }
}; //-struct raii_guard

extern void export_classes(lua_State* L);
extern int luaopen_lua_extensions(lua_State* L);

#ifndef USE_DL_ALLOCATOR
static void* lua_alloc(void* ud, void* ptr, size_t osize, size_t nsize)
{
    (void)ud;
    (void)osize;
    if (nsize == 0)
    {
        xr_free(ptr);
        return NULL;
    }
    else
#ifdef DEBUG_MEMORY_NAME
        return Memory.mem_realloc(ptr, nsize, "LUA");
#else // DEBUG_MEMORY_MANAGER
        return Memory.mem_realloc(ptr, nsize);
#endif // DEBUG_MEMORY_MANAGER
}

u32 game_lua_memory_usage()
{
    return (0);
}
#else //USE_DL_ALLOCATOR

#   ifdef USE_ARENA_ALLOCATOR
static const u32			s_arena_size = 96 * 1024 * 1024;
static char					s_fake_array[s_arena_size];
//        static doug_lea_allocator	s_allocator( s_fake_array, s_arena_size, "lua" );
#   else //-USE_ARENA_ALLOCATOR
//        static doug_lea_allocator	s_allocator(0, 0, "lua");
#   endif //-USE_ARENA_ALLOCATOR

static void* lua_alloc(void* ud, void* ptr, size_t osize, size_t nsize)
{
#ifndef USE_MEMORY_MONITOR
    (void)ud;
    (void)osize;
    if (!nsize)
    {
        s_allocator.free_impl(ptr);
        return 0;
    }
    if (!ptr)
        return s_allocator.malloc_impl((u32)nsize);

    return s_allocator.realloc_impl(ptr, (u32)nsize);
#else //USE_MEMORY_MONITOR
    if (!nsize) {
        memory_monitor::monitor_free(ptr);
        s_allocator.free_impl(ptr);
        return						NULL;
    }

    if (!ptr) {
        void* const result = s_allocator.malloc_impl((u32)nsize);
        memory_monitor::monitor_alloc(result, nsize, "LUA");
        return						result;
    }

    memory_monitor::monitor_free(ptr);
    void* const result = s_allocator.realloc_impl(ptr, (u32)nsize);
    memory_monitor::monitor_alloc(result, nsize, "LUA");
    return							result;
#endif //!USE_MEMORY_MONITOR
}

u32 game_lua_memory_usage()
{
    return (s_allocator.get_allocated_size());
}
#endif //!USE_DL_ALLOCATOR

static LPVOID __cdecl luabind_allocator(
    luabind::memory_allocation_function_parameter const,
    void const* const pointer,
    size_t const size
)
{
    if (!size)
    {
        LPVOID non_const_pointer = const_cast<LPVOID>(pointer);
        xr_free(non_const_pointer);
        return (0);
    }

    if (!pointer)
    {
#ifdef DEBUG
        return	(Memory.mem_alloc(size, "luabind"));
#else //!DEBUG
        return (Memory.mem_alloc(size));
#endif //-DEBUG
    }

    LPVOID non_const_pointer = const_cast<LPVOID>(pointer);
#ifdef DEBUG
    return		(Memory.mem_realloc(non_const_pointer, size, "luabind"));
#else //!DEBUG
    return (Memory.mem_realloc(non_const_pointer, size));
#endif //-DEBUG
}

void setup_luabind_allocator()
{
    luabind::allocator = &luabind_allocator;
    luabind::allocator_parameter = 0;
}


#ifdef USE_LUAJIT_ONE //  [1/14/2015 Andrey]

/* ---- start of LuaJIT extensions */
static void l_message(lua_State* state, const char* msg)
{
    Msg("! [LUA_JIT] %s", msg);
}


static int report(lua_State* L, int status)
{
    if (status && !lua_isnil(L, -1))
    {
        const char* msg = lua_tostring(L, -1);
        if (msg == NULL) msg = "(error object is not a string)";
        l_message(L, msg);
        lua_pop(L, 1);
    }
    return status;
}

static int loadjitmodule(lua_State* L, const char* notfound)
{
    lua_getglobal(L, "require");
    lua_pushliteral(L, "jit.");
    lua_pushvalue(L, -3);
    lua_concat(L, 2);
    if (lua_pcall(L, 1, 1, 0))
    {
        const char* msg = lua_tostring(L, -1);
        if (msg && !strncmp(msg, "module ", 7))
        {
            l_message(L, notfound);
            return 1;
        }
        else
            return report(L, 1);
    }
    lua_getfield(L, -1, "start");
    lua_remove(L, -2);  /* drop module table */
    return 0;
}

/* JIT engine control command: try jit library first or load add-on module */
static int dojitcmd(lua_State* L, const char* cmd)
{
    const char* val = strchr(cmd, '=');
    lua_pushlstring(L, cmd, val ? val - cmd : xr_strlen(cmd));
    lua_getglobal(L, "jit");  /* get jit.* table */
    lua_pushvalue(L, -2);
    lua_gettable(L, -2);  /* lookup library function */
    if (!lua_isfunction(L, -1))
    {
        lua_pop(L, 2);  /* drop non-function and jit.* table, keep module name */
        if (loadjitmodule(L, "unknown luaJIT command"))
            return 1;
    }
    else
    {
        lua_remove(L, -2);  /* drop jit.* table */
    }
    lua_remove(L, -2);  /* drop module name */
    if (val) lua_pushstring(L, val + 1);
    return report(L, lua_pcall(L, val ? 1 : 0, 0, 0));
}

void jit_command(lua_State* state, LPCSTR command)
{
    dojitcmd(state, command);
}

#ifndef DEBUG
/* start optimizer */
static int dojitopt(lua_State* L, const char* opt)
{
    lua_pushliteral(L, "opt");
    if (loadjitmodule(L, "LuaJIT optimizer module not installed"))
        return 1;
    lua_remove(L, -2);  /* drop module name */
    if (*opt) lua_pushstring(L, opt);
    return report(L, lua_pcall(L, *opt ? 1 : 0, 0, 0));
}

static void put_function(lua_State* state, u8 const* buffer, u32 const buffer_size, LPCSTR package_id)
{
    lua_getglobal(state, "package");
    lua_pushstring(state, "preload");
    lua_gettable(state, -2);

    lua_pushstring(state, package_id);
    luaL_loadbuffer(state, (char*)buffer, buffer_size, package_id);
    lua_settable(state, -3);
}

/* ---- end of LuaJIT extensions */
#endif //!DEBUG
#endif //-USE_LUAJIT_ONE

CScriptEngine::CScriptEngine()
{
    m_current_thread = 0;

#ifdef DEBUG
    m_stack_is_ready = false;
#endif //-DEBUG

    m_virtual_machine = 0;
    m_stack_level = 0;

#ifdef USE_DEBUGGER
#	ifndef USE_LUA_STUDIO
    m_scriptDebugger = NULL;
    restartDebugger();
#	else //USE_LUA_STUDIO
    m_lua_studio_world = 0;
#	endif //!USE_LUA_STUDIO
#endif
}

CScriptEngine::~CScriptEngine()
{
#ifdef LUA_DEBUG_PRINT
    flush_log();
#endif //-LUA_DEBUG_PRINT

#ifdef USE_DEBUGGER
#	ifndef USE_LUA_STUDIO
    xr_delete(m_scriptDebugger);
#	else // #ifndef USE_LUA_STUDIO
    disconnect_from_debugger();
#	endif // #ifndef USE_LUA_STUDIO
#endif

    if (m_virtual_machine)
        lua_close(m_virtual_machine);

    while (!m_script_processes.empty())
        remove_script_process(m_script_processes.begin()->first);
}

static int get_object_factory(lua_State* L)
{
    luabind::object(L, const_cast<CObjectFactory*>(&object_factory())).pushvalue();
    return (1);
}

void CScriptEngine::init()
{
#ifdef USE_LUA_STUDIO
    bool lua_studio_connected = !!m_lua_studio_world;
    if (lua_studio_connected)
        m_lua_studio_world->remove(lua());
#endif // #ifdef USE_LUA_STUDIO

    CScriptEngine::reinit();

#ifdef USE_LUA_STUDIO
    if (m_lua_studio_world || strstr(Core.Params, "-lua_studio")) {
        if (!lua_studio_connected)
            try_connect_to_debugger();
        else {
#ifdef USE_LUAJIT_ONE
            jit_command(lua(), "debug=2");
            jit_command(lua(), "off");
#else
            luaJIT_setmode(lua(), 0, LUAJIT_MODE_ENGINE | LUAJIT_MODE_OFF);
#endif
            m_lua_studio_world->add(lua());
        }
    }
#endif // #ifdef USE_LUA_STUDIO

    luabind::open(lua());
    setup_callbacks();
    export_classes(lua());

#ifdef DEBUG
    m_stack_is_ready = true;
#endif

#ifndef USE_LUA_STUDIO
#	ifdef DEBUG
#		if defined(USE_DEBUGGER) && !defined(USE_LUA_STUDIO)
    if (!debugger() || !debugger()->Active())
#		endif // #if defined(USE_DEBUGGER) && !defined(USE_LUA_STUDIO)
        lua_sethook(lua(), lua_hook_call, LUA_MASKLINE | LUA_MASKCALL | LUA_MASKRET, 0);
#	endif // #ifdef DEBUG
#endif // #ifndef USE_LUA_STUDIO
    //	lua_sethook							(lua(), lua_hook_call,	LUA_MASKLINE|LUA_MASKCALL|LUA_MASKRET,	0);

    lua_pushcfunction(lua(), get_object_factory);
    lua_setglobal(lua(), "get_object_factory");

    string_path path;
    if (luaL_dofile(lua(), FS.update_path(path, "$game_scripts$", "init.lua")))
    {
        LPCSTR e = lua_tostring(lua(), -1);
        lua_pop(lua(), 1);
        FATAL((std::string("Failed to load init.lua:\n") + e).c_str());
    }

    m_stack_level = lua_gettop(lua());
}

void CScriptEngine::reinit()
{
    if (m_virtual_machine)
        lua_close(m_virtual_machine);

#ifdef USE_GSC_MEM_ALLOC
    m_virtual_machine = lua_newstate(lua_alloc, NULL);
#else
    m_virtual_machine = luaL_newstate();
#endif //-USE_GSC_MEM_ALLOC

    if (!m_virtual_machine)
    {
        Msg("! ERROR : Cannot initialize script virtual machine!");
        return;
    }


#ifndef USE_LUAJIT_ONE
    luaL_openlibs(lua());
    if (strstr(Core.Params, "-nojit"))
        luaJIT_setmode(lua(), 0, LUAJIT_MODE_ENGINE | LUAJIT_MODE_OFF);
#else // USE_LUAJIT_ONE
    // initialize lua standard library functions
    struct luajit
    {
        static void open_lib(lua_State* L, pcstr module_name, lua_CFunction function)
        {
            lua_pushcfunction(L, function);
            lua_pushstring(L, module_name);
            lua_call(L, 1, 0);
        }
    }; // struct lua;

    luajit::open_lib(lua(), "", luaopen_base);
    luajit::open_lib(lua(), LUA_LOADLIBNAME, luaopen_package);
    luajit::open_lib(lua(), LUA_TABLIBNAME, luaopen_table);
    luajit::open_lib(lua(), LUA_IOLIBNAME, luaopen_io);
    luajit::open_lib(lua(), LUA_OSLIBNAME, luaopen_os);
    luajit::open_lib(lua(), LUA_MATHLIBNAME, luaopen_math);
    luajit::open_lib(lua(), LUA_STRLIBNAME, luaopen_string);

#ifdef DEBUG
    luajit::open_lib(lua(), LUA_DBLIBNAME, luaopen_debug);
#else //!DEBUG

    if (strstr(Core.Params, "-dbg"))
        luajit::open_lib(lua(), LUA_DBLIBNAME, luaopen_debug);
#endif //-DEBUG

    if (!strstr(Core.Params, "-nojit"))
    {
        luajit::open_lib(lua(), LUA_JITLIBNAME, luaopen_jit);
#ifndef DEBUG
        put_function(lua(), opt_lua_binary, sizeof(opt_lua_binary), "jit.opt");
        put_function(lua(), opt_inline_lua_binary, sizeof(opt_lua_binary), "jit.opt_inline");
        dojitopt(lua(), "2");
#endif //!DEBUG
    }

#endif //!USE_LUAJIT_ONE

    luaopen_lua_extensions(lua());
}

void CScriptEngine::unload()
{
    lua_settop(lua(), m_stack_level);
}

int CScriptEngine::lua_panic(lua_State* L)
{
    ai().script_engine().print_stack();
    print_output(L, "PANIC", LUA_ERRRUN);
    return (0);
}

// demonized: get lua stack in array
static std::vector<std::string> get_lua_stack(lua_State* L)
{
    std::vector<std::string> res;
    lua_Debug l_tDebugInfo;
    for (int i = 0; lua_getstack(L, i, &l_tDebugInfo); ++i)
    {
        lua_getinfo(L, "nSlu", &l_tDebugInfo);
        if (!l_tDebugInfo.name)
        {
            res.push_back(make_string("%2d : [%s] %s(%d) : %s", i, l_tDebugInfo.what, l_tDebugInfo.short_src, l_tDebugInfo.currentline, ""));
        }
        else
        {
            if (!xr_strcmp(l_tDebugInfo.what, "C"))
            {
                res.push_back(make_string("%2d : [C  ] %s", i, l_tDebugInfo.name));
            }
            else
            {
                res.push_back(make_string("%2d : [%s] %s(%d) : %s", i, l_tDebugInfo.what, l_tDebugInfo.short_src, l_tDebugInfo.currentline, l_tDebugInfo.name));
            }
        }
    }
    return res;
}

void CScriptEngine::lua_error(lua_State* L)
{
    ai().script_engine().print_stack();
    print_output(L, "", LUA_ERRRUN);
    ai().script_engine().on_error(L);

    // demonized: print first line with lua error
    auto stack = get_lua_stack(L);
    std::string lua_error_line = "";
    for (auto const& s : stack) {
        if (s.find("[Lua]") != std::string::npos) {
            lua_error_line = s;
            break;
        }
    }

    auto error_str = make_string("\n%s\n\nLUA error: %s\n\nCheck log for details", lua_error_line.c_str(), lua_tostring(L, -1));
    LPCSTR error_msg = error_str.c_str();

#if !XRAY_EXCEPTIONS
    Debug.fatal(DEBUG_INFO, error_msg);
#else
    throw					lua_tostring(L, -1);
#endif
}

void printLuaStack()
{
    ai().script_engine().print_stack();
}

int CScriptEngine::lua_pcall_failed(lua_State* L)
{
    ai().script_engine().print_stack();
    print_output(L, "", LUA_ERRRUN);
    ai().script_engine().on_error(L);

    // demonized: print first line with lua error
    auto stack = get_lua_stack(L);
    std::string lua_error_line = "";
    for (auto const& s : stack) {
        if (s.find("[Lua]") != std::string::npos) {
            lua_error_line = s;
            break;
        }
    }

    auto error_str = make_string("\n%s\n\nLUA error: %s\n\nCheck log for details", lua_error_line.c_str(), lua_isstring(L, -1) ? lua_tostring(L, -1) : "");
    LPCSTR error_msg = error_str.c_str();

#if !XRAY_EXCEPTIONS
    Debug.fatal(DEBUG_INFO, error_msg);
#endif
    if (lua_isstring(L, -1))
        lua_pop(L, 1);
    return (LUA_ERRRUN);
}

void lua_cast_failed(lua_State* L, LUABIND_TYPE_INFO info)
{
    CScriptEngine::print_output(L, "", LUA_ERRRUN);

    Debug.fatal(DEBUG_INFO, "LUA error: cannot cast lua value to %s", info->name());
}

int CScriptEngine::compile_buffer(lua_State* L, std::string caString, LPCSTR caScriptName, LPCSTR caNameSpaceName)
{
    luabind::functor<luabind::object> compiler;
    if (functor("_COMPILER", compiler))
    {
        luabind::object result = compiler(caString.c_str(), caScriptName, caNameSpaceName);
        result.pushvalue();
        return 0;
    }

    Msg("scam_compiler not available, loading as raw Lua...");
    return luaL_loadbuffer(L, caString.c_str(), caString.length(), caScriptName);
}

int CScriptEngine::load_buffer(
    lua_State* L,
    LPCSTR caBuffer,
    size_t tSize,
    LPCSTR caScriptName,
    LPCSTR caNameSpaceName
)
{
    int l_iErrorCode = compile_buffer(
        L,
        std::string(caBuffer, caBuffer + tSize),
        caScriptName,
        caNameSpaceName
    );
    if (l_iErrorCode)
    {
        //#ifdef DEBUG
        if (strstr(Core.Params, "-dbg")) print_output(L, caScriptName, l_iErrorCode);
        //#endif //-DEBUG
        on_error(L);
    }
    return l_iErrorCode;
}

bool CScriptEngine::namespace_loaded(LPCSTR N, bool remove_from_stack)
{
    int start = lua_gettop(lua());
    lua_getglobal(lua(), "package");
    VERIFY(lua_istable(lua(), -1));
    lua_getfield(lua(), -1, "loaded");
    VERIFY(lua_istable(lua(), -1));
    lua_remove(lua(), -2);
    string256 S2;
    xr_strcpy(S2, N);
    LPSTR S = S2;
    for (;;)
    {
        if (!xr_strlen(S))
        {
            VERIFY(lua_gettop(lua()) >= 1);
            lua_pop(lua(), 1);
            VERIFY(start == lua_gettop(lua()));
            return (false);
        }
        LPSTR S1 = strchr(S, '.');
        if (S1)
            *S1 = 0;
        lua_pushstring(lua(), S);
        lua_rawget(lua(), -2);
        if (lua_isnil(lua(), -1))
        {
            //			lua_settop		(lua(),0);
            VERIFY(lua_gettop(lua()) >= 2);
            lua_pop(lua(), 2);
            VERIFY(start == lua_gettop(lua()));
            return (false); //	there is no namespace!
        }
        else if (!lua_istable(lua(), -1))
        {
            std::string tn(lua_typename(lua(), -1));
            //				lua_settop	(lua(),0);
            VERIFY(lua_gettop(lua()) >= 1);
            lua_pop(lua(), 1);
            VERIFY(start == lua_gettop(lua()));
            if (S1)
                FATAL((std::string("Error : the namespace name ") + N + " is already being used by non-table object of type " + tn + "\n").c_str());
            return (true);
        }
        lua_remove(lua(), -2);
        if (S1)
            S = ++S1;
        else
            break;
    }
    if (!remove_from_stack)
    {
        VERIFY(lua_gettop(lua()) == start + 1);
    }
    else
    {
        VERIFY(lua_gettop(lua()) >= 1);
        lua_pop(lua(), 1);
        VERIFY(lua_gettop(lua()) == start);
    }
    return (true);
}

luabind::object CScriptEngine::name_space(LPCSTR namespace_name)
{
    string256 S1;
    xr_strcpy(S1, namespace_name);
    LPSTR S = S1;
    luabind::object lua_namespace = luabind::get_globals(lua());
    lua_namespace = lua_namespace["package"];
    lua_namespace = lua_namespace["loaded"];
    for (;;)
    {
        if (!xr_strlen(S))
            return (lua_namespace);
        LPSTR I = strchr(S, '.');
        if (!I)
            return (lua_namespace[S]);
        *I = 0;
        lua_namespace = lua_namespace[S];
        S = I + 1;
    }
}

int CScriptEngine::vscript_log(ScriptStorage::ELuaMessageType tLuaMessageType, LPCSTR caFormat, va_list marker)
{
#ifndef NO_XRGAME_SCRIPT_ENGINE
#   ifdef DEBUG
    if (!psAI_Flags.test(aiLua) && (tLuaMessageType != ScriptStorage::eLuaMessageTypeError))
        return(0);
#   endif //-DEBUG
#endif //!NO_XRGAME_SCRIPT_ENGINE

    //#ifndef PRINT_CALL_STACK
    //return		(0);
    //#else //PRINT_CALL_STACK
#   ifndef NO_XRGAME_SCRIPT_ENGINE
    //AVO: allow LUA debug prints (i.e.: ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError, "CWeapon : cannot access class member Weapon_IsScopeAttached!");)
#       ifndef DEBUG

    if (!strstr(Core.Params, "-dbg"))
        return (0);
#       endif //!DEBUG
#       ifndef LUA_DEBUG_PRINT
#           ifdef DEBUG
    if (!psAI_Flags.test(aiLua) && (tLuaMessageType != ScriptStorage::eLuaMessageTypeError))
        return(0);
#           endif //-DEBUG
#       else //!LUA_DEBUG_PRINT
    if (!psAI_Flags.test(aiLua) && (tLuaMessageType != ScriptStorage::eLuaMessageTypeError))
        return(0);
#       endif //-LUA_DEBUG_PRINT
#endif //-NO_XRGAME_SCRIPT_ENGINE

    LPCSTR S = "", SS = "";
    LPSTR S1;
    string4096 S2;
    switch (tLuaMessageType)
    {
    case ScriptStorage::eLuaMessageTypeInfo:
    {
        S = "* [LUA] ";
        SS = "[INFO]        ";
        break;
    }
    case ScriptStorage::eLuaMessageTypeError:
    {
        S = "! [LUA] ";
        SS = "[ERROR]       ";
        break;
    }
    case ScriptStorage::eLuaMessageTypeMessage:
    {
        S = "~ [LUA] ";
        SS = "[MESSAGE]     ";
        break;
    }
    case ScriptStorage::eLuaMessageTypeHookCall:
    {
        S = "[LUA][HOOK_CALL] ";
        SS = "[CALL]        ";
        break;
    }
    case ScriptStorage::eLuaMessageTypeHookReturn:
    {
        S = "[LUA][HOOK_RETURN] ";
        SS = "[RETURN]      ";
        break;
    }
    case ScriptStorage::eLuaMessageTypeHookLine:
    {
        S = "[LUA][HOOK_LINE] ";
        SS = "[LINE]        ";
        break;
    }
    case ScriptStorage::eLuaMessageTypeHookCount:
    {
        S = "[LUA][HOOK_COUNT] ";
        SS = "[COUNT]       ";
        break;
    }
    case ScriptStorage::eLuaMessageTypeHookTailReturn:
    {
        S = "[LUA][HOOK_TAIL_RETURN] ";
        SS = "[TAIL_RETURN] ";
        break;
    }
    default: NODEFAULT;
    }

    xr_strcpy(S2, S);
    S1 = S2 + xr_strlen(S);
    int l_iResult = vsprintf(S1, caFormat, marker);
    Msg("%s", S2);

    xr_strcpy(S2, SS);
    S1 = S2 + xr_strlen(SS);
    vsprintf(S1, caFormat, marker);
    xr_strcat(S2, "\r\n");

#ifdef LUA_DEBUG_PRINT //DEBUG
#	ifndef ENGINE_BUILD
    ai().script_engine().m_output.w(S2, xr_strlen(S2) * sizeof(char));
#	endif //!ENGINE_BUILD
#endif //-LUA_DEBUG_PRINT DEBUG

    return (l_iResult);
    //#endif //-PRINT_CALL_STACK
}

//#ifdef PRINT_CALL_STACK
void CScriptEngine::print_stack()
{
#ifdef DEBUG
    if (!m_stack_is_ready)
        return;

    m_stack_is_ready = false;
#endif //-DEBUG

    lua_State* L = lua();
    lua_Debug l_tDebugInfo;
    for (int i = 0; lua_getstack(L, i, &l_tDebugInfo); ++i)
    {
        lua_getinfo(L, "nSlu", &l_tDebugInfo);
        if (!l_tDebugInfo.name)
        {
            script_log_no_stack(ScriptStorage::eLuaMessageTypeError, "%2d : [%s] %s(%d) : %s", i, l_tDebugInfo.what,
                l_tDebugInfo.short_src, l_tDebugInfo.currentline, "");
            //script_log(ScriptStorage::eLuaMessageTypeError, "%2d : [%s] %s(%d) : %s", i, l_tDebugInfo.what, l_tDebugInfo.short_src, l_tDebugInfo.currentline, "");
        }
        else
        {
            if (!xr_strcmp(l_tDebugInfo.what, "C"))
            {
                script_log_no_stack(ScriptStorage::eLuaMessageTypeError, "%2d : [C  ] %s", i, l_tDebugInfo.name);
                //script_log(ScriptStorage::eLuaMessageTypeError, "%2d : [C  ] %s", i, l_tDebugInfo.name);  
            }
            else
            {
                script_log_no_stack(ScriptStorage::eLuaMessageTypeError, "%2d : [%s] %s(%d) : %s", i, l_tDebugInfo.what,
                    l_tDebugInfo.short_src, l_tDebugInfo.currentline, l_tDebugInfo.name);
                //script_log(ScriptStorage::eLuaMessageTypeError, "%2d : [%s] %s(%d) : %s", i, l_tDebugInfo.what, l_tDebugInfo.short_src, l_tDebugInfo.currentline, l_tDebugInfo.name);
            }
        }
    }
}

//#endif //-PRINT_CALL_STACK

//AVO: added to stop duplicate stack output prints in log
int __cdecl CScriptEngine::script_log_no_stack(ScriptStorage::ELuaMessageType tLuaMessageType, LPCSTR caFormat, ...)
{
    va_list marker;
    va_start(marker, caFormat);
    int result = vscript_log(tLuaMessageType, caFormat, marker);
    va_end(marker);
    return result;
}

//-AVO

int __cdecl CScriptEngine::script_log(ScriptStorage::ELuaMessageType tLuaMessageType, LPCSTR caFormat, ...)
{
    va_list marker;
    va_start(marker, caFormat);
    int result = vscript_log(tLuaMessageType, caFormat, marker);
    va_end(marker);

    static bool reenterability = false;
    if (!reenterability)
    {
        reenterability = true;
        if (tLuaMessageType == ScriptStorage::eLuaMessageTypeError) {
            ai().script_engine().print_stack();
        }
        else {
            reenterability = false;
        }
    }

    // #ifdef PRINT_CALL_STACK
    // #	ifndef ENGINE_BUILD
    //     static bool	reenterability = false;
    //     if (!reenterability)
    //     {
    //         reenterability = true;
    //         if (eLuaMessageTypeError == tLuaMessageType)
    //             ai().script_engine().print_stack();
    //         reenterability = false;
    //     }
    // #	endif //!ENGINE_BUILD
    // #endif //-PRINT_CALL_STACK

    return (result);
}


bool CScriptEngine::print_output(lua_State* L, LPCSTR caScriptFileName, int iErorCode)
{
    if (iErorCode)
        print_error(L, iErorCode);

    LPCSTR S = "see call_stack for details!";

    raii_guard guard(iErorCode, S);

    if (!lua_isstring(L, -1))
        return (false);

    S = lua_tostring(L, -1);
    if (!xr_strcmp(S, "cannot resume dead coroutine"))
    {
        VERIFY2("Please do not return any values from main!!!", caScriptFileName);
#ifdef USE_DEBUGGER
#	ifndef USE_LUA_STUDIO
        if (ai().script_engine().debugger() && ai().script_engine().debugger()->Active()) {
            ai().script_engine().debugger()->Write(S);
            ai().script_engine().debugger()->ErrorBreak();
        }
#	endif //!USE_LUA_STUDIO
#endif //-USE_DEBUGGER
    }
    else
    {
        if (!iErorCode)
            script_log(ScriptStorage::eLuaMessageTypeInfo, "Output from %s", caScriptFileName);
        script_log(iErorCode ? ScriptStorage::eLuaMessageTypeError : ScriptStorage::eLuaMessageTypeMessage, "%s", S);
#ifdef USE_DEBUGGER
#	ifndef USE_LUA_STUDIO
        if (ai().script_engine().debugger() && ai().script_engine().debugger()->Active()) {
            ai().script_engine().debugger()->Write(S);
            ai().script_engine().debugger()->ErrorBreak();
        }
#	endif //!USE_LUA_STUDIO
#endif //-USE_DEBUGGER
    }
    return (true);
}

void CScriptEngine::print_error(lua_State* L, int iErrorCode)
{
    switch (iErrorCode)
    {
    case LUA_ERRRUN:
    {
        script_log(ScriptStorage::eLuaMessageTypeError, "SCRIPT RUNTIME ERROR");
        break;
    }
    case LUA_ERRMEM:
    {
        script_log(ScriptStorage::eLuaMessageTypeError, "SCRIPT ERROR (memory allocation)");
        break;
    }
    case LUA_ERRERR:
    {
        script_log(ScriptStorage::eLuaMessageTypeError, "SCRIPT ERROR (while running the error handler function)");
        break;
    }
    case LUA_ERRFILE:
    {
        script_log(ScriptStorage::eLuaMessageTypeError, "SCRIPT ERROR (while running file)");
        break;
    }
    case LUA_ERRSYNTAX:
    {
        script_log(ScriptStorage::eLuaMessageTypeError, "SCRIPT SYNTAX ERROR");
        break;
    }
    case LUA_YIELD:
    {
        script_log(ScriptStorage::eLuaMessageTypeInfo, "Thread is yielded");
        break;
    }
    default: NODEFAULT;
    }
}

#ifdef LUA_DEBUG_PRINT //DEBUG
void CScriptEngine::flush_log()
{
    string_path			log_file_name;
    strconcat(sizeof(log_file_name), log_file_name, Core.ApplicationName, "_", Core.UserName, "_lua.log");
    FS.update_path(log_file_name, "$logs$", log_file_name);
    m_output.save_to(log_file_name);
}
#endif //-LUA_DEBUG_PRINT DEBUG

int CScriptEngine::error_log(LPCSTR format, ...)
{
    va_list marker;
    va_start(marker, format);

    LPCSTR S = "! [LUA][ERROR] ";
    LPSTR S1;
    string4096 S2;
    xr_strcpy(S2, S);
    S1 = S2 + xr_strlen(S);

    int result = vsprintf(S1, format, marker);
    va_end(marker);

    Msg("%s", S2);

    return (result);
}

#ifdef USE_DEBUGGER
#	ifndef USE_LUA_STUDIO
#		include "script_debugger.h"
#	else //USE_LUA_STUDIO
#		include "lua_studio.h"
typedef cs::lua_studio::create_world_function_type			create_world_function_type;
typedef cs::lua_studio::destroy_world_function_type			destroy_world_function_type;

static create_world_function_type	s_create_world				= 0;
static destroy_world_function_type	s_destroy_world				= 0;
static HMODULE						s_script_debugger_handle	= 0;
static LogCallback					s_old_log_callback			= 0;
#	endif //!USE_LUA_STUDIO
#endif

#ifndef XRSE_FACTORY_EXPORTS
#	ifdef DEBUG
#		include "ai_debug.h"
extern Flags32 psAI_Flags;
#	endif //-DEBUG
#endif //!XRSE_FACTORY_EXPORTS
#include "lua.hpp"

#ifdef USE_LUAJIT_ONE
void jit_command(lua_State*, LPCSTR);
#endif

#if defined(USE_DEBUGGER) && defined(USE_LUA_STUDIO)
static void log_callback			(LPCSTR message)
{
    if (s_old_log_callback)
        s_old_log_callback			(message);

    if (!ai().script_engine().debugger())
        return;

    ai().script_engine().debugger()->add_log_line	(message);
}

static void initialize_lua_studio	( lua_State* state, cs::lua_studio::world*& world, lua_studio_engine*& engine)
{
    engine							= 0;
    world							= 0;

    u32 const old_error_mode		= SetErrorMode(SEM_FAILCRITICALERRORS);
    s_script_debugger_handle		= LoadLibrary(CS_LUA_STUDIO_BACKEND_FILE_NAME);
    SetErrorMode					(old_error_mode);
    if (!s_script_debugger_handle) {
        Msg							("! cannot load %s dynamic library", CS_LUA_STUDIO_BACKEND_FILE_NAME);
        return;
    }

    R_ASSERT2						(s_script_debugger_handle, "can't load script debugger library");

    s_create_world					= (create_world_function_type)
        GetProcAddress(
        s_script_debugger_handle,
        "_cs_lua_studio_backend_create_world@12"
        );
    R_ASSERT2						(s_create_world, "can't find function \"cs_lua_studio_backend_create_world\"");

    s_destroy_world					= (destroy_world_function_type)
        GetProcAddress(
        s_script_debugger_handle,
        "_cs_lua_studio_backend_destroy_world@4"
        );
    R_ASSERT2						(s_destroy_world, "can't find function \"cs_lua_studio_backend_destroy_world\" in the library");

    engine							= xr_new<lua_studio_engine>();
    world							= s_create_world( *engine, false, false );
    VERIFY							(world);

    s_old_log_callback				= SetLogCB(&log_callback);

#ifdef USE_LUAJIT_ONE
    jit_command						(state, "debug=2");
    jit_command						(state, "off");
#else
	luaJIT_setmode(state, 0, LUAJIT_MODE_ENGINE | LUAJIT_MODE_OFF);
#endif

    world->add						(state);
}

static void finalize_lua_studio		( lua_State* state, cs::lua_studio::world*& world, lua_studio_engine*& engine)
{
    world->remove					(state);

    VERIFY							(world);
    s_destroy_world					(world);
    world							= 0;

    VERIFY							(engine);
    xr_delete						(engine);

    FreeLibrary						(s_script_debugger_handle);
    s_script_debugger_handle		= 0;

    SetLogCB						(s_old_log_callback);
}

void CScriptEngine::try_connect_to_debugger		()
{
    if (m_lua_studio_world)
        return;

    initialize_lua_studio			( lua(), m_lua_studio_world, m_lua_studio_engine );
}

void CScriptEngine::disconnect_from_debugger	()
{
    if (!m_lua_studio_world)
        return;

    finalize_lua_studio				( lua(), m_lua_studio_world, m_lua_studio_engine );
}
#endif //-(USE_DEBUGGER) && defined(USE_LUA_STUDIO)

void CScriptEngine::setup_callbacks()
{
#ifdef USE_DEBUGGER
#	ifndef USE_LUA_STUDIO
    if( debugger() )
        debugger()->PrepareLuaBind	();
#	endif // #ifndef USE_LUA_STUDIO
#endif

#ifdef USE_DEBUGGER
#	ifndef USE_LUA_STUDIO
    if (!debugger() || !debugger()->Active() )
#	endif // #ifndef USE_LUA_STUDIO
#endif
	{
#if !XRAY_EXCEPTIONS
		luabind::set_error_callback(CScriptEngine::lua_error);
#endif

		luabind::set_pcall_callback(CScriptEngine::lua_pcall_failed);
	}

#if !XRAY_EXCEPTIONS
	luabind::set_cast_failed_callback(lua_cast_failed);
#endif
	lua_atpanic(lua(), CScriptEngine::lua_panic);
}

#ifdef DEBUG
#	include "script_thread.h"
void CScriptEngine::lua_hook_call		(lua_State *L, lua_Debug *dbg)
{
    if (ai().script_engine().current_thread())
        ai().script_engine().current_thread()->script_hook(L,dbg);
    else
        ai().script_engine().m_stack_is_ready	= true;
}
#endif

void CScriptEngine::remove_script_process(const EScriptProcessors& process_id)
{
	CScriptProcessStorage::iterator I = m_script_processes.find(process_id);
	if (I != m_script_processes.end())
	{
		xr_delete((*I).second);
		m_script_processes.erase(I);
	}
}

bool CScriptEngine::load_package(LPCSTR caNamespaceName, bool warn_if_not_exist)
{
    if (*caNamespaceName && xr_strcmp(caNamespaceName, "_G") && namespace_loaded(caNamespaceName))
    {
        return true;
    }

    string_path caScriptName, S1;
    FS.update_path(caScriptName, "$game_scripts$", strconcat(sizeof(S1), S1, caNamespaceName, ".script"));
    if (!warn_if_not_exist && !FS.exist(caScriptName))
    {
#ifdef DEBUG
#	ifndef XRSE_FACTORY_EXPORTS
        if (psAI_Flags.test(aiNilObjectAccess))
#	endif
        {
            print_stack();
            Msg("* trying to access variable %s, which doesn't exist, or to load script %s, which doesn't exist too", file_name, S);
            m_stack_is_ready = true;
        }
#endif
        return false;
    }

    //#ifndef MASTER_GOLD
    if (strstr(Core.Params, "-dbg"))
        Msg("* loading script %s", S1);
    //#endif // MASTER_GOLD

    if (!caNamespaceName)
        caNamespaceName = "_G";
    
    int start = lua_gettop(lua());
    string_path l_caLuaFileName;
    IReader* l_tpFileReader = FS.r_open(caScriptName);

    if (!l_tpFileReader)
    {
        script_log(eLuaMessageTypeError, "Cannot open file \"%s\"", caScriptName);
        return (false);
    }

    auto scriptContents = static_cast<LPCSTR>(l_tpFileReader->pointer());
    auto scriptLength = (size_t)l_tpFileReader->length();

    strconcat(sizeof(l_caLuaFileName), l_caLuaFileName, "@", caScriptName);
    if (load_buffer(lua(), scriptContents, scriptLength, l_caLuaFileName, caNamespaceName))
    {
        //		VERIFY		(lua_gettop(lua()) >= 4);
        //		lua_pop		(lua(),4);
        //		VERIFY		(lua_gettop(lua()) == start - 3);
        lua_settop(lua(), start);
        FS.r_close(l_tpFileReader);
        return (false);
    }
    FS.r_close(l_tpFileReader);

    int errFuncId = -1;
#ifdef USE_DEBUGGER
#	ifndef USE_LUA_STUDIO
    if (ai().script_engine().debugger())
        errFuncId = ai().script_engine().debugger()->PrepareLua(lua());
#	endif // #ifndef USE_LUA_STUDIO
#endif // #ifdef USE_DEBUGGER
    if (0) //.
    {
        for (int i = 0; lua_type(lua(), -i - 1); i++)
            Msg("%2d : %s", -i - 1, lua_typename(lua(), lua_type(lua(), -i - 1)));
    }

    // because that's the first and the only call of the main chunk - there is no point to compile it
    //	luaJIT_setmode	(lua(),0,LUAJIT_MODE_ENGINE|LUAJIT_MODE_OFF);						// Oles
    int l_iErrorCode = lua_pcall(lua(), 0, 0, (-1 == errFuncId) ? 0 : errFuncId); // new_Andy
    //	luaJIT_setmode	(lua(),0,LUAJIT_MODE_ENGINE|LUAJIT_MODE_ON);						// Oles

#ifdef USE_DEBUGGER
#	ifndef USE_LUA_STUDIO
    if (ai().script_engine().debugger())
        ai().script_engine().debugger()->UnPrepareLua(lua(), errFuncId);
#	endif // #ifndef USE_LUA_STUDIO
#endif // #ifdef USE_DEBUGGER
    if (l_iErrorCode)
    {
        //#ifdef DEBUG
        if (strstr(Core.Params, "-dbg")) print_output(lua(), caScriptName, l_iErrorCode);
        //#endif
        on_error(lua());
        Msg("! [ERROR] --- Failed to load script %s", caNamespaceName);
        lua_settop(lua(), start);
        return (false);
    }

    VERIFY(lua_gettop(lua()) == start);
    return (true);
}

void CScriptEngine::unload_package(LPCSTR name)
{
    lua_getglobal(lua(), "package");
    lua_getfield(lua(), -1, "loaded");
    lua_remove(lua(), -2);
    lua_pushnil(lua());
    lua_setfield(lua(), -2, name);
    lua_remove(lua(), -1);
}

bool CScriptEngine::function_object(LPCSTR function_to_call, luabind::object& out, int type)
{
    int start = lua_gettop(lua());
    lua_getglobal(lua(), "function_object");
    lua_pushstring(lua(), function_to_call);

    int l_iErrorCode = lua_pcall(lua(), 1, 1, 0);
    VERIFY(lua_gettop(lua()) == start + 1);
    if (l_iErrorCode)
    {
        lua_pop(lua(), 1);
        VERIFY(lua_gettop(lua()) == start);
        return false;
    }

    bool is_type = lua_type(lua(), -1) == type;
    out = luabind::object(lua());
    out.set();

    return is_type;
}

void CScriptEngine::collect_all_garbage()
{
    lua_gc(lua(), LUA_GCCOLLECT, 0);
    lua_gc(lua(), LUA_GCCOLLECT, 0);
}

void CScriptEngine::on_error(lua_State* state)
{
#if defined(USE_DEBUGGER) && defined(USE_LUA_STUDIO)
    if (!debugger())
        return;

    debugger()->on_error(state);
#endif // #if defined(USE_DEBUGGER) && defined(USE_LUA_STUDIO)
}

#if defined(USE_DEBUGGER) && !defined(USE_LUA_STUDIO)
void CScriptEngine::stopDebugger				()
{
    if (debugger()){
        xr_delete	(m_scriptDebugger);
        Msg			("Script debugger succesfully stoped.");
    }
    else
        Msg			("Script debugger not present.");
}

void CScriptEngine::restartDebugger				()
{
    if(debugger())
        stopDebugger();

    m_scriptDebugger = xr_new<CScriptDebugger>();
    debugger()->PrepareLuaBind();
    Msg				("Script debugger succesfully restarted.");
}
#endif // #if defined(USE_DEBUGGER) && !defined(USE_LUA_STUDIO)
