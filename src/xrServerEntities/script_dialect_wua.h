#pragma once

#include "script_dialect_wua_g.h"

class CWuaDialect : public CWuaGDialect
{
public:
    virtual bool recognize(const std::string& src, LPCSTR caNameSpaceName) const override;
    virtual std::string lift(const std::string& src, LPCSTR caNameSpaceName) const override;
};
