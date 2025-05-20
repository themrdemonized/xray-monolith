#include "stdafx.h"
#include "script_compiler.h"
#include "ai_space.h"
#include "script_engine.h"
#include "../xrCore/mezz_stringbuffer.h"
#include "lua_macros.h"
#include "luabind/luabind.hpp"
#include <regex>
#include <lua.h>

Unlocalizers unlocalizers;
CWuaMacro wua;

void CScriptCompiler::load_unlocalizers()
{
    auto file_list = FS.file_list_open("$game_config$", "unlocalizers\\", FS_RootOnly | FS_ListFiles);
    if (!file_list) {
        return;
    }
    else {
        xr_string id;
        auto i = file_list->begin();
        auto e = file_list->end();
        for (; i != e; ++i)
        {
            u32 length = xr_strlen(*i);

            if (!((length >= 4) &&
                ((*i)[length - 4] == '.') &&
                ((*i)[length - 3] == 'l') &&
                ((*i)[length - 2] == 't') &&
                ((*i)[length - 1] == 'x')))
                continue;

            id.assign(*i, length - 4);

            string_path file_name;
            FS.update_path(file_name, "$game_config$", (xr_string("unlocalizers\\") + id).c_str());
            xr_strcat(file_name, ".ltx");

            Msg("opening file %s", file_name);
            auto config = xr_new<CInifile>(file_name);

            typedef CInifile::Root sections_type;
            sections_type& sections = config->sections();

            sections_type::const_iterator i = sections.begin();
            sections_type::const_iterator e = sections.end();
            for (; i != e; ++i)
            {
                auto sectionName = std::string((*i)->Name.c_str());
                toLowerCase(sectionName);
                if (unlocalizers.find(sectionName) == unlocalizers.end()) {

                    // construct set that contains top level variables to delocalize by section name
                    unlocalizers[sectionName].clear();
                    Msg("creating unlocalizer for script %s", sectionName.c_str());
                }
                auto& data = (*i)->Data;
                for (auto& item : data) {
                    unlocalizers[sectionName].insert(std::string(item.first.c_str()));
                    Msg("adding variable %s for unlocalizer for script %s", item.first.c_str(), sectionName.c_str());
                }
            }
            xr_delete(config);
        }
        FS.file_list_close(file_list);
    }
}

Unlocalizer* CScriptCompiler::get_unlocalizer(std::string name)
{
    toLowerCase(name);
    if (unlocalizers.find(name) != unlocalizers.end())
    {
        Msg("Found key %s in unlocalizers data", name);
        return &unlocalizers[name];
    }

    return NULL;
}

int CScriptCompiler::compile(
    lua_State* L,
    std::string caString,
    LPCSTR caScriptName,
    LPCSTR caNameSpaceName
)
{
    std::regex pattern(R"(#macro (.*)(\s+))");
    std::smatch match;
    if (std::regex_search(caString, match, pattern))
    {
        std::string macro_name = match[1];
        caString = std::string(match[2]) + std::string(match.suffix());
        if (macro_name != "wua")
        {
            luabind::functor<luabind::object> macro;
            if (ai().script_engine().functor((std::string("macro_") + macro_name).c_str(), macro))
            {

                luabind::object unlocs = luabind::newtable(ai().script_engine().lua());
                Unlocalizer* unlocalizer = get_unlocalizer(caNameSpaceName);
                if (unlocalizer)
                {
                    int i = 1;
                    for (auto unloc : *unlocalizer)
                    {
                        unlocs[i] = unloc.c_str();
                        i++;
                    }
                }

                luabind::object result = macro(caString.c_str(), caNameSpaceName, unlocs);
                result.pushvalue();
                return 0;
            }
            else
            {
                Msg("No such macro: %s", macro_name);
                FATAL("Failed to load macro");
            }
        }
    }

    caString = wua.lift(caString, caNameSpaceName);
    return luaL_loadbuffer(L, caString.c_str(), caString.length(), caScriptName);
}
