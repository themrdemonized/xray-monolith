#pragma once

#include "stdafx.h"
#include "script_dialect.h"
#include "script_dialect_lua.h"
#include "script_dialect_lisp.h"
#include "script_dialect_lisp_macro.h"

struct CScriptDialects {
	CLuaDialect lua;
	CLispDialect lisp;
	CLispMacroDialect lisp_macro;

	const CScriptDialect* parse(LPCSTR src) const;
};

static CScriptDialects dialects;
static const CScriptDialects& ScriptDialects()
{
	return dialects;
}