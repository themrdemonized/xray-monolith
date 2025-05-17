#include "stdafx.h"
#include "script_dialects.h"

const CScriptDialect* CScriptDialects::parse(LPCSTR src) const {
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