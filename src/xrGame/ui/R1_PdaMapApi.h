#pragma once

class CUIWindow;
class CUIMapWnd;
struct lua_State;

// Generic map capabilities. Tools, UI and save data remain in the addon.
namespace R1PdaMap
{
    class Store;
    Store* create(CUIMapWnd& map);
    void destroy(Store*& store);
    CUIWindow* create_canvas(Store& store);
    bool input(Store& store, int action, int dik = -1);
    void visibility(Store& store, bool shown);
    void reset(Store& store);
    void script_register(lua_State* L);
}
