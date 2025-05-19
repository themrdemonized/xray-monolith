#pragma once

#include <string>

typedef std::set<std::string> Unlocalizer;
typedef xr_unordered_map<std::string, Unlocalizer> Unlocalizers;

class CScriptDialect
{
public:
    virtual bool recognize(const std::string& src, LPCSTR caNameSpaceName) const = 0;
    virtual std::string unlocalize(Unlocalizer& unlocalizer, const std::string& src, LPCSTR caNameSpaceName) const;
    virtual std::string lift(const std::string& src, LPCSTR caNameSpaceName) const;
};
