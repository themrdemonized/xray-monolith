#include "pch_script.h"
#include "UIWindow.h"
#include "UIDialogHolder.h"
#include "UITextureMaster.h"
#include "../GamePersistent.h"
#include "../ScriptXMLInit.h"
#include "../UICursor.h"
#include "ServerList.h"
#include "UI3tButton.h"
#include "UIActorMenu.h"
#include "UIAnimatedStatic.h"
#include "UIButton.h"
#include "UICheckButton.h"
#include "UIComboBox.h"
#include "UICustomEdit.h"
#include "UICustomSpin.h"
#include "UIDialogWnd.h"
#include "UIEditBox.h"
#include "UIFrameLineWnd.h"
#include "UIFrameWindow.h"
#include "UIHint.h"
#include "UIHudStatesWnd.h"
#include "UIListBox.h"
#include "UIListBoxItem.h"
#include "UIListBoxItemMsgChain.h"
#include "UIMainIngameWnd.h"
#include "UIMapInfo.h"
#include "UIMapList.h"
#include "UIMessageBox.h"
#include "UIMessageBoxEx.h"
#include "UIMessagesWindow.h"
#include "UIMMShniaga.h"
#include "UIMotionIcon.h"
#include "UIPdaWnd.h"
#include "UIProgressBar.h"
#include "UIPropertiesBox.h"
#include "UIScriptWnd.h"
#include "UIScrollView.h"
#include "UISpinNum.h"
#include "UISpinText.h"
#include "UIStatic.h"
#include "UITabButton.h"
#include "UITabControl.h"
#include "UITrackBar.h"

CFontManager& mngr()
{
	return UI().Font();
}

// hud font
CGameFont* GetFontSmall()
{
	return mngr().pFontStat;
}

CGameFont* GetFontMedium()
{
	return mngr().pFontMedium;
}

CGameFont* GetFontDI()
{
	return mngr().pFontDI;
}

//шрифты для интерфейса
CGameFont* GetFontGraffiti19Russian()
{
	return mngr().pFontGraffiti19Russian;
}

CGameFont* GetFontGraffiti22Russian()
{
	return mngr().pFontGraffiti22Russian;
}

CGameFont* GetFontLetterica16Russian()
{
	return mngr().pFontLetterica16Russian;
}

CGameFont* GetFontLetterica18Russian()
{
	return mngr().pFontLetterica18Russian;
}

CGameFont* GetFontGraffiti32Russian()
{
	return mngr().pFontGraffiti32Russian;
}

CGameFont* GetFontGraffiti50Russian()
{
	return mngr().pFontGraffiti50Russian;
}

CGameFont* GetFontLetterica25()
{
	return mngr().pFontLetterica25;
}


int GetARGB(u16 a, u16 r, u16 g, u16 b)
{
	return color_argb(a, r, g, b);
}

int ClrGetA(u32 argb)
{
	return color_get_A(argb);
}

int ClrGetR(u32 argb)
{
	return color_get_R(argb);
}

int ClrGetG(u32 argb)
{
	return color_get_G(argb);
}

int ClrGetB(u32 argb)
{
	return color_get_B(argb);
}

int ClrSetA(u32 argb, u16 a)
{
	return subst_alpha(argb, a);
};

int ClrSetR(u32 argb, u16 r)
{
	return subst_red(argb, r);
};

int ClrSetG(u32 argb, u16 g)
{
	return subst_green(argb, g);
};

int ClrSetB(u32 argb, u16 b)
{
	return subst_blue(argb, b);
};

const Fvector2* get_wnd_pos(CUIWindow* w)
{
	return &w->GetWndPos();
}

Fvector2 GetCursorPosition_script()
{
	return GetUICursor().GetCursorPosition();
}

void SetCursorPosition_script(Fvector2& pos)
{
	GetUICursor().SetUICursorPosition(pos);
}

template <typename T>
T* ui_window_cast(CUIWindow* window)
{
	return smart_cast<T*>(window);
}

#define UI_WINDOW_CAST(class_name) &ui_window_cast<class_name>

using namespace luabind;
#pragma optimize("s",on)
void CUIWindow::script_register(lua_State* L)
{
	module(L)
	[
		def("GetARGB", &GetARGB),
		def("ClrGetA", &ClrGetA),
		def("ClrGetR", &ClrGetR),
		def("ClrGetG", &ClrGetG),
		def("ClrGetB", &ClrGetB),
		def("ClrSetA", &ClrSetA),
		def("ClrSetR", &ClrSetR),
		def("ClrSetG", &ClrSetG),
		def("ClrSetB", &ClrSetB),

		def("GetFontSmall", &GetFontSmall),
		def("GetFontMedium", &GetFontMedium),
		def("GetFontDI", &GetFontDI),
		def("GetFontGraffiti19Russian", &GetFontGraffiti19Russian),
		def("GetFontGraffiti22Russian", &GetFontGraffiti22Russian),
		def("GetFontLetterica16Russian", &GetFontLetterica16Russian),
		def("GetFontLetterica18Russian", &GetFontLetterica18Russian),
		def("GetFontGraffiti32Russian", &GetFontGraffiti32Russian),
		def("GetFontGraffiti50Russian", &GetFontGraffiti50Russian),
		def("GetFontLetterica25", &GetFontLetterica25),
		def("GetCursorPosition", &GetCursorPosition_script),
		def("SetCursorPosition", &SetCursorPosition_script),
		def("FitInRect", &fit_in_rect),

		class_<CUIWindow>("CUIWindow")
		.def(constructor<>())
		.def("AttachChild", &CUIWindow::AttachChild, adopt<2>())
		.def("AttachChildKeepOwner", &CUIWindow::AttachChild)
		.def("DetachChild", &CUIWindow::DetachChild)
		.def("FindChild", &CUIWindow::FindChild)
		.def("SetAutoDelete", &CUIWindow::SetAutoDelete)
		.def("IsAutoDelete", &CUIWindow::IsAutoDelete)

		.def("IsCursorOverWindow", &CUIWindow::CursorOverWindow)
		.def("FocusReceiveTime", &CUIWindow::FocusReceiveTime)
		.def("GetAbsoluteRect", &CUIWindow::GetAbsoluteRect)

		.def("SetWndRect", (void (CUIWindow::*)(Frect))&CUIWindow::SetWndRect_script)
		.def("SetWndPos", (void (CUIWindow::*)(Fvector2))&CUIWindow::SetWndPos_script)
		.def("SetWndSize", (void (CUIWindow::*)(Fvector2))&CUIWindow::SetWndSize_script)
		.def("GetWndPos", &get_wnd_pos)
		.def("GetWidth", &CUIWindow::GetWidth)
		.def("GetHeight", &CUIWindow::GetHeight)

		.def("Enable", &CUIWindow::Enable)
		.def("IsEnabled", &CUIWindow::IsEnabled)
		.def("Show", &CUIWindow::Show)
		.def("IsShown", &CUIWindow::IsShown)

		.def("WindowName", &CUIWindow::WindowName_script)
		.def("SetWindowName", &CUIWindow::SetWindowName)
		.def("SetPPMode", &CUIWindow::SetPPMode)
		.def("ResetPPMode", &CUIWindow::ResetPPMode)

		.def("cast_3tButton", UI_WINDOW_CAST(CUI3tButton))
		.def("cast_ActorMenu", UI_WINDOW_CAST(CUIActorMenu))
		.def("cast_Button", UI_WINDOW_CAST(CUIButton))
		.def("cast_CheckButton", UI_WINDOW_CAST(CUICheckButton))
		.def("cast_ComboBox", UI_WINDOW_CAST(CUIComboBox))
		.def("cast_CustomEdit", UI_WINDOW_CAST(CUICustomEdit))
		.def("cast_CustomSpin", UI_WINDOW_CAST(CUICustomSpin))
		.def("cast_DialogWnd", UI_WINDOW_CAST(CUIDialogWnd))
		.def("cast_EditBox", UI_WINDOW_CAST(CUIEditBox))
		.def("cast_FrameLineWnd", UI_WINDOW_CAST(CUIFrameLineWnd))
		.def("cast_FrameWindow", UI_WINDOW_CAST(CUIFrameWindow))
		.def("cast_Hint", UI_WINDOW_CAST(UIHint))
		.def("cast_HudStatesWnd", UI_WINDOW_CAST(CUIHudStatesWnd))
		.def("cast_ListBox", UI_WINDOW_CAST(CUIListBox))
		.def("cast_ListBoxItem", UI_WINDOW_CAST(CUIListBoxItem))
		.def("cast_ListBoxItemMsgChain", UI_WINDOW_CAST(CUIListBoxItemMsgChain))
		.def("cast_MMShniaga", UI_WINDOW_CAST(CUIMMShniaga))
		.def("cast_MainIngameWnd", UI_WINDOW_CAST(CUIMainIngameWnd))
		.def("cast_MapInfo", UI_WINDOW_CAST(CUIMapInfo))
		.def("cast_MapList", UI_WINDOW_CAST(CUIMapList))
		.def("cast_MessageBox", UI_WINDOW_CAST(CUIMessageBox))
		.def("cast_MessageBoxEx", UI_WINDOW_CAST(CUIMessageBoxEx))
		.def("cast_MessagesWindow", UI_WINDOW_CAST(CUIMessagesWindow))
		.def("cast_MotionIcon", UI_WINDOW_CAST(CUIMotionIcon))
		.def("cast_PdaWnd", UI_WINDOW_CAST(CUIPdaWnd))
		.def("cast_ProgressBar", UI_WINDOW_CAST(CUIProgressBar))
		.def("cast_PropertiesBox", UI_WINDOW_CAST(CUIPropertiesBox))
		.def("cast_ScriptWnd", UI_WINDOW_CAST(CUIDialogWndEx))
		.def("cast_ScrollView", UI_WINDOW_CAST(CUIScrollView))
		.def("cast_ServerList", UI_WINDOW_CAST(CServerList))
		.def("cast_SleepStatic", UI_WINDOW_CAST(CUISleepStatic))
		.def("cast_SpinFlt", UI_WINDOW_CAST(CUISpinFlt))
		.def("cast_SpinNum", UI_WINDOW_CAST(CUISpinNum))
		.def("cast_SpinText", UI_WINDOW_CAST(CUISpinText))
		.def("cast_Static", UI_WINDOW_CAST(CUIStatic))
		.def("cast_TabButton", UI_WINDOW_CAST(CUITabButton))
		.def("cast_TabControl", UI_WINDOW_CAST(CUITabControl))
		.def("cast_TextWnd", UI_WINDOW_CAST(CUITextWnd))
		.def("cast_TrackBar", UI_WINDOW_CAST(CUITrackBar))
	];

	module(L)
	[
		class_<CDialogHolder>("CDialogHolder")
		.def("AddDialogToRender", &CDialogHolder::AddDialogToRender)
		.def("RemoveDialogToRender", &CDialogHolder::RemoveDialogToRender),

		class_<CUIDialogWnd, CUIWindow>("CUIDialogWnd")
		.def("ShowDialog", &CUIDialogWnd::ShowDialog)
		.def("HideDialog", &CUIDialogWnd::HideDialog)
		.def("GetHolder", &CUIDialogWnd::GetHolder)
		.def("AllowMovement", &CUIDialogWnd::AllowMovement)
		.def("AllowCursor", &CUIDialogWnd::AllowCursor)
		.def("AllowCenterCursor", &CUIDialogWnd::AllowCenterCursor)
		.def("AllowWorkInPause", &CUIDialogWnd::AllowWorkInPause),

		class_<CUIFrameWindow, CUIWindow>("CUIFrameWindow")
		.def(constructor<>())
		.def("SetWidth", &CUIFrameWindow::SetWidth)
		.def("SetHeight", &CUIFrameWindow::SetHeight)
		.def("SetColor", &CUIFrameWindow::SetTextureColor),

		class_<CUIFrameLineWnd, CUIWindow>("CUIFrameLineWnd")
		.def(constructor<>())
		.def("SetWidth", &CUIFrameLineWnd::SetWidth)
		.def("SetHeight", &CUIFrameLineWnd::SetHeight)
		.def("SetColor", &CUIFrameLineWnd::SetTextureColor),

		class_<UIHint, CUIWindow>("UIHint")
		.def(constructor<>())
		.def("SetWidth", &UIHint::SetWidth)
		.def("SetHeight", &UIHint::SetHeight)
		.def("SetHintText", &UIHint::set_text)
		.def("GetHintText", &UIHint::get_text),

		class_<CUIMMShniaga, CUIWindow>("CUIMMShniaga")
		.enum_("enum_page_id")
		[
			value("epi_main", CUIMMShniaga::epi_main),
			value("epi_new_game", CUIMMShniaga::epi_new_game),
			value("epi_new_network_game", CUIMMShniaga::epi_new_network_game)
		]
		.def("SetVisibleMagnifier", &CUIMMShniaga::SetVisibleMagnifier)
		.def("SetPage", &CUIMMShniaga::SetPage)
		.def("ShowPage", &CUIMMShniaga::ShowPage),


		class_<CUIScrollView, CUIWindow>("CUIScrollView")
		.def(constructor<>())
		.def("AddWindow", &CUIScrollView::AddWindow)
		.def("RemoveWindow", &CUIScrollView::RemoveWindow)
		.def("Clear", &CUIScrollView::Clear)
		.def("ScrollToBegin", &CUIScrollView::ScrollToBegin)
		.def("ScrollToEnd", &CUIScrollView::ScrollToEnd)
		.def("GetMinScrollPos", &CUIScrollView::GetMinScrollPos)
		.def("GetMaxScrollPos", &CUIScrollView::GetMaxScrollPos)
		.def("GetCurrentScrollPos", &CUIScrollView::GetCurrentScrollPos)
		.def("SetFixedScrollBar", &CUIScrollView::SetFixedScrollBar)
		.def("SetScrollPos", &CUIScrollView::SetScrollPos),

		class_<enum_exporter<EUIMessages>>("ui_events")
		.enum_("events")
		[
			// CUIWindow
			value("WINDOW_LBUTTON_DOWN", int(WINDOW_LBUTTON_DOWN)),
			value("WINDOW_RBUTTON_DOWN", int(WINDOW_RBUTTON_DOWN)),
			value("WINDOW_LBUTTON_UP", int(WINDOW_LBUTTON_UP)),
			value("WINDOW_RBUTTON_UP", int(WINDOW_RBUTTON_UP)),
			value("WINDOW_MOUSE_MOVE", int(WINDOW_MOUSE_MOVE)),
			value("WINDOW_MOUSE_WHEEL_UP", int(WINDOW_MOUSE_WHEEL_UP)),
			value("WINDOW_MOUSE_WHEEL_DOWN", int(WINDOW_MOUSE_WHEEL_DOWN)),
			value("WINDOW_LBUTTON_DB_CLICK", int(WINDOW_LBUTTON_DB_CLICK)),
			value("WINDOW_KEY_PRESSED", int(WINDOW_KEY_PRESSED)),
			value("WINDOW_KEY_RELEASED", int(WINDOW_KEY_RELEASED)),
			value("WINDOW_KEYBOARD_CAPTURE_LOST", int(WINDOW_KEYBOARD_CAPTURE_LOST)),


			// CUIButton
			value("BUTTON_CLICKED", int(BUTTON_CLICKED)),
			value("BUTTON_DOWN", int(BUTTON_DOWN)),

			// CUITabControl
			value("TAB_CHANGED", int(TAB_CHANGED)),

			// CUICheckButton
			value("CHECK_BUTTON_SET", int(CHECK_BUTTON_SET)),
			value("CHECK_BUTTON_RESET", int(CHECK_BUTTON_RESET)),

			// CUIRadioButton
			value("RADIOBUTTON_SET", int(RADIOBUTTON_SET)),

			// CUIScrollBox
			value("SCROLLBOX_MOVE", int(SCROLLBOX_MOVE)),

			// CUIScrollBar
			value("SCROLLBAR_VSCROLL", int(SCROLLBAR_VSCROLL)),
			value("SCROLLBAR_HSCROLL", int(SCROLLBAR_HSCROLL)),

			// CUIListWnd
			value("LIST_ITEM_CLICKED", int(LIST_ITEM_CLICKED)),
			value("LIST_ITEM_SELECT", int(LIST_ITEM_SELECT)),

			// UIPropertiesBox
			value("PROPERTY_CLICKED", int(PROPERTY_CLICKED)),

			// CUIMessageBox
			value("MESSAGE_BOX_OK_CLICKED", int(MESSAGE_BOX_OK_CLICKED)),
			value("MESSAGE_BOX_YES_CLICKED", int(MESSAGE_BOX_YES_CLICKED)),
			value("MESSAGE_BOX_NO_CLICKED", int(MESSAGE_BOX_NO_CLICKED)),
			value("MESSAGE_BOX_CANCEL_CLICKED", int(MESSAGE_BOX_CANCEL_CLICKED)),
			value("MESSAGE_BOX_COPY_CLICKED", int(MESSAGE_BOX_COPY_CLICKED)),
			value("MESSAGE_BOX_QUIT_GAME_CLICKED", int(MESSAGE_BOX_QUIT_GAME_CLICKED)),
			value("MESSAGE_BOX_QUIT_WIN_CLICKED", int(MESSAGE_BOX_QUIT_WIN_CLICKED)),

			value("EDIT_TEXT_COMMIT", int(EDIT_TEXT_COMMIT)),
			// CMainMenu
			value("MAIN_MENU_RELOADED", int(MAIN_MENU_RELOADED))
		]
	];
}

#undef UI_WINDOW_CAST
