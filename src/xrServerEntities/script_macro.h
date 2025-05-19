#pragma once

#include "script_export_space.h"
#include <string>

typedef std::set<std::string> Unlocalizer;
typedef xr_unordered_map<std::string, Unlocalizer> Unlocalizers;

class CScriptMacro : public DLL_Pure
{
public:
    virtual std::string unlocalize(const std::string& src, LPCSTR caNameSpaceName, Unlocalizer& unlocalizer) const;
    virtual std::string lift(const std::string& src, LPCSTR caNameSpaceName) const;

DECLARE_SCRIPT_REGISTER_FUNCTION
};

add_to_type_list(CScriptMacro)
#undef script_type_list
#define script_type_list save_type_list(CScriptMacro)