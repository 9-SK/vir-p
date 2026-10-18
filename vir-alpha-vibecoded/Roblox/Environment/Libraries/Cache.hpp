#pragma once

#include <string>
#include <lstate.h>
#include <lualib.h>

#include <Roblox/Shared/Shared.hpp>
#include <Roblox/Hyperion/Execution/Execution.hpp>
#include <Roblox/Hyperion/TaskScheduler/TaskScheduler.hpp>

namespace HelpFunc {
	void ValidateInstance(lua_State* L, int idx)
	{
		const char* type_name = luaL_typename(L, idx);
		if (!type_name || strcmp(type_name, "Instance") != 0)
		{
			luaL_typeerrorL(L, idx, "Instance");
		}
	}
}

namespace Cache
{
	int invalidate(lua_State* L)
	{
		
		luaL_checktype(L, 1, LUA_TUSERDATA);
		HelpFunc::ValidateInstance(L, 1);

		void* instance_ptr = *static_cast<void**>(lua_touserdata(L, 1));

		lua_pushlightuserdata(L, (void*)Hyperion::InstanceBridge::Shared);
		lua_gettable(L, LUA_REGISTRYINDEX);

		lua_pushlightuserdata(L, instance_ptr);
		lua_pushnil(L);
		lua_settable(L, -3);

		lua_pop(L, 1);
		return 0;
	}

	int replace(lua_State* L)
	{
		
		luaL_checktype(L, 1, LUA_TUSERDATA);
		luaL_checktype(L, 2, LUA_TUSERDATA);

		HelpFunc::ValidateInstance(L, 1);
		HelpFunc::ValidateInstance(L, 2);

		void* old_instance_ptr = *static_cast<void**>(lua_touserdata(L, 1));

		lua_pushlightuserdata(L, (void*)Hyperion::InstanceBridge::Shared);
		lua_gettable(L, LUA_REGISTRYINDEX);

		lua_pushlightuserdata(L, old_instance_ptr);
		lua_pushvalue(L, 2);
		lua_settable(L, -3);

		lua_pop(L, 1);
		return 0;
	}

	int iscached(lua_State* L)
	{
		
		luaL_checktype(L, 1, LUA_TUSERDATA);
		HelpFunc::ValidateInstance(L, 1);

		void* instance_ptr = *static_cast<void**>(lua_touserdata(L, 1));

		lua_pushlightuserdata(L, (void*)Hyperion::InstanceBridge::Shared);
		lua_gettable(L, LUA_REGISTRYINDEX);

		lua_pushlightuserdata(L, instance_ptr);
		lua_gettable(L, -2);

		bool cached = !lua_isnil(L, -1);
		lua_pop(L, 2);

		lua_pushboolean(L, cached);
		return 1;
	}

    int validate(lua_State* L)
	{
		
		luaL_checktype(L, 1, LUA_TUSERDATA);

		void** userdata = static_cast<void**>(lua_touserdata(L, 1));
		if (!userdata || !*userdata) {
			lua_pushboolean(L, 0);
			return 1;
		}

		void* raw_userdata = *userdata;

		lua_pushlightuserdata(L, (void*)Hyperion::InstanceBridge::Shared);
		lua_rawget(L, LUA_REGISTRYINDEX);

		lua_pushlightuserdata(L, raw_userdata);
		lua_rawget(L, -2);

		bool already_cached = lua_type(L, -1) != LUA_TNIL;
		lua_pop(L, 1);

		if (!already_cached) {
			lua_pushlightuserdata(L, raw_userdata);
			lua_pushvalue(L, 1);
			lua_rawset(L, -3);
		}

		lua_pop(L, 1);

		lua_pushboolean(L, 1);
		return 1;
	}

    int cloneref(lua_State* L)
	{
		
		lua_normalisestack(L, 1);
		luaL_checktype(L, 1, LUA_TUSERDATA);

		std::string ud_t = luaL_typename(L, 1);
		if (ud_t != ("Instance"))
			luaL_typeerrorL(L, 1, ("Instance"));

		const auto instance = *static_cast<void**>(lua_touserdata(L, 1));

		lua_pushlightuserdata(L, Hyperion::InstanceBridge::Shared);

		lua_rawget(L, LUA_REGISTRYINDEX);

		lua_pushlightuserdata(L, instance);
		lua_rawget(L, -2);

		lua_pushlightuserdata(L, instance);
		lua_pushnil(L);
		lua_rawset(L, -4);

		Hyperion::InstanceBridge::UintPtr(L, reinterpret_cast<uintptr_t>(lua_touserdata(L, 1)));
		lua_pushlightuserdata(L, instance);
		lua_pushvalue(L, -3);
		lua_rawset(L, -5);

		if (lua_getmetatable(L, 1))
			lua_setmetatable(L, -2);

		lua_replace(L, -3);
		lua_pop(L, 1);

		return 1;
	}

    int compareinstances(lua_State* L)
	{
		
		lua_normalisestack(L, 2);
		luaL_checktype(L, 1, LUA_TUSERDATA);
		luaL_checktype(L, 2, LUA_TUSERDATA);

		std::string ud_t = luaL_typename(L, 1);
		if (ud_t != ("Instance"))
			luaL_typeerrorL(L, 1, ("Instance"));

		std::string ud2_t = luaL_typename(L, 2);
		if (ud2_t != ("Instance"))
			luaL_typeerrorL(L, 2, ("Instance"));

		lua_pushboolean(L, *static_cast<void**>(lua_touserdata(L, 1)) == *static_cast<void**>(lua_touserdata(L, 2)));
		return 1;
	}

    void RegisterLibrary(lua_State* L)
    {
        

		lua_newtable(L);
		Utils::AddTableFunction(L, "invalidate", invalidate);
		Utils::AddTableFunction(L, "validate", validate);
		Utils::AddTableFunction(L, "iscached", iscached);
		Utils::AddTableFunction(L, "replace", replace);
		lua_setglobal(L, "cache");

		lua_newtable(L);
		Utils::AddFunction(L, "cloneref", cloneref);
		Utils::AddFunction(L, "clonereference", cloneref);
		Utils::AddFunction(L, "compareinstances", compareinstances);
    }
}