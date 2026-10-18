#pragma once

#include <cstdio>
#include <cstdint>

#include <lstate.h>
#include <lualib.h>

#include <Roblox/Shared/Shared.hpp>

namespace Bit
{
    namespace
    {
        inline uint32_t tobit(double x)
        {
            return static_cast<uint32_t>(static_cast<int64_t>(x) & 0xFFFFFFFF);
        }

        inline double frombit(uint32_t x)
        {
            return static_cast<double>(x);
        }

        inline uint32_t checkuint(lua_State* L, int idx)
        {
            return tobit(luaL_checknumber(L, idx));
        }

        template<typename Fn>
        inline int variadic_op(lua_State* L, Fn fn)
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

        inline uint32_t badd_fn(uint32_t a, uint32_t b) { return a + b; }
        inline uint32_t bsub_fn(uint32_t a, uint32_t b) { return a - b; }
        inline uint32_t bmul_fn(uint32_t a, uint32_t b) { return a * b; }
        inline uint32_t bdiv_fn(uint32_t a, uint32_t b) { return b != 0 ? a / b : 0; }
        inline uint32_t band_fn(uint32_t a, uint32_t b) { return a & b; }
        inline uint32_t bor_fn(uint32_t a, uint32_t b) { return a | b; }
        inline uint32_t bxor_fn(uint32_t a, uint32_t b) { return a ^ b; }
    }

    inline int badd(lua_State* L) { return variadic_op(L, badd_fn); }
    inline int bsub(lua_State* L) { return variadic_op(L, bsub_fn); }
    inline int bmul(lua_State* L) { return variadic_op(L, bmul_fn); }
    inline int bdiv(lua_State* L) { return variadic_op(L, bdiv_fn); }
    inline int band(lua_State* L) { return variadic_op(L, band_fn); }
    inline int bor(lua_State* L) { return variadic_op(L, bor_fn); }
    inline int bxor(lua_State* L) { return variadic_op(L, bxor_fn); }

    inline int bnot(lua_State* L)
    {
        uint32_t val = checkuint(L, 1);
        lua_pushnumber(L, frombit(~val));
        return 1;
    }

    inline int bswap(lua_State* L)
    {
        uint32_t val = checkuint(L, 1);
        uint32_t result = ((val & 0xFF000000) >> 24) |
            ((val & 0x00FF0000) >> 8) |
            ((val & 0x0000FF00) << 8) |
            ((val & 0x000000FF) << 24);
        lua_pushnumber(L, frombit(result));
        return 1;
    }

    inline int ror(lua_State* L)
    {
        uint32_t val = checkuint(L, 1);
        int count = static_cast<int>(luaL_checknumber(L, 2)) & 31;
        uint32_t result = count ? ((val >> count) | (val << (32 - count))) : val;
        lua_pushnumber(L, frombit(result));
        return 1;
    }

    inline int rol(lua_State* L)
    {
        uint32_t val = checkuint(L, 1);
        int count = static_cast<int>(luaL_checknumber(L, 2)) & 31;
        uint32_t result = count ? ((val << count) | (val >> (32 - count))) : val;
        lua_pushnumber(L, frombit(result));
        return 1;
    }

    inline int lshift(lua_State* L)
    {
        uint32_t val = checkuint(L, 1);
        int count = static_cast<int>(luaL_checknumber(L, 2)) & 31;
        lua_pushnumber(L, frombit(val << count));
        return 1;
    }

    inline int rshift(lua_State* L)
    {
        uint32_t val = checkuint(L, 1);
        int count = static_cast<int>(luaL_checknumber(L, 2)) & 31;
        lua_pushnumber(L, frombit(val >> count));
        return 1;
    }

    inline int arshift(lua_State* L)
    {
        int32_t val = static_cast<int32_t>(checkuint(L, 1));
        int count = static_cast<int>(luaL_checknumber(L, 2)) & 31;
        lua_pushnumber(L, frombit(static_cast<uint32_t>(val >> count)));
        return 1;
    }

    inline int tohex(lua_State* L)
    {
        uint32_t val = checkuint(L, 1);
        char buf[16];
        snprintf(buf, sizeof(buf), ("%08x"), val);
        lua_pushstring(L, buf);
        return 1;
    }

    inline int tobit_fn(lua_State* L)
    {
        double num = luaL_checknumber(L, 1);
        lua_pushnumber(L, frombit(tobit(num)));
        return 1;
    }

    inline void RegisterLibrary(lua_State* L)
    {
        lua_newtable(L);

        Utils::AddTableFunction(L, ("badd"), Bit::badd);
        Utils::AddTableFunction(L, ("bsub"), Bit::bsub);
        Utils::AddTableFunction(L, ("bmul"), Bit::bmul);
        Utils::AddTableFunction(L, ("bdiv"), Bit::bdiv);
        Utils::AddTableFunction(L, ("band"), Bit::band);
        Utils::AddTableFunction(L, ("bor"), Bit::bor);
        Utils::AddTableFunction(L, ("bxor"), Bit::bxor);
        Utils::AddTableFunction(L, ("bnot"), Bit::bnot);
        Utils::AddTableFunction(L, ("bswap"), Bit::bswap);
        Utils::AddTableFunction(L, ("ror"), Bit::ror);
        Utils::AddTableFunction(L, ("rol"), Bit::rol);
        Utils::AddTableFunction(L, ("lshift"), Bit::lshift);
        Utils::AddTableFunction(L, ("rshift"), Bit::rshift);
        Utils::AddTableFunction(L, ("arshift"), Bit::arshift);
        Utils::AddTableFunction(L, ("tohex"), Bit::tohex);
        Utils::AddTableFunction(L, ("tobit"), Bit::tobit_fn);

        lua_setglobal(L, ("bit"));
    }
}
