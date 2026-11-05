#include "pch_script.h"
#include "script_imgui.h"
#include "script_imgui_inline.h"
#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"

using namespace luabind;
#pragma optimize("s",on)
void ScriptImGui::script_register(::lua_State* L)
{
	module(L, "ImGui")
	[
		def("GetVersion", &ImGui_GetVersion),

		def("Begin", &ImGui::Begin, out_value<2>()),
		def("End", &ImGui::End),

		def("IsWindowAppearing", &ImGui::IsWindowAppearing),
		def("IsWindowCollapsed", &ImGui::IsWindowCollapsed),
		def("IsWindowFocused", &ImGui::IsWindowFocused),
		def("IsWindowHovered", &ImGui::IsWindowHovered),

		def("GetWindowDpiScale", &ImGui::GetWindowDpiScale),
		def("GetWindowPos", &ImGui_GetWindowPos),
		def("GetWindowSize", &ImGui_GetWindowSize),
		def("GetWindowWidth", &ImGui::GetWindowWidth),
		def("GetWindowHeight", &ImGui::GetWindowHeight),

		def("SetNextWindowPos", (void (*)(Fvector2&)) &ImGui_SetNextWindowPos),
		def("SetNextWindowPos", (void (*)(Fvector2&, ImGuiCond)) &ImGui_SetNextWindowPos),
		def("SetNextWindowPos", (void (*)(Fvector2&, ImGuiCond, Fvector2&)) &ImGui_SetNextWindowPos),
		def("SetNextWindowSize", &ImGui_SetNextWindowSize),
		def("SetNextWindowSizeConstraints", &ImGui_SetNextWindowSizeConstraints),
		def("SetNextWindowContentSize", &ImGui_SetNextWindowContentSize),
		def("SetNextWindowCollapsed", &ImGui::SetNextWindowCollapsed),
		def("SetNextWindowFocus", &ImGui::SetNextWindowFocus),
		def("SetNextWindowScroll", &ImGui_SetNextWindowScroll),
		def("SetNextWindowBgAlpha", &ImGui::SetNextWindowBgAlpha),
		def("SetWindowPos", &ImGui_SetWindowPos),
		def("SetWindowSize", &ImGui_SetWindowSize),
		def("SetWindowCollapsed", (void (*)(bool)) &ImGui_SetWindowCollapsed),
		def("SetWindowCollapsed", (void (*)(bool, ImGuiCond)) &ImGui::SetWindowCollapsed),
		def("SetWindowCollapsed", (void (*)(LPCSTR, bool, ImGuiCond)) &ImGui::SetWindowCollapsed),
		def("SetWindowFocus", (void (*)())&ImGui::SetWindowFocus),
		def("SetWindowFocus", (void (*)(LPCSTR))&ImGui::SetWindowFocus),

		def("GetScrollX", &ImGui::GetScrollX),
		def("GetScrollY", &ImGui::GetScrollY),
		def("SetScrollX", (void (*)(float)) &ImGui::SetScrollX),
		def("SetScrollY", (void (*)(float)) &ImGui::SetScrollY),
		def("GetScrollMaxX", &ImGui::GetScrollMaxX),
		def("GetScrollMaxY", &ImGui::GetScrollMaxY),
		def("SetScrollHereX", &ImGui::SetScrollHereX),
		def("SetScrollHereY", &ImGui::SetScrollHereY),
		def("SetScrollFromPosX", (void (*)(float)) &ImGui_SetScrollFromPosX),
		def("SetScrollFromPosX", (void (*)(float, float)) &ImGui::SetScrollFromPosX),
		def("SetScrollFromPosY", (void (*)(float)) &ImGui_SetScrollFromPosY),
		def("SetScrollFromPosY", (void (*)(float, float)) &ImGui::SetScrollFromPosY),

		def("PushStyleColor", (void (*)(ImGuiCol, u32)) &ImGui::PushStyleColor),
		def("PushStyleColor", &ImGui_PushStyleColor),
		def("PopStyleColor", (void (*)()) &ImGui_PopStyleColor),
		def("PopStyleColor", (void (*)(int)) &ImGui::PopStyleColor),
		def("PushStyleVar", (void (*)(ImGuiStyleVar, float)) &ImGui::PushStyleVar),
		def("PushStyleVar", &ImGui_PushStyleVar),
		def("PushStyleVarX", &ImGui::PushStyleVarX),
		def("PushStyleVarY", &ImGui::PushStyleVarY),
		def("PopStyleVar", (void (*)()) &ImGui_PopStyleVar),
		def("PopStyleVar", (void (*)(int)) &ImGui::PopStyleVar),
		def("GetStyleColor", &ImGui_GetStyleColorVec4),

		def("PushItemFlag", &ImGui::PushItemFlag),
		def("PopItemFlag", &ImGui::PopItemFlag),
		def("PushItemWidth", &ImGui::PushItemWidth),
		def("PopItemWidth", &ImGui::PopItemWidth),
		def("SetNextItemWidth", &ImGui::SetNextItemWidth),
		def("CalcItemWidth", &ImGui::CalcItemWidth),
		def("PushTextWrapPos", &ImGui::PushTextWrapPos),
		def("PopTextWrapPos", &ImGui::PopTextWrapPos),

		def("GetCursorScreenPos", &ImGui_GetCursorScreenPos),
		def("SetCursorScreenPos", &ImGui_SetCursorScreenPos),
		def("GetContentRegionAvail", &ImGui_GetContentRegionAvail),
		def("GetCursorPos", &ImGui_GetCursorPos),
		def("GetCursorPosX", &ImGui::GetCursorPosX),
		def("GetCursorPosY", &ImGui::GetCursorPosY),
		def("SetCursorPos", &ImGui_SetCursorPos),
		def("SetCursorPosX", &ImGui::SetCursorPosX),
		def("SetCursorPosY", &ImGui::SetCursorPosY),
		def("GetCursorStartPos", &ImGui_GetCursorStartPos),

		def("Separator", &ImGui::Separator),
		def("SameLine", (void (*)()) &ImGui_SameLine),
		def("SameLine", (void (*)(float)) &ImGui_SameLine),
		def("SameLine", (void (*)(float, float)) &ImGui::SameLine),
		def("NewLine", &ImGui::NewLine),
		def("Spacing", &ImGui::Spacing),
		def("Dummy", &ImGui_Dummy),
		def("Indent", &ImGui::Indent),
		def("Unindent", &ImGui::Unindent),
		def("BeginGroup", &ImGui::BeginGroup),
		def("EndGroup", &ImGui::EndGroup),
		def("AlignTextToFramePadding", &ImGui::AlignTextToFramePadding),
		def("GetTextLineHeight", &ImGui::GetTextLineHeight),
		def("GetTextLineHeightWithSpacing", &ImGui::GetTextLineHeightWithSpacing),
		def("GetFrameHeight", &ImGui::GetFrameHeight),
		def("GetFrameHeightWithSpacing", &ImGui::GetFrameHeightWithSpacing),

		def("PushID", (void (*)(LPCSTR)) &ImGui::PushID),
		def("PushID", (void (*)(int)) &ImGui::PushID),
		def("PopID", &ImGui::PopID),
		def("GetID", (ImGuiID (*)(LPCSTR)) &ImGui::GetID),
		def("GetID", (ImGuiID (*)(int)) &ImGui::GetID),

		def("Text", &ImGui::TextUnformatted),
		def("TextUnformatted", &ImGui::TextUnformatted),
		def("TextColored", &ImGui_TextColored),
		def("TextDisabled", &ImGui_TextDisabled),
		def("TextWrapped", &ImGui_TextWrapped),
		def("TextLink", &ImGui::TextLink),
		def("TextLinkOpenURL", &ImGui::TextLinkOpenURL),
		def("LabelText", &ImGui_LabelText),
		def("Bullet", &ImGui::Bullet),
		def("BulletText", &ImGui_BulletText),
		def("SeparatorText", &ImGui::SeparatorText),

		def("Button", (bool (*)(LPCSTR)) &ImGui_Button),
		def("Button", (bool (*)(LPCSTR, Fvector2)) &ImGui_Button),
		def("SmallButton", &ImGui::SmallButton),
		def("InvisibleButton", (bool (*)(LPCSTR)) &ImGui_InvisibleButton),
		def("InvisibleButton", (bool (*)(LPCSTR, Fvector2)) &ImGui_InvisibleButton),
		def("InvisibleButton", (bool (*)(LPCSTR, Fvector2, ImGuiButtonFlags)) &ImGui_InvisibleButton),
		def("ArrowButton", &ImGui::ArrowButton),
		def("Checkbox", &ImGui::Checkbox, out_value<2>()),
		def("RadioButton", (bool (*)(LPCSTR, bool)) & ImGui::RadioButton),
		def("ProgressBar", &ImGui_ProgressBar),

		def("BeginCombo", &ImGui::BeginCombo),
		def("EndCombo", &ImGui::EndCombo),

		def("DragFloat", &ImGui::DragFloat, out_value<2>()),
		def("DragFloat2", &ImGui_DragFloat2, out_value<2>()),
		def("DragFloat3", &ImGui_DragFloat3, out_value<2>()),
		def("DragFloat4", &ImGui_DragFloat4, out_value<2>()),
		def("DragFloatRange2", &ImGui::DragFloatRange2, out_value<2>() + out_value<3>()),

		def("SliderFloat", &ImGui::SliderFloat, out_value<2>()),
		def("SliderFloat2", &ImGui_SliderFloat2, out_value<2>()),
		def("SliderFloat3", &ImGui_SliderFloat3, out_value<2>()),
		def("SliderFloat4", &ImGui_SliderFloat4, out_value<2>()),
		def("SliderAngle", &ImGui::SliderAngle, out_value<2>()),

		def("InputText", &ImGui_InputText, out_value<5>()),
		def("InputTextMultiline", &ImGui_InputTextMultiline, out_value<6>()),
		def("InputTextWithHint", &ImGui_InputTextWithHint, out_value<6>()),

		def("InputFloat", &ImGui::InputFloat, out_value<2>()),
		def("InputFloat2", &ImGui_InputFloat2, out_value<2>()),
		def("InputFloat3", &ImGui_InputFloat3, out_value<2>()),
		def("InputFloat4", &ImGui_InputFloat4, out_value<2>()),

		def("ColorEdit3", &ImGui_ColorEdit3, out_value<2>()),
		def("ColorEdit4", &ImGui_ColorEdit4, out_value<2>()),
		def("ColorPicker3", &ImGui_ColorPicker3, out_value<2>()),
		def("ColorPicker4", &ImGui_ColorPicker4, out_value<2>()),
		def("ColorButton", &ImGui_ColorButton, out_value<2>()),

		def("TreeNode", (bool (*)(LPCSTR))& ImGui::TreeNode),
		def("TreeNodeEx", (bool (*)(LPCSTR, ImGuiTreeNodeFlags))& ImGui::TreeNodeEx),
		def("TreePush", (void (*)(LPCSTR))& ImGui::TreePush),
		def("TreePop", &ImGui::TreePop),
		def("GetTreeNodeToLabelSpacing", &ImGui::GetTreeNodeToLabelSpacing),
		def("CollapsingHeader", (bool (*)(LPCSTR))& ImGui_CollapsingHeader),
		def("CollapsingHeader", (bool (*)(LPCSTR, ImGuiTreeNodeFlags))& ImGui::CollapsingHeader),
		def("CollapsingHeader", (bool (*)(LPCSTR, bool*))& ImGui_CollapsingHeader, out_value<2>()),
		def("CollapsingHeader", (bool (*)(LPCSTR, bool*, ImGuiTreeNodeFlags))& ImGui::CollapsingHeader, out_value<2>()),
		def("SetNextItemOpen", &ImGui::SetNextItemOpen),

		def("BeginListBox", &ImGui_BeginListBox),
		def("EndListBox", &ImGui::EndListBox),
		def("Selectable", &ImGui_Selectable),

		def("BeginMenuBar", &ImGui::BeginMenuBar),
		def("EndMenuBar", &ImGui::EndMenuBar),
		def("BeginMenu", (bool(*)(LPCSTR))& ImGui_BeginMenu),
		def("BeginMenu", (bool(*)(LPCSTR, bool))& ImGui::BeginMenu),
		def("EndMenu", &ImGui::EndMenu),
		def("MenuItem", (bool (*)(LPCSTR, LPCSTR, bool*, bool))& ImGui::MenuItem, out_value<3>()),

		def("BeginTooltip", &ImGui::BeginTooltip),
		def("EndTooltip", &ImGui::EndTooltip),
		def("SetTooltip", &ImGui_SetTooltip),
		def("BeginItemTooltip", &ImGui::BeginItemTooltip),
		def("SetItemTooltip", &ImGui_SetItemTooltip),

		def("BeginPopup", &ImGui::BeginPopup),
		def("BeginPopupModal", &ImGui::BeginPopupModal, out_value<2>()),
		def("EndPopup", &ImGui::EndPopup),
		def("OpenPopup", (void (*)(LPCSTR))& ImGui_OpenPopup),
		def("OpenPopup", (void (*)(LPCSTR, ImGuiPopupFlags))& ImGui::OpenPopup),
		def("OpenPopup", (void (*)(ImGuiID))& ImGui_OpenPopup),
		def("OpenPopup", (void (*)(ImGuiID, ImGuiPopupFlags))& ImGui::OpenPopup),
		def("OpenPopupOnItemClick", &ImGui::OpenPopupOnItemClick),
		def("CloseCurrentPopup", &ImGui::CloseCurrentPopup),
		def("BeginPopupContextItem", &ImGui::BeginPopupContextItem),
		def("BeginPopupContextWindow", &ImGui::BeginPopupContextWindow),
		def("BeginPopupContextVoid", &ImGui::BeginPopupContextVoid),
		def("IsPopupOpen", (bool (*)(LPCSTR))&ImGui_IsPopupOpen),
		def("IsPopupOpen", (bool (*)(LPCSTR, ImGuiPopupFlags))&ImGui::IsPopupOpen),

		def("BeginTable", &ImGui_BeginTable),
		def("EndTable", &ImGui::EndTable),
		def("TableNextRow", &ImGui::TableNextRow),
		def("TableNextColumn", &ImGui::TableNextColumn),
		def("TableSetColumnIndex", &ImGui::TableSetColumnIndex),
		def("TableSetupColumn", &ImGui::TableSetupColumn),
		def("TableSetupScrollFreeze", &ImGui::TableSetupScrollFreeze),
		def("TableHeader", &ImGui::TableHeader),
		def("TableHeadersRow", &ImGui::TableHeadersRow),
		def("TableAngledHeadersRow", &ImGui::TableAngledHeadersRow),
		def("TableGetColumnCount", &ImGui::TableGetColumnCount),
		def("TableGetColumnIndex", &ImGui::TableGetColumnIndex),
		def("TableGetRowIndex", &ImGui::TableGetRowIndex),
		def("TableGetColumnName", (LPCSTR (*)(int))&ImGui::TableGetColumnName),
		def("TableGetColumnFlags", &ImGui::TableGetColumnFlags),
		def("TableSetColumnEnabled", &ImGui::TableAngledHeadersRow),
		def("TableGetHoveredColumn", &ImGui::TableGetHoveredColumn),
		def("TableSetBgColor", &ImGui_TableSetBgColor),

		def("BeginTabBar", &ImGui::BeginTabBar),
		def("EndTabBar", &ImGui::EndTabBar),
		def("BeginTabItem", &ImGui::BeginTabItem, out_value<2>()),
		def("EndTabItem", &ImGui::EndTabItem),
		def("TabItemButton", &ImGui::TabItemButton),
		def("SetTabItemClosed", &ImGui::SetTabItemClosed),

		def("BeginDisabled", (void (*)())& ImGui_BeginDisabled),
		def("BeginDisabled", (void (*)(bool))& ImGui::BeginDisabled),
		def("EndDisabled", &ImGui::EndDisabled),

		def("PushClipRect", &ImGui_PushClipRect),
		def("PopClipRect", &ImGui::PopClipRect),

		def("SetItemDefaultFocus", &ImGui::SetItemDefaultFocus),
		def("SetKeyboardFocusHere", &ImGui::SetKeyboardFocusHere),
		def("SetNavCursorVisible", &ImGui::SetNavCursorVisible),
		def("SetNextItemAllowOverlap", &ImGui::SetNextItemAllowOverlap),

		def("IsItemHovered", &ImGui::IsItemHovered),
		def("IsItemActive", &ImGui::IsItemActive),
		def("IsItemFocused", &ImGui::IsItemFocused),
		def("IsItemClicked", &ImGui::IsItemClicked),
		def("IsItemVisible", &ImGui::IsItemVisible),
		def("IsItemEdited", &ImGui::IsItemEdited),
		def("IsItemActivated", &ImGui::IsItemActivated),
		def("IsItemDeactivated", &ImGui::IsItemDeactivated),
		def("IsItemDeactivatedAfterEdit", &ImGui::IsItemDeactivatedAfterEdit),
		def("IsItemToggledOpen", &ImGui::IsItemToggledOpen),
		def("IsAnyItemHovered", &ImGui::IsAnyItemHovered),
		def("IsAnyItemActive", &ImGui::IsAnyItemActive),
		def("IsAnyItemFocused", &ImGui::IsAnyItemFocused),
		def("GetItemID", &ImGui::GetItemID),
		def("GetItemRectMin", &ImGui_GetItemRectMin),
		def("GetItemRectMax", &ImGui_GetItemRectMax),
		def("GetItemRectSize", &ImGui_GetItemRectSize),

		def("IsKeyDown", (bool (*)(ImGuiKey))&ImGui::IsKeyDown),
		def("IsKeyPressed", (bool (*)(ImGuiKey))&ImGui_IsKeyPressed),
		def("IsKeyPressed", (bool (*)(ImGuiKey, bool))&ImGui::IsKeyPressed),
		def("IsKeyReleased", (bool (*)(ImGuiKey))&ImGui::IsKeyReleased),
		def("IsKeyChordPressed", (bool (*)(ImGuiKeyChord))&ImGui::IsKeyChordPressed),
		def("GetKeyPressedAmount", &ImGui::GetKeyPressedAmount),
		def("GetKeyName", &ImGui_GetKeyName),
		def("SetNextFrameWantCaptureKeyboard", &ImGui::SetNextFrameWantCaptureKeyboard),

		def("Shortcut", (bool (*)(ImGuiKeyChord))&ImGui_Shortcut),
		def("Shortcut", (bool (*)(ImGuiKeyChord, ImGuiInputFlags))&ImGui::Shortcut),
		def("SetNextItemShortcut", &ImGui::SetNextItemShortcut),

		def("SetItemKeyOwner", (void (*)(ImGuiKey))&ImGui::SetItemKeyOwner),
		def("SetItemKeyOwner", (void (*)(ImGuiKey, ImGuiInputFlags))&ImGui::SetItemKeyOwner),

		def("CalcTextSize", &ImGui_CalcTextSize),

		def("IsMouseDown", (bool (*)(ImGuiMouseButton))&ImGui::IsMouseDown),
		def("IsMouseClicked", (bool (*)(ImGuiMouseButton, bool))&ImGui::IsMouseClicked),
		def("IsMouseReleased", (bool (*)(ImGuiMouseButton))&ImGui::IsMouseReleased),
		def("IsMouseDoubleClicked", (bool (*)(ImGuiMouseButton))&ImGui::IsMouseDoubleClicked),
		def("IsMouseReleasedWithDelay", &ImGui::IsMouseReleasedWithDelay),
		def("GetMouseClickedCount", &ImGui::GetMouseClickedCount),
		def("IsMouseHoveringRect", (bool (*)(Fvector2, Fvector2))& ImGui_IsMouseHoveringRect),
		def("IsMouseHoveringRect", (bool (*)(Fvector2, Fvector2, bool))& ImGui_IsMouseHoveringRect),
		def("IsMousePosValid", &ImGui_IsMousePosValid),
		def("IsAnyMouseDown", &ImGui::IsAnyMouseDown),
		def("GetMousePos", &ImGui_GetMousePos),
		def("GetMousePosOnOpeningCurrentPopup", &ImGui_GetMousePosOnOpeningCurrentPopup),
		def("IsMouseDragging", &ImGui::IsMouseDragging),
		def("GetMouseDragDelta", &ImGui_GetMouseDragDelta),
		def("ResetMouseDragDelta", &ImGui::ResetMouseDragDelta),
		def("SetNextFrameWantCaptureMouse", &ImGui::SetNextFrameWantCaptureMouse),

		def("PushFont", &ImGui_PushFont),
		def("PopFont", &ImGui::PopFont),
		def("GetFontSize", &ImGui::GetFontSize),

		def("GetClipboardText", &ImGui_GetClipboardText),
		def("SetClipboardText", &ImGui::SetClipboardText)
	],
	
	module(L)
	[
		class_<enum_exporter<ImGuiCond_>>("ImGuiCond")
		.enum_("ImGuiCond")
		[
			value("Always", (int)ImGuiCond_Always),
			value("Once", (int)ImGuiCond_Once),
			value("FirstUseEver", (int)ImGuiCond_FirstUseEver),
			value("Appearing", (int)ImGuiCond_Appearing)
		],

		class_<enum_exporter<ImGuiDir>>("ImGuiDir")
		.enum_("ImGuiDir")
		[
			value("Left", (int)ImGuiDir_Left),
			value("Right", (int)ImGuiDir_Right),
			value("Up", (int)ImGuiDir_Up),
			value("Down", (int)ImGuiDir_Down)
		],

		class_<enum_exporter<ImGuiWindowFlags_>>("ImGuiWindowFlags")
		.enum_("ImGuiWindowFlags")
		[
			value("NoTitleBar", (int)ImGuiWindowFlags_NoTitleBar),
			value("NoResize", (int)ImGuiWindowFlags_NoResize),
			value("NoMove", (int)ImGuiWindowFlags_NoMove),
			value("NoScrollbar", (int)ImGuiWindowFlags_NoScrollbar),
			value("NoScrollWithMouse", (int)ImGuiWindowFlags_NoScrollWithMouse),
			value("NoCollapse", (int)ImGuiWindowFlags_NoCollapse),
			value("AlwaysAutoResize", (int)ImGuiWindowFlags_AlwaysAutoResize),
			value("NoBackground", (int)ImGuiWindowFlags_NoBackground),
			value("NoSavedSettings", (int)ImGuiWindowFlags_NoSavedSettings),
			value("NoMouseInputs", (int)ImGuiWindowFlags_NoMouseInputs),
			value("MenuBar", (int)ImGuiWindowFlags_MenuBar),
			value("HorizontalScrollbar", (int)ImGuiWindowFlags_HorizontalScrollbar),
			value("NoFocusOnAppearing", (int)ImGuiWindowFlags_NoFocusOnAppearing),
			value("NoBringToFrontOnFocus", (int)ImGuiWindowFlags_NoBringToFrontOnFocus),
			value("AlwaysVerticalScrollbar", (int)ImGuiWindowFlags_AlwaysVerticalScrollbar),
			value("AlwaysHorizontalScrollbar", (int)ImGuiWindowFlags_AlwaysHorizontalScrollbar),
			value("NoNavInputs", (int)ImGuiWindowFlags_NoNavInputs),
			value("NoNavFocus", (int)ImGuiWindowFlags_NoNavFocus),
			value("UnsavedDocument", (int)ImGuiWindowFlags_UnsavedDocument),
			value("NoDocking", (int)ImGuiWindowFlags_NoDocking),
			value("NoNav", (int)ImGuiWindowFlags_NoNav),
			value("NoDecoration", (int)ImGuiWindowFlags_NoDecoration),
			value("NoInputs", (int)ImGuiWindowFlags_NoInputs)
		],

		class_<enum_exporter<ImGuiItemFlags_>>("ImGuiItemFlags")
		.enum_("ImGuiItemFlags")
		[
			value("NoTabStop", (int)ImGuiItemFlags_NoTabStop),
			value("NoNav", (int)ImGuiItemFlags_NoNav),
			value("NoNavDefaultFocus", (int)ImGuiItemFlags_NoNavDefaultFocus),
			value("ButtonRepeat", (int)ImGuiItemFlags_ButtonRepeat),
			value("AutoClosePopups", (int)ImGuiItemFlags_AutoClosePopups),
			value("AllowDuplicateId", (int)ImGuiItemFlags_AllowDuplicateId)
		],

		class_<enum_exporter<ImGuiSelectableFlags_>>("ImGuiSelectableFlags")
		.enum_("ImGuiSelectableFlags")
		[
			value("NoAutoClosePopups", (int)ImGuiSelectableFlags_NoAutoClosePopups),
			value("SpanAllColumns", (int)ImGuiSelectableFlags_SpanAllColumns),
			value("AllowDoubleClick", (int)ImGuiSelectableFlags_AllowDoubleClick),
			value("Disabled", (int)ImGuiSelectableFlags_Disabled),
			value("AllowOverlap", (int)ImGuiSelectableFlags_AllowOverlap),
			value("Highlight", (int)ImGuiSelectableFlags_Highlight)
		],

		class_<enum_exporter<ImGuiInputTextFlags_>>("ImGuiInputTextFlags")
		.enum_("ImGuiInputTextFlags")
		[
			value("CharsDecimal", (int)ImGuiInputTextFlags_CharsDecimal),
			value("CharsHexadecimal", (int)ImGuiInputTextFlags_CharsHexadecimal),
			value("CharsScientific", (int)ImGuiInputTextFlags_CharsScientific),
			value("CharsUppercase", (int)ImGuiInputTextFlags_CharsUppercase),
			value("CharsNoBlank", (int)ImGuiInputTextFlags_CharsNoBlank),
			value("AllowTabInput", (int)ImGuiInputTextFlags_AllowTabInput),
			value("EnterReturnsTrue", (int)ImGuiInputTextFlags_EnterReturnsTrue),
			value("EscapeClearsAll", (int)ImGuiInputTextFlags_EscapeClearsAll),
			value("CtrlEnterForNewLine", (int)ImGuiInputTextFlags_CtrlEnterForNewLine),
			value("ReadOnly", (int)ImGuiInputTextFlags_ReadOnly),
			value("Password", (int)ImGuiInputTextFlags_Password),
			value("AlwaysOverwrite", (int)ImGuiInputTextFlags_AlwaysOverwrite),
			value("AutoSelectAll", (int)ImGuiInputTextFlags_AutoSelectAll),
			value("ParseEmptyRefVal", (int)ImGuiInputTextFlags_ParseEmptyRefVal),
			value("DisplayEmptyRefVal", (int)ImGuiInputTextFlags_DisplayEmptyRefVal),
			value("NoHorizontalScroll", (int)ImGuiInputTextFlags_NoHorizontalScroll),
			value("NoUndoRedo", (int)ImGuiInputTextFlags_NoUndoRedo),
			value("ElideLeft", (int)ImGuiInputTextFlags_ElideLeft),
			value("CallbackCompletion", (int)ImGuiInputTextFlags_CallbackCompletion),
			value("CallbackHistory", (int)ImGuiInputTextFlags_CallbackHistory),
			value("CallbackAlways", (int)ImGuiInputTextFlags_CallbackAlways),
			value("CallbackCharFilter", (int)ImGuiInputTextFlags_CallbackCharFilter),
			value("CallbackResize", (int)ImGuiInputTextFlags_CallbackResize),
			value("CallbackEdit", (int)ImGuiInputTextFlags_CallbackEdit)
		],

		class_<enum_exporter<ImGuiTreeNodeFlags_>>("ImGuiTreeNodeFlags")
		.enum_("ImGuiTreeNodeFlags")
		[
			value("Selected", (int)ImGuiTreeNodeFlags_Selected),
			value("Framed", (int)ImGuiTreeNodeFlags_Framed),
			value("AllowOverlap", (int)ImGuiTreeNodeFlags_AllowOverlap),
			value("NoTreePushOnOpen", (int)ImGuiTreeNodeFlags_NoTreePushOnOpen),
			value("NoAutoOpenOnLog", (int)ImGuiTreeNodeFlags_NoAutoOpenOnLog),
			value("DefaultOpen", (int)ImGuiTreeNodeFlags_DefaultOpen),
			value("OpenOnDoubleClick", (int)ImGuiTreeNodeFlags_OpenOnDoubleClick),
			value("OpenOnArrow", (int)ImGuiTreeNodeFlags_OpenOnArrow),
			value("Leaf", (int)ImGuiTreeNodeFlags_Leaf),
			value("Bullet", (int)ImGuiTreeNodeFlags_Bullet),
			value("FramePadding", (int)ImGuiTreeNodeFlags_FramePadding),
			value("SpanAvailWidth", (int)ImGuiTreeNodeFlags_SpanAvailWidth),
			value("SpanFullWidth", (int)ImGuiTreeNodeFlags_SpanFullWidth),
			value("SpanLabelWidth", (int)ImGuiTreeNodeFlags_SpanLabelWidth),
			value("SpanAllColumns", (int)ImGuiTreeNodeFlags_SpanAllColumns),
			value("LabelSpanAllColumns", (int)ImGuiTreeNodeFlags_LabelSpanAllColumns),
			value("NavLeftJumpsBackHere", (int)ImGuiTreeNodeFlags_NavLeftJumpsBackHere),
			value("CollapsingHeader", (int)ImGuiTreeNodeFlags_CollapsingHeader)
		],

		class_<enum_exporter<ImGuiPopupFlags_>>("ImGuiPopupFlags")
		.enum_("ImGuiPopupFlags")
		[
			value("MouseButtonLeft", (int)ImGuiPopupFlags_MouseButtonLeft),
			value("MouseButtonRight", (int)ImGuiPopupFlags_MouseButtonRight),
			value("MouseButtonMiddle", (int)ImGuiPopupFlags_MouseButtonMiddle),
			value("NoReopen", (int)ImGuiPopupFlags_NoReopen),
			value("NoOpenOverExistingPopup", (int)ImGuiPopupFlags_NoOpenOverExistingPopup),
			value("NoOpenOverItems", (int)ImGuiPopupFlags_NoOpenOverItems),
			value("AnyPopupId", (int)ImGuiPopupFlags_AnyPopupId),
			value("AnyPopupLevel", (int)ImGuiPopupFlags_AnyPopupLevel),
			value("AnyPopup", (int)ImGuiPopupFlags_AnyPopup)
		],

		class_<enum_exporter<ImGuiTableRowFlags_>>("ImGuiTableRowFlags")
		.enum_("ImGuiTableRowFlags")
		[
			value("Headers", (int)ImGuiTableRowFlags_Headers)
		],

		class_<enum_exporter<ImGuiTableBgTarget_>>("ImGuiTableBgTarget")
		.enum_("ImGuiTableBgTarget")
		[
			value("RowBg0", (int)ImGuiTableBgTarget_RowBg0),
			value("RowBg1", (int)ImGuiTableBgTarget_RowBg1),
			value("CellBg", (int)ImGuiTableBgTarget_CellBg)
		],

		class_<enum_exporter<ImGuiTableFlags_>>("ImGuiTableFlags")
		.enum_("ImGuiTableFlags")
		[
			value("Resizable", (int)ImGuiTableFlags_Resizable),
			value("Reorderable", (int)ImGuiTableFlags_Reorderable),
			value("Hideable", (int)ImGuiTableFlags_Hideable),
			value("Sortable", (int)ImGuiTableFlags_Sortable),
			value("NoSavedSettings", (int)ImGuiTableFlags_NoSavedSettings),
			value("ContextMenuInBody", (int)ImGuiTableFlags_ContextMenuInBody),
			value("RowBg", (int)ImGuiTableFlags_RowBg),
			value("BordersInnerH", (int)ImGuiTableFlags_BordersInnerH),
			value("BordersOuterH", (int)ImGuiTableFlags_BordersOuterH),
			value("BordersInnerV", (int)ImGuiTableFlags_BordersInnerV),
			value("BordersOuterV", (int)ImGuiTableFlags_BordersOuterV),
			value("BordersH", (int)ImGuiTableFlags_BordersH),
			value("BordersV", (int)ImGuiTableFlags_BordersV),
			value("BordersInner", (int)ImGuiTableFlags_BordersInner),
			value("BordersOuter", (int)ImGuiTableFlags_BordersOuter),
			value("Borders", (int)ImGuiTableFlags_Borders),
			value("NoBordersInBody", (int)ImGuiTableFlags_NoBordersInBody),
			value("NoBordersInBodyUntilResize", (int)ImGuiTableFlags_NoBordersInBodyUntilResize),
			value("SizingFixedFit", (int)ImGuiTableFlags_SizingFixedFit),
			value("SizingFixedSame", (int)ImGuiTableFlags_SizingFixedSame),
			value("SizingStretchProp", (int)ImGuiTableFlags_SizingStretchProp),
			value("SizingStretchSame", (int)ImGuiTableFlags_SizingStretchSame),
			value("NoHostExtendX", (int)ImGuiTableFlags_NoHostExtendX),
			value("NoHostExtendY", (int)ImGuiTableFlags_NoHostExtendY),
			value("NoKeepColumnsVisible", (int)ImGuiTableFlags_NoKeepColumnsVisible),
			value("PreciseWidths", (int)ImGuiTableFlags_PreciseWidths),
			value("NoClip", (int)ImGuiTableFlags_NoClip),
			value("PadOuterX", (int)ImGuiTableFlags_PadOuterX),
			value("NoPadOuterX", (int)ImGuiTableFlags_NoPadOuterX),
			value("NoPadInnerX", (int)ImGuiTableFlags_NoPadInnerX),
			value("ScrollX", (int)ImGuiTableFlags_ScrollX),
			value("ScrollY", (int)ImGuiTableFlags_ScrollY),
			value("SortMulti", (int)ImGuiTableFlags_SortMulti),
			value("SortTristate", (int)ImGuiTableFlags_SortTristate),
			value("HighlightHoveredColumn", (int)ImGuiTableFlags_HighlightHoveredColumn)
		],

		class_<enum_exporter<ImGuiTableColumnFlags_>>("ImGuiTableColumnFlags")
		.enum_("ImGuiTableColumnFlags")
		[
			value("Disabled", (int)ImGuiTableColumnFlags_Disabled),
			value("DefaultHide", (int)ImGuiTableColumnFlags_DefaultHide),
			value("DefaultSort", (int)ImGuiTableColumnFlags_DefaultSort),
			value("WidthStretch", (int)ImGuiTableColumnFlags_WidthStretch),
			value("WidthFixed", (int)ImGuiTableColumnFlags_WidthFixed),
			value("NoResize", (int)ImGuiTableColumnFlags_NoResize),
			value("NoReorder", (int)ImGuiTableColumnFlags_NoReorder),
			value("NoHide", (int)ImGuiTableColumnFlags_NoHide),
			value("NoClip", (int)ImGuiTableColumnFlags_NoClip),
			value("NoSort", (int)ImGuiTableColumnFlags_NoSort),
			value("NoSortAscending", (int)ImGuiTableColumnFlags_NoSortAscending),
			value("NoSortDescending", (int)ImGuiTableColumnFlags_NoSortDescending),
			value("NoHeaderLabel", (int)ImGuiTableColumnFlags_NoHeaderLabel),
			value("NoHeaderWidth", (int)ImGuiTableColumnFlags_NoHeaderWidth),
			value("PreferSortAscending", (int)ImGuiTableColumnFlags_PreferSortAscending),
			value("PreferSortDescendin", (int)ImGuiTableColumnFlags_PreferSortDescending),
			value("IndentEnable", (int)ImGuiTableColumnFlags_IndentEnable),
			value("IndentDisable", (int)ImGuiTableColumnFlags_IndentDisable),
			value("AngledHeader", (int)ImGuiTableColumnFlags_AngledHeader),
			value("IsEnabled", (int)ImGuiTableColumnFlags_IsEnabled),
			value("IsVisible", (int)ImGuiTableColumnFlags_IsVisible),
			value("IsSorted", (int)ImGuiTableColumnFlags_IsSorted),
			value("IsHovered", (int)ImGuiTableColumnFlags_IsHovered)
		],

		class_<enum_exporter<ImGuiTabBarFlags_>>("ImGuiTabBarFlags")
		.enum_("ImGuiTabBarFlags")
		[
			value("Disabled", (int)ImGuiTabBarFlags_Reorderable),
			value("DefaultHide", (int)ImGuiTabBarFlags_AutoSelectNewTabs),
			value("DefaultSort", (int)ImGuiTabBarFlags_TabListPopupButton),
			value("WidthStretch", (int)ImGuiTabBarFlags_NoCloseWithMiddleMouseButton),
			value("WidthFixed", (int)ImGuiTabBarFlags_NoTabListScrollingButtons),
			value("NoResize", (int)ImGuiTabBarFlags_NoTooltip),
			value("NoReorder", (int)ImGuiTabBarFlags_DrawSelectedOverline),
			value("NoHide", (int)ImGuiTabBarFlags_FittingPolicyResizeDown),
			value("NoClip", (int)ImGuiTabBarFlags_FittingPolicyScroll)
		],

		class_<enum_exporter<ImGuiTabItemFlags_>>("ImGuiTabItemFlags")
		.enum_("ImGuiTabItemFlags")
		[
			value("UnsavedDocument", (int)ImGuiTabItemFlags_UnsavedDocument),
			value("SetSelected", (int)ImGuiTabItemFlags_SetSelected),
			value("NoCloseWithMiddleMouseButton", (int)ImGuiTabItemFlags_NoCloseWithMiddleMouseButton),
			value("NoPushId", (int)ImGuiTabItemFlags_NoPushId),
			value("NoTooltip", (int)ImGuiTabItemFlags_NoTooltip),
			value("NoReorder", (int)ImGuiTabItemFlags_NoReorder),
			value("Leading", (int)ImGuiTabItemFlags_Leading),
			value("Trailing", (int)ImGuiTabItemFlags_Trailing),
			value("NoAssumedClosure", (int)ImGuiTabItemFlags_NoAssumedClosure)
		],

		class_<enum_exporter<ImGuiHoveredFlags_>>("ImGuiHoveredFlags")
		.enum_("ImGuiHoveredFlags")
		[
			value("ChildWindows", (int)ImGuiHoveredFlags_ChildWindows),
			value("RootWindow", (int)ImGuiHoveredFlags_RootWindow),
			value("AnyWindow", (int)ImGuiHoveredFlags_AnyWindow),
			value("NoPopupHierarchy", (int)ImGuiHoveredFlags_NoPopupHierarchy),
			value("DockHierarchy", (int)ImGuiHoveredFlags_DockHierarchy),
			value("AllowWhenBlockedByPopup", (int)ImGuiHoveredFlags_AllowWhenBlockedByPopup),
			value("AllowWhenBlockedByActiveItem", (int)ImGuiHoveredFlags_AllowWhenBlockedByActiveItem),
			value("AllowWhenOverlappedByItem", (int)ImGuiHoveredFlags_AllowWhenOverlappedByItem),
			value("AllowWhenOverlappedByWindow", (int)ImGuiHoveredFlags_AllowWhenOverlappedByWindow),
			value("AllowWhenDisabled", (int)ImGuiHoveredFlags_AllowWhenDisabled),
			value("NoNavOverride", (int)ImGuiHoveredFlags_NoNavOverride),
			value("AllowWhenOverlapped", (int)ImGuiHoveredFlags_AllowWhenOverlapped),
			value("RectOnly", (int)ImGuiHoveredFlags_RectOnly),
			value("RootAndChildWindows", (int)ImGuiHoveredFlags_RootAndChildWindows),
			value("ForTooltip", (int)ImGuiHoveredFlags_ForTooltip),
			value("Stationary", (int)ImGuiHoveredFlags_Stationary),
			value("_DelayNone", (int)ImGuiHoveredFlags_DelayNone),
			value("DelayShort", (int)ImGuiHoveredFlags_DelayShort),
			value("DelayNormal", (int)ImGuiHoveredFlags_DelayNormal),
			value("NoSharedDelay", (int)ImGuiHoveredFlags_NoSharedDelay)
		],

		class_<enum_exporter<ImGuiCol_>>("ImGuiCol")
		.enum_("ImGuiCol")
		[
			value("Text", (int)ImGuiCol_Text),
			value("TextDisabled", (int)ImGuiCol_TextDisabled),
			value("WindowBg", (int)ImGuiCol_WindowBg),
			value("ChildBg", (int)ImGuiCol_ChildBg),
			value("PopupBg", (int)ImGuiCol_PopupBg),
			value("Border", (int)ImGuiCol_Border),
			value("BorderShadow", (int)ImGuiCol_BorderShadow),
			value("FrameBg", (int)ImGuiCol_FrameBg),
			value("FrameBgHovered", (int)ImGuiCol_FrameBgHovered),
			value("FrameBgActive", (int)ImGuiCol_FrameBgActive),
			value("TitleBg", (int)ImGuiCol_TitleBg),
			value("TitleBgActive", (int)ImGuiCol_TitleBgActive),
			value("TitleBgCollapsed", (int)ImGuiCol_TitleBgCollapsed),
			value("MenuBarBg", (int)ImGuiCol_MenuBarBg),
			value("ScrollbarBg", (int)ImGuiCol_ScrollbarBg),
			value("ScrollbarGrab", (int)ImGuiCol_ScrollbarGrab),
			value("ScrollbarGrabHovered", (int)ImGuiCol_ScrollbarGrabHovered),
			value("ScrollbarGrabActive", (int)ImGuiCol_ScrollbarGrabActive),
			value("CheckMark", (int)ImGuiCol_CheckMark),
			value("SliderGrab", (int)ImGuiCol_SliderGrab),
			value("SliderGrabActive", (int)ImGuiCol_SliderGrabActive),
			value("Button", (int)ImGuiCol_Button),
			value("ButtonHovered", (int)ImGuiCol_ButtonHovered),
			value("ButtonActive", (int)ImGuiCol_ButtonActive),
			value("Header", (int)ImGuiCol_Header),
			value("HeaderHovered", (int)ImGuiCol_HeaderHovered),
			value("HeaderActive", (int)ImGuiCol_HeaderActive),
			value("Separator", (int)ImGuiCol_Separator),
			value("SeparatorHovered", (int)ImGuiCol_SeparatorHovered),
			value("SeparatorActive", (int)ImGuiCol_SeparatorActive),
			value("ResizeGrip", (int)ImGuiCol_ResizeGrip),
			value("ResizeGripHovered", (int)ImGuiCol_ResizeGripHovered),
			value("ResizeGripActive", (int)ImGuiCol_ResizeGripActive),
			value("TabHovered", (int)ImGuiCol_TabHovered),
			value("Tab", (int)ImGuiCol_Tab),
			value("TabSelected", (int)ImGuiCol_TabSelected),
			value("TabSelectedOverline", (int)ImGuiCol_TabSelectedOverline),
			value("TabDimmed", (int)ImGuiCol_TabDimmed),
			value("TabDimmedSelected", (int)ImGuiCol_TabDimmedSelected),
			value("TabDimmedSelectedOverline", (int)ImGuiCol_TabDimmedSelectedOverline),
			value("DockingPreview", (int)ImGuiCol_DockingPreview),
			value("DockingEmptyBg", (int)ImGuiCol_DockingEmptyBg),
			value("PlotLines", (int)ImGuiCol_PlotLines),
			value("PlotLinesHovered", (int)ImGuiCol_PlotLinesHovered),
			value("PlotHistogram", (int)ImGuiCol_PlotHistogram),
			value("PlotHistogramHovered", (int)ImGuiCol_PlotHistogramHovered),
			value("TableHeaderBg", (int)ImGuiCol_TableHeaderBg),
			value("TableBorderStrong", (int)ImGuiCol_TableBorderStrong),
			value("TableBorderLight", (int)ImGuiCol_TableBorderLight),
			value("TableRowBg", (int)ImGuiCol_TableRowBg),
			value("TableRowBgAlt", (int)ImGuiCol_TableRowBgAlt),
			value("TextLink", (int)ImGuiCol_TextLink),
			value("TextSelectedBg", (int)ImGuiCol_TextSelectedBg),
			value("DragDropTarget", (int)ImGuiCol_DragDropTarget),
			value("NavCursor", (int)ImGuiCol_NavCursor),
			value("NavWindowingHighlight", (int)ImGuiCol_NavWindowingHighlight),
			value("NavWindowingDimBg", (int)ImGuiCol_NavWindowingDimBg),
			value("ModalWindowDimBg", (int)ImGuiCol_ModalWindowDimBg)
		],

		class_<enum_exporter<ImGuiStyleVar_>>("ImGuiStyleVar")
		.enum_("ImGuiStyleVar")
		[
			value("Alpha", (int)ImGuiStyleVar_Alpha),
			value("DisabledAlpha", (int)ImGuiStyleVar_DisabledAlpha),
			value("WindowPadding", (int)ImGuiStyleVar_WindowPadding),
			value("WindowRounding", (int)ImGuiStyleVar_WindowRounding),
			value("WindowBorderSize", (int)ImGuiStyleVar_WindowBorderSize),
			value("WindowMinSize", (int)ImGuiStyleVar_WindowMinSize),
			value("WindowTitleAlign", (int)ImGuiStyleVar_WindowTitleAlign),
			value("ChildRounding", (int)ImGuiStyleVar_ChildRounding),
			value("ChildBorderSize", (int)ImGuiStyleVar_ChildBorderSize),
			value("PopupRounding", (int)ImGuiStyleVar_PopupRounding),
			value("PopupBorderSize", (int)ImGuiStyleVar_PopupBorderSize),
			value("FramePadding", (int)ImGuiStyleVar_FramePadding),
			value("FrameRounding", (int)ImGuiStyleVar_FrameRounding),
			value("FrameBorderSize", (int)ImGuiStyleVar_FrameBorderSize),
			value("ItemSpacing", (int)ImGuiStyleVar_ItemSpacing),
			value("ItemInnerSpacing", (int)ImGuiStyleVar_ItemInnerSpacing),
			value("IndentSpacing", (int)ImGuiStyleVar_IndentSpacing),
			value("CellPadding", (int)ImGuiStyleVar_CellPadding),
			value("ScrollbarSize", (int)ImGuiStyleVar_ScrollbarSize),
			value("ScrollbarRounding", (int)ImGuiStyleVar_ScrollbarRounding),
			value("GrabMinSize", (int)ImGuiStyleVar_GrabMinSize),
			value("GrabRounding", (int)ImGuiStyleVar_GrabRounding),
			value("TabRounding", (int)ImGuiStyleVar_TabRounding),
			value("TabBorderSize", (int)ImGuiStyleVar_TabBorderSize),
			value("TabBarBorderSize", (int)ImGuiStyleVar_TabBarBorderSize),
			value("TabBarOverlineSize", (int)ImGuiStyleVar_TabBarOverlineSize),
			value("TableAngledHeadersAngle", (int)ImGuiStyleVar_TableAngledHeadersAngle),
			value("TableAngledHeadersTextAlign", (int)ImGuiStyleVar_TableAngledHeadersTextAlign),
			value("ButtonTextAlign", (int)ImGuiStyleVar_ButtonTextAlign),
			value("SelectableTextAlign", (int)ImGuiStyleVar_SelectableTextAlign),
			value("SeparatorTextBorderSize", (int)ImGuiStyleVar_SeparatorTextBorderSize),
			value("SeparatorTextAlign", (int)ImGuiStyleVar_SeparatorTextAlign),
			value("SeparatorTextPadding", (int)ImGuiStyleVar_SeparatorTextPadding),
			value("DockingSeparatorSize", (int)ImGuiStyleVar_DockingSeparatorSize)
		],

		class_<enum_exporter<ImGuiComboFlags_>>("ImGuiComboFlags")
		.enum_("ImGuiComboFlags")
		[
			value("PopupAlignLeft", (int)ImGuiComboFlags_PopupAlignLeft),
			value("HeightSmall", (int)ImGuiComboFlags_HeightSmall),
			value("HeightRegular", (int)ImGuiComboFlags_HeightRegular),
			value("HeightLarge", (int)ImGuiComboFlags_HeightLarge),
			value("HeightLargest", (int)ImGuiComboFlags_HeightLargest),
			value("NoArrowButton", (int)ImGuiComboFlags_NoArrowButton),
			value("NoPreview", (int)ImGuiComboFlags_NoPreview),
			value("WidthFitPreview", (int)ImGuiComboFlags_WidthFitPreview)
		],

		class_<enum_exporter<ImGuiSortDirection>>("ImGuiSortDirection")
		.enum_("ImGuiSortDirection")
		[
			value("Ascending", (int)ImGuiSortDirection_Ascending),
			value("Descending", (int)ImGuiSortDirection_Descending)
		],

		class_<enum_exporter<ImGuiMouseButton_>>("ImGuiMouseButton")
		.enum_("ImGuiMouseButton")
		[
			value("Left", (int)ImGuiMouseButton_Left),
			value("Right", (int)ImGuiMouseButton_Right),
			value("Middle", (int)ImGuiMouseButton_Middle)
		],

		class_<enum_exporter<ImGuiButtonFlags_>>("ImGuiButtonFlags")
		.enum_("ImGuiButtonFlags")
		[
			value("MouseButtonLeft", (int)ImGuiButtonFlags_MouseButtonLeft),
			value("MouseButtonRight", (int)ImGuiButtonFlags_MouseButtonRight),
			value("MouseButtonMiddle", (int)ImGuiButtonFlags_MouseButtonMiddle),
			value("EnableNav", (int)ImGuiButtonFlags_EnableNav)
		],

		class_<enum_exporter<ImGuiKey>>("ImGuiKey")
		.enum_("ImGuiKey")
		[
			value("A", (int)ImGuiKey_A),
			value("B", (int)ImGuiKey_B),
			value("C", (int)ImGuiKey_C),
			value("D", (int)ImGuiKey_D),
			value("E", (int)ImGuiKey_E),
			value("F", (int)ImGuiKey_F),
			value("G", (int)ImGuiKey_G),
			value("H", (int)ImGuiKey_H),
			value("I", (int)ImGuiKey_I),
			value("J", (int)ImGuiKey_J),
			value("K", (int)ImGuiKey_K),
			value("L", (int)ImGuiKey_L),
			value("M", (int)ImGuiKey_M),
			value("N", (int)ImGuiKey_N),
			value("O", (int)ImGuiKey_O),
			value("P", (int)ImGuiKey_P),
			value("Q", (int)ImGuiKey_Q),
			value("R", (int)ImGuiKey_R),
			value("S", (int)ImGuiKey_S),
			value("T", (int)ImGuiKey_T),
			value("U", (int)ImGuiKey_U),
			value("V", (int)ImGuiKey_V),
			value("W", (int)ImGuiKey_W),
			value("X", (int)ImGuiKey_X),
			value("Y", (int)ImGuiKey_Y),
			value("Z", (int)ImGuiKey_Z),
			value("_1", (int)ImGuiKey_1),
			value("_2", (int)ImGuiKey_2),
			value("_3", (int)ImGuiKey_3),
			value("_4", (int)ImGuiKey_4),
			value("_5", (int)ImGuiKey_5),
			value("_6", (int)ImGuiKey_6),
			value("_7", (int)ImGuiKey_7),
			value("_8", (int)ImGuiKey_8),
			value("_9", (int)ImGuiKey_9),
			value("_0", (int)ImGuiKey_0),
			value("Enter", (int)ImGuiKey_Enter),
			value("Escape", (int)ImGuiKey_Escape),
			value("Backspace", (int)ImGuiKey_Backspace),
			value("Tab", (int)ImGuiKey_Tab),
			value("Space", (int)ImGuiKey_Space),
			value("Minus", (int)ImGuiKey_Minus),
			value("Equal", (int)ImGuiKey_Equal),
			value("LeftBracket", (int)ImGuiKey_LeftBracket),
			value("RightBracket", (int)ImGuiKey_RightBracket),
			value("Backslash", (int)ImGuiKey_Backslash),
			value("Semicolon", (int)ImGuiKey_Semicolon),
			value("Apostrophe", (int)ImGuiKey_Apostrophe),
			value("GraveAccent", (int)ImGuiKey_GraveAccent),
			value("Comma", (int)ImGuiKey_Comma),
			value("Period", (int)ImGuiKey_Period),
			value("Slash", (int)ImGuiKey_Slash),
			value("CapsLock", (int)ImGuiKey_CapsLock),
			value("F1", (int)ImGuiKey_F1),
			value("F2", (int)ImGuiKey_F2),
			value("F3", (int)ImGuiKey_F3),
			value("F4", (int)ImGuiKey_F4),
			value("F5", (int)ImGuiKey_F5),
			value("F6", (int)ImGuiKey_F6),
			value("F7", (int)ImGuiKey_F7),
			value("F8", (int)ImGuiKey_F8),
			value("F9", (int)ImGuiKey_F9),
			value("F10", (int)ImGuiKey_F10),
			value("F11", (int)ImGuiKey_F11),
			value("F12", (int)ImGuiKey_F12),
			value("ScrollLock", (int)ImGuiKey_ScrollLock),
			value("Pause", (int)ImGuiKey_Pause),
			value("Insert", (int)ImGuiKey_Insert),
			value("Home", (int)ImGuiKey_Home),
			value("PageUp", (int)ImGuiKey_PageUp),
			value("Delete", (int)ImGuiKey_Delete),
			value("End", (int)ImGuiKey_End),
			value("PageDown", (int)ImGuiKey_PageDown),
			value("RightArrow", (int)ImGuiKey_RightArrow),
			value("LeftArrow", (int)ImGuiKey_LeftArrow),
			value("DownArrow", (int)ImGuiKey_DownArrow),
			value("UpArrow", (int)ImGuiKey_UpArrow),
			value("NumLock", (int)ImGuiKey_NumLock),
			value("KeypadDivide", (int)ImGuiKey_KeypadDivide),
			value("KeypadMultiply", (int)ImGuiKey_KeypadMultiply),
			value("KeypadSubtract", (int)ImGuiKey_KeypadSubtract),
			value("KeypadAdd", (int)ImGuiKey_KeypadAdd),
			value("KeypadEnter", (int)ImGuiKey_KeypadEnter),
			value("Keypad1", (int)ImGuiKey_Keypad1),
			value("Keypad2", (int)ImGuiKey_Keypad2),
			value("Keypad3", (int)ImGuiKey_Keypad3),
			value("Keypad4", (int)ImGuiKey_Keypad4),
			value("Keypad5", (int)ImGuiKey_Keypad5),
			value("Keypad6", (int)ImGuiKey_Keypad6),
			value("Keypad7", (int)ImGuiKey_Keypad7),
			value("Keypad8", (int)ImGuiKey_Keypad8),
			value("Keypad9", (int)ImGuiKey_Keypad9),
			value("Keypad0", (int)ImGuiKey_Keypad0),
			value("KeypadDecimal", (int)ImGuiKey_KeypadDecimal),
			value("Menu", (int)ImGuiKey_Menu),
			value("KeypadEqual", (int)ImGuiKey_KeypadEqual),
			value("LeftCtrl", (int)ImGuiKey_LeftCtrl),
			value("LeftShift", (int)ImGuiKey_LeftShift),
			value("LeftAlt", (int)ImGuiKey_LeftAlt),
			value("LeftSuper", (int)ImGuiKey_LeftSuper),
			value("RightCtrl", (int)ImGuiKey_RightCtrl),
			value("RightShift", (int)ImGuiKey_RightShift),
			value("RightAlt", (int)ImGuiKey_RightAlt),
			value("RightSuper", (int)ImGuiKey_RightSuper)
		],

		class_<enum_exporter<ImGuiInputFlagsPrivate_>>("ImGuiInputFlags")
		.enum_("ImGuiInputFlags")
		[
			value("RepeatRateDefault", (int)ImGuiInputFlags_RepeatRateDefault),
			value("RepeatRateNavMove", (int)ImGuiInputFlags_RepeatRateNavMove),
			value("RepeatRateNavTweak", (int)ImGuiInputFlags_RepeatRateNavTweak),
			value("RepeatUntilRelease", (int)ImGuiInputFlags_RepeatUntilRelease),
			value("RepeatUntilKeyModsChange", (int)ImGuiInputFlags_RepeatUntilKeyModsChange),
			value("RepeatUntilKeyModsChangeFromNone", (int)ImGuiInputFlags_RepeatUntilKeyModsChangeFromNone),
			value("RepeatUntilOtherKeyPress", (int)ImGuiInputFlags_RepeatUntilOtherKeyPress),
			value("LockThisFrame", (int)ImGuiInputFlags_LockThisFrame),
			value("LockUntilRelease", (int)ImGuiInputFlags_LockUntilRelease),
			value("CondHovered", (int)ImGuiInputFlags_CondHovered),
			value("CondActive", (int)ImGuiInputFlags_CondActive)
		],

		class_<enum_exporter<ImGuiColorEditFlags_>>("ImGuiColorEditFlags")
		.enum_("ImGuiColorEditFlags")
		[
			value("NoAlpha", (int)ImGuiColorEditFlags_NoAlpha),
			value("NoPicker", (int)ImGuiColorEditFlags_NoPicker),
			value("NoOptions", (int)ImGuiColorEditFlags_NoOptions),
			value("NoSmallPreview", (int)ImGuiColorEditFlags_NoSmallPreview),
			value("NoInputs", (int)ImGuiColorEditFlags_NoInputs),
			value("NoTooltip", (int)ImGuiColorEditFlags_NoTooltip),
			value("NoLabel", (int)ImGuiColorEditFlags_NoLabel),
			value("NoSidePreview", (int)ImGuiColorEditFlags_NoSidePreview),
			value("NoDragDrop", (int)ImGuiColorEditFlags_NoDragDrop),
			value("NoBorder", (int)ImGuiColorEditFlags_NoBorder),
			value("AlphaOpaque", (int)ImGuiColorEditFlags_AlphaOpaque),
			value("AlphaNoBg", (int)ImGuiColorEditFlags_AlphaNoBg),
			value("AlphaPreviewHalf", (int)ImGuiColorEditFlags_AlphaPreviewHalf),
			value("AlphaBar", (int)ImGuiColorEditFlags_AlphaBar),
			value("HDR", (int)ImGuiColorEditFlags_HDR),
			value("DisplayRGB", (int)ImGuiColorEditFlags_DisplayRGB),
			value("DisplayHSV", (int)ImGuiColorEditFlags_DisplayHSV),
			value("DisplayHex", (int)ImGuiColorEditFlags_DisplayHex),
			value("Uint8", (int)ImGuiColorEditFlags_Uint8),
			value("Float", (int)ImGuiColorEditFlags_Float),
			value("PickerHueBar", (int)ImGuiColorEditFlags_PickerHueBar),
			value("PickerHueWheel", (int)ImGuiColorEditFlags_PickerHueWheel),
			value("InputRGB", (int)ImGuiColorEditFlags_InputRGB),
			value("InputHSV", (int)ImGuiColorEditFlags_InputHSV)
		],

		class_<enum_exporter<ImGuiSliderFlags_>>("ImGuiSliderFlags")
		.enum_("ImGuiSliderFlags")
		[
			value("Logarithmic", (int)ImGuiSliderFlags_Logarithmic),
			value("NoRoundToFormat", (int)ImGuiSliderFlags_NoRoundToFormat),
			value("NoInput", (int)ImGuiSliderFlags_NoInput),
			value("WrapAround", (int)ImGuiSliderFlags_WrapAround),
			value("ClampOnInput", (int)ImGuiSliderFlags_ClampOnInput),
			value("ClampZeroRange", (int)ImGuiSliderFlags_ClampZeroRange),
			value("NoSpeedTweaks", (int)ImGuiSliderFlags_NoSpeedTweaks),
			value("AlwaysClamp", (int)ImGuiSliderFlags_AlwaysClamp)
		],

		class_<enum_exporter<ImGuiFocusedFlags_>>("ImGuiFocusedFlags")
		.enum_("ImGuiFocusedFlags")
		[
			value("ChildWindows", (int)ImGuiFocusedFlags_ChildWindows),
			value("RootWindow", (int)ImGuiFocusedFlags_RootWindow),
			value("AnyWindow", (int)ImGuiFocusedFlags_AnyWindow),
			value("NoPopupHierarchy", (int)ImGuiFocusedFlags_NoPopupHierarchy),
			value("DockHierarchy", (int)ImGuiFocusedFlags_DockHierarchy),
			value("RootAndChildWindows", (int)ImGuiFocusedFlags_RootAndChildWindows)
		],

		class_<enum_exporter<ImGuiComboFlags_>>("ImGuiComboFlags")
		.enum_("ImGuiComboFlags")
		[
			value("PopupAlignLeft", (int)ImGuiComboFlags_PopupAlignLeft),
			value("HeightSmall", (int)ImGuiComboFlags_HeightSmall),
			value("HeightRegular", (int)ImGuiComboFlags_HeightRegular),
			value("HeightLarge", (int)ImGuiComboFlags_HeightLarge),
			value("HeightLargest", (int)ImGuiComboFlags_HeightLargest),
			value("NoArrowButton", (int)ImGuiComboFlags_NoArrowButton),
			value("NoPreview", (int)ImGuiComboFlags_NoPreview),
			value("WidthFitPreview", (int)ImGuiComboFlags_WidthFitPreview)
		],
		
		class_<enum_exporter<ImGuiHoveredFlags_>>("ImGuiHoveredFlags")
		.enum_("ImGuiHoveredFlags")
		[
			value("ChildWindows", (int)ImGuiHoveredFlags_ChildWindows),
			value("RootWindow", (int)ImGuiHoveredFlags_RootWindow),
			value("AnyWindow", (int)ImGuiHoveredFlags_AnyWindow),
			value("NoPopupHierarchy", (int)ImGuiHoveredFlags_NoPopupHierarchy),
			value("DockHierarchy", (int)ImGuiHoveredFlags_DockHierarchy),
			value("AllowWhenBlockedByPopup", (int)ImGuiHoveredFlags_AllowWhenBlockedByPopup),
			value("AllowWhenBlockedByActiveItem", (int)ImGuiHoveredFlags_AllowWhenBlockedByActiveItem),
			value("AllowWhenOverlappedByItem", (int)ImGuiHoveredFlags_AllowWhenOverlappedByItem),
			value("AllowWhenOverlappedByWindow", (int)ImGuiHoveredFlags_AllowWhenOverlappedByWindow),
			value("AllowWhenDisabled", (int)ImGuiHoveredFlags_AllowWhenDisabled),
			value("NoNavOverride", (int)ImGuiHoveredFlags_NoNavOverride),
			value("AllowWhenOverlapped", (int)ImGuiHoveredFlags_AllowWhenOverlapped),
			value("RectOnly", (int)ImGuiHoveredFlags_RectOnly),
			value("RootAndChildWindows", (int)ImGuiHoveredFlags_RootAndChildWindows),
			value("ForTooltip", (int)ImGuiHoveredFlags_ForTooltip),
			value("Stationary", (int)ImGuiHoveredFlags_Stationary),
			value("DelayNone", (int)ImGuiHoveredFlags_DelayNone),
			value("DelayShort", (int)ImGuiHoveredFlags_DelayShort),
			value("DelayNormal", (int)ImGuiHoveredFlags_DelayNormal),
			value("NoSharedDelay", (int)ImGuiHoveredFlags_NoSharedDelay)
		]
	];
}