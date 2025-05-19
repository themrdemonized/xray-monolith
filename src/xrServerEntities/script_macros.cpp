#include "stdafx.h"
#include "script_macros.h"
#include "ai_space.h"
#include "script_engine.h"
#include "../xrCore/mezz_stringbuffer.h"
#include "lua_macros.h"
#include "luabind/luabind.hpp"
#include <regex>
#include <lua.h>

std::string CScriptMacros::lift(
    std::string caString,
    LPCSTR caScriptName,
    LPCSTR caNameSpaceName,
    Unlocalizers* unlocalizers
) const
{
    Unlocalizer* unlocalizer = NULL;
    if (caNameSpaceName)
    {
        std::string loweredNameSpaceName;
        loweredNameSpaceName += caNameSpaceName;
        toLowerCase(loweredNameSpaceName);
        if (unlocalizers && unlocalizers->find(loweredNameSpaceName) != unlocalizers->end())
        {
            Msg("found script %s in unlocalizers data", caNameSpaceName);
            // Iterate lines and unlocalize variables
            unlocalizer = &(*unlocalizers)[loweredNameSpaceName];
        }
    }

    std::regex pattern(R"(#macro (.*)(\s+))");
    std::smatch match;
    if (std::regex_search(caString, match, pattern))
    {
        std::string macro_name = match[1];
        caString = std::string(match[2]) + std::string(match.suffix());
        if (macro_name != "wua")
        {
            luabind::functor<LPCSTR> macro;
            if (ai().script_engine().functor(string_format("macro_%s.%s", macro_name, macro_name).c_str(), macro))
            {

                luabind::object unlocs = luabind::newtable(ai().script_engine().lua());
                if (unlocalizer)
                {
                    int i = 1;
                    for (auto unloc : *unlocalizer)
                    {
                        unlocs[i] = unloc.c_str();
                        i++;
                    }
                }

                return std::string(macro(caString.c_str(), caNameSpaceName, unlocs));
            }
            else
            {
                Msg("No such macro: %s", macro_name);
                FATAL("Failed to load macro");
            }
        }
    }

    if (unlocalizer)
        caString = wua.unlocalize(caString, caNameSpaceName, *unlocalizer);

    return wua.lift(caString, caNameSpaceName);
}
