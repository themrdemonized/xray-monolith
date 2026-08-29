#include "pch_script.h"
#include "UIWindow.h"
#include "UIDialogHolder.h"
#include "UITextureMaster.h"
#include "../GamePersistent.h"
#include "../ScriptXMLInit.h"
#include "../UICursor.h"

// Sub-classes of CUIWindow exported to the script engine (see UI_WINDOW_SUBCLASSES)
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

// -----------------------------------------------------------------------------
// Every sub-class of CUIWindow exported to the script engine gets a
// CUIWindow:cast_<name>() method, so that a window obtained as a plain
// CUIWindow (through FindChild for instance) can be used as what it really is.
// The cast returns nil when the window is not of that type.
// To expose one more sub-class, add it to the list below and include its header.
// -----------------------------------------------------------------------------
#define UI_WINDOW_SUBCLASSES(op) \
	op(3tButton,            CUI3tButton) \
	op(ActorMenu,           CUIActorMenu) \
	op(Button,              CUIButton) \
	op(CheckButton,         CUICheckButton) \
	op(ComboBox,            CUIComboBox) \
	op(CustomEdit,          CUICustomEdit) \
	op(CustomSpin,          CUICustomSpin) \
	op(DialogWnd,           CUIDialogWnd) \
	op(EditBox,             CUIEditBox) \
	op(FrameLineWnd,        CUIFrameLineWnd) \
	op(FrameWindow,         CUIFrameWindow) \
	op(Hint,                UIHint) \
	op(HudStatesWnd,        CUIHudStatesWnd) \
	op(ListBox,             CUIListBox) \
	op(ListBoxItem,         CUIListBoxItem) \
	op(ListBoxItemMsgChain, CUIListBoxItemMsgChain) \
	op(MainIngameWnd,       CUIMainIngameWnd) \
	op(MapInfo,             CUIMapInfo) \
	op(MapList,             CUIMapList) \
	op(MessageBox,          CUIMessageBox) \
	op(MessageBoxEx,        CUIMessageBoxEx) \
	op(MessagesWindow,      CUIMessagesWindow) \
	op(MMShniaga,           CUIMMShniaga) \
	op(MotionIcon,          CUIMotionIcon) \
	op(PdaWnd,              CUIPdaWnd) \
	op(ProgressBar,         CUIProgressBar) \
	op(PropertiesBox,       CUIPropertiesBox) \
	op(ScriptWnd,           CUIDialogWndEx) \
	op(ScrollView,          CUIScrollView) \
	op(ServerList,          CServerList) \
	op(SleepStatic,         CUISleepStatic) \
	op(SpinFlt,             CUISpinFlt) \
	op(SpinNum,             CUISpinNum) \
	op(SpinText,            CUISpinText) \
	op(Static,              CUIStatic) \
	op(TabButton,           CUITabButton) \
	op(TabControl,          CUITabControl) \
	op(TextWnd,             CUITextWnd) \
	op(TrackBar,            CUITrackBar)

namespace
{
#define UI_WINDOW_DEFINE_CAST(name, class_name) \
	class_name* cast_##name(CUIWindow* window) \
	{ \
		return smart_cast<class_name*>(window); \
	}

	UI_WINDOW_SUBCLASSES(UI_WINDOW_DEFINE_CAST)

#undef UI_WINDOW_DEFINE_CAST
}

#define UI_WINDOW_EXPORT_CAST(name, class_name) .def("cast_" #name, &cast_##name)

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

        UI_WINDOW_SUBCLASSES(UI_WINDOW_EXPORT_CAST)
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

#undef UI_WINDOW_EXPORT_CAST
#undef UI_WINDOW_SUBCLASSES
