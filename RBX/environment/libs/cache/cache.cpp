#include "Cache.hpp"

#include <lstate.h>
#include <lualib.h>
#include <lgc.h>
#include <lapi.h>

#include <RBX/environment/env.hpp>
#include <Utilitys/Utils.hpp>
#include <Utilitys/Globals.hpp>

namespace Cache
{
    static inline void luaL_trimstack(lua_State* L, int n) {
        lua_settop(L, n);
    }

    int invalidate(lua_State* L)
    {
        luaL_checktype(L, 1, LUA_TUSERDATA);

        const auto rawUserdata = *static_cast<void**>(lua_touserdata(L, 1));

        lua_pushlightuserdata(L, (void*)Roblox::PushInstance);
        lua_gettable(L, LUA_REGISTRYINDEX);

        lua_pushlightuserdata(L, reinterpret_cast<void*>(rawUserdata));
        lua_pushnil(L);
        lua_settable(L, -3);

        return 0;
    }

    int replace(lua_State* L)
    {
        luaL_checktype(L, 1, LUA_TUSERDATA);
        luaL_checktype(L, 2, LUA_TUSERDATA);

        const auto rawUserdata = *static_cast<void**>(lua_touserdata(L, 1));

        lua_pushlightuserdata(L, (void*)Roblox::PushInstance);
        lua_gettable(L, LUA_REGISTRYINDEX);

        lua_pushlightuserdata(L, rawUserdata);
        lua_pushvalue(L, 2);
        lua_settable(L, -3);

        return 0;
    }

    int iscached(lua_State* L)
    {
        luaL_checktype(L, 1, LUA_TUSERDATA);

        const auto rawUserdata = *static_cast<void**>(lua_touserdata(L, 1));

        lua_pushlightuserdata(L, (void*)Roblox::PushInstance);
        lua_gettable(L, LUA_REGISTRYINDEX);

        lua_pushlightuserdata(L, rawUserdata);
        lua_gettable(L, -2);

        lua_pushboolean(L, lua_type(L, -1) != LUA_TNIL);

        return 1;
    }

    int getreg(lua_State* L) {
        Cache::luaL_trimstack(L, (int)0);

        lua_rawcheckstack(L, 1);
        luaC_threadbarrier(L);

        lua_pushvalue(L, LUA_REGISTRYINDEX);
        return 1;
    }

    int compareinstances(lua_State* L)
    {
        luaL_checktype(L, 1, LUA_TUSERDATA);
        luaL_checktype(L, 2, LUA_TUSERDATA);

        uintptr_t instance_one = *reinterpret_cast<uintptr_t*>(lua_touserdata(L, 1));
        uintptr_t instance_two = *reinterpret_cast<uintptr_t*>(lua_touserdata(L, 2));

        lua_pushboolean(L, instance_one == instance_two);

        return 1;
    }

    void register_lib(lua_State* L)
    {
        if (!L) return;

        lua_newtable(L);

        Environment->make_table_function(L, ("invalidate"), invalidate, funcnameinc++);
        Environment->make_table_function(L, ("iscached"), iscached, funcnameinc++);
        Environment->make_table_function(L, ("replace"), replace, funcnameinc++);

        lua_setglobal(L, ("cache"));

        Environment->make_global(L, ("getreg"), getreg, funcnameinc++);
        Environment->make_global(L, ("compareinstances"), compareinstances, funcnameinc++);
    }
}
