#pragma once

#include <string>
#include <lstate.h>
#include <lualib.h>
#include <filesystem>
#include <Windows.h>

namespace Filesystem
{
    static int funcnameinc = 0;

    int readfile(lua_State* L);
    int writefile(lua_State* L);
    int appendfile(lua_State* L);
    int listfiles(lua_State* L);
    int isfile(lua_State* L);
    int isfolder(lua_State* L);
    int makefolder(lua_State* L);
    int delfile(lua_State* L);
    int delfolder(lua_State* L);
    int loadfile(lua_State* L);
    int dofile(lua_State* L);
    int getcustomasset(lua_State* L);

    void register_lib(lua_State* L);
}
