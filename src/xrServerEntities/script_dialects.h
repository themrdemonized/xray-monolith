#pragma once

#include "stdafx.h"
#include "script_dialect.h"
#include "script_dialect_lua.h"
#include "script_dialect_fennel.h"

struct CScriptDialects {
	CLuaDialect lua;
	CFennelDialect fennel;

	const CScriptDialect* parse(LPCSTR src) const {
		if (lua.parse(src)) {
			return &lua;
		}
		else if (fennel.parse(src)) {
			return &fennel;
		}
		return NULL;
	}
};

static CScriptDialects dialects;
static const CScriptDialects& ScriptDialects()
{
	return dialects;
}