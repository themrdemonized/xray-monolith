#include "pch_script.h"
#include "script_wallmarks_manager.h"

using namespace luabind;

ScriptWallmarksManager* GetManager()
{
	return &g_pGamePersistent->GetWallmarksManager();
}

#pragma optimize("s",on)
void CScriptWallmarksManager::script_register(lua_State *L)
{
	module(L)
	[
		class_<ScriptWallmarksManager>("ScriptWallmarksManager")
		.def(constructor<>())
		.def("place", (void (ScriptWallmarksManager::*)(Fvector, Fvector, float, float, LPCSTR, CScriptGameObject*, float))(&ScriptWallmarksManager::PlaceWallmark))
		.def("place", (void (ScriptWallmarksManager::*)(Fvector, Fvector, float, float, LPCSTR, CScriptGameObject*, float, bool))(&ScriptWallmarksManager::PlaceWallmark))
		.def("place_skeleton", &ScriptWallmarksManager::PlaceSkeletonWallmark),

		def("wallmarks_manager", &GetManager)
	];
}