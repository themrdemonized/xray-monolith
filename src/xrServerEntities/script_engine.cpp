////////////////////////////////////////////////////////////////////////////
//	Module 		: script_engine.cpp
//	Created 	: 01.04.2004
//  Modified 	: [1/14/2015 Andrey]
//	Author		: Dmitriy Iassenev
//	Description : XRay Script Engine
////////////////////////////////////////////////////////////////////////////

#include "pch_script.h"
#include "script_engine.h"
#include "script_storage.h"
#include "ai_space.h"
#include "object_factory.h"
#include "../build_config_defines.h"
#include "../xrCore/mezz_stringbuffer.h"
#include <stdarg.h>
#include <unordered_map>
#include <set>
#include <boost/noncopyable.hpp>
#include "luabind/object.hpp"
#include "luabind/functor.hpp"

using namespace ScriptEngine;

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

#ifndef PURE_ALLOC
//#	ifndef USE_MEMORY_MONITOR
//#		define USE_DL_ALLOCATOR
//#	endif //!USE_MEMORY_MONITOR
#endif //!PURE_ALLOC

extern void export_classes(lua_State* L);
extern int luaopen_lua_extensions(lua_State* L);

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
}; //-struct raii_guard

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

void printLuaStack()
{
    ai().script_engine().print_stack();
}

void on_lua_error(lua_State* L)
{
    ai().script_engine().print_stack();
    ai().script_engine().print_output(L, "", LUA_ERRRUN);
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

int on_lua_pcall_failed(lua_State* L)
{
    ai().script_engine().print_stack();
    ai().script_engine().print_output(L, "", LUA_ERRRUN);
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

int on_lua_panic(lua_State* L)
{
    ai().script_engine().print_stack();
    ai().script_engine().print_output(L, "PANIC", LUA_ERRRUN);
    return (0);
}

void on_lua_cast_failed(lua_State* L, LUABIND_TYPE_INFO info)
{
    CScriptEngine::print_output(L, "", LUA_ERRRUN);
    Debug.fatal(DEBUG_INFO, "LUA error: cannot cast lua value to %s", info->name());
}

CScriptEngine::CScriptEngine()
{
#ifdef DEBUG
    m_stack_is_ready = false;
#endif //-DEBUG

    m_virtual_machine = 0;
    m_stack_level = 0;
}

CScriptEngine::~CScriptEngine()
{
#ifdef LUA_DEBUG_PRINT
    flush_log();
#endif //-LUA_DEBUG_PRINT

    if (m_virtual_machine)
        lua_close(m_virtual_machine);
}

// Low level path -> string content loader to work around intractable Lua IReader::r_stringZ behaviour
static int load_file(lua_State* L)
{
    LPCSTR path = lua_tostring(L, 1);
    if (!path)
        FATAL("Invalid path");

    IReader* file = FS.r_open(path);

    if (!file)
        FATAL("Invalid file");

    lua_pushlstring(L, (LPCSTR)file->pointer(), file->length());

    FS.r_close(file);

    return (1);
}

void CScriptEngine::init()
{
    CScriptEngine::reinit();

    luabind::open(lua());
    setup_callbacks();
    export_classes(lua());

#ifdef DEBUG
    m_stack_is_ready = true;
#endif

    //	lua_sethook(lua(), lua_hook_call, LUA_MASKLINE|LUA_MASKCALL|LUA_MASKRET, 0);

    // Force the FS to recursively enumerate the scripts folder
    FS_Path* P = FS.get_path("$game_scripts$");
    P->m_Flags.set(FS_Path::flNeedRescan, TRUE);
    FS.rescan_pathes();

    // Emplace the object factory
    luabind::object(lua(), const_cast<CObjectFactory*>(&object_factory())).pushvalue();
    lua_setglobal(lua(), "_OBJECT_FACTORY");

    // Emplace script storage
    CScriptStorage::script_register(lua());
    luabind::object(lua(), const_cast<CScriptStorage*>(&ScriptStorage())).pushvalue();
    lua_setglobal(lua(), "_SCRIPT_STORAGE");

    lua_pushcfunction(lua(), load_file);
    lua_setglobal(lua(), "_LOAD_FILE");

    Msg("* engine: loading init.lua");

    // Fetch init.lua's path from the FS
    string_path path;
    FS.update_path(path, "$game_scripts$", "init.lua");
    if (!path)
        FATAL("* engine: invalid init.lua path");

    // Open a file handle and read it into a string
    auto file = FS.r_open(path);
    if (!file)
        FATAL("* engine: failed to load init.lua");
    std::string src((char*)file->pointer(), file->length());
    FS.r_close(file);

    // Run the resulting source
    if (luaL_dostring(lua(), src.c_str()))
    {
        LPCSTR e = lua_tostring(lua(), -1);
        lua_pop(lua(), 1);
        FATAL((std::string("! engine: error loading init.lua:\n") + e).c_str());
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
    // Restore original stack level
    lua_settop(lua(), m_stack_level);
}

void CScriptEngine::setup_callbacks()
{
#if !XRAY_EXCEPTIONS
    luabind::set_error_callback(on_lua_error);
#endif

    luabind::set_pcall_callback(on_lua_pcall_failed);

#if !XRAY_EXCEPTIONS
    luabind::set_cast_failed_callback(on_lua_cast_failed);
#endif
    lua_atpanic(lua(), on_lua_panic);
}

int CScriptEngine::load_string(
    LPCSTR caString,
    LPCSTR caScriptName,
    LPCSTR caNameSpaceName
)
{
    lua_getglobal(lua(), "_COMPILER");
    if (!lua_isfunction(lua(), -1))
    {
        FATAL("_COMPILER not available");
    }

    lua_pushstring(lua(), caString);
    lua_pushstring(lua(), caNameSpaceName);
    lua_pushstring(lua(), caScriptName);
    int l_iErrorCode = lua_pcall(lua(), 3, 1, 0);
    if (l_iErrorCode)
    {
        //#ifdef DEBUG
        if (strstr(Core.Params, "-dbg")) print_output(lua(), caScriptName, l_iErrorCode);
        //#endif //-DEBUG
        on_error(lua());
    }
    return l_iErrorCode;
}

int CScriptEngine::do_string(
    LPCSTR caString,
    LPCSTR caScriptName,
    LPCSTR caNameSpaceName
)
{
    int l_iErrorCode;

    l_iErrorCode = load_string(caString, caScriptName, caNameSpaceName);
    if (l_iErrorCode)
    {
        ai().script_engine().print_output(ai().script_engine().lua(), caScriptName, l_iErrorCode);
        ai().script_engine().on_error(ai().script_engine().lua());
        return l_iErrorCode;
    }

    l_iErrorCode = lua_pcall(ai().script_engine().lua(), 0, 0, 0);
    if (l_iErrorCode)
    {
        ai().script_engine().print_output(ai().script_engine().lua(), caScriptName, l_iErrorCode);
        ai().script_engine().on_error(ai().script_engine().lua());
        return l_iErrorCode;
    }

    return l_iErrorCode;
}

int CScriptEngine::vscript_log(ELuaMessageType tLuaMessageType, LPCSTR caFormat, va_list marker)
{
#ifndef NO_XRGAME_SCRIPT_ENGINE
#   ifdef DEBUG
    if (!psAI_Flags.test(aiLua) && (tLuaMessageType != eLuaMessageTypeError))
        return(0);
#   endif //-DEBUG
#endif //!NO_XRGAME_SCRIPT_ENGINE

    //#ifndef PRINT_CALL_STACK
    //return		(0);
    //#else //PRINT_CALL_STACK
#   ifndef NO_XRGAME_SCRIPT_ENGINE
    //AVO: allow LUA debug prints (i.e.: ai().script_engine().script_log(eLuaMessageTypeError, "CWeapon : cannot access class member Weapon_IsScopeAttached!");)
#       ifndef DEBUG

    if (!strstr(Core.Params, "-dbg"))
        return (0);
#       endif //!DEBUG
#       ifndef LUA_DEBUG_PRINT
#           ifdef DEBUG
    if (!psAI_Flags.test(aiLua) && (tLuaMessageType != eLuaMessageTypeError))
        return(0);
#           endif //-DEBUG
#       else //!LUA_DEBUG_PRINT
    if (!psAI_Flags.test(aiLua) && (tLuaMessageType != eLuaMessageTypeError))
        return(0);
#       endif //-LUA_DEBUG_PRINT
#endif //-NO_XRGAME_SCRIPT_ENGINE

    LPCSTR S = "", SS = "";
    LPSTR S1;
    string4096 S2;
    switch (tLuaMessageType)
    {
    case eLuaMessageTypeInfo:
    {
        S = "* [LUA] ";
        SS = "[INFO]        ";
        break;
    }
    case eLuaMessageTypeError:
    {
        S = "! [LUA] ";
        SS = "[ERROR]       ";
        break;
    }
    case eLuaMessageTypeMessage:
    {
        S = "~ [LUA] ";
        SS = "[MESSAGE]     ";
        break;
    }
    case eLuaMessageTypeHookCall:
    {
        S = "[LUA][HOOK_CALL] ";
        SS = "[CALL]        ";
        break;
    }
    case eLuaMessageTypeHookReturn:
    {
        S = "[LUA][HOOK_RETURN] ";
        SS = "[RETURN]      ";
        break;
    }
    case eLuaMessageTypeHookLine:
    {
        S = "[LUA][HOOK_LINE] ";
        SS = "[LINE]        ";
        break;
    }
    case eLuaMessageTypeHookCount:
    {
        S = "[LUA][HOOK_COUNT] ";
        SS = "[COUNT]       ";
        break;
    }
    case eLuaMessageTypeHookTailReturn:
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
            script_log_no_stack(eLuaMessageTypeError, "%2d : [%s] %s(%d) : %s", i, l_tDebugInfo.what,
                l_tDebugInfo.short_src, l_tDebugInfo.currentline, "");
            //script_log(eLuaMessageTypeError, "%2d : [%s] %s(%d) : %s", i, l_tDebugInfo.what, l_tDebugInfo.short_src, l_tDebugInfo.currentline, "");
        }
        else
        {
            if (!xr_strcmp(l_tDebugInfo.what, "C"))
            {
                script_log_no_stack(eLuaMessageTypeError, "%2d : [C  ] %s", i, l_tDebugInfo.name);
                //script_log(eLuaMessageTypeError, "%2d : [C  ] %s", i, l_tDebugInfo.name);  
            }
            else
            {
                script_log_no_stack(eLuaMessageTypeError, "%2d : [%s] %s(%d) : %s", i, l_tDebugInfo.what,
                    l_tDebugInfo.short_src, l_tDebugInfo.currentline, l_tDebugInfo.name);
                //script_log(eLuaMessageTypeError, "%2d : [%s] %s(%d) : %s", i, l_tDebugInfo.what, l_tDebugInfo.short_src, l_tDebugInfo.currentline, l_tDebugInfo.name);
            }
        }
    }
}

//#endif //-PRINT_CALL_STACK

//AVO: added to stop duplicate stack output prints in log
int __cdecl CScriptEngine::script_log_no_stack(ELuaMessageType tLuaMessageType, LPCSTR caFormat, ...)
{
    va_list marker;
    va_start(marker, caFormat);
    int result = vscript_log(tLuaMessageType, caFormat, marker);
    va_end(marker);
    return result;
}

//-AVO

int __cdecl CScriptEngine::script_log(ELuaMessageType tLuaMessageType, LPCSTR caFormat, ...)
{
    va_list marker;
    va_start(marker, caFormat);
    int result = vscript_log(tLuaMessageType, caFormat, marker);
    va_end(marker);

    static bool reenterability = false;
    if (!reenterability)
    {
        reenterability = true;
        if (tLuaMessageType == eLuaMessageTypeError) {
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
    }
    else
    {
        if (!iErorCode)
            script_log(eLuaMessageTypeInfo, "Output from %s", caScriptFileName);
        script_log(iErorCode ? eLuaMessageTypeError : eLuaMessageTypeMessage, "%s", S);
    }
    return (true);
}

void CScriptEngine::print_error(lua_State* L, int iErrorCode)
{
    switch (iErrorCode)
    {
    case LUA_ERRRUN:
    {
        script_log(eLuaMessageTypeError, "SCRIPT RUNTIME ERROR");
        break;
    }
    case LUA_ERRMEM:
    {
        script_log(eLuaMessageTypeError, "SCRIPT ERROR (memory allocation)");
        break;
    }
    case LUA_ERRERR:
    {
        script_log(eLuaMessageTypeError, "SCRIPT ERROR (while running the error handler function)");
        break;
    }
    case LUA_ERRFILE:
    {
        script_log(eLuaMessageTypeError, "SCRIPT ERROR (while running file)");
        break;
    }
    case LUA_ERRSYNTAX:
    {
        script_log(eLuaMessageTypeError, "SCRIPT SYNTAX ERROR");
        break;
    }
    case LUA_YIELD:
    {
        script_log(eLuaMessageTypeInfo, "Thread is yielded");
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

#ifdef DEBUG
void CScriptEngine::lua_hook_call(lua_State *L, lua_Debug *dbg)
{
    ai().script_engine().m_stack_is_ready = true;
}
#endif

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

CScriptProcesses CScriptEngine::script_processes()
{
    return CScriptProcesses(lua());
}

void CScriptEngine::collect_all_garbage()
{
    lua_gc(lua(), LUA_GCCOLLECT, 0);
}

void CScriptEngine::on_error(lua_State* L)
{
    CScriptEngine::print_output(L, "", LUA_ERRRUN);
    FATAL("LUA error");
}
