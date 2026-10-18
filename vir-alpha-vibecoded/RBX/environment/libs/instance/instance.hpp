#pragma once

#define lua_torawuserdata(L, idx) lua_touserdata(L, idx)

#include <lstate.h>

inline void luaL_trimstack(lua_State* L, int n) {
    lua_settop(L, n);
}

namespace Instance
{
    static int funcnameinc = 0;

    int getinstances(lua_State* L);
    int getnilinstances(lua_State* L);
    int isnetworkowner(lua_State* L);
    int getspecialinfo(lua_State* L);
    int getthreads(lua_State* L);
    int getproperties(lua_State* L);
    int getsimulationradius(lua_State* L);
    int setsimulationradius(lua_State* L);
    int getinstancecache(lua_State* L);

    void register_lib(lua_State* L);
}
