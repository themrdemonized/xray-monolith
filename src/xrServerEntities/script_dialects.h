#pragma once

#include "stdafx.h"
#include "script_storage.h"
#include "script_dialect.h"
#include "script_dialect_wua.h"
#include "script_dialect_lua.h"
#include "script_dialect_lisp.h"
#include "script_dialect_lisp_macro.h"

struct CScriptDialects {
	CWuaGDialect wua_g;
	CWuaDialect wua;
	CLuaDialect lua;
	CLispDialect lisp;
	CLispMacroDialect lisp_macro;

	const CScriptDialect* recognize(
		const std::string& src,
		LPCSTR caNameSpaceName = 0
	) const;
	std::string lift(
		std::string caString,
		LPCSTR caScriptName,
		LPCSTR caNameSpaceName = 0,
		Unlocalizers* unlocalizers = 0
	) const;
};

static CScriptDialects dialects;
static const CScriptDialects& ScriptDialects()
{
	return dialects;
}