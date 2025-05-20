#pragma once

#include "script_export_space.h"
#include <string>

class CScriptMacro : public DLL_Pure
{
public:
    virtual std::string lift(std::string src, LPCSTR caNameSpaceName) const;

DECLARE_SCRIPT_REGISTER_FUNCTION
};

add_to_type_list(CScriptMacro)
#undef script_type_list
#define script_type_list save_type_list(CScriptMacro)