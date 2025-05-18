#pragma once

#include <string>

typedef std::set<std::string> Unlocalizer;
typedef xr_unordered_map<std::string, Unlocalizer> Unlocalizers;

class CScriptDialect
{
private:
    virtual const char* tag() const = 0;
public:
    size_t tag_length() const;
    bool parse(const std::string& src) const;

    virtual std::string wrap_namespace(const std::string& src, LPCSTR caNameSpaceName) const;
    virtual std::string wrap_body(const std::string& src, LPCSTR caNameSpaceName) const;
    virtual std::string unlocalize(Unlocalizer& unlocalizer, const std::string& src, LPCSTR caNameSpaceName) const;
};
