#pragma once

#include <lstate.h>

namespace Input
{
    static int funcnameinc = 0;

    int setclipboard(lua_State* L);
    int getclipboard(lua_State* L);
    int messagebox(lua_State* L);
    int isrbxactive(lua_State* L);
    int iswindowactive(lua_State* L);
    int keypress(lua_State* L);
    int keyrelease(lua_State* L);
    int mouse1click(lua_State* L);
    int mouse1press(lua_State* L);
    int mouse1release(lua_State* L);
    int mouse2click(lua_State* L);
    int mouse2press(lua_State* L);
    int mouse2release(lua_State* L);
    int mousemoverel(lua_State* L);
    int mousemoveabs(lua_State* L);
    int mousescroll(lua_State* L);
    int keyclick(lua_State* L);
    int gethwid(lua_State* L);

    void register_lib(lua_State* L);
}
