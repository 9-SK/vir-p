#pragma once

#include <string>
#include <lstate.h>
#include <lualib.h>

#include <Roblox/Shared/Shared.hpp>
#include <Roblox/Hyperion/Execution/Execution.hpp>
#include <Roblox/Hyperion/TaskScheduler/TaskScheduler.hpp>

namespace Instance
{
    int getinstances(lua_State* L)
    {
        
        luaL_trimstack(L, 0);

        lua_pushvalue(L, LUA_REGISTRYINDEX);
        lua_pushlightuserdata(L, reinterpret_cast<void*>(Hyperion::InstanceBridge::Shared));
        lua_rawget(L, -2);

        if (!lua_istable(L, -1)) { lua_pop(L, 1); lua_pushnil(L); return 1; };

        lua_newtable(L);

        int index = 0;

        lua_pushnil(L);
        while (lua_next(L, -3) != 0) {

            if (!lua_isnil(L, -1)) {
                lua_getglobal(L, "typeof");
                lua_pushvalue(L, -2);
                lua_pcall(L, 1, 1, 0);

                std::string type = lua_tostring(L, -1);
                lua_pop(L, 1);

                if (type == "Instance") {
                    lua_pushinteger(L, ++index);

                    lua_pushvalue(L, -2);
                    lua_settable(L, -5);
                }
            }

            lua_pop(L, 1);
        }

        lua_remove(L, -2);

        return 1;
    }

    int getnilinstances(lua_State* L)
    {
        
        luaL_trimstack(L, 0);

        lua_pushvalue(L, LUA_REGISTRYINDEX);
        lua_pushlightuserdata(L, reinterpret_cast<void*>(Hyperion::InstanceBridge::Shared));
        lua_rawget(L, -2);

        if (!lua_istable(L, -1)) { lua_pop(L, 1); lua_pushnil(L); return 1; };

        lua_newtable(L);

        int index = 0;

        lua_pushnil(L);
        while (lua_next(L, -3) != 0) {

            if (!lua_isnil(L, -1)) {
                lua_getglobal(L, "typeof");
                lua_pushvalue(L, -2);
                lua_pcall(L, 1, 1, 0);

                std::string type = lua_tostring(L, -1);
                lua_pop(L, 1);

                if (type == "Instance") {
                    lua_getfield(L, -1, "Parent");
                    int parentType = lua_type(L, -1);
                    lua_pop(L, 1);

                    if (parentType == LUA_TNIL) {
                        lua_pushinteger(L, ++index);

                        lua_pushvalue(L, -2);
                        lua_settable(L, -5);
                    }
                }
            }

            lua_pop(L, 1);
        }

        lua_remove(L, -2);

        return 1;
    }

    int gethui(lua_State* L)
    {
        
        lua_getfield(L, LUA_GLOBALSINDEX, "game");
        lua_getfield(L, -1, "CoreGui");
        lua_getfield(L, -1, "RobloxGui");
        return 1;
    }

    void RegisterLibrary(lua_State* L)
    {
        
        Utils::AddFunction(L, "getinstances", getinstances);
        Utils::AddFunction(L, "getnilinstances", getnilinstances);

        Utils::AddFunction(L, ("gethui"), gethui);
        Utils::AddFunction(L, ("gethiddenui"), gethui);
        Utils::AddFunction(L, ("gethiddenuserinterface"), gethui);
        Utils::AddFunction(L, ("get_hidden_gui"), gethui);
    }
}
