#pragma once
#include <string>

struct lua_State;
struct Closure;

namespace Closures::Helpers
{
	bool IsCaller( lua_State* L );
	bool IsExecutor( Closure* Function );
	bool IsBridge( lua_State* L, Closure* Function );
	Closure* CheckFunction( lua_State* L, int Index );
	int PushClone( lua_State* L, int Index );
	int PushWrapper( lua_State* L, int Index );
	int PushHook( lua_State* L, int Target, int Hook );
	int Restore( lua_State* L, int Index );
	int IsHooked( lua_State* L );
	std::string FunctionHash( Closure* Function );
	int PushHash( lua_State* L );
	int PushLoadstring( lua_State* L );
	int LoadBytecode( lua_State* L, const std::string& ByteCode, const std::string& ChunkName, const std::string& Error );
}
