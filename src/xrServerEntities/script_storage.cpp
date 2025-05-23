#include "stdafx.h"
#include "script_storage.h"

LPCSTR CScriptStorage::get(LPCSTR name_space, LPCSTR key)
{
	auto cache = &m_cache[name_space];
	if (cache->find(key) == cache->end())
		return NULL;

	return cache->at(std::string(key)).c_str();
}

void CScriptStorage::set(LPCSTR name_space, LPCSTR key, LPCSTR val)
{
	auto cache = &m_cache[name_space];
	if (key && val)
		cache->insert(std::pair<std::string, std::string>(std::string(key), std::string(val)));
}

CScriptStorage g_script_storage;
CScriptStorage& ScriptStorage()
{
	return g_script_storage;
}