#pragma once
#include <cstdint>

struct lua_State;
struct LuaTable;

namespace Scripts::Helpers
{
	int PushBytecode( lua_State* L );
	int PushClosure( lua_State* L );
	int PushHash( lua_State* L );
	int PushInstances( lua_State* L );
	int PushRunningScripts( lua_State* L );
	int PushLoadedModules( lua_State* L );
	bool IsScript( const char* ClassName );
	bool IsCoreScript( lua_State* L, int Index );
	LuaTable* FindEnvironment( lua_State* State, uintptr_t Instance );
	void PushScript( lua_State* L, lua_State* Thread );
	int PushCallingScript( lua_State* L, lua_State* Thread = nullptr );
}
