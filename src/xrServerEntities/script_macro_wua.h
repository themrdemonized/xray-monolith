#pragma once

#include "script_macro.h"

class CWuaMacro : public CScriptMacro
{
public:
    virtual std::string lift(std::string src, LPCSTR caNameSpaceName) const override;
};
