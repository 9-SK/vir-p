#pragma once

#include <lstate.h>
#include <lualib.h>

namespace Metatable {

    static int funcnameinc = 0;

    int getrawmetatable(lua_State* LS);
    int setreadonly(lua_State* L);
    int isreadonly(lua_State* L);
    int setrawmetatable(lua_State* L);
    int makereadonly(lua_State* L);
    int makewriteable(lua_State* L);
    int getnamecallmethod(lua_State* L);
    int hookmetamethod(lua_State* L);

    void register_lib(lua_State* L);
}
