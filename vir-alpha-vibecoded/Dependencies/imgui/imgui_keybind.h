#pragma once
#include "imgui.h"
#include <Windows.h>
#include <string>

namespace KeyBind
{
    inline bool Listening = false;
    inline int CurrentBind = 0;

    inline const char* GetKeyName(int vk)
    {
        static char name[128] = {};

        if (vk == 0)
            return "None";

        switch (vk)
        {
            // Mouse
        case VK_LBUTTON: return "Left Mouse";
        case VK_RBUTTON: return "Right Mouse";
        case VK_MBUTTON: return "Middle Mouse";
        case VK_XBUTTON1: return "XButton 1";
        case VK_XBUTTON2: return "XButton 2";

            // Control keys
        case VK_CANCEL: return "Break";
        case VK_BACK: return "Backspace";
        case VK_TAB: return "Tab";
        case VK_CLEAR: return "Clear";
        case VK_RETURN: return "Enter";
        case VK_SHIFT: return "Shift";
        case VK_LSHIFT: return "Left Shift";
        case VK_RSHIFT: return "Right Shift";
        case VK_CONTROL: return "Ctrl";
        case VK_LCONTROL: return "Left Ctrl";
        case VK_RCONTROL: return "Right Ctrl";
        case VK_MENU: return "Alt";
        case VK_LMENU: return "Left Alt";
        case VK_RMENU: return "Right Alt";
        case VK_PAUSE: return "Pause";
        case VK_CAPITAL: return "Caps Lock";
        case VK_ESCAPE: return "Escape";

            // Navigation
        case VK_SPACE: return "Space";
        case VK_PRIOR: return "Page Up";
        case VK_NEXT: return "Page Down";
        case VK_END: return "End";
        case VK_HOME: return "Home";
        case VK_LEFT: return "Left Arrow";
        case VK_UP: return "Up Arrow";
        case VK_RIGHT: return "Right Arrow";
        case VK_DOWN: return "Down Arrow";
        case VK_SELECT: return "Select";
        case VK_PRINT: return "Print";
        case VK_EXECUTE: return "Execute";
        case VK_SNAPSHOT: return "Print Screen";
        case VK_INSERT: return "Insert";
        case VK_DELETE: return "Delete";
        case VK_HELP: return "Help";

            // Numbers and letters handled by fallback (Windows will name them)

            // Numpad
        case VK_NUMLOCK: return "Num Lock";
        case VK_SCROLL: return "Scroll Lock";
        case VK_NUMPAD0: return "Numpad 0";
        case VK_NUMPAD1: return "Numpad 1";
        case VK_NUMPAD2: return "Numpad 2";
        case VK_NUMPAD3: return "Numpad 3";
        case VK_NUMPAD4: return "Numpad 4";
        case VK_NUMPAD5: return "Numpad 5";
        case VK_NUMPAD6: return "Numpad 6";
        case VK_NUMPAD7: return "Numpad 7";
        case VK_NUMPAD8: return "Numpad 8";
        case VK_NUMPAD9: return "Numpad 9";
        case VK_MULTIPLY: return "Numpad *";
        case VK_ADD: return "Numpad +";
        case VK_SEPARATOR: return "Separator";
        case VK_SUBTRACT: return "Numpad -";
        case VK_DECIMAL: return "Numpad .";
        case VK_DIVIDE: return "Numpad /";

            // Function keys
        case VK_F1: return "F1";
        case VK_F2: return "F2";
        case VK_F3: return "F3";
        case VK_F4: return "F4";
        case VK_F5: return "F5";
        case VK_F6: return "F6";
        case VK_F7: return "F7";
        case VK_F8: return "F8";
        case VK_F9: return "F9";
        case VK_F10: return "F10";
        case VK_F11: return "F11";
        case VK_F12: return "F12";
        case VK_F13: return "F13";
        case VK_F14: return "F14";
        case VK_F15: return "F15";
        case VK_F16: return "F16";
        case VK_F17: return "F17";
        case VK_F18: return "F18";
        case VK_F19: return "F19";
        case VK_F20: return "F20";
        case VK_F21: return "F21";
        case VK_F22: return "F22";
        case VK_F23: return "F23";
        case VK_F24: return "F24";

            // Windows keys
        case VK_LWIN: return "Left Win";
        case VK_RWIN: return "Right Win";
        case VK_APPS: return "Menu/Application";

            // OEM keys
        case VK_OEM_1: return "OEM 1 (; :)";
        case VK_OEM_PLUS: return "OEM Plus (+)";
        case VK_OEM_COMMA: return "OEM Comma (,)";
        case VK_OEM_MINUS: return "OEM Minus (-)";
        case VK_OEM_PERIOD: return "OEM Period (.)";
        case VK_OEM_2: return "OEM 2 (/ ?)";
        case VK_OEM_3: return "OEM 3 (` ~)";
        case VK_OEM_4: return "OEM 4 ([ {)";
        case VK_OEM_5: return "OEM 5 (\\ |)";
        case VK_OEM_6: return "OEM 6 (] })";
        case VK_OEM_7: return "OEM 7 (' \")";
        case VK_OEM_8: return "OEM 8";
        case VK_OEM_102: return "OEM 102 (< >)";

            // Browser keys
        case VK_BROWSER_BACK: return "Browser Back";
        case VK_BROWSER_FORWARD: return "Browser Forward";
        case VK_BROWSER_REFRESH: return "Browser Refresh";
        case VK_BROWSER_STOP: return "Browser Stop";
        case VK_BROWSER_SEARCH: return "Browser Search";
        case VK_BROWSER_FAVORITES: return "Browser Favourites";
        case VK_BROWSER_HOME: return "Browser Home";

            // Media keys
        case VK_VOLUME_MUTE: return "Volume Mute";
        case VK_VOLUME_DOWN: return "Volume Down";
        case VK_VOLUME_UP: return "Volume Up";
        case VK_MEDIA_NEXT_TRACK: return "Next Track";
        case VK_MEDIA_PREV_TRACK: return "Previous Track";
        case VK_MEDIA_STOP: return "Media Stop";
        case VK_MEDIA_PLAY_PAUSE: return "Play/Pause";

            // App launch keys
        case VK_LAUNCH_MAIL: return "Mail";
        case VK_LAUNCH_MEDIA_SELECT: return "Media Select";
        case VK_LAUNCH_APP1: return "App 1";
        case VK_LAUNCH_APP2: return "App 2";
        }

        UINT scanCode = MapVirtualKeyA(vk, MAPVK_VK_TO_VSC);
        if (scanCode == 0)
            return "Unknown";

        if (GetKeyNameTextA(scanCode << 16, name, sizeof(name)) == 0)
            return "Unknown";

        return name;
    }


    inline bool KeyBindButton(const char* label, int* key)
    {
        ImGui::PushID(label);
        ImGui::PushStyleColor(ImGuiCol_Button, Listening ? ImVec4(0.9f, 0.2f, 0.2f, 1.f) : ImGui::GetStyle().Colors[ImGuiCol_Button]);

        std::string btnText = Listening ? "Press a key..." : GetKeyName(*key);
        if (ImGui::Button(btnText.c_str(), ImVec2(100, 0)))
        {
            Listening = !Listening;
            CurrentBind = *key;
        }

        ImGui::PopStyleColor();
        ImGui::PopID();

        if (Listening)
        {
            for (int i = 1; i < 256; ++i)
            {
                if (GetAsyncKeyState(i) & 0x8000)
                {
                    *key = i;
                    Listening = false;
                    return true;
                }
            }
        }

        return false;
    }

    // true for toggle, false for hold
    inline bool IsPressed(int vk, bool holytoggle)
    {
        if (vk == 0)
            return false;

        if (holytoggle)
        {

            return (GetAsyncKeyState(vk) & 0x0001) != 0;
        }
        else
        {
            return (GetAsyncKeyState(vk) & 0x8000) != 0;
        }
    }
}