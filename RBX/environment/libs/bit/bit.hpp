#pragma once

#include <cstdint>
#include <lualib.h>

namespace Bit
{
    static int funcnameinc = 0;

    int badd(lua_State* L);
    int bsub(lua_State* L);
    int bmul(lua_State* L);
    int bdiv(lua_State* L);
    int band(lua_State* L);
    int bor(lua_State* L);
    int bxor(lua_State* L);
    int bnot(lua_State* L);
    int bswap(lua_State* L);
    int ror(lua_State* L);
    int rol(lua_State* L);
    int lshift(lua_State* L);
    int rshift(lua_State* L);
    int arshift(lua_State* L);
    int tohex(lua_State* L);
    int tobit_fn(lua_State* L);

    void register_lib(lua_State* L);
}
