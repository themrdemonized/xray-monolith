#pragma once

#include "script_macro.h"

class CWuaMacro : public CScriptMacro
{
public:
    virtual std::string unlocalize(const std::string& src, LPCSTR caNameSpaceName, Unlocalizer& unlocalizer) const override;
    virtual std::string lift(const std::string& src, LPCSTR caNameSpaceName) const override;
};
