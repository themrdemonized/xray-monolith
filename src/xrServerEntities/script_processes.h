#include <luabind/luabind.hpp>
#include "script_process.h"

// Luabind wrapper for xr/processes
class CScriptProcesses
{
private:
    luabind::object m_obj;

public:
    CScriptProcesses(lua_State* L)
    {
        lua_getglobal(L, "require");
        lua_pushstring(L, "xr.processes");
        lua_call(L, 1, 1);

        m_obj = luabind::object(L);
        m_obj.set();
    }

    ~CScriptProcesses() {}

    template <typename T>
    luabind::functor<T> functor(LPCSTR field)
    {
        return luabind::object_cast<luabind::functor<T>>(m_obj[field]);
    }

    void add(LPCSTR name, LPCSTR scripts)
    {
        functor<void>("add")(m_obj, name, scripts);
    }

    void remove(LPCSTR name)
    {
        functor<void>("remove")(m_obj, name);
    }

    bool has(LPCSTR name)
    {
        return functor<bool>("has")(m_obj, name);
    }

    CScriptProcess get(LPCSTR name)
    {
        return CScriptProcess(functor<luabind::object>("get")(m_obj, name));
    }
};