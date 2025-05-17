#pragma once

#include "script_dialect.h"

class CLispMacroDialect : public CScriptDialect
{
private:
    const char* tag() const;
public:
    std::string wrap(const std::string& src, LPCSTR caNameSpaceName) const;
    std::string unlocalize(Unlocalizer& unlocalizer, const std::string& src, LPCSTR caNameSpaceName) const;
};
