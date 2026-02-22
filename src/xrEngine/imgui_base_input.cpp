#include "stdafx.h"

#include "imgui_base.h"
#include "imgui_helper.h"
#include <imgui.h>
#include <imgui_internal.h>

#include "xr_input.h"
#include "../xrGame/xr_level_controller.h"
#include "../xrCore/os_clipboard.h"

#include "render.h"
#include "../xrGame/UICursor.h"

namespace xr_imgui
{
    static bool xray_cursor_state = true;
    
    struct ide_backend
    {
        char* clipboard_text_data;
    };

    static void ImGui_UpdateKeyboardCodePage(UINT32& keyboard_code_page)
    {
        // Retrieve keyboard code page, required for handling of non-Unicode Windows.
        HKL keyboard_layout = ::GetKeyboardLayout(0);
        LCID keyboard_lcid = MAKELCID(HIWORD(keyboard_layout), SORT_DEFAULT);
        if (::GetLocaleInfoA(keyboard_lcid, (LOCALE_RETURN_NUMBER | LOCALE_IDEFAULTANSICODEPAGE), (LPSTR)&keyboard_code_page, sizeof(keyboard_code_page)) == 0)
            keyboard_code_page = CP_ACP; // Fallback to default ANSI code page when fails.
    }

    void ide::InitBackend()
    {
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_DockingEnable;
        io.BackendPlatformName = "imgui_impl_xray";

        // Clipboard functionality
        io.SetClipboardTextFn = [](void*, const char* text)
            {
                os_clipboard::copy_to_clipboard(text);
            };
        io.GetClipboardTextFn = [](void* user_data) -> const char*
            {
                ide_backend& bd = *(ide_backend*)user_data;
                if (bd.clipboard_text_data)
                    xr_free(bd.clipboard_text_data);
                os_clipboard::paste_from_clipboard(&bd.clipboard_text_data[0], 2048);
                return bd.clipboard_text_data;
            };
        io.ClipboardUserData = m_backend_data;

        ImGui_UpdateKeyboardCodePage(keyboard_code_page);
    }

    void ide::ShutdownBackend()
    {
        ide_backend& bd = *m_backend_data;

        if (bd.clipboard_text_data)
        {
            xr_free(bd.clipboard_text_data);
            bd.clipboard_text_data = nullptr;
        }
    }

    void ide::OnAppActivate()
    {
        ImGuiIO& io = ImGui::GetIO();
        io.AddFocusEvent(true);
    }

    void ide::OnAppDeactivate()
    {
        ImGuiIO& io = ImGui::GetIO();
        io.AddFocusEvent(false);
    }

    void ide::IR_Capture()
    {
        if (m_input) return;
        m_input = true;
        IInputReceiver::IR_Capture();
        ImGuiIO& io = ImGui::GetIO();
        io.MouseDrawCursor = true;

        Ivector2 p;
        IR_GetMousePosReal(Device.m_hWnd, p);
        ImGui::TeleportMousePos(ImVec2{(float)p.x, (float)p.y});

        xray_cursor_state = GetUICursor().IsVisible();
        GetUICursor().Hide();
    }

    void ide::IR_Release()
    {
        if (!m_input) return;
        m_input = false;
        IInputReceiver::IR_Release();
        ImGuiIO& io = ImGui::GetIO();
        io.MouseDrawCursor = false;

        Fvector2 pos = { io.MousePos.x, io.MousePos.y };
        pos.x = iFloor(pos.x * (UI_BASE_WIDTH / (float)Device.dwWidth));
        pos.y = iFloor(pos.y * (UI_BASE_HEIGHT / (float)Device.dwHeight));

        GetUICursor().SetUICursorPosition(pos);

        if (xray_cursor_state)
            GetUICursor().Show();
    }

    void ide::IR_OnMousePress(int key)
    {
        ImGuiIO& io = ImGui::GetIO();
        io.AddMouseButtonEvent(key, true);
    }

    void ide::IR_OnMouseRelease(int key)
    {
        ImGuiIO& io = ImGui::GetIO();
        io.AddMouseButtonEvent(key, false);
    }

    void ide::IR_OnMouseWheel(int direction)
    {
        ImGuiIO& io = ImGui::GetIO();
        io.AddMouseWheelEvent(0.f, static_cast<float>(direction));
    }

    void ide::IR_OnMouseMove(int /*x*/, int /*y*/)
    {
        // x and y are relative
        // ImGui accepts absolute coordinates
        Ivector2 p;
        IR_GetMousePosReal(Device.m_hWnd, p);

        ImGuiIO& io = ImGui::GetIO();
        io.AddMousePosEvent(static_cast<float>(p.x), static_cast<float>(p.y));
    }

    void ide::IR_OnKeyboardPress(int key)
    {
        ImGuiIO& io = ImGui::GetIO();

        switch (get_binded_action(key))
        {
        case kQUIT:
            if (io.WantTextInput)
                break; // bypass to ImGui

            Show(false);
            return;

        case kEDITOR:
            bool rcon = !!pInput->iGetAsyncKeyState(DIK_RCONTROL);
            if (rcon)
            {
                EnableInput(false);
                return;
            }

            Show(false);
            return;
        }

        switch (key)
        {
        case DIK_LCONTROL:
        case DIK_RCONTROL:
            io.AddKeyEvent(ImGuiMod_Ctrl, true);
            break;

        case DIK_LSHIFT:
        case DIK_RSHIFT:
            io.AddKeyEvent(ImGuiMod_Shift, true);
            break;

        case DIK_LALT:
        case DIK_RALT:
            io.AddKeyEvent(ImGuiMod_Alt, true);
            break;

        case DIK_LWIN:
        case DIK_RWIN:
            io.AddKeyEvent(ImGuiMod_Super, true);
            break;
        }

        const auto imkey = xr_key_to_imgui_key(key);
        if (imkey == ImGuiKey_None)
            return;
        io.AddKeyEvent(imkey, true);
    }

    void ide::IR_OnKeyboardRelease(int key)
    {
        ImGuiIO& io = ImGui::GetIO();

        const auto check = [&, this](ImGuiKey mod, int xr_key)
            {
                if (!IR_GetKeyState(xr_key))
                    io.AddKeyEvent(mod, false);
            };

        switch (key)
        {
        case DIK_LCONTROL:  check(ImGuiMod_Ctrl,    DIK_RCONTROL);  break;
        case DIK_RCONTROL:  check(ImGuiMod_Ctrl,    DIK_LCONTROL);  break;
        case DIK_LSHIFT:    check(ImGuiMod_Shift,   DIK_RSHIFT);    break;
        case DIK_RSHIFT:    check(ImGuiMod_Shift,   DIK_LSHIFT);    break;
        case DIK_LALT:      check(ImGuiMod_Alt,     DIK_RALT);      break;
        case DIK_RALT:      check(ImGuiMod_Alt,     DIK_LALT);      break;
        case DIK_LWIN:      check(ImGuiMod_Super,   DIK_RWIN);      break;
        case DIK_RWIN:      check(ImGuiMod_Super,   DIK_LWIN);      break;
        }                                              

        const auto imkey = xr_key_to_imgui_key(key);
        if (imkey == ImGuiKey_None)
            return;
        io.AddKeyEvent(imkey, false);
    }

    void ide::UpdateInputLang()
    {
        ImGui_UpdateKeyboardCodePage(keyboard_code_page);
    }

    void ide::InputChar(WPARAM param)
    {
        ImGuiIO& io = ImGui::GetIO();
        if (!io.WantTextInput) return;

        wchar_t wch = 0;
        ::MultiByteToWideChar(keyboard_code_page, MB_PRECOMPOSED, (char*)&param, 1, &wch, 1);
        io.AddInputCharacter(wch);
    }

    void ide::EnableInput(bool bInput)
    {
        if (m_input == bInput) return;
        bInput ? IR_Capture() : IR_Release();
    }
} // namespace xr_imgui