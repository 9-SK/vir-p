#pragma once

#include <Windows.h>
#include <lgc.h>
#include <lstate.h>
#include <lualib.h>
#include <string>
#include <vector>

#include <Roblox/Shared/Shared.hpp>
#include <ltable.h>

namespace Input
{
    inline HWND roblox_win = FindWindowA(nullptr, "Roblox");

    inline bool is_roblox_active()
    {
        return GetForegroundWindow() == roblox_win;
    }

    inline int isrbxactive(lua_State* L)
    {
        luaL_trimstack(L, 0);
        lua_pushboolean(L, is_roblox_active());
        return 1;
    }

    inline int keypress(lua_State* L)
    {
        luaL_checktype(L, 1, LUA_TNUMBER);
        UINT key = lua_tointeger(L, 1);

        if (is_roblox_active())
            keybd_event(0, (BYTE)MapVirtualKeyA(key, MAPVK_VK_TO_VSC), KEYEVENTF_SCANCODE, 0);

        return 0;
    }

    inline int keytap(lua_State* L)
    {
        luaL_checktype(L, 1, LUA_TNUMBER);
        UINT key = lua_tointeger(L, 1);

        if (!is_roblox_active())
            return 0;

        keybd_event(0, (BYTE)MapVirtualKeyA(key, MAPVK_VK_TO_VSC), KEYEVENTF_SCANCODE, 0);
        keybd_event(0, (BYTE)MapVirtualKeyA(key, MAPVK_VK_TO_VSC), KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP, 0);

        return 0;
    }

    inline int keyrelease(lua_State* L)
    {
        luaL_checktype(L, 1, LUA_TNUMBER);
        UINT key = lua_tointeger(L, 1);

        if (is_roblox_active())
            keybd_event(0, (BYTE)MapVirtualKeyA(key, MAPVK_VK_TO_VSC), KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP, 0);

        return 0;
    }

    inline int mouse1click(lua_State* L)
    {
        if (is_roblox_active())
            mouse_event(MOUSEEVENTF_LEFTDOWN | MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);

        return 0;
    }

    inline int mouse1press(lua_State* L)
    {
        if (is_roblox_active())
            mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);

        return 0;
    }

    inline int mouse1release(lua_State* L)
    {
        if (is_roblox_active())
            mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);

        return 0;
    }

    inline int mouse2click(lua_State* L)
    {
        if (is_roblox_active())
            mouse_event(MOUSEEVENTF_RIGHTDOWN | MOUSEEVENTF_RIGHTUP, 0, 0, 0, 0);

        return 0;
    }

    inline int mouse2press(lua_State* L)
    {
        if (is_roblox_active())
            mouse_event(MOUSEEVENTF_RIGHTDOWN, 0, 0, 0, 0);

        return 0;
    }

    inline int mouse2release(lua_State* L)
    {
        if (is_roblox_active())
            mouse_event(MOUSEEVENTF_RIGHTUP, 0, 0, 0, 0);

        return 0;
    }

    inline int mousemoveabs(lua_State* L)
    {
        luaL_checktype(L, 1, LUA_TNUMBER);
        luaL_checktype(L, 2, LUA_TNUMBER);

        int x = lua_tointeger(L, 1);
        int y = lua_tointeger(L, 2);

        if (!is_roblox_active())
            return 0;

        int width = GetSystemMetrics(SM_CXSCREEN) - 1;
        int height = GetSystemMetrics(SM_CYSCREEN) - 1;

        RECT CRect;
        GetClientRect(GetForegroundWindow(), &CRect);

        POINT Point{ CRect.left, CRect.top };
        ClientToScreen(GetForegroundWindow(), &Point);

        x = (x + Point.x) * (65535 / width);
        y = (y + Point.y) * (65535 / height);

        mouse_event(MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_MOVE, x, y, 0, 0);
        return 0;
    }

    inline int mousemoverel(lua_State* L)
    {
        luaL_checktype(L, 1, LUA_TNUMBER);
        luaL_checktype(L, 2, LUA_TNUMBER);

        int x = lua_tointeger(L, 1);
        int y = lua_tointeger(L, 2);

        if (is_roblox_active())
            mouse_event(MOUSEEVENTF_MOVE, x, y, 0, 0);

        return 0;
    }

    inline int mousescroll(lua_State* L)
    {
        luaL_checktype(L, 1, LUA_TNUMBER);

        int amt = lua_tointeger(L, 1);

        if (is_roblox_active())
            mouse_event(MOUSEEVENTF_WHEEL, 0, 0, amt, 0);

        return 0;
    }

    inline int messagebox(lua_State* L)
    {
        const char* text = luaL_checkstring(L, 1);
        const char* caption = luaL_checkstring(L, 2);
        int flags = luaL_optinteger(L, 3, MB_OK);

        int result = MessageBoxA(nullptr, text, caption, flags);
        lua_pushinteger(L, result);
        return 1;
    }

    inline int setclipboard(lua_State* L)
    {
        if (!lua_isstring(L, 1)) {
            return 0;
        }

        const char* text = lua_tostring(L, 1);
        if (!text) return 0;

        size_t len = strlen(text);
        if (len == 0) return 0;

        if (!OpenClipboard(nullptr)) {
            return 0;
        }

        EmptyClipboard();

        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, len + 1);
        if (hMem) {
            char* pMem = (char*)GlobalLock(hMem);
            if (pMem) {
                for (size_t i = 0; i <= len; i++) {
                    pMem[i] = text[i];
                }
                GlobalUnlock(hMem);
                SetClipboardData(CF_TEXT, hMem);
            }
            else {
                GlobalFree(hMem);
            }
        }

        CloseClipboard();
        return 0;
    }

    inline void RegisterLibrary(lua_State* L)
    {
        Utils::AddFunction(L, "isrbxactive", isrbxactive);
        Utils::AddFunction(L, "isgameactive", isrbxactive);
        Utils::AddFunction(L, "iswindowactive", isrbxactive);

        Utils::AddFunction(L, "keypress", keypress);
        Utils::AddFunction(L, "keytap", keytap);
        Utils::AddFunction(L, "keyclick", keytap);
        Utils::AddFunction(L, "keyrelease", keyrelease);
        Utils::AddFunction(L, "mouse1click", mouse1click);
        Utils::AddFunction(L, "mouse1press", mouse1press);
        Utils::AddFunction(L, "mouse1release", mouse1release);
        Utils::AddFunction(L, "mouse2click", mouse2click);
        Utils::AddFunction(L, "mouse2press", mouse2press);
        Utils::AddFunction(L, "mouse2release", mouse2release);
        Utils::AddFunction(L, "mousemoveabs", mousemoveabs);
        Utils::AddFunction(L, "mousemoverel", mousemoverel);
        Utils::AddFunction(L, "mousescroll", mousescroll);
        Utils::AddFunction(L, "messagebox", messagebox);
        Utils::AddFunction(L, "setclipboard", setclipboard);
        Utils::AddFunction(L, "toclipboard", setclipboard);
        Utils::AddFunction(L, "setrbxclipboard", setclipboard);
    }
}
