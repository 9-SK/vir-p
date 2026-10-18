#pragma once

#include <lstate.h>

namespace Http
{
    static int funcnameinc = 0;

    int HttpGet(lua_State* L);
    int request(lua_State* L);
    int HttpPost(lua_State* L);
    int HttpPostAsync(lua_State* L);

    void register_lib(lua_State* L);
}
