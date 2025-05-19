#pragma once

#include "script_dialect.h"

class CLispMacroDialect : public CScriptDialect
{
public:
    virtual bool recognize(const std::string& src, LPCSTR caNameSpaceName) const;
    virtual std::string lift(const std::string& src, LPCSTR caNameSpaceName) const override;
};
