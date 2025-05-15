#include <cstdio>
#include <windows.h>
#include <string>

#include "lfennel.h"
#include "../../xrEngine/resource.h"

HRSRC hResource = NULL;
HGLOBAL hMemory = NULL;
std::size_t size = 0;
LPVOID data = NULL;

int luaopen_fennel(lua_State *L)
{
    hResource = FindResource(NULL, MAKEINTRESOURCE(IDR_FENNEL), "LUA");
    if (hResource == NULL)
    {
        DWORD e = GetLastError();
        throw e;
    }

    hMemory = LoadResource(NULL, hResource);
    if (hMemory == NULL)
    {
        DWORD e = GetLastError();
        throw e;
    }

    size = SizeofResource(NULL, hResource);
    data = LockResource(hMemory);

    const char* string = reinterpret_cast<const char*>(data);

    luaL_findtable(L, LUA_REGISTRYINDEX, "_PRELOAD", 16);
    lua_pushstring(L, "fennel");
    luaL_loadbuffer(L, string, size, "Fennel library");
    lua_settable(L, -3);

    return 0;
}

