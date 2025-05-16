#pragma once

#include "script_storage_space.h"
#include "script_space_forward.h"

class CScriptDialect
{
private:
    virtual const char* tag() const = 0;
public:
    size_t tag_length() const;
    bool parse(LPCSTR src) const;
    virtual size_t wrap_ofs() const = 0;
    virtual size_t wrap(LPSTR dest, LPCSTR src, size_t tSize) const = 0;
};
