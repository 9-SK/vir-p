#pragma once

#include <string>
#include <vector>
#include <lstate.h>
#include <lualib.h>
#include <luacode.h>
#include <Utilitys/Globals.hpp>

class EnvironmentAPI {
public:
	int func_id_counter = 0;
	std::vector<Closure*> function_array;

	// Track a closure as executor-owned
	void TrackClosure(Closure* c);

	// Register a global function with SEH guard
	// Usage: Environment->make_global(L, funcname, myFunc, funcnameinc++);
	void make_global(lua_State* LuaState, const char* name, lua_CFunction func, int id);

	// Register a function on the table at stack top (-2)
	// Usage: Environment->make_table_function(L, encode, base64encode, funcnameinc++);
	void make_table_function(lua_State* LuaState, const char* name, lua_CFunction func, int id);

	// Register multiple aliases for the same function
	void make_aliases(lua_State* LuaState, lua_CFunction func, int id, std::initializer_list<const char*> aliases);

	// Full environment setup (called by TaskScheduler after exploit init)
	void initalizeEnvironment(lua_State* LuaState);

	// Reset state between teleport/re-execution
	void ResetState();
};

extern EnvironmentAPI* Environment;
