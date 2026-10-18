#pragma once

#include <lstate.h>
#include <lualib.h>

namespace Miscellaneous {

    static int funcnameinc = 0;

    int SetIdentity(lua_State* L);
    int setthreadidentity(lua_State* L);
    int getthreadidentity(lua_State* L);
    int getgenv(lua_State* L);
    int identifyexecutor(lua_State* L);
    int getexecutorname(lua_State* L);
    int GetObjects(lua_State* L);
    int setfpscap(lua_State* L);
    int getfpscap(lua_State* L);
    int queueonteleport(lua_State* L);
    int clearteleportqueue(lua_State* L);
    int gethui(lua_State* L);
    int decompile(lua_State* L);
    int saveinstance(lua_State* L);
    void register_lib(lua_State* L);
}
