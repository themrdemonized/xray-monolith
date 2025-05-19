#pragma once

#include "script_dialect.h"

class CWuaGDialect : public CScriptDialect
{
public:
    virtual bool recognize(const std::string& src, LPCSTR caNameSpaceName) const;
    virtual std::string unlocalize(Unlocalizer& unlocalizer, const std::string& src, LPCSTR caNameSpaceName) const override;
    virtual std::string lift(const std::string& src, LPCSTR caNameSpaceName) const override;
};
