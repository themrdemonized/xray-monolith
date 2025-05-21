#include "stdafx.h"
#include "lua_macros.h"
#include "ai_space.h"
#include "script_engine.h"

#include <string>
#include <sstream>
#include <regex>
#include <luabind/luabind.hpp>
#include "../xrCore/mezz_stringbuffer.h"

typedef std::set<std::string> Unlocalizer;
typedef xr_unordered_map<std::string, Unlocalizer> Unlocalizers;

static bool unlocalRegex(Unlocalizer& unlocals, std::string& s, const std::regex& pattern, const int group, const std::string& replacement) {
    if (std::regex_match(s, pattern)) {
        //Msg("matching local function pattern");
        std::smatch match;
        std::regex_search(s, match, pattern);
        std::string variable = match[group];
        if (unlocals.find(variable) != unlocals.end()) {
            Msg("[unlocalRegex] found variable %s to unlocal", variable.c_str());
            s = std::regex_replace(s, pattern, replacement);
            return true;
        }
    }
    else {
        return false;
    }
    return false;
};

static std::string join_list(const std::vector<std::string>& items_vec, std::string delim = "\n") {
    std::string ret;
    for (const auto& i : items_vec) {
        if (!ret.empty()) {
            ret += delim;
        }
        ret += i;
    }
    return ret;
};

static std::string unlocalize(const std::string& src, LPCSTR caNameSpaceName)
{
    if (!caNameSpaceName)
        return src;

    Unlocalizer unlocalizer;

    luabind::functor<luabind::object> f;
    if (xr_strcmp(caNameSpaceName, "unlocalizers") == 0)
        return src;

    VERIFY(!ai().script_engine().functor("unlocalizers.get", f));

    luabind::object table = f(caNameSpaceName);

    if (table.type() != LUA_TTABLE)
        return src;

    for (luabind::object o : table)
    {
        unlocalizer.insert(luabind::object_cast<LPCSTR>(o));
    }

    bool unlocalPerformed = false;
    std::string unlocalizerResult;

    // Get contents of the script file and split by lines
    std::vector<std::string> tokens;
    std::string temp;
    temp += src;

    std::stringstream stringStream(temp);
    std::string line;
    tokens.clear();
    while (std::getline(stringStream, line)) {
        tokens.push_back(line);
    }

    /*for (auto& u : unlocalizer) {
        Msg("Unlocalizer: %s", u);
    }*/

    for (std::string& s : tokens) {

        //Msg("Line: %s", s.c_str());

        trim(s, "\n\r");
        if (s.empty()) {
            //Msg("Empty, continuing");
            continue;
        }

        std::regex pattern;

        //local function x(a,b,c)
        pattern = std::regex(R"((^local)([\t ]+)(function)([\t ]+)([_a-zA-Z].*)([\t ]*)(\(.*$))");
        if (unlocalRegex(unlocalizer, s, pattern, 5, "$3$4$5$6$7")) {
            //Msg("Regex matched");
            unlocalPerformed = true;
            continue;
        }

        //Msg("Regex not matched");

        //local a = ...
        //local a
        //local a,b,c = ... (if one of a,b,c is in unlocalizers list - all of them will be unlocalized)
        //local x; local y; - unsupported yet
        pattern = std::regex(R"((^local)([\t ]+)(.*))");
        if (std::regex_match(s, pattern)) {
            std::smatch match;
            std::regex_search(s, match, pattern);
            std::string m = match[3];

            // strip comments
            std::regex r = std::regex(R"((.*)--.*)");
            if (std::regex_match(m, r)) {
                //Msg("found comments\n");
                std::smatch noncomments;
                std::regex_search(m, noncomments, r);
                m = noncomments[1];
            }

            auto variablesAndValues = splitStringLimit(m, "=", 1);
            bool hasValue = variablesAndValues.size() > 1;
            auto variables = splitStringMulti(variablesAndValues[0], ",");
            for (auto v : variables) {
                trim(v);
                //Msg("%s\n", v.c_str());
                if (unlocalizer.find(v) != unlocalizer.end()) {
                    unlocalPerformed = true;
                    Msg("found variable %s to unlocal", v.c_str());
                    s = std::regex_replace(s, pattern, "$3");
                    if (!hasValue) {

                        // strip comments
                        std::regex r = std::regex(R"((.*)(--.*))");
                        if (std::regex_match(s, r)) {
                            //Msg("found comments\n");
                            std::smatch noncomments;
                            std::regex_search(s, noncomments, r);
                            s = std::string(noncomments[1]) + "= nil " + std::string(noncomments[2]);
                        }
                        else {
                            s += " = nil";
                        }
                    }
                    break;
                }
            }
        }
    }

    // Store result back
    /*for (auto& s : tokens) {
        Msg("%s", s.c_str());
    }*/

    if (unlocalPerformed)
    {
        return join_list(tokens);
    }

    return src;
}

static LPCSTR compile_wua(LPCSTR buffer, LPCSTR caNameSpaceName)
{
    std::string src(buffer);
    src = unlocalize(src, caNameSpaceName);
    bool is_g = caNameSpaceName && xr_strcmp(caNameSpaceName, "_G") == 0;
    std::string out;
    out += wua_environment("G");
    out += R"(
local mt = {}
mt.__index = G
    )";
    if (is_g)
        out += R"(
mt.__newindex = function(self, key, value)
    _G[key] = value
end
        )";

    out += R"(
local this = {}
this._G = G 
    )";

    if (!is_g)
    {
        out += string_format(
            R"(
package.loaded["%s"] = this
            )",
            caNameSpaceName
        );
    }

    out += R"(
setmetatable(this, mt)
setfenv(1, this)
    )";

    out += string_format(
        R"(
local function script_name()
return "%s"
end
        )",
        caNameSpaceName
    );

    out += "\n" + src;

    return out.c_str();
}
