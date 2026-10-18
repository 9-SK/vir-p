#pragma once

#include <Windows.h>
#include <lstate.h>
#include <lobject.h>
#include <lfunc.h>
#include <lstring.h>
#include <ltable.h>
#include <lapi.h>
#include <lgc.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <string>
#include <lualib.h>

#include <lstate.h>
#include <lualib.h>
#include <string>
#include <cstdint>
#include <cstring>

#include <lstate.h>
#include <ldo.h>
#include <lfunc.h>
#include <lgc.h>
#include <lualib.h>
#include <unordered_map>
#include <unordered_set>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include <lstate.h>
#include <lualib.h>
#include <zstd.h>
#include <xxhash.h>

#include <Roblox/Shared/Shared.hpp>
#include <Roblox/Update/Offsets.hpp>
#include <Roblox/Hyperion/Execution/Execution.hpp>
#include <Roblox/Hyperion/TaskScheduler/TaskScheduler.hpp>
#include <Windows.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <string>
#include <type_traits>
#include <cstring>


namespace Metatable {
    inline int getrawmetatable(lua_State* LS) {

        if (LS->userdata == nullptr)
            return 0;

        luaL_checkany(LS, 1);

        if (!lua_getmetatable(LS, 1))
            lua_pushnil(LS);

        return 1;
    };

    int setreadonly(lua_State* L) {
        luaL_checktype(L, 1, LUA_TTABLE);
        luaL_checktype(L, 2, LUA_TBOOLEAN);
        hvalue(luaA_toobject(L, 1))->readonly = lua_toboolean(L, 2);
        return 0;
    }

    int isreadonly(lua_State* L) {
        luaL_checktype(L, 1, LUA_TTABLE);
        lua_pushboolean(L, hvalue(luaA_toobject(L, 1))->readonly);
        return 1;
    }


    int setrawmetatable(lua_State* L)
    {
        luaL_checkany(L, 1);

        const int metatableType = lua_type(L, 2);

        if (metatableType != LUA_TTABLE && metatableType != LUA_TNIL)
            luaL_typeerror(L, 2, ("table or nil"));

        lua_settop(L, 2);
        lua_setmetatable(L, 1);

        lua_pushvalue(L, 1);
        return 1;
    }

    int makereadonly(lua_State* L) {

        luaL_checktype(L, 1, LUA_TTABLE);

        LuaTable* table = hvalue(luaA_toobject(L, 1));

        table->readonly = true;

        return 0;
    }
    int makewriteable(lua_State* L) {

        luaL_checktype(L, 1, LUA_TTABLE);

        LuaTable* table = hvalue(luaA_toobject(L, 1));

        table->readonly = false;

        return 0;
    }

    inline int getnamecallmethod(lua_State* L)
    {
        const char* namecall = lua_namecallatom(L, nullptr);

        if (!namecall)
        {
            lua_pushnil(L);
            return 1;
        }

        lua_pushstring(L, namecall);
        return 1;
    }

    int hookmetamethod(lua_State* L)
    {
        luaL_checkany(L, 1);
        luaL_checktype(L, 2, LUA_TSTRING);
        luaL_checktype(L, 3, LUA_TFUNCTION);

        const char* metamethod = lua_tostring(L, 2);

        if (!lua_getmetatable(L, 1)) {
            luaL_argerror(L, 1, ("object has no metatable"));
        }
        int mt_idx = lua_gettop(L);
        bool wasReadonly = lua_getreadonly(L, mt_idx);
        if (wasReadonly) {
            lua_setreadonly(L, mt_idx, false);
        }
        lua_getfield(L, mt_idx, metamethod);
        if (lua_isnil(L, -1)) {
            lua_pop(L, 1);
            if (wasReadonly) lua_setreadonly(L, mt_idx, true);
            luaL_argerror(L, 2, ("metamethod does not exist"));
        }
        int old_method = lua_gettop(L);
        if (lua_type(L, 1) == LUA_TTABLE && lua_isfunction(L, old_method))
        {
            lua_pushcfunction(L, Closures::hookfunction, "hookfunction");
            lua_pushvalue(L, old_method);
            lua_pushvalue(L, 3);
            if (lua_pcall(L, 2, 1, 0) != LUA_OK)
            {
                std::string Error = lua_isstring(L, -1) ? lua_tostring(L, -1) : "hookfunction failed";
                lua_pop(L, 1);
                lua_pop(L, 1);
                if (wasReadonly) lua_setreadonly(L, mt_idx, true);
                lua_pop(L, 1);
                luaL_error(L, "%s", Error.c_str());
                return 0;
            }
            if (wasReadonly) lua_setreadonly(L, mt_idx, true);
            lua_remove(L, old_method);
            return 1;
        }
        lua_pushvalue(L, 3);
        lua_setfield(L, mt_idx, metamethod);

        if (wasReadonly) {
            lua_setreadonly(L, mt_idx, true);
        }
        return 1;
    }
    inline int firemetamethod(lua_State* L)
    {
        luaL_checkany(L, 1);
        luaL_checktype(L, 2, LUA_TSTRING);

        const char* metamethod = lua_tostring(L, 2);
        int originalTop = lua_gettop(L);

        if (!lua_getmetatable(L, 1)) {
            lua_pushnil(L);
            return 1;
        }

        int mt_idx = lua_gettop(L);
        lua_getfield(L, mt_idx, metamethod);

        if (!lua_isfunction(L, -1)) {
            lua_pop(L, 2);
            lua_pushnil(L);
            return 1;
        }

        lua_pushvalue(L, 1);
        for (int i = 3; i <= originalTop; i++) {
            lua_pushvalue(L, i);
        }

        int nargs = 1 + (originalTop - 2);
        if (lua_pcall(L, nargs, LUA_MULTRET, 0) != LUA_OK) {
            lua_pop(L, 1);
            lua_pushnil(L);
            return 1;
        }

        return lua_gettop(L) - mt_idx;
    }

    inline int getrawmetatable_from_class(lua_State* L)
    {
        const char* className = luaL_checkstring(L, 1);
        if (!className) {
            lua_pushnil(L);
            return 1;
        }

        lua_getglobal(L, ("game"));
        if (!lua_isuserdata(L, -1)) { lua_pop(L, 1); lua_pushnil(L); return 1; }

        lua_getfield(L, -1, ("FindFirstChildOfClass"));
        if (!lua_isfunction(L, -1)) { lua_pop(L, 2); lua_pushnil(L); return 1; }

        lua_pushvalue(L, -2);
        lua_pushstring(L, className);

        if (lua_pcall(L, 2, 1, 0) != LUA_OK || !lua_isuserdata(L, -1)) {
            lua_pop(L, 2);
            lua_pushnil(L);
            return 1;
        }

        if (!lua_getmetatable(L, -1)) {
            lua_pop(L, 2);
            lua_pushnil(L);
            return 1;
        }

        lua_remove(L, -2);
        lua_remove(L, -2);
        return 1;
    }
    int getidentity(lua_State* L)
    {

        lua_pushinteger(L, L->userdata->identity);
        return 1;
    }

    int setidentity(lua_State* L)
    {

        luaL_checktype(L, 1, LUA_TNUMBER);
        int identity = (int)lua_tointeger(L, 1);

        if (!L->userdata)
            return 0;

        uint64_t caps = 0xFFFFFFFFFFFFFF00 | Hyperion::GetCapabilities(&identity);

        if (identity == 8)
        {
            caps = MaxCapabilities;
        }

        L->userdata->identity = identity;
        L->userdata->capabilities = caps;

        const uintptr_t identity_ptr = Hyperion::GetIdentityStruct(*reinterpret_cast<uintptr_t*>(Offsets::Identity::Identity));

        if (identity_ptr) {
            *reinterpret_cast<uint64_t*>(identity_ptr) = identity;
            *reinterpret_cast<uint64_t*>(identity_ptr + Offsets::Capabilities::Capabilities) = caps;
        }

        return 0;
    }

    inline void RegisterLibrary(lua_State* L)
    {
        Utils::AddFunction(L, ("hookmetamethod"), hookmetamethod);
        Utils::AddFunction(L, ("getnamecallmethod"), getnamecallmethod);
        Utils::AddFunction(L, ("setreadonly"), setreadonly);
        Utils::AddFunction(L, ("isreadonly"), isreadonly);
        Utils::AddFunction(L, ("getrawmetatable"), getrawmetatable);
        Utils::AddFunction(L, ("setrawmetatable"), setrawmetatable);
        Utils::AddFunction(L, ("makereadonly"), makereadonly);
        Utils::AddFunction(L, ("makewriteable"), makewriteable);
        Utils::AddFunction(L, ("firemetamethod"), firemetamethod);
        Utils::AddFunction(L, "getidentity", getidentity);
        Utils::AddFunction(L, "getthreadidentity", getidentity);
        Utils::AddFunction(L, "getthreadcontext", getidentity);

        Utils::AddFunction(L, "setidentity", setidentity);
        Utils::AddFunction(L, "setthreadidentity", setidentity);
        Utils::AddFunction(L, "setthreadcontext", setidentity);
        Utils::AddFunction(L, ("getrawmetatable_from_class"), getrawmetatable_from_class);
    }
}