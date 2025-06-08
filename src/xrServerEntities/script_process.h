#include <luabind/luabind.hpp>

class CScriptProcess
{
private:
    luabind::object m_obj;

public:
    CScriptProcess(luabind::object obj)
    {
        m_obj = obj;
    }

    ~CScriptProcess() {}

    template <typename T>
    luabind::functor<T> functor(LPCSTR field)
    {
        return luabind::object_cast<luabind::functor<T>>(m_obj[field]);
    }

    void update()
    {
        functor<void>("update")(m_obj);
    }

    void add_script(LPCSTR name, bool reload)
    {
        functor<void>("add_script")(m_obj, name, reload);
    }

    void add_string(LPCSTR src)
    {
        functor<void>("add_string")(m_obj, src);
    }
};
