#include "stdafx.h"
#include "script_dialects.h"
#include "../xrCore/mezz_stringbuffer.h"
#include "lua_macros.h"

const CScriptDialect* CScriptDialects::recognize(const std::string& src, LPCSTR caNameSpaceName) const {
    if (lua.recognize(src, caNameSpaceName))
        return &lua;
	else if (lisp_macro.recognize(src, caNameSpaceName))
		return &lisp_macro;
	else if (lisp.recognize(src, caNameSpaceName))
		return &lisp;
	return &wua;
}

std::string CScriptDialects::lift(
    std::string caString,
    LPCSTR caScriptName,
    LPCSTR caNameSpaceName,
    Unlocalizers* unlocalizers
) const
{
    const CScriptDialect* dialect = recognize(caString, caNameSpaceName);

    if (caNameSpaceName)
    {
        std::string loweredNameSpaceName;
        loweredNameSpaceName += caNameSpaceName;
        toLowerCase(loweredNameSpaceName);
        if (unlocalizers && unlocalizers->find(loweredNameSpaceName) != unlocalizers->end())
        {
            Msg("found script %s in unlocalizers data", caNameSpaceName);
            // Iterate lines and unlocalize variables
            Unlocalizer& unlocalizer = (*unlocalizers)[loweredNameSpaceName];
            caString = dialect->unlocalize(unlocalizer, caString, caNameSpaceName);
        }
    }

    return dialect->lift(caString, caNameSpaceName);
}
