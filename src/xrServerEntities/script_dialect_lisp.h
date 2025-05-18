#pragma once

#include "script_dialect.h"

class CLispDialect : public CScriptDialect
{
private:
    const char* tag() const;
public:
    virtual std::string wrap_namespace(const std::string& src, LPCSTR caNameSpaceName) const;
    virtual std::string wrap_body(const std::string& src, LPCSTR caNameSpaceName) const;
    virtual std::string unlocalize(Unlocalizer& unlocalizer, const std::string& src, LPCSTR caNameSpaceName) const;
};
