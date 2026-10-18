#pragma once

struct lua_State;
struct Closure;

namespace Stack::Helpers
{
	bool Hidden( lua_State* L, Closure* Function );
	int PhysicalLevel( lua_State* L, int Level );
	int SetHidden( lua_State* L );
	int IsHidden( lua_State* L );
	int Info( lua_State* L );
	int Traceback( lua_State* L );
	int GetEnvironment( lua_State* L );
	int Error( lua_State* L );
}
