#pragma once

#include <lualib.h>

namespace Cache
{
    static int funcnameinc = 0;

    int invalidate(lua_State* L);
    int replace(lua_State* L);
    int iscached(lua_State* L);
    int getreg(lua_State* L);
    int compareinstances(lua_State* L);

    void register_lib(lua_State* L);
}
