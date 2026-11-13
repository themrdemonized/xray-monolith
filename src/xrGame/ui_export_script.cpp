#include "pch_script.h"

#include "script_ui_registrator.h"
#include "MainMenu.h"

#include "UIGameCustom.h"
#include "UI/UIScriptWnd.h"
#include "UI/UIButton.h"
#include "UI/UIProgressBar.h"
#include "UI/UIEditBox.h"
#include "UI/UIMessageBox.h"
#include "UI/UIPropertiesBox.h"
#include "UI/UITabControl.h"
#include "UI/UIComboBox.h"
#include "ui/UIOptionsManagerScript.h"
#include "ui/UIMapInfo.h"
#include "ui/UIMapWnd.h"
#include "ui/UIMap.h"
#include <cmath>
#include "ScriptXmlInit.h"
#include "ui/UIActorMenu.h"

#include "login_manager.h"
#include "account_manager.h"
#include "profile_store.h"

using namespace luabind;

CMainMenu* MainMenu();

extern CUIMapWnd* GetMapWnd();

static u32 r1_pda_map_version()
{
    return 2;
}

static CUIWindow* r1_pda_map_window()
{
    CUIMapWnd* window = GetMapWnd();
    return window && window->GlobalMap() ? window->MapFrame() : nullptr;
}

// Read-only live map geometry; addons own their UI, drawing data and input.
static luabind::object r1_pda_map_info(lua_State* L, LPCSTR name)
{
    luabind::object result = luabind::newtable(L);
    result["ready"] = false;
    CUIMapWnd* window = GetMapWnd();
    if (!name || !window || !window->GlobalMap() || !window->MapFrame())
        return result;

    const bool global = xr_strcmp(name, "global_map") == 0;
    CUICustomMap* map = global ? window->GlobalMap() : nullptr;
    if (!global)
    {
        const auto& maps = window->GameMaps();
        const auto it = maps.find(shared_str(name));
        if (it != maps.end())
            map = it->second;
    }
    if (!map || map->Heading())
        return result;

    const float zoom = map->GetCurrentZoom().x;
    const float aspect = UI().get_current_kx();
    if (!std::isfinite(zoom) || zoom <= EPS_L || !std::isfinite(aspect) || aspect <= EPS_L)
        return result;
    Frect rect;
    map->GetAbsoluteRect(rect);
    const Frect& bound = map->BoundRect();
    result["ready"] = true;
    result["global"] = global;
    result["shown"] = window->IsShown() && map->IsShown();
    result["locked"] = map->Locked();
    result["x"] = rect.x1;
    result["y"] = rect.y1;
    result["width"] = rect.width();
    result["height"] = rect.height();
    result["bound_x1"] = bound.x1;
    result["bound_y1"] = bound.y1;
    result["bound_x2"] = bound.x2;
    result["bound_y2"] = bound.y2;
    result["zoom"] = zoom;
    result["aspect"] = aspect;
    return result;
}

#pragma optimize("s",on)
void UIRegistrator::script_register(lua_State* L)
{
	CUIWindow::script_register(L);
	CUIStatic::script_register(L);
	CUIButton::script_register(L);
	CUIProgressBar::script_register(L);
	CUIComboBox::script_register(L);
	CUIEditBox::script_register(L);
	CUITabControl::script_register(L);
	CUIMessageBox::script_register(L);
	CUIListBox::script_register(L);
	CUIDialogWndEx::script_register(L);
	CUIPropertiesBox::script_register(L);
	CUIOptionsManagerScript::script_register(L);
	CUIMapInfo::script_register(L);
	CScriptXmlInit::script_register(L);
	CUIGameCustom::script_register(L);
	CUIActorMenu::script_register(L);

    module(L, "pda_map")
    [
        def("version", &r1_pda_map_version),
        def("window", &r1_pda_map_window),
        def("info", &r1_pda_map_info, raw<1>())
    ];

	module(L)
	[

		class_<CGameFont>("CGameFont")
		.enum_("EAligment")
		[
			value("alLeft", int(CGameFont::alLeft)),
			value("alRight", int(CGameFont::alRight)),
			value("alCenter", int(CGameFont::alCenter))
		],


		class_<Patch_Dawnload_Progress>("Patch_Dawnload_Progress")
		.def("GetInProgress", &Patch_Dawnload_Progress::GetInProgress)
		.def("GetStatus", &Patch_Dawnload_Progress::GetStatus)
		.def("GetFlieName", &Patch_Dawnload_Progress::GetFlieName)
		.def("GetProgress", &Patch_Dawnload_Progress::GetProgress),

		class_<CMainMenu>("CMainMenu")
		.def("GetPatchProgress", &CMainMenu::GetPatchProgress)
		.def("CancelDownload", &CMainMenu::CancelDownload)
		.def("ValidateCDKey", &CMainMenu::ValidateCDKey)
		.def("GetGSVer", &CMainMenu::GetGSVer)
		.def("GetCDKey", &CMainMenu::GetCDKeyFromRegistry)
		.def("GetPlayerName", &CMainMenu::GetPlayerName)
		.def("GetDemoInfo", &CMainMenu::GetDemoInfo)
		//.def("GetLoginMngr",			&CMainMenu::GetLoginMngr)
		//.def("GetAccountMngr",			&CMainMenu::GetAccountMngr)
		//.def("GetProfileStore",			&CMainMenu::GetProfileStore)
	];

	module(L, "main_menu")
	[
		def("get_main_menu", &MainMenu)
	];
}
