#pragma once
#include "stdafx.h"
#include "imgui/imgui.h"

static bool ugly_hack = false;
static ::std::string imgui_text_buffer;

IC LPCSTR ImGui_GetVersion()
{
	return ImGui::GetVersion();
}

IC void ImGui_SetNextWindowPos(Fvector2& pos)
{
	ImGui::SetNextWindowPos(*(ImVec2*)&pos, 0, *(ImVec2*)&Fvector2().set(0, 0));
}

IC void ImGui_SetNextWindowPos(Fvector2& pos, ImGuiCond cond)
{
	ImGui::SetNextWindowPos(*(ImVec2*)&pos, cond, *(ImVec2*)&Fvector2().set(0,0));
}

IC void ImGui_SetNextWindowPos(Fvector2& pos, ImGuiCond cond, Fvector2& pivot)
{
	ImGui::SetNextWindowPos(*(ImVec2*)&pos, cond, *(ImVec2*)&pivot);
}

IC void ImGui_SetNextWindowSize(Fvector2& size, ImGuiCond cond = 0)
{
	ImGui::SetNextWindowSize(*(ImVec2*)&size, cond);
}

IC void ImGui_SetNextWindowSizeConstraints(Fvector2& size_min, Fvector2& size_max)
{
	ImGui::SetNextWindowSizeConstraints(*(ImVec2*)&size_min, *(ImVec2*)&size_max);
}

IC void ImGui_SetNextWindowContentSize(Fvector2& size)
{
	ImGui::SetNextWindowContentSize(*(ImVec2*)&size);
}

IC void ImGui_SetNextWindowScroll(Fvector2& scroll)
{
	ImGui::SetNextWindowScroll(*(ImVec2*)&scroll);
}

IC Fvector2 ImGui_GetWindowPos()
{
	return *(Fvector2*)&ImGui::GetWindowPos();
}

IC Fvector2 ImGui_GetWindowSize()
{
	return *(Fvector2*)&ImGui::GetWindowSize();
}

IC void ImGui_SetWindowPos(Fvector2& pos, ImGuiCond cond = 0)
{
	ImGui::SetWindowPos(*(ImVec2*)&pos, cond);
}

IC void ImGui_SetWindowSize(Fvector2& size, ImGuiCond cond = 0)
{
	ImGui::SetWindowSize(*(ImVec2*)&size, cond);
}

IC void ImGui_SetWindowCollapsed(bool collapsed)
{
	ImGui::SetWindowCollapsed(collapsed);
}

IC void ImGui_SetScrollFromPosX(float pos)
{
	ImGui::SetScrollFromPosX(pos);
}

IC void ImGui_SetScrollFromPosY(float pos)
{
	ImGui::SetScrollFromPosY(pos);
}

IC Fvector2 ImGui_GetCursorScreenPos()
{
	return *(Fvector2*)&ImGui::GetCursorScreenPos();
}

IC void ImGui_SetCursorScreenPos(Fvector2& pos)
{
	ImGui::SetCursorScreenPos(*(ImVec2*)&pos);
}

IC Fvector2 ImGui_GetContentRegionAvail()
{
	return *(Fvector2*)&ImGui::GetContentRegionAvail();
}

IC Fvector2 ImGui_GetCursorPos()
{
	return *(Fvector2*)&ImGui::GetCursorPos();
}

IC void ImGui_SetCursorPos(Fvector2& pos)
{
	ImGui::SetCursorPos(*(ImVec2*)&pos);
}

IC Fvector2 ImGui_GetCursorStartPos()
{
	return *(Fvector2*)&ImGui::GetCursorStartPos();
}

IC void ImGui_SameLine()
{
	ImGui::SameLine();
}

IC void ImGui_SameLine(float offset)
{
	ImGui::SameLine(offset);
}

IC void ImGui_Dummy(Fvector2& size)
{
	ImGui::Dummy(*(ImVec2*)&size);
}

IC void ImGui_TextDisabled(LPCSTR text)
{
	ImGui::TextDisabled(text, 0);
}

IC void ImGui_TextWrapped(LPCSTR text)
{
	ImGui::TextWrapped(text, 0);
}

IC void ImGui_TextColored(Fcolor& col, LPCSTR text)
{
	ImGui::TextColoredV(ImVec4{ col.r, col.g, col.b, col.a }, text, 0);
}

IC void ImGui_LabelText(LPCSTR label, LPCSTR text)
{
	ImGui::LabelText(label, text, 0);
}

IC void ImGui_BulletText(LPCSTR text)
{
	ImGui::BulletText(text, 0);
}

IC bool ImGui_Button(LPCSTR label)
{
	return ImGui::Button(label, *(ImVec2*)&Fvector2().set(80,22));
}

IC bool ImGui_Button(LPCSTR label, Fvector2 size)
{
	return ImGui::Button(label, *(ImVec2*)&size);
}

IC bool ImGui_InvisibleButton(LPCSTR label)
{
	return ImGui::InvisibleButton(label, *(ImVec2*)&Fvector2().set(80,22), 0);
}

IC bool ImGui_InvisibleButton(LPCSTR label, Fvector2 size)
{
	return ImGui::InvisibleButton(label, *(ImVec2*)&size, 0);
}

IC bool ImGui_InvisibleButton(LPCSTR label, Fvector2 size, ImGuiButtonFlags flags)
{
	return ImGui::InvisibleButton(label, *(ImVec2*)&size, flags);
}

IC void ImGui_ProgressBar(float fraction, Fvector2 size = Fvector2{FLT_MAX,0}, LPCSTR overlay = 0)
{
	ImGui::ProgressBar(fraction, *(ImVec2*)&size, overlay);
}

IC bool ImGui_DragFloat2(LPCSTR name, Fvector2& vec, float speed = 1.f, float min = 0.f, float max = 0.f, LPCSTR format = "%.3f", ImGuiSliderFlags flags = 0)
{
	return ImGui::DragFloat2(name, (float*)&vec, speed, min, max, format, flags);
}

IC bool ImGui_DragFloat3(LPCSTR name, Fvector& vec, float speed = 1.f, float min = 0.f, float max = 0.f, LPCSTR format = "%.3f", ImGuiSliderFlags flags = 0)
{
	return ImGui::DragFloat3(name, (float*)&vec, speed, min, max, format, flags);
}

IC bool ImGui_DragFloat4(LPCSTR name, Fvector4& vec, float speed = 1.f, float min = 0.f, float max = 0.f, LPCSTR format = "%.3f", ImGuiSliderFlags flags = 0)
{
	return ImGui::DragFloat4(name, (float*)&vec, speed, min, max, format, flags);
}

IC bool ImGui_ColorPicker3(LPCSTR name, Fcolor& color, ImGuiColorEditFlags flags = 0)
{
	return ImGui::ColorPicker3(name, (float*)&color, flags);
}

IC bool ImGui_ColorPicker4(LPCSTR name, Fcolor& color, ImGuiColorEditFlags flags = 0)
{
	return ImGui::ColorPicker4(name, (float*)&color, flags);
}

IC bool ImGui_ColorEdit3(LPCSTR name, Fcolor& color, ImGuiColorEditFlags flags = 0)
{
	return ImGui::ColorEdit3(name, (float*)&color, flags);
}

IC bool ImGui_ColorEdit4(LPCSTR name, Fcolor& color, ImGuiColorEditFlags flags = 0)
{
	return ImGui::ColorEdit4(name, (float*)&color, flags);
}

IC bool ImGui_SliderFloat2(LPCSTR name, Fvector2& vec, float min = 0.f, float max = 0.f, LPCSTR format = "%.3f", ImGuiSliderFlags flags = 0)
{
	return ImGui::SliderFloat2(name, (float*)&vec, min, max, format, flags);
}

IC bool ImGui_SliderFloat3(LPCSTR name, Fvector& vec, float min = 0.f, float max = 0.f, LPCSTR format = "%.3f", ImGuiSliderFlags flags = 0)
{
	return ImGui::SliderFloat3(name, (float*)&vec, min, max, format, flags);
}

IC bool ImGui_SliderFloat4(LPCSTR name, Fvector4& vec, float min = 0.f, float max = 0.f, LPCSTR format = "%.3f", ImGuiSliderFlags flags = 0)
{
	return ImGui::SliderFloat4(name, (float*)&vec, min, max, format, flags);
}

// ImGui writes up to `capacity` bytes into the buffer, so it must be at least that
// large, and never smaller than the incoming text. ImGui still receives `capacity`
// as the edit limit, so behavior for script callers is unchanged.
IC char* ImGui_PrepareTextBuffer(LPCSTR text, int capacity)
{
	imgui_text_buffer = (text && xr_strlen(text)) ? text : "";
	size_t size = _max(size_t(capacity > 0 ? capacity : 0), imgui_text_buffer.size() + 1);
	imgui_text_buffer.resize(size, '\0');
	return &imgui_text_buffer[0];
}

// Drop the padding after the terminator ImGui wrote.
IC LPCSTR ImGui_FinishTextBuffer()
{
	imgui_text_buffer.resize(xr_strlen(imgui_text_buffer.c_str()));
	return imgui_text_buffer.c_str();
}

IC LPCSTR ImGui_InputText(LPCSTR label, LPCSTR text, int capacity = 100, ImGuiInputTextFlags flags = 0, bool& changed = ugly_hack)
{
	changed = ImGui::InputText(label, ImGui_PrepareTextBuffer(text, capacity), capacity, flags);
	return ImGui_FinishTextBuffer();
}

IC LPCSTR ImGui_InputTextMultiline(LPCSTR label, LPCSTR text, int capacity = 100, Fvector2 size = Fvector2{ 0,0 }, ImGuiInputTextFlags flags = 0, bool& changed = ugly_hack)
{
	changed = ImGui::InputTextMultiline(label, ImGui_PrepareTextBuffer(text, capacity), capacity, *(ImVec2*)&size, flags);
	return ImGui_FinishTextBuffer();
}

IC LPCSTR ImGui_InputTextWithHint(LPCSTR label, LPCSTR hint, LPCSTR text, int capacity = 100, ImGuiInputTextFlags flags = 0, bool& changed = ugly_hack)
{
	changed = ImGui::InputTextWithHint(label, hint, ImGui_PrepareTextBuffer(text, capacity), capacity, flags);
	return ImGui_FinishTextBuffer();
}

IC bool ImGui_CollapsingHeader(LPCSTR label)
{
	return ImGui::CollapsingHeader(label);
}

IC bool ImGui_CollapsingHeader(LPCSTR label, bool* visible)
{
	return ImGui::CollapsingHeader(label, visible);
}

IC bool ImGui_InputFloat2(LPCSTR name, Fvector2& vec, LPCSTR format = "%.3f", ImGuiInputTextFlags flags = 0)
{
	return ImGui::InputFloat2(name, (float*)&vec, format, flags);
}

IC bool ImGui_InputFloat3(LPCSTR name, Fvector& vec, LPCSTR format = "%.3f", ImGuiInputTextFlags flags = 0)
{
	return ImGui::InputFloat3(name, (float*)&vec, format, flags);
}

IC bool ImGui_InputFloat4(LPCSTR name, Fvector4& vec, LPCSTR format = "%.3f", ImGuiInputTextFlags flags = 0)
{
	return ImGui::InputFloat4(name, (float*)&vec, format, flags);
}

IC bool ImGui_ColorButton(LPCSTR name, Fcolor& color, ImGuiColorEditFlags flags = 0, Fvector2 size = Fvector2{ 0,0 })
{
	return ImGui::ColorButton(name, *(ImVec4*)&color, flags, *(ImVec2*)&size);
}

IC bool ImGui_Selectable(LPCSTR name, bool selected, ImGuiSelectableFlags flags = 0, Fvector2 size = Fvector2{ 0,0 })
{
	return ImGui::Selectable(name, selected, flags, *(ImVec2*)&size);
}

IC bool ImGui_BeginMenu(LPCSTR name)
{
	return ImGui::BeginMenu(name, true);
}

IC bool ImGui_BeginListBox(LPCSTR name, Fvector2 size = Fvector2{ 0,0 })
{
	return ImGui::BeginListBox(name, *(ImVec2*)&size);
}

IC void ImGui_SetTooltip(LPCSTR text)
{
	ImGui::SetTooltip(text, 0);
}

IC void ImGui_SetItemTooltip(LPCSTR text)
{
	ImGui::SetItemTooltip(text, 0);
}

IC void ImGui_OpenPopup(LPCSTR name)
{
	ImGui::OpenPopup(name);
}

IC void ImGui_OpenPopup(ImGuiID id)
{
	ImGui::OpenPopup(id);
}

IC bool ImGui_IsPopupOpen(LPCSTR name)
{
	return ImGui::IsPopupOpen(name);
}

IC bool ImGui_BeginTable(LPCSTR name, int colums, ImGuiTableFlags flags = 0, Fvector2 size = Fvector2{ 0,0 }, float width = 0)
{
	return ImGui::BeginTable(name, colums, flags, *(ImVec2*)&size, width);
}

IC void ImGui_TableSetBgColor(ImGuiTableBgTarget target, Fcolor& color, int column_n = -1)
{
	ImGui::TableSetBgColor(target, color.get(), column_n);
}

IC void ImGui_BeginDisabled()
{
	ImGui::BeginDisabled(true);
}

IC void ImGui_PushClipRect(Fvector2 min, Fvector2 max, bool intersect)
{
	ImGui::PushClipRect(*(ImVec2*)&min, *(ImVec2*)&max, intersect);
}

IC Fvector2 ImGui_GetItemRectMin()
{
	return *(Fvector2*)&ImGui::GetItemRectMin();
}

IC Fvector2 ImGui_GetItemRectMax()
{
	return *(Fvector2*)&ImGui::GetItemRectMax();
}

IC Fvector2 ImGui_GetItemRectSize()
{
	return *(Fvector2*)&ImGui::GetItemRectSize();
}

IC bool ImGui_IsKeyPressed(ImGuiKey key)
{
	return ImGui::IsKeyPressed(key, true);
}

IC LPCSTR ImGui_GetKeyName(ImGuiKey key)
{
	return ImGui::GetKeyName(key);
}

IC bool ImGui_Shortcut(ImGuiKeyChord keys)
{
	return ImGui::Shortcut(keys, 0);
}

IC Fvector2 ImGui_CalcTextSize(LPCSTR text, bool hide_after_double_hash = false, float wrap_width = -1.f)
{
	return *(Fvector2*)&ImGui::CalcTextSize(text, 0, hide_after_double_hash, wrap_width);
}

IC bool ImGui_IsMouseHoveringRect(Fvector2 min, Fvector2 max)
{
	return ImGui::IsMouseHoveringRect(*(ImVec2*)&min, *(ImVec2*)&max, true);
}

IC bool ImGui_IsMouseHoveringRect(Fvector2 min, Fvector2 max, bool clip)
{
	return ImGui::IsMouseHoveringRect(*(ImVec2*)&min, *(ImVec2*)&max, clip);
}

IC bool ImGui_IsMousePosValid(Fvector2 pos = {0,0})
{
	return ImGui::IsMousePosValid((ImVec2*)&pos);
}

IC Fvector2 ImGui_GetMousePos()
{
	return *(Fvector2*)&ImGui::GetMousePos();
}

IC Fvector2 ImGui_GetMousePosOnOpeningCurrentPopup()
{
	return *(Fvector2*)&ImGui::GetMousePosOnOpeningCurrentPopup();
}

IC Fvector2 ImGui_GetMouseDragDelta(ImGuiMouseButton button = 0, float lock_treshold = -1.f)
{
	return *(Fvector2*)&ImGui::GetMouseDragDelta(button, lock_treshold);
}

IC LPCSTR ImGui_GetClipboardText()
{
	return ImGui::GetClipboardText();
}

IC Fcolor ImGui_GetStyleColorVec4(ImGuiCol idx)
{
	return *(Fcolor*)&ImGui::GetStyleColorVec4(idx);
}

IC void ImGui_PushStyleVar(ImGuiStyleVar var,Fvector2 val)
{
	ImGui::PushStyleVar(var, *(ImVec2*)&val);
}

IC void ImGui_PopStyleVar()
{
	ImGui::PopStyleVar(1);
}

IC void ImGui_PushStyleColor(ImGuiCol idx, Fcolor& col)
{
	ImGui::PushStyleColor(idx, *(ImVec4*)&col);
}

IC void ImGui_PopStyleColor()
{
	ImGui::PopStyleColor(1);
}

IC void ImGui_PushFont(LPCSTR name)
{
	ImGui::PushFont(Device.imgui().GetFont(name));
}