#pragma once

#include "script_dialect.h"

class CFennelDialect : public CScriptDialect
{
private:
    const char* tag() const;
public:
    size_t wrap_ofs() const;
    size_t wrap(LPSTR dest, LPCSTR src, size_t tSize) const;
};
