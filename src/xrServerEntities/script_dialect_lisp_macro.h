#pragma once

#include "script_dialect.h"

class CLispMacroDialect : public CScriptDialect
{
private:
    const char* tag() const;
public:
    std::string wrap_body(const std::string& src, LPCSTR caNameSpaceName) const;
};
