#include "Input.hpp"

#include <Windows.h>
#include <string>

#include <RBX/environment/env.hpp>


int Input::setclipboard(lua_State* L)
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

int Input::getclipboard(lua_State* L)
{
    if (!OpenClipboard(nullptr)) {
        lua_pushstring(L, (""));
        return 1;
    }

    HANDLE hData = GetClipboardData(CF_TEXT);
    if (hData) {
        char* pszText = static_cast<char*>(GlobalLock(hData));
        if (pszText) {
            lua_pushstring(L, pszText);
            GlobalUnlock(hData);
            CloseClipboard();
            return 1;
        }
    }

    CloseClipboard();
    lua_pushstring(L, (""));
    return 1;
}

int Input::messagebox(lua_State* L)
{
    const char* text = luaL_checkstring(L, 1);
    const char* caption = luaL_checkstring(L, 2);
    int flags = luaL_optinteger(L, 3, MB_OK);

    // Owned by the game window (foreground at call time): modal to the game,
    // returns focus correctly on dismiss. Explicit Lua intent, so showing
    // here is allowed; flags are passed through unchanged (no new TOPMOST).
    HWND owner = GetForegroundWindow();
    int result = MessageBoxA(owner, text, caption, flags);
    lua_pushinteger(L, result);
    return 1;
}

int Input::isrbxactive(lua_State* L)
{
    HWND fg = GetForegroundWindow();
    HWND rbx = FindWindowA(nullptr, ("Roblox"));

    if (!rbx) {
        rbx = FindWindowA(("WINDOWSCLIENT"), nullptr);
    }

    bool active = (fg == rbx);
    lua_pushboolean(L, active ? 1 : 0);
    return 1;
}

int Input::iswindowactive(lua_State* L)
{
    HWND hwnd = GetForegroundWindow();
    DWORD pid;
    GetWindowThreadProcessId(hwnd, &pid);
    lua_pushboolean(L, pid == GetCurrentProcessId() ? 1 : 0);
    return 1;
}

int Input::keypress(lua_State* L)
{
    int key = luaL_checkinteger(L, 1);
    if (key < 0 || key > 255) {
        return 0;
    }

    keybd_event(static_cast<BYTE>(key), 0, 0, 0);
    return 0;
}

int Input::keyrelease(lua_State* L)
{
    int key = luaL_checkinteger(L, 1);
    if (key < 0 || key > 255) {
        return 0;
    }

    keybd_event(static_cast<BYTE>(key), 0, KEYEVENTF_KEYUP, 0);
    return 0;
}

int Input::mouse1click(lua_State* L)
{
    mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
    Sleep(10);
    mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
    return 0;
}

int Input::mouse1press(lua_State* L)
{
    mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
    return 0;
}

int Input::mouse1release(lua_State* L)
{
    mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
    return 0;
}

int Input::mouse2click(lua_State* L)
{
    mouse_event(MOUSEEVENTF_RIGHTDOWN, 0, 0, 0, 0);
    Sleep(10);
    mouse_event(MOUSEEVENTF_RIGHTUP, 0, 0, 0, 0);
    return 0;
}

int Input::mouse2press(lua_State* L)
{
    mouse_event(MOUSEEVENTF_RIGHTDOWN, 0, 0, 0, 0);
    return 0;
}

int Input::mouse2release(lua_State* L)
{
    mouse_event(MOUSEEVENTF_RIGHTUP, 0, 0, 0, 0);
    return 0;
}

int Input::mousemoverel(lua_State* L)
{
    int x = luaL_checkinteger(L, 1);
    int y = luaL_checkinteger(L, 2);

    mouse_event(MOUSEEVENTF_MOVE, x, y, 0, 0);
    return 0;
}

int Input::mousemoveabs(lua_State* L)
{
    int x = luaL_checkinteger(L, 1);
    int y = luaL_checkinteger(L, 2);

    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);

    int absX = (x * 65535) / screenWidth;
    int absY = (y * 65535) / screenHeight;

    mouse_event(MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_MOVE, absX, absY, 0, 0);
    return 0;
}

int Input::mousescroll(lua_State* L)
{
    int delta = luaL_checkinteger(L, 1);
    mouse_event(MOUSEEVENTF_WHEEL, 0, 0, delta, 0);
    return 0;
}

int Input::keyclick(lua_State* L)
{
    int key = luaL_checkinteger(L, 1);
    keybd_event((BYTE)key, 0, 0, 0);
    Sleep(10);
    keybd_event((BYTE)key, 0, KEYEVENTF_KEYUP, 0);
    return 0;
}

int Input::gethwid(lua_State* L)
{
    HW_PROFILE_INFOA hwProfileInfo;
    if (GetCurrentHwProfileA(&hwProfileInfo)) {
        lua_pushstring(L, hwProfileInfo.szHwProfileGuid);
        return 1;
    }
    return 0;
}

void Input::register_lib(lua_State* L)
{
    if (!L) return;

    Environment->make_global(L, ("setclipboard"), Input::setclipboard, Input::funcnameinc++);
    Environment->make_global(L, ("setrbxclipboard"), Input::setclipboard, Input::funcnameinc++);
    Environment->make_global(L, ("getclipboard"), Input::getclipboard, Input::funcnameinc++);
    Environment->make_global(L, ("toclipboard"), Input::setclipboard, Input::funcnameinc++);

    Environment->make_global(L, ("messagebox"), Input::messagebox, Input::funcnameinc++);

    Environment->make_global(L, ("gethwid"), Input::gethwid, Input::funcnameinc++);
    Environment->make_global(L, ("isrbxactive"), Input::isrbxactive, Input::funcnameinc++);
    Environment->make_global(L, ("isgameactive"), Input::isrbxactive, Input::funcnameinc++);
    Environment->make_global(L, ("iswindowactive"), Input::iswindowactive, Input::funcnameinc++);

    Environment->make_global(L, ("keypress"), Input::keypress, Input::funcnameinc++);
    Environment->make_global(L, ("keyclick"), Input::keyclick, Input::funcnameinc++);
    Environment->make_global(L, ("keytap"), Input::keyclick, Input::funcnameinc++);
    Environment->make_global(L, ("keyrelease"), Input::keyrelease, Input::funcnameinc++);

    Environment->make_global(L, ("mouse1click"), Input::mouse1click, Input::funcnameinc++);
    Environment->make_global(L, ("mouse1press"), Input::mouse1press, Input::funcnameinc++);
    Environment->make_global(L, ("mouse1release"), Input::mouse1release, Input::funcnameinc++);
    Environment->make_global(L, ("mouse2click"), Input::mouse2click, Input::funcnameinc++);
    Environment->make_global(L, ("mouse2press"), Input::mouse2press, Input::funcnameinc++);
    Environment->make_global(L, ("mouse2release"), Input::mouse2release, Input::funcnameinc++);
    Environment->make_global(L, ("mousemoverel"), Input::mousemoverel, Input::funcnameinc++);
    Environment->make_global(L, ("mousemoveabs"), Input::mousemoveabs, Input::funcnameinc++);
    Environment->make_global(L, ("mousescroll"), Input::mousescroll, Input::funcnameinc++);
}
