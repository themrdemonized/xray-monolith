#pragma once

#include "script_storage_space.h"
#include "script_space_forward.h"
#include "script_storage.h"

static std::string parse_namespace(std::string src)
{
	std::string lsrc;
	std::string dest;
	while (true)
	{
		int sep = src.find(".");
		if (sep > -1)
		{
			std::string cur = src.substr(0, sep);
			dest += lsrc + cur + " = " + lsrc + cur + " or {}\n";
			lsrc += cur + ".";
			src.erase(0, sep + 1);
			continue;
		}

		dest += lsrc + src + " = ";
		return dest;
	}
}

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
