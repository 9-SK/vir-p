#include "Bit.hpp"

#include <cstdio>
#include <cstdint>
#include <lualib.h>

#include <RBX/environment/env.hpp>


namespace Bit
{
    static inline uint32_t tobit(double x)
    {
        return static_cast<uint32_t>(static_cast<int64_t>(x) & 0xFFFFFFFF);
    }

    static inline double frombit(uint32_t x)
    {
        return static_cast<double>(x);
    }

    static uint32_t checkuint(lua_State* L, int idx)
    {
        return tobit(luaL_checknumber(L, idx));
    }

    template<typename Fn>
    static int variadic_op(lua_State* L, Fn fn)
    {
        int n = lua_gettop(L);
        if (n == 0) { lua_pushnumber(L, 0); return 1; }

        uint32_t result = 0;
        bool first = true;
        for (int i = 1; i <= n; i++)
        {
            if (!lua_isnumber(L, i)) continue;
            uint32_t val = tobit(lua_tonumber(L, i));
            if (first) { result = val; first = false; }
            else { result = fn(result, val); }
        }
        lua_pushnumber(L, frombit(result));
        return 1;
    }

    static uint32_t badd_fn(uint32_t a, uint32_t b) { return a + b; }
    static uint32_t bsub_fn(uint32_t a, uint32_t b) { return a - b; }
    static uint32_t bmul_fn(uint32_t a, uint32_t b) { return a * b; }
    static uint32_t bdiv_fn(uint32_t a, uint32_t b) { return b != 0 ? a / b : 0; }
    static uint32_t band_fn(uint32_t a, uint32_t b) { return a & b; }
    static uint32_t bor_fn(uint32_t a, uint32_t b) { return a | b; }
    static uint32_t bxor_fn(uint32_t a, uint32_t b) { return a ^ b; }

    int badd(lua_State* L) { return variadic_op(L, badd_fn); }
    int bsub(lua_State* L) { return variadic_op(L, bsub_fn); }
    int bmul(lua_State* L) { return variadic_op(L, bmul_fn); }
    int bdiv(lua_State* L) { return variadic_op(L, bdiv_fn); }
    int band(lua_State* L) { return variadic_op(L, band_fn); }
    int bor(lua_State* L) { return variadic_op(L, bor_fn); }
    int bxor(lua_State* L) { return variadic_op(L, bxor_fn); }

    int bnot(lua_State* L)
    {
        uint32_t val = checkuint(L, 1);
        lua_pushnumber(L, frombit(~val));
        return 1;
    }

    int bswap(lua_State* L)
    {
        uint32_t val = checkuint(L, 1);
        uint32_t result = ((val & 0xFF000000) >> 24) |
                          ((val & 0x00FF0000) >> 8)  |
                          ((val & 0x0000FF00) << 8)  |
                          ((val & 0x000000FF) << 24);
        lua_pushnumber(L, frombit(result));
        return 1;
    }

    int ror(lua_State* L)
    {
        uint32_t val = checkuint(L, 1);
        int count = static_cast<int>(luaL_checknumber(L, 2)) & 31;
        uint32_t result = (val >> count) | (val << (32 - count));
        lua_pushnumber(L, frombit(result));
        return 1;
    }

    int rol(lua_State* L)
    {
        uint32_t val = checkuint(L, 1);
        int count = static_cast<int>(luaL_checknumber(L, 2)) & 31;
        uint32_t result = (val << count) | (val >> (32 - count));
        lua_pushnumber(L, frombit(result));
        return 1;
    }

    int lshift(lua_State* L)
    {
        uint32_t val = checkuint(L, 1);
        int count = static_cast<int>(luaL_checknumber(L, 2)) & 31;
        lua_pushnumber(L, frombit(val << count));
        return 1;
    }

    int rshift(lua_State* L)
    {
        uint32_t val = checkuint(L, 1);
        int count = static_cast<int>(luaL_checknumber(L, 2)) & 31;
        lua_pushnumber(L, frombit(val >> count));
        return 1;
    }

    int arshift(lua_State* L)
    {
        int32_t val = static_cast<int32_t>(checkuint(L, 1));
        int count = static_cast<int>(luaL_checknumber(L, 2)) & 31;
        lua_pushnumber(L, frombit(static_cast<uint32_t>(val >> count)));
        return 1;
    }

    int tohex(lua_State* L)
    {
        uint32_t val = checkuint(L, 1);
        char buf[16];
        snprintf(buf, sizeof(buf), ("%08x"), val);
        lua_pushstring(L, buf);
        return 1;
    }

    int tobit_fn(lua_State* L)
    {
        double num = luaL_checknumber(L, 1);
        lua_pushnumber(L, frombit(tobit(num)));
        return 1;
    }

    void register_lib(lua_State* L)
    {
        lua_newtable(L);

        Environment->make_table_function(L, ("badd"), badd, funcnameinc++);
        Environment->make_table_function(L, ("bsub"), bsub, funcnameinc++);
        Environment->make_table_function(L, ("bmul"), bmul, funcnameinc++);
        Environment->make_table_function(L, ("bdiv"), bdiv, funcnameinc++);
        Environment->make_table_function(L, ("band"), band, funcnameinc++);
        Environment->make_table_function(L, ("bor"), bor, funcnameinc++);
        Environment->make_table_function(L, ("bxor"), bxor, funcnameinc++);
        Environment->make_table_function(L, ("bnot"), bnot, funcnameinc++);
        Environment->make_table_function(L, ("bswap"), bswap, funcnameinc++);
        Environment->make_table_function(L, ("ror"), ror, funcnameinc++);
        Environment->make_table_function(L, ("rol"), rol, funcnameinc++);
        Environment->make_table_function(L, ("lshift"), lshift, funcnameinc++);
        Environment->make_table_function(L, ("rshift"), rshift, funcnameinc++);
        Environment->make_table_function(L, ("arshift"), arshift, funcnameinc++);
        Environment->make_table_function(L, ("tohex"), tohex, funcnameinc++);
        Environment->make_table_function(L, ("tobit"), tobit_fn, funcnameinc++);

        lua_setglobal(L, ("bit"));
    }
}
