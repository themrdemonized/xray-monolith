#pragma once

#include "script_export_space.h"
#include <string>
#include <map>

// Persistent storage for the Lua environment
class CScriptStorage
{
private:
	std::map<std::string, std::map<std::string, std::string>> m_cache;
public:
	LPCSTR get(LPCSTR name_space, LPCSTR key);
	void set(LPCSTR name_space, LPCSTR key, LPCSTR value);

DECLARE_SCRIPT_REGISTER_FUNCTION
};

CScriptStorage& ScriptStorage();

add_to_type_list(CScriptStorage)
#undef script_type_list
#define script_type_list save_type_list(CScriptStorage)
