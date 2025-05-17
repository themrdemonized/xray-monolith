#include "stdafx.h"
#include "script_dialects.h"
#include "../xrCore/mezz_stringbuffer.h"

const CScriptDialect* CScriptDialects::parse(const std::string& src) const {
	if (lua.parse(src)) {
		return &lua;
	}
	else if (lisp_macro.parse(src)) {
		return &lisp_macro;
	}
	else if (lisp.parse(src)) {
		return &lisp;
	}
	return NULL;
}

std::string CScriptDialects::wrap_buffer(
    std::string caString,
    LPCSTR caScriptName,
    LPCSTR caNameSpaceName,
    Unlocalizers* unlocalizers
) const
{
    const CScriptDialect* dialect = parse(caString);
    size_t lang_tag_len = 0;
    if (dialect)
        lang_tag_len = dialect->tag_length();
    else
        dialect = &dialects.lua;

    if (lang_tag_len > 0)
        caString.erase(0, lang_tag_len);

    std::string loweredNameSpaceName;
    if (caNameSpaceName)
    {
        loweredNameSpaceName += caNameSpaceName;
        toLowerCase(loweredNameSpaceName);
    }

    if (unlocalizers && unlocalizers->find(loweredNameSpaceName) != unlocalizers->end())
    {
        Msg("found script %s in unlocalizers data", caNameSpaceName);
        // Iterate lines and unlocalize variables
        Unlocalizer& unlocalizer = (*unlocalizers)[loweredNameSpaceName];
        caString = dialect->unlocalize(unlocalizer, caString, caNameSpaceName);
    }

    caString = dialect->wrap_body(caString, caNameSpaceName);

    if (caNameSpaceName && xr_strcmp("_G", caNameSpaceName))
    {
        caString = dialect->wrap_namespace(caString, caNameSpaceName);
    }

    return caString;
}
