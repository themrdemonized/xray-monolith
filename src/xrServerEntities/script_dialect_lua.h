#pragma once

#include "script_dialect.h"

class CLuaDialect : public CScriptDialect
{
private:
    const char* tag() const;
public:
    std::string unlocalize(Unlocalizer& unlocalizer, const std::string& src, LPCSTR caNameSpaceName) const;
};
